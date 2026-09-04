#pragma once

#include <cstdint>
#include <functional>

namespace Menu
{
	/// Chains our procedure in front of the window's own. a_offerKey is given
	/// every key press first and returns whether it took it; a_wantsToggle is
	/// then asked whether the key is the toggle key, a_onToggle records the
	/// wish, and a_isOpen decides whether input goes to ImGui or to the game.
	///
	/// a_offerKey comes first deliberately. Asked the other way round, the
	/// toggle key could never be rebound onto itself.
	///
	/// Safe to call once; further calls are ignored.
	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_offerKey,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept;
}
