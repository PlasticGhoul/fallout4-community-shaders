#include "Render/PassStatistics.h"

#include <algorithm>
#include <cmath>

namespace Render
{
	void PassStatistics::Push(float a_ms) noexcept
	{
		_history[_head] = a_ms;
		_head = (_head + 1) % kHistorySize;
		if (_count < kHistorySize) {
			++_count;
		}

		_last = a_ms;
	}

	void PassStatistics::Clear() noexcept
	{
		_head = 0;
		_count = 0;
		_last = 0.0f;
	}

	float PassStatistics::Average() const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		float total = 0.0f;
		for (std::size_t i = 0; i < _count; ++i) {
			total += Sample(i);
		}

		return total / static_cast<float>(_count);
	}

	float PassStatistics::Percentile(float a_p) const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		// Sorted on demand rather than kept sorted: this runs once per pass per
		// drawn frame, over at most three hundred floats, and a second ordered
		// structure would have to be kept right on every push.
		std::array<float, kHistorySize> sorted{};
		for (std::size_t i = 0; i < _count; ++i) {
			sorted[i] = Sample(i);
		}
		std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(_count));

		const float clamped = std::clamp(a_p, 0.0f, 100.0f);
		const float rank = clamped / 100.0f * static_cast<float>(_count - 1);
		const auto lower = static_cast<std::size_t>(std::floor(rank));
		const auto upper = std::min(lower + 1, _count - 1);
		const float fraction = rank - static_cast<float>(lower);

		return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
	}

	float PassStatistics::Sample(std::size_t a_index) const noexcept
	{
		if (a_index >= _count) {
			return 0.0f;
		}

		// _head points at the next slot to write, so the oldest sample sits
		// _count places behind it.
		const std::size_t oldest = (_head + kHistorySize - _count) % kHistorySize;
		return _history[(oldest + a_index) % kHistorySize];
	}
}
