#include "Util/ObjectRTTI.h"

#include "Util/SafeRead.h"

#include <REX/W32/RTTI.h>

#include <cstring>
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

		// A decorated class name longer than this is not one of the engine's.
		// The bound exists so the name can be read out of memory that was only
		// proven readable up to a point.
		constexpr std::size_t kMaxNameLength = 512;
	}

	std::optional<ObjectInfo> DescribeObject(const void* a_object) noexcept
	{
		// Every dereference from here on follows a pointer that may be a guess:
		// callers ask about slots found by scanning, and about objects reached
		// through measured offsets. Each one is proven readable first, which is
		// what turns a wrong guess into an empty answer instead of a crash.
		if (!IsReadableRange(a_object, sizeof(void*))) {
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
		const auto* const slot =
			reinterpret_cast<const REX::W32::RTTICompleteObjectLocator* const*>(a_vtable) - 1;
		if (!IsReadableRange(slot, sizeof(void*))) {
			return std::nullopt;
		}

		const auto* const locator = *slot;
		if (!IsReadableRange(locator, sizeof(REX::W32::RTTICompleteObjectLocator)) ||
			locator->signature != 1) {
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
		if (!IsReadableRange(descriptor, offsetof(TypeDescriptor, name) + kMaxNameLength)) {
			return std::nullopt;
		}

		// Bounded rather than strlen: the readability just proven reaches
		// kMaxNameLength and no further.
		std::string_view decorated{
			descriptor->name,
			::strnlen(descriptor->name, kMaxNameLength)
		};

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
