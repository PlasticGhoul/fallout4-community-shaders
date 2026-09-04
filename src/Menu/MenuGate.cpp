#include "Menu/MenuGate.h"

namespace Menu
{
	Gate::Gate(Suppress a_suppress, Restore a_restore) :
		_suppress(std::move(a_suppress)),
		_restore(std::move(a_restore))
	{}

	void Gate::SetToggleKey(std::uint32_t a_key) noexcept
	{
		_toggleKey = a_key;
	}

	bool Gate::IsToggleKey(std::uint32_t a_key) const noexcept
	{
		return _toggleKey != 0 && a_key == _toggleKey;
	}

	void Gate::RequestToggle() noexcept
	{
		_toggleWanted.store(true, std::memory_order_release);
	}

	bool Gate::Tick() noexcept
	{
		if (!_toggleWanted.exchange(false, std::memory_order_acq_rel)) {
			return _open;
		}

		if (_open) {
			// Only give back what was actually taken. A suppression that failed
			// left the engine untouched, and restoring it would be a second
			// call into something that never worked.
			if (_suppressing && _restore) {
				_restore();
			}
			_suppressing = false;
			_open = false;
			return false;
		}

		_suppressing = _suppress && _suppress();
		_open = true;
		return true;
	}
}
