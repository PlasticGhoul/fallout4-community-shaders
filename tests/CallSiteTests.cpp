#include "Upscale/CallSite.h"

#include <array>
#include <cstdio>

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
	// The UpdateTemporalData site on 1.11.240: rva 0x21f0e4c (0x21f0ac0 +
	// 0x38c), e8 2f c7 63 ff, target rva 0x182d580 (id 2277095).
	const std::array<std::uint8_t, 5> call{ 0xE8, 0x2F, 0xC7, 0x63, 0xFF };
	const auto target = Upscale::CallTarget(call, 0x21F0E4C);
	Check(target.has_value() && *target == 0x182D580, "a backward call resolves to its target");

	// The composite site: rva 0x21f8bb5 (0x21f82a0 + 0x915), e8 a6 6f 02 00,
	// target rva 0x221fb60 (id 2318699).
	const std::array<std::uint8_t, 5> forward{ 0xE8, 0xA6, 0x6F, 0x02, 0x00 };
	const auto ahead = Upscale::CallTarget(forward, 0x21F8BB5);
	Check(ahead.has_value() && *ahead == 0x221FB60, "a forward call resolves to its target");

	const std::array<std::uint8_t, 5> jump{ 0xE9, 0x00, 0x00, 0x00, 0x00 };
	Check(!Upscale::CallTarget(jump, 0x1000).has_value(), "a jump is not a call site");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
