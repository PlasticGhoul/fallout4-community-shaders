#include "Runtime.h"

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
	Check(
		Runtime::IsSupported(F4SE::RUNTIME_1_11_240),
		"accepts the validated runtime 1.11.240");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_163),
		"rejects OG 1.10.163");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_980),
		"rejects NG 1.10.980");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_984),
		"rejects NG 1.10.984");
	Check(
		!Runtime::IsSupported(REL::Version{ 1, 11, 241, 0 }),
		"rejects a future version that would fall through to the AE bucket");
	Check(
		!Runtime::IsSupported(REL::Version{ 1, 11, 240, 1 }),
		"rejects a differing build field");

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
