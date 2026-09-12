#include "Menu/Hotkeys.h"

#include "Menu/KeyLatch.h"

#include <memory>

namespace Menu
{
	bool Hotkeys::Register(KeyLatch& a_latch) noexcept
	{
		for (auto& slot : _latches) {
			if (slot.load(std::memory_order_acquire) == std::addressof(a_latch)) {
				return false;
			}
		}

		for (auto& slot : _latches) {
			KeyLatch* expected = nullptr;
			if (slot.compare_exchange_strong(
					expected, std::addressof(a_latch), std::memory_order_acq_rel)) {
				return true;
			}
		}

		return false;
	}

	void Hotkeys::Unregister(KeyLatch& a_latch) noexcept
	{
		for (auto& slot : _latches) {
			KeyLatch* expected = std::addressof(a_latch);
			if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
				return;
			}
		}
	}

	void Hotkeys::Offer(std::uint32_t a_key) noexcept
	{
		for (auto& slot : _latches) {
			if (auto* const latch = slot.load(std::memory_order_acquire); latch != nullptr) {
				latch->Offer(a_key);
			}
		}
	}

	std::size_t Hotkeys::Count() const noexcept
	{
		std::size_t count = 0;
		for (const auto& slot : _latches) {
			if (slot.load(std::memory_order_acquire) != nullptr) {
				++count;
			}
		}
		return count;
	}

	Hotkeys& TheHotkeys() noexcept
	{
		static Hotkeys hotkeys;
		return hotkeys;
	}
}
