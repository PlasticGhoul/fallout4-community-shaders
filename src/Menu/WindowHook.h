#pragma once

#include <cstdint>
#include <functional>

namespace Menu
{
	/// Chains our procedure in front of the window's own. a_wantsToggle is
	/// asked whether a key is the toggle key, a_onToggle records the wish, and
	/// a_isOpen decides whether input goes to ImGui or to the game.
	///
	/// Safe to call once; further calls are ignored.
	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept;
}
