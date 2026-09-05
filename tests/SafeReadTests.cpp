#include "Util/SafeRead.h"

#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}
}

int main()
{
	Check(!Util::IsReadableRange(nullptr, 8), "a null address is not readable");

	int local = 0;
	Check(!Util::IsReadableRange(&local, 0), "an empty range is not readable");
	Check(Util::IsReadableRange(&local, sizeof(local)), "a stack variable is readable");

	auto heap = std::make_unique<std::uint8_t[]>(64);
	Check(Util::IsReadableRange(heap.get(), 64), "a heap allocation is readable");

	// An address no allocation reaches. Reading it would end the process, which
	// is the whole reason for asking first.
	Check(
		!Util::IsReadableRange(reinterpret_cast<const void*>(std::uintptr_t{ 0x10 }), 8),
		"the low address space is not readable");
	Check(
		!Util::IsReadableRange(
			reinterpret_cast<const void*>(std::uintptr_t{ 0x0000'7FFF'FFFF'0000 }),
			8),
		"the top of user address space is not readable");

	// Reserved but never committed: the pages exist as an address range and
	// nothing else. VirtualQuery is what tells the two apart.
	auto* const reserved = ::VirtualAlloc(nullptr, 0x2000, MEM_RESERVE, PAGE_READWRITE);
	Check(reserved != nullptr, "reserved a range to test against");
	if (reserved != nullptr) {
		Check(!Util::IsReadableRange(reserved, 8), "reserved but uncommitted memory is not readable");

		auto* const committed = ::VirtualAlloc(reserved, 0x1000, MEM_COMMIT, PAGE_READWRITE);
		Check(committed != nullptr, "committed the first page");
		if (committed != nullptr) {
			Check(Util::IsReadableRange(committed, 0x1000), "the committed page is readable");

			// The decisive case: a range that starts in a good region and runs
			// into a bad one. Asking only about the first byte would say yes.
			Check(
				!Util::IsReadableRange(committed, 0x1800),
				"a range running past the committed page is not readable");
		}

		auto* const noAccess = ::VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
		Check(noAccess != nullptr, "committed a page without access");
		if (noAccess != nullptr) {
			Check(!Util::IsReadableRange(noAccess, 8), "a PAGE_NOACCESS page is not readable");
			::VirtualFree(noAccess, 0, MEM_RELEASE);
		}

		::VirtualFree(reserved, 0, MEM_RELEASE);
	}

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
