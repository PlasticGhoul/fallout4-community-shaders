#include "Features/FrameTrace.h"

#include "Features/FrameTrace/FrameTraceLog.h"
#include "Menu/Hotkeys.h"
#include "Menu/KeyLatch.h"
#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/S/Sky.h>

#include <REX/W32/D3D11.h>

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Features
{
	namespace
	{
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;
		constexpr std::size_t kClassCount = 13;
		constexpr std::uint32_t kDefaultKey = 0x79;  // VK_F10

		std::array<Render::VTablePatch, kClassCount> g_patches{};
		std::array<void*, kClassCount> g_original{};
		bool g_installed = false;

		/// Set by the key, cleared when recording starts.
		std::atomic<bool> g_armed{ false };

		/// True for exactly the frame being recorded. Read in the thunk.
		std::atomic<bool> g_recording{ false };

		Trace::Recorder g_recorder;

		Menu::KeyLatch& TheLatch() noexcept
		{
			// Static, never destroyed: Hotkeys may be offering it a key on the
			// window thread at any moment, so it must outlive the process.
			static Menu::KeyLatch latch;
			return latch;
		}

		void ReleaseViews(Trace::RawRecord& a_record) noexcept
		{
			for (auto*& view : a_record.targets) {
				if (view != nullptr) {
					static_cast<REX::W32::ID3D11View*>(view)->Release();
					view = nullptr;
				}
			}
			if (a_record.depth != nullptr) {
				static_cast<REX::W32::ID3D11View*>(a_record.depth)->Release();
				a_record.depth = nullptr;
			}
			for (auto*& view : a_record.resources) {
				if (view != nullptr) {
					static_cast<REX::W32::ID3D11View*>(view)->Release();
					view = nullptr;
				}
			}
		}

		void Capture(std::size_t a_index, void* a_self, std::uint32_t a_pass) noexcept
		{
			auto* const context = Render::GetContext();
			if (context == nullptr) {
				return;
			}

			Trace::RawRecord record{};
			record.classIndex = a_index;
			record.self = a_self;
			record.technique = a_pass;

			// Every Get hands out a reference. They are kept until Present
			// resolves the names, then released there. A record the field
			// refuses releases its own right away.
			context->OMGetRenderTargets(
				static_cast<std::uint32_t>(Trace::RawRecord::kTargets),
				reinterpret_cast<REX::W32::ID3D11RenderTargetView**>(record.targets),
				reinterpret_cast<REX::W32::ID3D11DepthStencilView**>(std::addressof(record.depth)));
			context->PSGetShaderResources(
				0,
				static_cast<std::uint32_t>(Trace::RawRecord::kResources),
				reinterpret_cast<REX::W32::ID3D11ShaderResourceView**>(record.resources));

			if (!g_recorder.Push(record)) {
				ReleaseViews(record);
			}
		}

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			if (g_recording.load(std::memory_order_relaxed)) {
				Capture(N, a_self, a_pass);
			}
			return reinterpret_cast<SetupTechniqueFn>(g_original[N])(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kClassCount>{});

		std::string NameOfView(void* a_view) noexcept
		{
			if (a_view == nullptr) {
				return {};
			}
			auto name = Render::GetViewTargetName(static_cast<REX::W32::ID3D11View*>(a_view));
			return name.empty() ? std::format("{}", a_view) : name;
		}

		/// Turns the raw records into named entries and releases every view.
		std::vector<Trace::Entry> Resolve(std::span<const Shader::ShaderClass> a_classes) noexcept
		{
			std::vector<Trace::Entry> entries;
			entries.reserve(g_recorder.Records().size());

			// The class we patched has to be the class we caught, once per
			// class and object, before slot 09 is called on it - the census's
			// own rule.
			std::array<const void*, kClassCount> confirmed{};

			for (const auto& raw : g_recorder.Records()) {
				Trace::Entry entry;
				entry.classIndex = raw.classIndex;
				entry.className = a_classes[raw.classIndex].className;
				entry.technique = raw.technique;

				if (confirmed[raw.classIndex] != raw.self) {
					const auto info = Util::DescribeObject(raw.self);
					if (info.has_value() &&
						info->className == a_classes[raw.classIndex].className &&
						info->subobjectOffset == 0) {
						confirmed[raw.classIndex] = raw.self;
					}
				}
				if (confirmed[raw.classIndex] == raw.self) {
					entry.techniqueName = Shader::TechniqueName(raw.self, raw.technique);
				}

				for (std::uint32_t i = 0; i < Trace::RawRecord::kTargets; ++i) {
					if (raw.targets[i] != nullptr) {
						entry.targets.push_back({ i, NameOfView(raw.targets[i]) });
					}
				}
				entry.depth = NameOfView(raw.depth);
				for (std::uint32_t i = 0; i < Trace::RawRecord::kResources; ++i) {
					if (raw.resources[i] != nullptr) {
						entry.resources.push_back({ i, NameOfView(raw.resources[i]) });
					}
				}

				entries.push_back(std::move(entry));
			}

			// Records() is const; release through a copy of each pointer set.
			for (auto raw : g_recorder.Records()) {
				ReleaseViews(raw);
			}
			g_recorder.Clear();

			return entries;
		}

		void LogHeader() noexcept
		{
			const auto* const sky = RE::Sky::GetSingleton();
			const auto* const state = RE::BSGraphics::State::GetSingleton();
			const auto* const camera = RE::Main::WorldRootCamera();

			REX::INFO("=== frame trace: frame {} ===", Render::FrameCount());

			if (sky != nullptr) {
				REX::INFO(
					"  sky mode {}, fog distances [{:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f}], "
					"height {:.1f}, power {:.3f}, clamp {:.3f}, high density scale {:.3f}",
					static_cast<std::uint32_t>(sky->mode.get()),
					sky->fogDistances[0], sky->fogDistances[1], sky->fogDistances[2], sky->fogDistances[3],
					sky->fogDistances[4], sky->fogDistances[5], sky->fogDistances[6], sky->fogDistances[7],
					sky->fogHeight, sky->fogPower, sky->fogClamp, sky->fogHighDensityScale);
			}

			if (state != nullptr) {
				const auto& fog = state->fogState;
				REX::INFO(
					"  fogState range [{:.6f} {:.6f} {:.6f} {:.6f}] highLow [{:.6f} {:.6f} {:.6f} {:.6f}] "
					"power {:.3f} clamp {:.3f} highDensityScale {:.3f}",
					fog.rangeData.x, fog.rangeData.y, fog.rangeData.z, fog.rangeData.w,
					fog.highLowRangeData.x, fog.highLowRangeData.y, fog.highLowRangeData.z, fog.highLowRangeData.w,
					fog.power, fog.clamp, fog.highDensityScale);
				REX::INFO(
					"  fogState colours nearLow [{:.3f} {:.3f} {:.3f}] nearHigh [{:.3f} {:.3f} {:.3f}] "
					"farLow [{:.3f} {:.3f} {:.3f}] farHigh [{:.3f} {:.3f} {:.3f}]",
					fog.nearLowColor.r, fog.nearLowColor.g, fog.nearLowColor.b,
					fog.nearHighColor.r, fog.nearHighColor.g, fog.nearHighColor.b,
					fog.farLowColor.r, fog.farLowColor.g, fog.farLowColor.b,
					fog.farHighColor.r, fog.farHighColor.g, fog.farHighColor.b);
			}

			if (camera != nullptr) {
				const auto& t = camera->GetWorldTransform().translate;
				REX::INFO("  camera at [{:.1f} {:.1f} {:.1f}]", t.x, t.y, t.z);
			}
		}
	}

	void FrameTrace::Declare()
	{
		Settings::DeclareFeature("FrameTrace", false)
			.Label("feature.frame_trace.name", "Frame Trace")
			.Help(
				"feature.frame_trace.help",
				"Diagnostic. Writes one frame of the engine's shader calls to the log, with "
				"what each had bound, when the key is pressed.");

		Settings::DeclareKey("FrameTrace/key", kDefaultKey)
			.Label("feature.frame_trace.key", "Trace Key")
			.Help("feature.frame_trace.key_help", "Records the next frame.");
	}

	bool FrameTrace::Setup()
	{
		// The census restores vtable entries once a class has reported, and
		// what it would write back over ours is its own remembered original.
		// Refused rather than raced.
		if (Settings::GetBool("ShaderCensus/enabled")) {
			REX::ERROR(
				"FrameTrace: refusing while ShaderCensus is enabled, it restores the same vtable slots");
			return false;
		}

		if (!g_installed) {
			const auto classes = Shader::ShaderClasses();
			std::size_t installed = 0;

			for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
				auto** const table = reinterpret_cast<void**>(classes[i].vtable);
				if (table == nullptr) {
					REX::WARN("FrameTrace: no vtable address for {}", classes[i].className);
					continue;
				}

				const auto identity = Util::DescribeVTable(table);
				if (!identity.has_value() ||
					identity->className != classes[i].className ||
					identity->subobjectOffset != 0) {
					REX::WARN(
						"FrameTrace: the vtable id for {} names {} at +0x{:X}, leaving it alone",
						classes[i].className,
						identity.has_value() ? identity->className : std::string{ "nothing" },
						identity.has_value() ? identity->subobjectOffset : 0);
					continue;
				}

				if (!g_patches[i].InstallAtTable(
						table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[i]))) {
					REX::WARN("FrameTrace: could not patch {}", classes[i].className);
					continue;
				}

				g_original[i] = g_patches[i].Original();
				++installed;
			}

			if (installed == 0) {
				REX::ERROR("FrameTrace: no class could be patched");
				return false;
			}

			g_installed = true;
			REX::INFO(
				"FrameTrace: watching {} of {} shader classes, patches stay for the process",
				installed,
				classes.size());
		}

		TheLatch().SetKey(Settings::GetUInt32("FrameTrace/key"));
		if (!Menu::TheHotkeys().Register(TheLatch())) {
			REX::ERROR("FrameTrace: no room in the hotkey table");
			return false;
		}

		return true;
	}

	void FrameTrace::Frame()
	{
		// Read every frame, so a rebind through the overlay takes effect.
		TheLatch().SetKey(Settings::GetUInt32("FrameTrace/key"));

		if (TheLatch().Take()) {
			g_armed.store(true, std::memory_order_relaxed);
		}

		// Present is the frame boundary. Armed here means: record everything
		// from now to the next Present. Recording here means: that frame is
		// over, write it out.
		if (g_recording.load(std::memory_order_relaxed)) {
			g_recording.store(false, std::memory_order_relaxed);

			const auto classes = Shader::ShaderClasses();
			const auto overflow = g_recorder.Overflow();
			const auto entries = Resolve(classes);

			LogHeader();
			for (std::size_t i = 0; i < entries.size(); ++i) {
				REX::INFO("{}", Trace::FormatLine(i + 1, entries[i]));
			}

			const auto counts = Trace::CountByClass(entries, classes.size());
			for (std::size_t i = 0; i < counts.size(); ++i) {
				if (counts[i] > 0) {
					REX::INFO("  {:<24} {} call(s)", classes[i].className, counts[i]);
				}
			}
			REX::INFO(
				"=== frame trace: {} call(s){} ===",
				entries.size(),
				overflow > 0 ? std::format(", {} more not recorded", overflow) : std::string{});
			return;
		}

		if (g_armed.exchange(false, std::memory_order_relaxed)) {
			g_recorder.Clear();
			g_recording.store(true, std::memory_order_relaxed);
			REX::INFO("FrameTrace: recording the next frame");
		}
	}

	void FrameTrace::Shutdown()
	{
		Menu::TheHotkeys().Unregister(TheLatch());
		g_armed.store(false, std::memory_order_relaxed);

		// A recording in flight is dropped, and its references with it. The
		// patches stay: taking them back in a running game is the race
		// FramePhase.h describes.
		if (g_recording.exchange(false, std::memory_order_relaxed)) {
			for (auto raw : g_recorder.Records()) {
				ReleaseViews(raw);
			}
			g_recorder.Clear();
		}
	}
}
