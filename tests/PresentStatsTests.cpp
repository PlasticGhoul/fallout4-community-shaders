#include "Bridge/PresentStats.h"

#include <cmath>
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
	Bridge::PresentStats stats;
	Check(!stats.Sample(10.0).has_value(), "the first sample only sets the clock");

	for (int i = 0; i < 90; ++i) {
		stats.Frame(false, 0);
	}
	Check(!stats.Sample(10.5).has_value(), "half a second is not a sample");

	for (int i = 0; i < 90; ++i) {
		stats.Frame(false, 7);
	}
	const auto off = stats.Sample(11.0);
	Check(off && std::abs(off->rendered - 180.0) < 1e-9 && std::abs(off->shown - 180.0) < 1e-9,
		"without generation every rendered frame is one shown, whatever Streamline counts");

	for (int i = 0; i < 70; ++i) {
		stats.Frame(true, 2);
	}
	const auto on = stats.Sample(12.0);
	Check(on && std::abs(on->rendered - 70.0) < 1e-9 && std::abs(on->shown - 140.0) < 1e-9,
		"with generation the shown count is what Streamline presented");
	Check(std::abs(stats.Last().shown - 140.0) < 1e-9, "Last keeps the latest sample for the display");

	for (int i = 0; i < 100; ++i) {
		stats.Frame(true, 2);
	}
	const auto longer = stats.Sample(14.0);
	Check(longer && std::abs(longer->rendered - 50.0) < 1e-9, "a longer stretch is divided by its own length");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
