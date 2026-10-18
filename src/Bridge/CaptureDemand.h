#pragma once

#include <cstdint>

namespace Bridge
{
	/// Which consumers want the bridge capture this frame. Each consumer that
	/// starts wanting it again gets a reset: its history is from before its
	/// own off stretch, even if another consumer kept the capture running.
	class CaptureDemand
	{
	public:
		/// True when this consumer just started wanting the capture.
		[[nodiscard]] bool Want(std::uint32_t a_consumer, bool a_want) noexcept
		{
			const bool had = (_wanted & a_consumer) != 0;
			if (a_want) {
				_wanted |= a_consumer;
			} else {
				_wanted &= ~a_consumer;
			}
			return a_want && !had;
		}
		[[nodiscard]] bool Active() const noexcept { return _wanted != 0; }
		void Clear() noexcept { _wanted = 0; }

	private:
		std::uint32_t _wanted{ 0 };
	};
}
