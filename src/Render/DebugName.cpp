#include "Render/DebugName.h"

namespace Render
{
	namespace
	{
		// WKPDID_D3DDebugObjectName. REX::W32 does not declare it, so it is
		// defined here once - and only here. The inherited Skyrim project made
		// exactly this a rule: never duplicate the GUID across call sites.
		inline constexpr REX::W32::GUID kDebugObjectName{
			0x429B8C22,
			0x9188,
			0x4B0C,
			{ 0x87, 0x42, 0xAC, 0xB0, 0xBF, 0x85, 0xC2, 0x00 }
		};
	}

	bool SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept
	{
		if (a_object == nullptr || a_name.empty()) {
			return false;
		}

		// The name is stored as a plain byte blob without a terminator; the
		// length is what identifies its extent.
		const auto result = a_object->SetPrivateData(
			kDebugObjectName,
			static_cast<std::uint32_t>(a_name.size()),
			a_name.data());

		return result >= 0;
	}
}
