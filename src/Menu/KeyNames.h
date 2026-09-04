#pragma once

#include <cstdint>
#include <string>

namespace Menu
{
	/// Builds the parameter GetKeyNameTextW wants from an extended scan code,
	/// the kind MapVirtualKeyW returns for MAPVK_VK_TO_VSC_EX.
	///
	/// Two fields matter. Bits 16..23 carry the scan code. Bit 24 says the key
	/// is an extended one, and without it a scan code that two keys share is
	/// reported as the wrong one of the two: End and the numeric keypad's 1
	/// are both scan code 0x4F, and only the extended bit tells them apart.
	///
	/// MAPVK_VK_TO_VSC_EX puts 0xE0 - or 0xE1, for Pause - in the high byte of
	/// its result for exactly those keys, which is where the bit comes from.
	[[nodiscard]] inline constexpr std::int32_t KeyNameParam(std::uint32_t a_scanCodeEx) noexcept
	{
		const auto prefix = (a_scanCodeEx >> 8) & 0xFFu;
		const auto scanCode = a_scanCodeEx & 0xFFu;

		std::uint32_t param = scanCode << 16;
		if (prefix == 0xE0u || prefix == 0xE1u) {
			param |= 1u << 24;
		}

		return static_cast<std::int32_t>(param);
	}

	/// The name Windows gives a virtual key code, in UTF-8. Empty when the code
	/// has no name, which is true of a few keys and of anything that is not one.
	[[nodiscard]] std::string KeyName(std::uint32_t a_key);
}
