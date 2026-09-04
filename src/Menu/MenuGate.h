#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace Menu
{
	/// Owns whether the overlay is open, and turns a pending key press into a
	/// transition.
	///
	/// It knows neither ImGui nor the engine: the input layer arrives as two
	/// callbacks. That is what makes the whole state machine testable without a
	/// game, and it is the reason for the seam - not tidiness.
	class Gate
	{
	public:
		/// Takes the game's input away. Returns whether it worked; a false
		/// still opens the overlay, because an overlay without an input layer
		/// is worth more than no overlay at all.
		using Suppress = std::function<bool()>;

		/// Gives it back. Called only when the matching Suppress succeeded.
		using Restore = std::function<void()>;

		Gate(Suppress a_suppress, Restore a_restore);

		void SetToggleKey(std::uint32_t a_key) noexcept;

		/// A key of zero matches nothing, so a settings file that names no key
		/// disables the overlay rather than opening it on every keystroke.
		[[nodiscard]] bool IsToggleKey(std::uint32_t a_key) const noexcept;

		/// Called from the window procedure, on the window thread. Records the
		/// wish and returns; key repeat therefore costs one transition, not one
		/// per repeat.
		void RequestToggle() noexcept;

		/// Called once per frame on the render thread, which is where the
		/// engine calls belong. Carries out a pending wish and returns whether
		/// the overlay is open.
		bool Tick() noexcept;

		[[nodiscard]] bool IsOpen() const noexcept { return _open; }

	private:
		Suppress _suppress;
		Restore _restore;

		std::atomic<bool> _toggleWanted{ false };
		bool _open{ false };
		bool _suppressing{ false };
		std::uint32_t _toggleKey{ 0 };
	};
}
