#pragma once

#include <string_view>

namespace Bridge::Log
{
	/// The native half of the bridge cannot see REX - its headers clash with
	/// the Windows SDK's - so it logs through these, which do.
	void Info(std::string_view a_text) noexcept;
	void Warn(std::string_view a_text) noexcept;
	void Error(std::string_view a_text) noexcept;
}
