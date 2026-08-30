#include "Runtime.h"

namespace Runtime
{
	bool IsSupported(REL::Version a_version) noexcept
	{
		return a_version == kSupported;
	}

	std::string_view BucketName() noexcept
	{
		switch (REX::FModule::GetRuntimeIndex()) {
		case REX::FModule::Runtime::kOG:
			return "OG"sv;
		case REX::FModule::Runtime::kNG:
			return "NG"sv;
		case REX::FModule::Runtime::kAE:
			return "AE"sv;
		default:
			return "unknown"sv;
		}
	}
}
