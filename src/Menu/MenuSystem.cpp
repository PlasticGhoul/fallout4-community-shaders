#include "Menu/MenuSystem.h"

#include "I18n/I18n.h"
#include "Menu/InputLayer.h"
#include "Menu/MenuGate.h"
#include "Menu/MousePointer.h"
#include "Menu/Overlay.h"
#include "Menu/SettingsPanel.h"
#include "Menu/Theme.h"
#include "Menu/Win32.h"
#include "Menu/WindowHook.h"
#include "Render/Profiler.h"
#include "Render/SwapChainHook.h"
#include "Settings/Settings.h"
#include "Util/GamePaths.h"

#include <string>
#include <vector>

namespace Menu
{
	namespace
	{
		constexpr auto kToggleKeyPath = "Menu/toggleKey"sv;
		constexpr auto kFontSizePath = "Menu/fontSize"sv;
		constexpr auto kLanguagePath = "Menu/language"sv;

		// VK_END. Unbound in Fallout 4 and common among its plugins.
		constexpr std::uint32_t kDefaultToggleKey = 0x23;

		constexpr auto kMeasurePath = "Performance/measure"sv;
		constexpr auto kHudPath = "Performance/hud"sv;
		constexpr auto kCornerPath = "Performance/corner"sv;
		constexpr auto kLogKeyPath = "Performance/logKey"sv;

		// VK_F11. Fallout 4 takes F5 and F9 for quick save and load, and Steam
		// sits on F12.
		constexpr std::uint32_t kDefaultLogKey = 0x7A;

		InputLayer& TheInputLayer()
		{
			static InputLayer layer;
			return layer;
		}

		MousePointer& ThePointer()
		{
			static MousePointer pointer;
			return pointer;
		}

		Gate& TheGate()
		{
			static Gate gate{
				[] { return TheInputLayer().Suppress(); },
				[] { TheInputLayer().Restore(); }
			};
			return gate;
		}

		Overlay& TheOverlay()
		{
			static Overlay overlay;
			return overlay;
		}

		/// Anything else reads as the top right corner. A settings file edited
		/// by hand must not be able to put the display off the screen.
		[[nodiscard]] int CornerFromSetting(std::string_view a_corner) noexcept
		{
			if (a_corner == "top-left") {
				return 0;
			}
			if (a_corner == "bottom-left") {
				return 2;
			}
			if (a_corner == "bottom-right") {
				return 3;
			}

			return 1;
		}
	}

	void StartSystem() noexcept
	{
		// Before the declaration below, not after: the list of locales is what
		// the choice is made of, and it comes from the directory.
		I18n::GetSingleton()->Init(Util::PluginDataDirectory() / "Translations");

		std::vector<std::string> locales;
		for (const auto& [code, name] : I18n::GetSingleton()->AvailableLocales()) {
			locales.push_back(code);
		}
		if (locales.empty()) {
			locales.emplace_back("en");
		}

		Settings::DeclareChoice(kLanguagePath, "en", std::move(locales))
			.Label("setting.menu.language", "Language")
			.Help("setting.menu.language.help", "Language of this overlay.");

		Settings::DeclareSlider(kFontSizePath, kReferenceFontSize, 12.0, 32.0)
			.Label("setting.menu.font_size", "Font size")
			.Help("setting.menu.font_size.help", "Size of the text in this overlay.");

		Settings::DeclareKey(kToggleKeyPath, kDefaultToggleKey)
			.Label("setting.menu.toggle_key", "Toggle key")
			.Help("setting.menu.toggle_key.help", "Opens and closes this overlay.");

		// A block with no feature of that name, so the panel draws it as a
		// general setting without knowing what it is for.
		Settings::DeclareBool(kMeasurePath, true)
			.Label("setting.performance.measure", "Measure performance")
			.Help(
				"setting.performance.measure.help",
				"Times each pass on the CPU and the GPU. Off issues no queries at all.");

		Settings::DeclareBool(kHudPath, true)
			.Label("setting.performance.hud", "Show while playing")
			.Help(
				"setting.performance.hud.help",
				"Draws a small display while this overlay is closed.");

		Settings::DeclareChoice(
			kCornerPath,
			"top-right",
			std::vector<std::string>{ "top-left", "top-right", "bottom-left", "bottom-right" })
			.Label("setting.performance.corner", "Corner")
			.Help("setting.performance.corner.help", "Where the small display sits.");

		Settings::DeclareKey(kLogKeyPath, kDefaultLogKey)
			.Label("setting.performance.log_key", "Write to log")
			.Help(
				"setting.performance.log_key.help",
				"Writes the current numbers to the plugin log.");
	}

