#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace Upscale
{
	/// The target of a five byte relative call (E8 rel32) at a_site, or
	/// nothing when the bytes are not one. Pure, so that the check every hook
	/// makes before it writes can be tested without the game.
	[[nodiscard]] std::optional<std::uintptr_t> CallTarget(std::span<const std::uint8_t, 5> a_bytes, std::uintptr_t a_site) noexcept;
}
