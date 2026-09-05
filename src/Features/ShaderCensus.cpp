#include "Features/ShaderCensus.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Render/VTablePatch.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::size_t kClassCount = 13;

		// BSShader::SetupTechnique, vtable slot 02 by commonlibf4's numbering.
		// It is pure virtual on BSShader, so every one of the thirteen classes
		// has an entry of its own there - which is what makes patching the
		// table a way to catch each class separately.
		constexpr std::size_t kSetupTechniqueSlot = 2;

		// How many views to ask the pipeline about. D3D11 allows eight render
		// targets and 128 pixel shader resources; sixteen of the latter covers
		// what a deferred pass plausibly reads without making the log useless.
		constexpr std::uint32_t kMaxRenderTargets = 8;
		constexpr std::uint32_t kMaxShaderResources = 16;

		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		// SetupTechnique runs on the render thread and so does Frame, but the
		// hook sits on the path of every draw call. Atomics rather than a lock:
		// a lock there would be paid thousands of times a frame to guard a
		// write that happens once.
		std::array<std::atomic<void*>, kClassCount> g_seen{};
		std::array<Render::VTablePatch, kClassCount> g_patches{};

		// Kept apart from the patch, and never cleared. Restoring the vtable
		// entry does not stop a call already on its way into the thunk, and
		// that call still has to reach the engine's own function.
		std::array<void*, kClassCount> g_original{};

		std::array<std::atomic<bool>, kClassCount> g_wantSnapshot{};
		std::array<std::vector<std::string>, kClassCount> g_snapshots{};
		std::mutex g_snapshotMutex;

		[[nodiscard]] std::string NameOfView(REX::W32::ID3D11View* a_view) noexcept
		{
			if (a_view == nullptr) {
				return {};
			}

			auto name = Render::GetViewTargetName(a_view);
			return name.empty() ? std::format("{}", static_cast<void*>(a_view)) : name;
		}

		// Reads back what the pipeline has bound at this moment and turns it
		// into lines the log can carry. Runs once per deferred class, from
		// inside the hook, because that is the only moment we know the pass
		// this belongs to.
		//
		// The inventory from subproject B2 named 267 D3D objects, so a bound
		// resource comes back as FO4_RT_042 rather than as an address. The
		// engine names nothing itself.
		void SnapshotPipeline(std::size_t a_index) noexcept
		{
			auto* const context = Render::GetContext();
			if (context == nullptr) {
				return;
			}

			std::array<REX::W32::ID3D11RenderTargetView*, kMaxRenderTargets> targets{};
			REX::W32::ID3D11DepthStencilView* depth = nullptr;
			context->OMGetRenderTargets(kMaxRenderTargets, targets.data(), &depth);

			// Nothing bound means this call is not the pass itself - the engine
			// sets techniques up outside a render pass too. Leave the request
			// standing and try again on the next call.
			const bool bound =
				depth != nullptr ||
				std::any_of(targets.begin(), targets.end(), [](auto* a_view) {
					return a_view != nullptr;
				});

			std::array<REX::W32::ID3D11ShaderResourceView*, kMaxShaderResources> resources{};
			if (bound) {
				context->PSGetShaderResources(0, kMaxShaderResources, resources.data());

				std::vector<std::string> lines;
				for (std::uint32_t i = 0; i < kMaxRenderTargets; ++i) {
					if (targets[i] != nullptr) {
						lines.push_back(std::format("RTV{} {}", i, NameOfView(targets[i])));
					}
				}

				if (depth != nullptr) {
					lines.push_back(std::format("DSV  {}", NameOfView(depth)));
				}

				for (std::uint32_t i = 0; i < kMaxShaderResources; ++i) {
					if (resources[i] != nullptr) {
						lines.push_back(std::format("SRV{:<2} {}", i, NameOfView(resources[i])));
					}
				}

				{
					const std::lock_guard guard{ g_snapshotMutex };
					g_snapshots[a_index] = std::move(lines);
				}

				g_wantSnapshot[a_index].store(false, std::memory_order_relaxed);
			}

			// Every getter above handed out a reference of its own.
			for (auto* const view : targets) {
				if (view != nullptr) {
					view->Release();
				}
			}

			if (depth != nullptr) {
				depth->Release();
			}

			for (auto* const view : resources) {
				if (view != nullptr) {
					view->Release();
				}
			}
		}

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			if (g_seen[N].load(std::memory_order_relaxed) == nullptr) {
				g_seen[N].store(a_self, std::memory_order_relaxed);
			}

			if (g_wantSnapshot[N].load(std::memory_order_relaxed)) {
				SnapshotPipeline(N);
			}

			return reinterpret_cast<SetupTechniqueFn>(g_original[N])(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kClassCount>{});

		// kDFPrepass, kDFLight and kDFComposite. These are the three that
		// decide whether a screen space feature has anywhere to put its result,
		// so they are the ones whose bound resources are worth a snapshot.
		[[nodiscard]] constexpr bool IsDeferredPass(std::int32_t a_shaderType) noexcept
		{
			return a_shaderType >= 0x4 && a_shaderType <= 0x6;
		}

		void RemoveHooks() noexcept
		{
			for (auto& patch : g_patches) {
				if (patch.Installed()) {
					static_cast<void>(patch.Restore());
				}
			}
		}
	}

	void ShaderCensus::Declare()
	{
		Settings::DeclareFeature("ShaderCensus", true)
			.Label("feature.shader_census.name", "Shader Census")
			.Help(
				"feature.shader_census.help",
				"Counts the techniques of the engine's thirteen shader classes and reports "
				"what the deferred passes have bound. A measurement for the port, with no "
				"effect on the picture.");

		Settings::DeclareBool("ShaderCensus/techniqueNames", true)
			.Label("feature.shader_census.names", "Ask the engine for technique names")
			.Help(
				"feature.shader_census.names_help",
				"Calls the engine's own GetTechniqueName for every technique found, which "
				"is a call into the game rather than a read of it. Turn it off if the "
				"census does not survive a run.");
	}

	bool ShaderCensus::Setup()
	{
		_frames = 0;
		_reported = false;

		const auto classes = Shader::ShaderClasses();
		std::size_t installed = 0;

		for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
			g_seen[i].store(nullptr, std::memory_order_relaxed);
			g_wantSnapshot[i].store(IsDeferredPass(classes[i].shaderType), std::memory_order_relaxed);

			{
				const std::lock_guard guard{ g_snapshotMutex };
				g_snapshots[i].clear();
			}

			auto** const table = reinterpret_cast<void**>(classes[i].vtable);
			if (table == nullptr) {
				REX::WARN("ShaderCensus: no vtable address for {}", classes[i].className);
				continue;
			}

			// The table has to be the one the id promised, and it has to be a
			// primary table, before an entry of it is touched. Finding out
			// afterwards is not an option: the first call through a wrongly
			// patched entry ends the process, and the locator sits in front of
			// the table anyway.
			const auto identity = Util::DescribeVTable(table);
			if (!identity.has_value() ||
				identity->className != classes[i].className ||
				identity->subobjectOffset != 0) {
				REX::ERROR(
					"ShaderCensus: the vtable id for {} names {} at +0x{:X}, leaving it alone",
					classes[i].className,
					identity.has_value() ? identity->className : std::string{ "nothing" },
					identity.has_value() ? identity->subobjectOffset : 0);
				continue;
			}

			if (!g_patches[i].InstallAtTable(
					table,
					kSetupTechniqueSlot,
					reinterpret_cast<void*>(kThunks[i]))) {
				REX::WARN("ShaderCensus: could not patch {}", classes[i].className);
				continue;
			}

			g_original[i] = g_patches[i].Original();
			++installed;
		}

		if (installed == 0) {
			REX::ERROR("ShaderCensus: not one vtable could be patched");
			return false;
		}

		REX::INFO("ShaderCensus: watching {} of {} shader classes", installed, classes.size());
		return true;
	}

	void ShaderCensus::Frame()
	{
		if (_reported) {
			return;
		}

		++_frames;

		const auto classes = Shader::ShaderClasses();

		std::size_t seen = 0;
		for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
			if (g_seen[i].load(std::memory_order_relaxed) != nullptr) {
				++seen;
			}
		}

		if (seen < classes.size() && _frames < kSettleFrames) {
			return;
		}

		const bool withNames = Settings::GetBool("ShaderCensus/techniqueNames");

		REX::INFO("=== shader census, frame {}, {} of {} classes seen ===",
			_frames,
			seen,
			classes.size());

		for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
			auto* const shader = g_seen[i].load(std::memory_order_relaxed);

			// The class we patched has to be the class we caught. That is the
			// cross-check for the whole approach: it proves the vtable id named
			// what we believed and that slot 02 really is SetupTechnique - and
			// only once it holds is calling slot 09 for a name defensible.
			bool confirmed = false;
			if (shader != nullptr) {
				const auto info = Util::DescribeObject(shader);
				confirmed = info.has_value() &&
				            info->className == classes[i].className &&
				            info->subobjectOffset == 0;

				if (!confirmed) {
					REX::WARN(
						"{}: caught an object the RTTI calls {}, not what the vtable id "
						"promised - reporting it without asking the engine for names",
						classes[i].className,
						info.has_value() ? info->className : std::string{ "nothing" });
				}
			}

			Shader::ReportShaderTechniques(classes[i], shader, withNames && confirmed);

			const std::lock_guard guard{ g_snapshotMutex };
			if (!g_snapshots[i].empty()) {
				REX::INFO("    bound while it ran:");
				for (const auto& line : g_snapshots[i]) {
					REX::INFO("             {}", line);
				}
			} else if (IsDeferredPass(classes[i].shaderType) && shader != nullptr) {
				REX::INFO("    bound while it ran: nothing was, in any call");
			}
		}

		REX::INFO("=== end of census ===");

		_reported = true;
		RemoveHooks();
	}

	void ShaderCensus::Shutdown()
	{
		RemoveHooks();

		for (auto& seen : g_seen) {
			seen.store(nullptr, std::memory_order_relaxed);
		}

		const std::lock_guard guard{ g_snapshotMutex };
		for (auto& snapshot : g_snapshots) {
			snapshot.clear();
		}
	}
}
