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

	std::string GetDebugName(REX::W32::ID3D11DeviceChild* a_object) noexcept
	{
		if (a_object == nullptr) {
			return {};
		}

		// Asked with a null buffer, GetPrivateData reports the size it would
		// need. A resource that was never named answers zero.
		std::uint32_t size = 0;
		if (a_object->GetPrivateData(kDebugObjectName, &size, nullptr) < 0 || size == 0) {
			return {};
		}

		std::string name(size, '\0');
		if (a_object->GetPrivateData(kDebugObjectName, &size, name.data()) < 0) {
			return {};
		}

		// The blob carries no terminator of its own, but a name set by another
		// tool may well carry one. Cut at the first, so a name never grows a
		// trailing null in the log.
		name.resize(name.find('\0') == std::string::npos ? size : name.find('\0'));
		return name;
	}

	std::string GetViewTargetName(REX::W32::ID3D11View* a_view) noexcept
	{
		if (a_view == nullptr) {
			return {};
		}

		REX::W32::ID3D11Resource* resource = nullptr;
		a_view->GetResource(&resource);
		if (resource == nullptr) {
			return {};
		}

		// GetResource hands out a reference of its own.
		auto name = GetDebugName(resource);
		resource->Release();
		return name;
	}
}
