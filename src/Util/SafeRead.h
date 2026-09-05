#pragma once

#include <cstddef>

namespace Util
{
	/// Whether every page of [a_address, a_address + a_size) is committed and
	/// readable right now.
	///
	/// This exists because reading the engine through measured offsets means
	/// sometimes being wrong about them, and being wrong about a pointer costs
	/// the whole process. Asking first turns a crash into a log line.
	///
	/// It answers about this moment only. Nothing here makes a pointer stay
	/// valid, so it belongs immediately before the read it guards, not in a
	/// cache.
	[[nodiscard]] bool IsReadableRange(const void* a_address, std::size_t a_size) noexcept;
}
