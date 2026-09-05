#include "Menu/KeyLatch.h"

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
	{
		Menu::KeyLatch latch;
		Check(!latch.Take(), "a fresh latch has nothing to take");
	}

	{
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		Check(latch.Key() == 0x7A, "the latch reports the key it was given");
		latch.Offer(0x7A);
		Check(latch.Take(), "the bound key latches");
		Check(!latch.Take(), "and is taken only once");
	}

	{
		// Key repeat sends the same press over and over. One press, one action.
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x7A);
		latch.Offer(0x7A);
		latch.Offer(0x7A);
		Check(latch.Take(), "repeats latch");
		Check(!latch.Take(), "repeats collapse into one");
	}

	{
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x70);
		Check(!latch.Take(), "another key does not latch");
	}

	{
		// A settings file naming no key must not fire on every keystroke.
		Menu::KeyLatch latch;
		latch.SetKey(0);
		latch.Offer(0);
		Check(!latch.Take(), "key zero matches nothing");
		latch.Offer(0x7A);
		Check(!latch.Take(), "an unbound latch takes nothing");
	}

	{
		// The key is read from the settings every frame, so it can change while
		// a press is pending. The press was legitimate when it happened.
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x7A);
		latch.SetKey(0x70);
		Check(latch.Take(), "a pending press survives a rebind");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
