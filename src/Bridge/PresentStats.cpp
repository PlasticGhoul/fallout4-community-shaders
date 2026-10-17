#include "Bridge/PresentStats.h"

namespace Bridge
{
	void PresentStats::Frame(bool a_generating, std::uint32_t a_actuallyPresented) noexcept
	{
		++_rendered;
		_shown += a_generating ? a_actuallyPresented : 1u;
	}

	std::optional<PresentStats::Rates> PresentStats::Sample(double a_nowSeconds) noexcept
	{
		if (_since < 0.0) {
			_since = a_nowSeconds;
			_rendered = 0;
			_shown = 0;
			return std::nullopt;
		}
		const auto span = a_nowSeconds - _since;
		if (span < 1.0) {
			return std::nullopt;
		}
		_last = Rates{ static_cast<double>(_rendered) / span, static_cast<double>(_shown) / span };
		_since = a_nowSeconds;
		_rendered = 0;
		_shown = 0;
		return _last;
	}
}
