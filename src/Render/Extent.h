#pragma once

#include <cstdint>

namespace Render
{
	/// A size in pixels. Its own type rather than two loose integers because
	/// F5.5a carries two of them through every pass - the size the world is
	/// rendered at and the size it is shown at - and mixing them up is the
	/// error this subproject is most exposed to.
	struct Extent
	{
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };

		bool operator==(const Extent&) const = default;
	};
}
