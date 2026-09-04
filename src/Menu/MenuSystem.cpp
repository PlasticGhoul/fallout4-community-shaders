#include "Menu/MenuSystem.h"

#include "Feature/FeatureSettings.h"
#include "Menu/MenuGate.h"
#include "Menu/Overlay.h"
#include "Render/SwapChainHook.h"

#include <REX/W32/USER32.h>

namespace Menu
{
	namespace
	{
		constexpr auto kToggleKeyPath = "Menu/toggleKey"sv;

		// VK_END. Unbound in Fallout 4 and common among its plugins.
		constexpr std::uint32_t kDefaultToggleKey = 0x23;

		Gate& TheGate()
		{
			// The input layer arrives in task 4; until then the gate opens the
			// overlay without taking anything away, which is exactly what its
			// failed-suppression path already does.
			static Gate gate{ [] { return false; }, [] {} };
			return gate;
		}

		Overlay& TheOverlay()
		{
			static Overlay overlay;
			return overlay;
		}

		// Polled here rather than taken from a window procedure, because there
		// is none yet. Task 4 replaces this and keeps the gate.
		void PollToggleKey()
		{
			static bool wasDown = false;

			const auto key = Features::Settings::GetUInt32(kToggleKeyPath);
			if (key == 0) {
				return;
			}

			const bool isDown = (REX::W32::GetKeyState(static_cast<std::int32_t>(key)) & 0x8000) != 0;
			if (isDown && !wasDown) {
				TheGate().RequestToggle();
			}
			wasDown = isDown;
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
		PollToggleKey();

		TheOverlay().Draw(TheGate().Tick(), Render::FrameCount());
	}
}
