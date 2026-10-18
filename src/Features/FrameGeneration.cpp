#include "Features/FrameGeneration.h"

#include "Bridge/Capture.h"
#include "Bridge/SimulationHook.h"
#include "Menu/MenuSystem.h"
#include "Settings/Settings.h"
#include "Upscale/EngineHooks.h"

#include <RE/M/Main.h>
#include <RE/U/UI.h>

#include <format>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// The engine's TAA switch: without it the engine writes no motion
		/// vectors (F5.5a); DLSS forces it on while it runs.
		constexpr std::uint64_t kEnableTAA = 2704658;
	}

	void FrameGeneration::Declare()
	{
		Settings::DeclareFeature("FrameGeneration", false)
			.Label("feature.framegeneration.name", "Frame Generation")
			.Help(
				"feature.framegeneration.help",
				"NVIDIA DLSS Frame Generation adds a generated frame after every rendered one for smoother motion, with "
				"NVIDIA Reflex keeping the delay down. Needs borderless windowed mode; switching it on takes effect at the "
				"next game start. Uses NVIDIA DLSS and Reflex technology.");
		Settings::DeclareChoice("FrameGeneration/reflex", "on", std::vector<std::string>{ "on", "boost" })
			.Label("feature.framegeneration.reflex", "Reflex")
			.Help("feature.framegeneration.reflex_help", "Low latency mode, or with boost, which also keeps the GPU clock up.");
		Settings::DeclareChoice("FrameGeneration/view", "off", std::vector<std::string>{ "off", "split" })
			.Label("feature.framegeneration.view", "Debug view")
			.Help(
				"feature.framegeneration.view_help",
				"Split shows the picture frame generation sees on the left half: it must have no HUD there.");
	}

	bool FrameGeneration::Setup()
	{
		_frames = 0;
		_generating = false;
		_blocker = "<start>";

		if (!Bridge::Active()) {
			REX::ERROR("FrameGeneration: no bridge - {}", Bridge::Refusal());
			return false;
		}
		if (!Upscale::Hooks::Installed()) {
			REX::ERROR("FrameGeneration: the engine hooks are not installed ({})", Upscale::Hooks::FailedSite());
			return false;
		}
		if (!Bridge::SimulationHookInstalled()) {
			REX::ERROR("FrameGeneration: no frame start for Reflex (PollInputDevices detour missing)");
			return false;
		}
		if (!Bridge::TheCapture().Acquire(Bridge::Consumer::kFrameGeneration)) {
			REX::ERROR("FrameGeneration: the bridge capture could not start");
			return false;
		}
		_captureAcquired = true;
		REX::INFO("FrameGeneration: running");
		return true;
	}

	const char* FrameGeneration::Blocker() const noexcept
	{
		auto* const ui = RE::UI::GetSingleton();
		if (ui != nullptr && ui->GetMenuOpen("LoadingMenu")) {
			return "loading screen";
		}
		if (ui != nullptr && ui->GetMenuOpen("PipboyMenu")) {
			return "Pip-Boy";
		}
		if (auto* const main = RE::Main::GetSingleton(); main != nullptr && main->inMenuMode) {
			return "a game menu is open";
		}
		if (Menu::IsOverlayOpen()) {
			return "the overlay is open";
		}
		if (!*reinterpret_cast<const bool*>(REL::ID(kEnableTAA).address())) {
			return "the game's TAA is off, so it writes no motion vectors";
		}
		const auto status = Bridge::CurrentStatus();
		if (status.minWidthOrHeight != 0 && (status.width < status.minWidthOrHeight || status.height < status.minWidthOrHeight)) {
			return "the picture is smaller than DLSS-G accepts";
		}
		return nullptr;
	}

	void FrameGeneration::Frame()
	{
		++_frames;
		const char* const blocker = Blocker();
		const bool generate = blocker == nullptr;
		_generating = generate;
		Bridge::TheCapture().Want(Bridge::Consumer::kFrameGeneration, generate);
		Bridge::SetGeneration(generate);
		Bridge::SetReflexMode(Settings::GetString("FrameGeneration/reflex") == "boost" ? 2u : 1u);
		Bridge::SetSplitView(Settings::GetString("FrameGeneration/view") == "split");

		const std::string reason = blocker != nullptr ? blocker : "";
		if (reason != _blocker) {
			REX::INFO("FrameGeneration: {}", reason.empty() ? std::string{ "generating" } : std::format("off - {}", reason));
			_blocker = reason;
		}

		if (_frames % kLogInterval == 0) {
			const auto status = Bridge::CurrentStatus();
			REX::INFO(
				"frame generation: rendered {:.0f}/s, shown {:.0f}/s, {}, DLSS-G status 0x{:X}",
				status.renderedPerSecond, status.shownPerSecond, status.generating ? "generating" : "not generating", status.dlssgStatus);
			Bridge::TheCapture().LogSecond();
		}
	}

	void FrameGeneration::Shutdown()
	{
		Bridge::SetGeneration(false);
		Bridge::SetSplitView(false);
		// Boost keeps the GPU clock up; that must not outlive the switch.
		Bridge::SetReflexMode(1);
		if (_captureAcquired) {
			Bridge::TheCapture().Release(Bridge::Consumer::kFrameGeneration);
			_captureAcquired = false;
		}
		_generating = false;
	}
}
