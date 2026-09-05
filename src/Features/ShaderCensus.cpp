#include "Features/ShaderCensus.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Render/VTablePatch.h"
#include "Settings/Settings.h"
#include "Shader/BSShaderLayout.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ModuleScan.h"
#include "Util/ObjectRTTI.h"
#include "Util/SafeRead.h"

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
		_complete = false;
		_state = {};

		const auto classes = Shader::ShaderClasses();
		std::size_t installed = 0;

		for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
			g_seen[i].store(nullptr, std::memory_order_relaxed);

			// Every class, not only the deferred three. What kLighting reads
			// and writes is worth as much as what kDFLight does, and a
			// snapshot costs one call the first time a class runs.
			g_wantSnapshot[i].store(true, std::memory_order_relaxed);

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

	void ShaderCensus::ReportOne(std::size_t a_index, const Shader::ShaderClass& a_class) noexcept
	{
		auto* const shader = g_seen[a_index].load(std::memory_order_relaxed);

		// The class we patched has to be the class we caught. That is the
		// cross-check for the whole approach: it proves the vtable id named
		// what we believed and that slot 02 really is SetupTechnique - and only
		// once it holds is calling slot 09 for a name defensible.
		bool confirmed = false;
		if (shader != nullptr) {
			const auto info = Util::DescribeObject(shader);
			confirmed = info.has_value() &&
			            info->className == a_class.className &&
			            info->subobjectOffset == 0;

			if (!confirmed) {
				REX::WARN(
					"{}: caught an object the RTTI calls {}, not what the vtable id "
					"promised - reporting it without asking the engine for names",
					a_class.className,
					info.has_value() ? info->className : std::string{ "nothing" });
			}
		}

		REX::INFO("--- census: {}, first seen at frame {} ---",
			a_class.className,
			_state[a_index].firstSeenFrame);

		Shader::ReportShaderTechniques(
			a_class,
			shader,
			Settings::GetBool("ShaderCensus/techniqueNames") && confirmed);

		const std::lock_guard guard{ g_snapshotMutex };
		if (!g_snapshots[a_index].empty()) {
			REX::INFO("    bound while it ran:");
			for (const auto& line : g_snapshots[a_index]) {
				REX::INFO("             {}", line);
			}
		} else if (shader != nullptr) {
			REX::INFO("    bound while it ran: nothing was, in any call");
		}
	}

	void ShaderCensus::AdoptFromEngineTable() noexcept
	{
		const auto classes = Shader::ShaderClasses();
		const auto total = std::min(classes.size(), kClassCount);

		void* anchor = nullptr;
		std::string_view anchorName;
		for (std::size_t i = 0; i < total; ++i) {
			if (auto* const shader = g_seen[i].load(std::memory_order_relaxed); shader != nullptr) {
				anchor = shader;
				anchorName = classes[i].className;
				break;
			}
		}

		if (anchor == nullptr) {
			REX::WARN("ShaderCensus: nothing caught yet, so there is no anchor to look up");
			return;
		}

		const auto places = Util::FindPointerInModuleData(anchor);
		REX::INFO("=== census: {} at {} sits in {} place(s) of the module's data ===",
			anchorName,
			anchor,
			places.size());

		for (auto* const* const place : places) {
			REX::INFO("  slot {}", static_cast<const void*>(place));

			for (int offset = -kNeighbourhood; offset <= kNeighbourhood; ++offset) {
				auto* const* const slot = place + offset;
				if (!Util::IsReadableRange(slot, sizeof(void*))) {
					continue;
				}

				auto* const candidate = *slot;
				const auto info = Util::DescribeObject(candidate);
				if (!info.has_value() || info->subobjectOffset != 0) {
					continue;
				}

				REX::INFO("    [{:+3}] {} at {}", offset, info->className, candidate);

				for (std::size_t i = 0; i < total; ++i) {
					if (info->className != classes[i].className ||
						g_seen[i].load(std::memory_order_relaxed) != nullptr) {
						continue;
					}

					g_seen[i].store(candidate, std::memory_order_relaxed);
					REX::INFO("ShaderCensus: adopted {} without ever seeing it run",
						classes[i].className);
				}
			}
		}
	}

	void ShaderCensus::Frame()
	{
		++_frames;

		if (_complete || _frames % kPollInterval != 0) {
			return;
		}

		const auto classes = Shader::ShaderClasses();
		const auto total = std::min(classes.size(), kClassCount);

		std::size_t reported = 0;
		std::string missing;

		for (std::size_t i = 0; i < total; ++i) {
			auto& state = _state[i];
			if (state.reported) {
				++reported;
				continue;
			}

			auto* const shader = g_seen[i].load(std::memory_order_relaxed);
			if (shader == nullptr) {
				missing += missing.empty() ? "" : ", ";
				missing += classes[i].enumerator;
				continue;
			}

			if (state.firstSeenFrame == 0) {
				state.firstSeenFrame = _frames;
				REX::INFO("ShaderCensus: {} first ran at frame {}",
					classes[i].className,
					_frames);
			}

			// A refused map is almost always a map the engine is in the middle
			// of growing, so it counts as not settled rather than as a finding.
			// Patience is what turns a lasting refusal back into one.
			const auto totals = Shader::SummariseMaps(shader);
			if (totals.techniques == state.lastCount && totals.techniques > 0 &&
				totals.refused == 0) {
				++state.stablePolls;
			} else {
				state.lastCount = totals.techniques;
				state.stablePolls = 0;
			}

			// Settled, or out of patience. The second case is what keeps a
			// class that never fills a map - or one whose count keeps
			// creeping - from being lost altogether.
			const bool settled = state.stablePolls >= kStablePolls;
			const bool impatient = _frames - state.firstSeenFrame >= kPatienceFrames;
			if (!settled && !impatient) {
				continue;
			}

			if (!settled) {
				REX::INFO(
					"ShaderCensus: {} has not settled in {} frames, reporting it as it stands",
					classes[i].className,
					kPatienceFrames);
			}

			ReportOne(i, classes[i]);
			state.reported = true;
			static_cast<void>(g_patches[i].Restore());
			++reported;
		}

		if (reported == total) {
			REX::INFO("=== census complete, all {} classes reported ===", total);
			_complete = true;
			return;
		}

		if (_frames % kProgressInterval == 0) {
			REX::INFO("ShaderCensus: {} of {} reported, still waiting for {}",
				reported,
				total,
				missing.empty() ? std::string{ "none of them" } : missing);

			// One look in the engine's own table, once a class has had a full
			// interval to turn up on its own.
			if (!_adopted && !missing.empty()) {
				_adopted = true;
				AdoptFromEngineTable();
			}

			// A reported class can still grow: the engine compiles a
			// permutation the first time something needs it, so a count taken
			// in Sanctuary is not the count in a vault. Saying so is cheap and
			// is itself a finding.
			for (std::size_t i = 0; i < total; ++i) {
				if (!_state[i].reported) {
					continue;
				}

				auto* const shader = g_seen[i].load(std::memory_order_relaxed);
				const auto count = Shader::SummariseMaps(shader).techniques;
				if (count != _state[i].lastCount) {
					REX::INFO("ShaderCensus: {} grew from {} to {} techniques",
						classes[i].className,
						_state[i].lastCount,
						count);
					_state[i].lastCount = count;
				}
			}
		}
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
