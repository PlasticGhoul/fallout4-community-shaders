#pragma once

#include <cstdint>
#include <optional>

namespace Bridge
{
	/// Rendered against shown frames, per second. A rendered frame is one
	/// Present of the game's. Shown is what Streamline reports as actually
	/// presented since the last query, generated frames included - or one per
	/// rendered frame while nothing is generated, whatever Streamline counts
	/// then. The profiler keeps measuring rendered frames only.
	class PresentStats
	{
	public:
		struct Rates
		{
			double rendered{ 0.0 };
			double shown{ 0.0 };
		};

		void Frame(bool a_generating, std::uint32_t a_actuallyPresented) noexcept;

		/// The rates since the last sample, once at least a second has passed
		/// since it. The very first call only starts the clock.
		[[nodiscard]] std::optional<Rates> Sample(double a_nowSeconds) noexcept;

		[[nodiscard]] Rates Last() const noexcept { return _last; }

	private:
		double _since{ -1.0 };
		std::uint64_t _rendered{ 0 };
		std::uint64_t _shown{ 0 };
		Rates _last{};
	};
}
