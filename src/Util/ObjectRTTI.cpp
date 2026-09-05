#include "Util/ObjectRTTI.h"

#include <REX/W32/RTTI.h>

#include <string_view>

namespace Util
{
	namespace
	{
		// MSVC's type descriptor: a vftable pointer, a spare, then the
		// decorated name as a plain null terminated string.
		struct TypeDescriptor
		{
			const void* vftable;
			void* spare;
			char name[1];
		};

	}

	std::optional<ObjectInfo> DescribeObject(const void* a_object) noexcept
	{
		if (a_object == nullptr) {
			return std::nullopt;
		}

		return DescribeVTable(*static_cast<const void* const* const*>(a_object));
	}

	std::optional<ObjectInfo> DescribeVTable(const void* const* a_vtable) noexcept
	{
		if (a_vtable == nullptr) {
			return std::nullopt;
		}

		// MSVC stores a pointer to the complete object locator immediately
		// before the first entry of every polymorphic vtable.
		const auto* const locator =
			*(reinterpret_cast<const REX::W32::RTTICompleteObjectLocator* const*>(a_vtable) - 1);

		if (locator == nullptr || locator->signature != 1) {
			return std::nullopt;
		}

		// The locator records its own RVA. Recomputing the module base from it
		// and comparing against the game module proves the vtable really
		// belongs to Fallout4.exe rather than to whatever the pointer happened
		// to land in.
		const auto base = reinterpret_cast<std::uintptr_t>(locator) - locator->self;
		if (base != REX::FModule::GetExecutingModule().GetBaseAddress()) {
			return std::nullopt;
		}

		const auto* const descriptor =
			reinterpret_cast<const TypeDescriptor*>(base + locator->typeDescriptor);

		std::string_view decorated{ descriptor->name };
		if (!decorated.starts_with(".?AV")) {
			return std::nullopt;
		}

		decorated.remove_prefix(4);
		if (decorated.ends_with("@@")) {
			decorated.remove_suffix(2);
		}

		return ObjectInfo{ std::string{ decorated }, locator->offset };
	}
}
