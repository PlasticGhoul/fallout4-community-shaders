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

		if (TheOverlay().Draw(open, panel, pointer)) {
			// Through the gate rather than straight to a flag, so that the
			// button and the toggle key close the overlay the same way, input
			// layer and all.
			TheGate().RequestToggle();
		}
	}
}
