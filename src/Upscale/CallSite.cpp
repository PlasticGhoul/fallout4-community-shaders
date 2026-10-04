#include "Upscale/CallSite.h"

#include <cstring>

namespace Upscale
{
	std::optional<std::uintptr_t> CallTarget(std::span<const std::uint8_t, 5> a_bytes, std::uintptr_t a_site) noexcept
	{
		if (a_bytes[0] != 0xE8) {
			return std::nullopt;
		}
		std::int32_t relative = 0;
		std::memcpy(std::addressof(relative), a_bytes.data() + 1, sizeof(relative));
		return a_site + 5 + static_cast<std::intptr_t>(relative);
	}
}
