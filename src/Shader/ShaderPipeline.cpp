#include "Shader/ShaderPipeline.h"

#include "Shader/ImagespaceCatalog.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderOverride.h"
#include "Shader/ShaderSource.h"
#include "Shader/ShaderWatcher.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

namespace Shader
{
	namespace
	{
		constexpr auto kShaderFile = "ImagespaceCopy.hlsl";
		constexpr auto kEntryPoint = "main";
		constexpr auto kDebugName = "FO4CS_PS_ImagespaceCopy"sv;

		// The pass we want, and the rule for picking a stand-in when it is not
		// there: the first proven pass with exactly one technique.
		constexpr auto kPreferredClass = "BSImagespaceShaderCopy"sv;

		// The effect list may not be populated on the very first frame. Asking
		// once a second for ten seconds is generous, and spacing the attempts
		// out keeps a persistently empty list from filling the log with the
		// same warning six hundred times.
		constexpr std::uint64_t kCatalogInterval = 60;
		constexpr std::uint64_t kCatalogAttempts = 10;

		constexpr auto kPollInterval = std::chrono::milliseconds{ 500 };

		std::filesystem::path ShaderRoot()
		{
			// Derived from the game module rather than the working directory:
			// the working directory is not ours to rely on.
			const std::filesystem::path exe = REX::FModule::GetExecutingModule().GetFileName();
			return exe.parent_path() / "Data" / "Shaders" / "FO4";
		}

		std::mutex g_mutex;
		std::vector<std::uint8_t> g_pending;
		std::atomic<bool> g_hasPending{ false };
		std::atomic<bool> g_stop{ false };
		std::atomic<bool> g_armed{ false };

		PixelShaderOverride g_override;
		std::uint64_t g_frames = 0;
		std::uint64_t g_catalogTries = 0;
		bool g_catalogDone = false;

		// Reads, splices and compiles. Creating the D3D object is left to the
		// render thread: keeping every D3D call on one thread is one fewer
		// assumption to be wrong about.
		void CompileAndPublish(FileWatch& a_watch)
		{
			const auto source = LoadSource(ShaderRoot(), kShaderFile);
			if (!source.has_value()) {
				REX::WARN("{}", source.error());
				return;
			}

			a_watch.Reset(source->files);

			const auto compiled = CompilePixelShader(source->text, kShaderFile, kEntryPoint);
			if (!compiled.diagnostics.empty()) {
				REX::WARN("shader diagnostics:\n{}", compiled.diagnostics);
			}

			if (!compiled.Succeeded()) {
				// Whatever is installed stays installed. A typo must not be
				// able to produce a black screen.
				REX::ERROR("{} did not compile, keeping the shader in place", kShaderFile);
				return;
			}

			{
				const std::scoped_lock lock{ g_mutex };
				g_pending = compiled.bytecode;
			}
			g_hasPending.store(true, std::memory_order_release);
		}

		void WatcherLoop()
		{
			FileWatch watch;
			bool loadedOnce = false;

			while (!g_stop.load(std::memory_order_acquire)) {
				if (g_armed.load(std::memory_order_acquire)) {
					if (!loadedOnce) {
						CompileAndPublish(watch);
						loadedOnce = true;
					} else if (watch.Poll()) {
						REX::INFO("{} changed, recompiling", kShaderFile);
						CompileAndPublish(watch);
					}
				}

				std::this_thread::sleep_for(kPollInterval);
			}
		}

		// Picks the pass to replace: the preferred class if the catalog found it
		// with a single technique, otherwise the first pass that has one.
		const ImagespacePass* ChoosePass(const std::vector<ImagespacePass>& a_passes)
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

		void RunCatalogOnce()
		{
			const auto passes = RunImagespaceCatalog();
			if (passes.empty()) {
				return;
			}

			const auto* const chosen = ChoosePass(passes);
			if (chosen == nullptr) {
				REX::ERROR("no pass with a single technique, nothing to replace");
				g_catalogDone = true;
				return;
			}

			REX::INFO("replacing {} technique {}", chosen->className, chosen->techniqueID);

			g_override.Adopt(chosen->slot);
			g_catalogDone = true;
			g_armed.store(true, std::memory_order_release);
		}
	}

	void StartPipeline() noexcept
	{
		REX::INFO("shader root is {}", ShaderRoot().generic_string());

		// Detached rather than joined: F4SE gives no unload path to join in, and
		// the loop owns nothing the process needs back.
		std::thread{ WatcherLoop }.detach();
	}

	void TickPipeline() noexcept
	{
		++g_frames;

		if (!g_catalogDone && g_catalogTries < kCatalogAttempts &&
			g_frames % kCatalogInterval == 0) {
			++g_catalogTries;
			RunCatalogOnce();

			if (!g_catalogDone && g_catalogTries == kCatalogAttempts) {
				REX::ERROR(
					"no usable image space pass after {} attempts, giving up",
					kCatalogAttempts);
			}
		}

		if (g_hasPending.exchange(false, std::memory_order_acq_rel)) {
			std::vector<std::uint8_t> bytecode;
			{
				const std::scoped_lock lock{ g_mutex };
				bytecode = std::move(g_pending);
				g_pending.clear();
			}

			static_cast<void>(g_override.Install(bytecode, kDebugName));
		}

		g_override.Guard();
	}
}
