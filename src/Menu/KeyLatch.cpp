#include "Menu/KeyLatch.h"

namespace Menu
{
	void KeyLatch::SetKey(std::uint32_t a_key) noexcept
	{
		_key.store(a_key, std::memory_order_relaxed);
	}

	std::uint32_t KeyLatch::Key() const noexcept
	{
		return _key.load(std::memory_order_relaxed);
	}

	void KeyLatch::Offer(std::uint32_t a_key) noexcept
	{
		const auto wanted = _key.load(std::memory_order_relaxed);
		if (wanted == 0 || a_key != wanted) {
			return;
		}

		_pending.store(true, std::memory_order_relaxed);
	}

	bool KeyLatch::Take() noexcept
	{
		// Exchange rather than load and clear: the window thread may set this
		// between the two, and that press would be lost.
		return _pending.exchange(false, std::memory_order_relaxed);
	}
}
