#include "Util/SafeRead.h"

#include <Windows.h>

#include <cstdint>

namespace Util
{
	namespace
	{
		// Every protection under which a read succeeds. PAGE_NOACCESS and
		// PAGE_EXECUTE are deliberately absent, and PAGE_GUARD turns any of the
		// others into a fault on first touch.
		constexpr DWORD kReadable =
			PAGE_READONLY |
			PAGE_READWRITE |
			PAGE_WRITECOPY |
			PAGE_EXECUTE_READ |
			PAGE_EXECUTE_READWRITE |
			PAGE_EXECUTE_WRITECOPY;
	}

	bool IsReadableRange(const void* a_address, std::size_t a_size) noexcept
	{
		if (a_address == nullptr || a_size == 0) {
			return false;
		}

		auto cursor = reinterpret_cast<std::uintptr_t>(a_address);
		const auto end = cursor + a_size;
		if (end < cursor) {
			return false;  // The range wrapped, so it was never a range.
		}

		// A range can straddle several regions, and only the first is described
		// by one query. Walking region by region is what makes the answer hold
		// for the whole span rather than for its first byte.
		while (cursor < end) {
			MEMORY_BASIC_INFORMATION info{};
			if (::VirtualQuery(reinterpret_cast<const void*>(cursor), &info, sizeof(info)) == 0) {
				return false;
			}

			if (info.State != MEM_COMMIT) {
				return false;
			}

			if ((info.Protect & kReadable) == 0 || (info.Protect & PAGE_GUARD) != 0) {
				return false;
			}

			const auto regionEnd =
				reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
			if (regionEnd <= cursor) {
				return false;  // No progress, so no termination either.
			}

			cursor = regionEnd;
		}

		return true;
	}
}
