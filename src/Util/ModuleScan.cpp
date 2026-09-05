#include "Util/ModuleScan.h"

#include "Util/SafeRead.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>

namespace Util
{
	namespace
	{
		// A pointer to an engine object lives in a section that the loader made
		// writable. The read only ones hold vtables and string literals, which
		// is not what we are looking for and is by far the larger part of the
		// image.
		[[nodiscard]] bool HoldsObjectPointers(const IMAGE_SECTION_HEADER& a_section) noexcept
		{
			return (a_section.Characteristics & IMAGE_SCN_MEM_WRITE) != 0 &&
			       (a_section.Characteristics & IMAGE_SCN_MEM_READ) != 0 &&
			       (a_section.Characteristics & IMAGE_SCN_CNT_CODE) == 0;
		}
	}

	std::vector<void* const*> FindPointerInModuleData(const void* a_value, std::size_t a_limit)
	{
		std::vector<void* const*> found;
		if (a_value == nullptr || a_limit == 0) {
			return found;
		}

		const auto base = REX::FModule::GetExecutingModule().GetBaseAddress();
		if (base == 0 || !IsReadableRange(reinterpret_cast<const void*>(base), sizeof(IMAGE_DOS_HEADER))) {
			return found;
		}

		const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return found;
		}

		const auto* const nt =
			reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + static_cast<std::uintptr_t>(dos->e_lfanew));
		if (!IsReadableRange(nt, sizeof(IMAGE_NT_HEADERS64)) || nt->Signature != IMAGE_NT_SIGNATURE) {
			return found;
		}

		const auto* section = IMAGE_FIRST_SECTION(nt);
		const auto count = nt->FileHeader.NumberOfSections;

		for (std::uint16_t i = 0; i < count && found.size() < a_limit; ++i, ++section) {
			if (!IsReadableRange(section, sizeof(IMAGE_SECTION_HEADER)) ||
				!HoldsObjectPointers(*section)) {
				continue;
			}

			const auto start = base + section->VirtualAddress;
			const auto size = static_cast<std::size_t>(section->Misc.VirtualSize);
			if (size < sizeof(void*) || !IsReadableRange(reinterpret_cast<const void*>(start), size)) {
				continue;
			}

			// A pointer the compiler stored is aligned, so only aligned places
			// are worth reading. That is also what makes the read safe: the
			// whole section was just proven readable.
			const auto first = (start + alignof(void*) - 1) & ~(alignof(void*) - 1);
			const auto last = start + size;

			for (auto cursor = first; cursor + sizeof(void*) <= last; cursor += alignof(void*)) {
				if (*reinterpret_cast<void* const*>(cursor) == a_value) {
					found.push_back(reinterpret_cast<void* const*>(cursor));
					if (found.size() >= a_limit) {
						break;
					}
				}
			}
		}

		return found;
	}
}
