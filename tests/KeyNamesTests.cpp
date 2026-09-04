#include "Menu/KeyNames.h"

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
	// A plain key: the scan code goes into bits 16..23 and nothing else is set.
	// 0x4F is the numeric keypad's 1.
	Check(Menu::KeyNameParam(0x004F) == 0x004F0000, "a plain scan code lands in bits 16..23");

	// The same scan code with the 0xE0 prefix is the dedicated End key, and the
	// two are told apart by bit 24 alone. Without it Windows names the wrong
	// one of the pair - which is how "End" came back as "1 (numeric keypad)".
	Check(Menu::KeyNameParam(0xE04F) == 0x014F0000, "an extended one also sets bit 24");

	Check(
		Menu::KeyNameParam(0x004F) != Menu::KeyNameParam(0xE04F),
		"so the two keys that share a scan code do not share a parameter");

	// 0xE1 is Pause, the one key with a prefix of its own.
	Check(Menu::KeyNameParam(0xE145) == 0x01450000, "the 0xE1 prefix counts as extended too");

	// Anything else in the high byte is not a prefix and must not set the bit.
	Check(Menu::KeyNameParam(0x1234) == 0x00340000, "another high byte does not");

	// A key with no scan code at all yields no parameter to ask about.
	Check(Menu::KeyNameParam(0x0000) == 0, "a scan code of zero yields zero");

	// The function is usable where a constant is required, which is what keeps
	// the table above honest about being pure arithmetic.
	static_assert(Menu::KeyNameParam(0xE04F) == 0x014F0000);

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
