#include "Menu/MenuSystem.h"

#include "Feature/FeatureSettings.h"
#include "Menu/InputLayer.h"
#include "Menu/MenuGate.h"
#include "Menu/Overlay.h"
#include "Menu/WindowHook.h"
#include "Render/SwapChainHook.h"

namespace Menu
{
	namespace
	{
		constexpr auto kToggleKeyPath = "Menu/toggleKey"sv;

		// VK_END. Unbound in Fallout 4 and common among its plugins.
		constexpr std::uint32_t kDefaultToggleKey = 0x23;

		InputLayer& TheInputLayer()
		{
			static InputLayer layer;
			return layer;
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
		Features::Settings::DeclareUInt32(kToggleKeyPath, kDefaultToggleKey);
	}

	void TickSystem() noexcept
	{
		if (!TheOverlay().EnsureReady()) {
			return;
		}

		TheGate().SetToggleKey(Features::Settings::GetUInt32(kToggleKeyPath));

		// Installed here rather than at kGameDataReady: the window handle comes
		// from the swap chain, and EnsureReady is what reads it.
		InstallWindowHook(
			TheOverlay().Window(),
			[](std::uint32_t a_key) { return TheGate().IsToggleKey(a_key); },
			[] { TheGate().RequestToggle(); },
			[] { return TheGate().IsOpen(); });

		TheOverlay().Draw(TheGate().Tick(), Render::FrameCount());
	}
}
