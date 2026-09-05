#pragma once

#include <atomic>
#include <cstdint>

namespace Menu
{
	/// One key press, seen on the window thread and acted on once by the render
	/// thread.
	///
	/// The small sibling of Gate: the same split across the two threads,
	/// without the state machine, because a hotkey that does something has
	/// nothing to open or close. Gate stays about the overlay.
	class KeyLatch
	{
	public:
		/// From the render thread, where the settings are read.
		void SetKey(std::uint32_t a_key) noexcept;
		[[nodiscard]] std::uint32_t Key() const noexcept;

		/// From the window thread, for every key press that nothing above took.
		/// A key of zero matches nothing, so a settings file naming no key is
		/// silent rather than firing on every keystroke.
		void Offer(std::uint32_t a_key) noexcept;

		/// From the render thread. True once per press, however many repeats
		/// the window procedure saw.
		[[nodiscard]] bool Take() noexcept;

	private:
		std::atomic<std::uint32_t> _key{ 0 };
		std::atomic<bool> _pending{ false };
	};
}
