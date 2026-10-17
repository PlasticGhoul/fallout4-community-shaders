#include "Bridge/SlotRing.h"

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
	Bridge::SlotRing ring;
	Check(ring.Current() == 0 && ring.ReleaseValue() == 0, "a fresh ring starts at slot 0 with nothing to wait for");
	Check(ring.Reusable(0), "and its allocator may be reset at once");

	ring.Submit(5);
	Check(ring.Current() == 1 && ring.ReleaseValue() == 0, "the second slot was never handed over either");

	ring.Submit(9);
	Check(ring.Current() == 0 && ring.ReleaseValue() == 5, "back at slot 0, D3D11 waits for what D3D12 signals after reading it");
	Check(!ring.Reusable(4), "before that value completes the allocator is still in use");
	Check(ring.Reusable(5), "once it has, it is free");

	ring.Submit(12);
	Check(ring.Current() == 1 && ring.ReleaseValue() == 9, "slot 1 waits for its own value, not for slot 0's");

	ring.Reset();
	Check(ring.Current() == 0 && ring.ReleaseValue() == 0 && ring.Reusable(0), "Reset forgets every handed-over value");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
