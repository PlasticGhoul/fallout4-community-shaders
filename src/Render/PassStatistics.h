#pragma once

#include <array>
#include <cstddef>

namespace Render
{
	/// A pass's timings over the recent past, as a ring of samples.
	///
	/// Deliberately free of D3D: this is the half of the profiler that
	/// computes, and computing is the half that can be tested without a game.
	class PassStatistics
	{
	public:
		/// Three hundred samples is about 1.7 seconds at 180 fps and five at
		/// 60. Long enough for a p99 to mean something, short enough that
		/// walking into a new cell shows up rather than being averaged away.
		static constexpr std::size_t kHistorySize = 300;

		void Push(float a_ms) noexcept;
		void Clear() noexcept;

		[[nodiscard]] float Last() const noexcept { return _last; }
		[[nodiscard]] std::size_t Count() const noexcept { return _count; }

		[[nodiscard]] float Average() const noexcept;

		/// Interpolated, with a_p in [0, 100]. Zero when there is nothing to
		/// take a percentile of.
		[[nodiscard]] float Percentile(float a_p) const noexcept;

		/// Oldest first, which is the order a plot wants. Zero past the end.
		[[nodiscard]] float Sample(std::size_t a_index) const noexcept;

	private:
		std::array<float, kHistorySize> _history{};
		std::size_t _head{ 0 };
		std::size_t _count{ 0 };
		float _last{ 0.0f };
	};
}
