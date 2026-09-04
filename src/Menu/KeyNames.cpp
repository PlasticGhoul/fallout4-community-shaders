#include "Menu/KeyNames.h"

#include "Menu/Win32.h"

#include <REX/W32/KERNEL32.h>

#include <iterator>

namespace Menu
{
	std::string KeyName(std::uint32_t a_key)
	{
		if (a_key == 0) {
			return {};
		}

		const auto scanCodeEx = Win32::MapVirtualKeyW(a_key, Win32::MAPVK_VK_TO_VSC_EX);
		if (scanCodeEx == 0) {
			return {};
		}

		wchar_t wide[64]{};
		const auto length = REX::W32::GetKeyNameTextW(
			KeyNameParam(scanCodeEx),
			wide,
			static_cast<std::int32_t>(std::size(wide)));

		if (length <= 0) {
			return {};
		}

		// Through UTF-8 rather than the A variant of GetKeyNameText: a key name
		// on a non-Latin layout is not ASCII, and the ANSI form would hand
		// ImGui bytes it draws as mojibake.
		const auto bytes = REX::W32::WideCharToMultiByte(
			REX::W32::CP_UTF8,
			0,
			wide,
			length,
			nullptr,
			0,
			nullptr,
			nullptr);

		if (bytes <= 0) {
			return {};
		}

		std::string name(static_cast<std::size_t>(bytes), '\0');
		REX::W32::WideCharToMultiByte(
			REX::W32::CP_UTF8,
			0,
			wide,
			length,
			name.data(),
			bytes,
			nullptr,
			nullptr);

		return name;
	}
}
