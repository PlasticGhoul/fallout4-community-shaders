#include "Bridge/Log.h"

namespace Bridge::Log
{
	void Info(std::string_view a_text) noexcept
	{
		REX::INFO("{}", a_text);
	}

	void Warn(std::string_view a_text) noexcept
	{
		REX::WARN("{}", a_text);
	}

	void Error(std::string_view a_text) noexcept
	{
		REX::ERROR("{}", a_text);
	}
}
