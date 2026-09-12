#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Menu
{
	class KeyLatch;

	/// Latches outside the menu that want the key presses nothing above took.
	///
	/// WindowHook hands every remaining press to one callback; the menu owns
	/// that callback and used it for its own log key alone. A feature that
	/// wants a key of its own registers a latch here, and the menu offers the
	/// press to every one of them. The latch decides whether it matches.
	///
	/// Read on the window thread, written on the render thread, which is why
	/// the entries are atomic pointers. A registered latch has to outlive the
	/// process or be unregistered before it is destroyed: Offer may be
	/// reading it at that moment. FrameTrace keeps its latch static.
	class Hotkeys
	{
	public:
		static constexpr std::size_t kMaxLatches = 8;

		/// False when the table is full or the latch is already in it.
		[[nodiscard]] bool Register(KeyLatch& a_latch) noexcept;

		/// Harmless for a latch that is not registered.
		void Unregister(KeyLatch& a_latch) noexcept;

		/// From the window thread: every latch is offered the press.
		void Offer(std::uint32_t a_key) noexcept;

		[[nodiscard]] std::size_t Count() const noexcept;

	private:
		std::array<std::atomic<KeyLatch*>, kMaxLatches> _latches{};
	};

	[[nodiscard]] Hotkeys& TheHotkeys() noexcept;
}