	void TickSystem() noexcept
	{
		if (!TheOverlay().EnsureReady()) {
			return;
		}

		const auto toggleKey = Settings::GetUInt32(kToggleKeyPath);
		TheGate().SetToggleKey(toggleKey);

		// Applied from the setting rather than switched at the click: the
		// setting is the single truth, and it also carries a language chosen in
		// a previous session.
		if (const auto language = Settings::GetString(kLanguagePath);
			!language.empty() && language != I18n::GetSingleton()->CurrentLocale()) {
			I18n::GetSingleton()->SetLocale(language);
		}

		// Taken here rather than in the panel, because the gate lives here and
		// the panel should not have to know it.
		if (const auto captured = TheGate().TakeCapturedKey(); captured != 0) {
			REX::INFO("overlay toggle key rebound to 0x{:02X}", captured);
			Settings::SetUInt32(kToggleKeyPath, captured);
			Settings::Save();
		}

		// Announced once, next to the window handle and the ImGui version, so
		// that a report of "the key does nothing" can be answered from the log
		// instead of from a guess. A zero here disables the overlay.
		static bool announced = false;
		if (!announced) {
			REX::INFO("overlay toggle key is 0x{:02X}", toggleKey);
			announced = true;
		}

		// Installed here rather than at kGameDataReady: the window handle comes
		// from the swap chain, and EnsureReady is what reads it.
		InstallWindowHook(
			TheOverlay().Window(),
			[](std::uint32_t a_key) { return TheGate().OfferKey(a_key); },
			[](std::uint32_t a_key) { return TheGate().IsToggleKey(a_key); },
			[] { TheGate().RequestToggle(); },
			[] { return TheGate().IsOpen(); });

		const bool open = TheGate().Tick();

		MousePointer::Point pointer;
		if (open) {
			if (!ThePointer().IsActive()) {
				ThePointer().Acquire(TheOverlay().Window());

				// The game keeps the cursor to its own monitor, which is a
				// second problem on a desktop that has more than one. Lifted
				// once here rather than every frame, because the measurement
				// showed the game sets it once on entry.
				Win32::ClipCursor(nullptr);
			}
			pointer = ThePointer().Update();
		} else if (ThePointer().IsActive()) {
			ThePointer().Release();
		}

		PanelContext panel;
		panel.frame = Render::FrameCount();
		panel.armCapture = [] { TheGate().ArmCapture(); };
		panel.isCapturing = [] { return TheGate().IsCapturing(); };

		// Read every frame rather than cached, like every other setting: there
		// is no change notification, so a cached corner would need refreshing
		// by whoever cached it.
		const auto& profiler = Render::Profiler::GetSingleton();

		PerformanceContext performance;
		performance.passes = profiler.Results();
		performance.frameGpuMs = profiler.FrameGpuMs();
		performance.frameCpuMs = profiler.FrameCpuMs();
		performance.measuring = profiler.IsMeasuring();
		performance.hud = Settings::GetBool(kHudPath);
		performance.corner = CornerFromSetting(Settings::GetString(kCornerPath));

		if (TheOverlay().Draw(open, panel, performance, pointer)) {
			// Through the gate rather than straight to a flag, so that the
			// button and the toggle key close the overlay the same way, input
			// layer and all.
			TheGate().RequestToggle();
		}
	}
}
