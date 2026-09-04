#include "Features/ImagespaceTint.h"

#include "Settings/Settings.h"
#include "Shader/ImagespaceCatalog.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <chrono>
#include <memory>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "ImagespaceCopy.hlsl";
		constexpr auto kEntryPoint = "main";
		constexpr auto kDebugName = "FO4CS_PS_ImagespaceCopy"sv;

		// The pass we want, and the rule for picking a stand-in when it is not
		// there: the first proven pass with exactly one technique.
		constexpr auto kPreferredClass = "BSImagespaceShaderCopy"sv;

		// The effect list exists early, but the engine fills its technique maps
		// much later - the first run of subproject C found 226 named passes
		// within two seconds of kGameDataReady and not one of them with a
		// technique yet, because the game was still sitting in the main menu.
		// So the catalog keeps asking, once a second, until it finds something.
		constexpr std::uint64_t kCatalogInterval = 60;

		// One line a minute while nothing is found, so a wait is visible in the
		// log without drowning it.
		constexpr std::uint64_t kCatalogHeartbeat = 60;

		// Half a second between polls, but slept in slices, because Shutdown
		// joins this thread from the render thread and must not wait out a
		// whole interval to do it.
		constexpr auto kSlice = std::chrono::milliseconds{ 50 };
		constexpr int kSlicesPerPoll = 10;

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		// Picks the pass to replace: the preferred class if the catalog found it
		// with a single technique, otherwise the first pass that has one.
		const Shader::ImagespacePass* ChoosePass(
			const std::vector<Shader::ImagespacePass>& a_passes)
		{
			for (const auto& pass : a_passes) {
				if (pass.className == kPreferredClass && pass.slot != nullptr) {
					return std::addressof(pass);
				}
			}

			for (const auto& pass : a_passes) {
				if (pass.slot != nullptr) {
					REX::WARN(
						"{} was not available, falling back to {}",
						kPreferredClass,
						pass.className);
					return std::addressof(pass);
				}
			}

			return nullptr;
		}
	}

	ImagespaceTint::~ImagespaceTint()
	{
		_stop.store(true, std::memory_order_release);
		if (_watcher.joinable()) {
			_watcher.join();
		}
	}

	void ImagespaceTint::Declare()
	{
		Settings::DeclareFeature("ImagespaceTint", true)
			.Label("feature.imagespace_tint.name", "Imagespace Tint")
			.Help(
				"feature.imagespace_tint.help",
				"Replaces an imagespace pixel shader with one of our own.");
	}

	bool ImagespaceTint::Setup()
	{
		_stop.store(false, std::memory_order_release);
		_frames = 0;
		_catalogTries = 0;
		_catalogDone = false;

		REX::INFO("shader root is {}", ShaderRoot().generic_string());

		_watcher = std::thread{ [this] { WatcherLoop(); } };

		// Deliberately not the catalog. The engine fills its technique maps
		// only once a world is loaded, so a Setup that insisted on finding a
		// pass would refuse itself in the main menu and stay refused until the
		// user touched the settings file.
		return true;
	}

	void ImagespaceTint::Frame()
	{
		++_frames;

		if (!_catalogDone && _frames % kCatalogInterval == 0) {
			TryCatalog();
		}

		// The bytecode is only drained once there is a slot to put it in.
		// Subproject C gated the compile itself on that; gating the install
		// instead lets the compile happen while the game is still loading, and
		// keeps the result until the catalog catches up. Dropping it here would
		// lose it for good, because nothing recompiles until the file changes.
		if (_override.Adopted() && _hasPending.exchange(false, std::memory_order_acq_rel)) {
			std::vector<std::uint8_t> bytecode;
			{
				const std::scoped_lock lock{ _mutex };
				bytecode = std::move(_pending);
				_pending.clear();
			}

			static_cast<void>(_override.Install(bytecode, kDebugName));
		}

		_override.Guard();
	}

	void ImagespaceTint::Shutdown()
	{
		_stop.store(true, std::memory_order_release);
		if (_watcher.joinable()) {
			_watcher.join();
		}

		// Restore has existed since subproject C and has never been called.
		// This is the first time the pointer goes back.
		_override.Restore();

		{
			const std::scoped_lock lock{ _mutex };
			_pending.clear();
		}
		_hasPending.store(false, std::memory_order_release);
		_catalogDone = false;
	}

	void ImagespaceTint::WatcherLoop()
	{
		bool loadedOnce = false;
		int slices = 0;

		while (!_stop.load(std::memory_order_acquire)) {
			if (++slices >= kSlicesPerPoll) {
				slices = 0;
				if (!loadedOnce) {
					CompileAndPublish();
					loadedOnce = true;
				} else if (_watch.Poll()) {
					REX::INFO("{} changed, recompiling", kShaderFile);
					CompileAndPublish();
				}
			}

			// Sliced so that a toggle does not wait out a full poll interval.
			std::this_thread::sleep_for(kSlice);
		}
	}

	// Reads, splices and compiles. Creating the D3D object is left to the
	// render thread: keeping every D3D call on one thread is one fewer
	// assumption to be wrong about.
	void ImagespaceTint::CompileAndPublish()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source.has_value()) {
			REX::WARN("{}", source.error());
			return;
		}

		_watch.Reset(source->files);

		const auto compiled = Shader::CompilePixelShader(source->text, kShaderFile, kEntryPoint);
		if (!compiled.diagnostics.empty()) {
			REX::WARN("shader diagnostics:\n{}", compiled.diagnostics);
		}

		if (!compiled.Succeeded()) {
			// Whatever is installed stays installed. A typo must not be able to
			// produce a black screen.
			REX::ERROR("{} did not compile, keeping the shader in place", kShaderFile);
			return;
		}

		{
			const std::scoped_lock lock{ _mutex };
			_pending = compiled.bytecode;
		}
		_hasPending.store(true, std::memory_order_release);
	}

	void ImagespaceTint::TryCatalog()
	{
		// The first attempt of the session is verbose, so that an empty result
		// is visible; the ones after it stay quiet until something turns up.
		const bool verbose = !_tableLogged && _catalogTries == 0;

		auto passes = Shader::RunImagespaceCatalog(verbose);
		++_catalogTries;

		if (passes.empty()) {
			if (_catalogTries % kCatalogHeartbeat == 0) {
				REX::INFO(
					"still waiting for a usable image space pass, attempt {}",
					_catalogTries);
			}
			return;
		}

		// Something turned up on a quiet attempt: print the table once, now
		// that there is a table worth printing.
		if (!verbose && !_tableLogged) {
			passes = Shader::RunImagespaceCatalog(true);
		}
		_tableLogged = true;

		const auto* const chosen = ChoosePass(passes);
		if (chosen == nullptr) {
			REX::ERROR("no pass with a single technique, nothing to replace");
			_catalogDone = true;
			return;
		}

		REX::INFO("replacing {} technique {}", chosen->className, chosen->techniqueID);

		_override.Adopt(chosen->slot);
		_catalogDone = true;
	}
}
