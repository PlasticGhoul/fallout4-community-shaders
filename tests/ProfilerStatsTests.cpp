#include "Render/PassStatistics.h"

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

	bool Near(float a_value, float a_expected)
	{
		const auto difference = a_value > a_expected ? a_value - a_expected : a_expected - a_value;
		return difference < 0.001f;
	}
}

int main()
{
	{
		Render::PassStatistics stats;
		Check(stats.Count() == 0, "a fresh history is empty");
		Check(stats.Average() == 0.0f, "an empty history averages zero");
		Check(stats.Percentile(50.0f) == 0.0f, "an empty history has no median");
	}

	{
		Render::PassStatistics stats;
		stats.Push(2.0f);
		Check(stats.Count() == 1, "one sample counts as one");
		Check(Near(stats.Last(), 2.0f), "the last sample is the one pushed");
		Check(Near(stats.Average(), 2.0f), "one sample averages itself");
		Check(Near(stats.Percentile(0.0f), 2.0f), "p0 of one sample is that sample");
		Check(Near(stats.Percentile(100.0f), 2.0f), "p100 of one sample is that sample");
	}

	{
		Render::PassStatistics stats;
		for (int i = 1; i <= 4; ++i) {
			stats.Push(static_cast<float>(i));
		}
		Check(Near(stats.Average(), 2.5f), "four samples average correctly");
		Check(Near(stats.Percentile(0.0f), 1.0f), "p0 is the smallest sample");
		Check(Near(stats.Percentile(100.0f), 4.0f), "p100 is the largest sample");

		// Rank 0.5 * 3 = 1.5, so halfway between the second and third sample.
		Check(Near(stats.Percentile(50.0f), 2.5f), "the median interpolates between neighbours");
	}

	{
		// The samples arrive unsorted; a percentile has to sort them.
		Render::PassStatistics stats;
		stats.Push(9.0f);
		stats.Push(1.0f);
		stats.Push(5.0f);
		Check(Near(stats.Percentile(0.0f), 1.0f), "p0 finds the smallest whatever the order");
		Check(Near(stats.Percentile(100.0f), 9.0f), "p100 finds the largest whatever the order");
	}

	{
		// One more than the buffer holds: the oldest has to fall out, and the
		// order the plot reads has to survive the wrap.
		Render::PassStatistics stats;
		for (std::size_t i = 0; i < Render::PassStatistics::kHistorySize + 1; ++i) {
			stats.Push(static_cast<float>(i));
		}
		Check(stats.Count() == Render::PassStatistics::kHistorySize, "the buffer stops at its size");
		Check(Near(stats.Sample(0), 1.0f), "the oldest sample fell out");
		Check(
			Near(stats.Sample(Render::PassStatistics::kHistorySize - 1),
				static_cast<float>(Render::PassStatistics::kHistorySize)),
			"the newest sample is last");
		Check(Near(stats.Last(), static_cast<float>(Render::PassStatistics::kHistorySize)),
			"the last sample is the newest");
	}

	{
		Render::PassStatistics stats;
		stats.Push(1.0f);
		stats.Clear();
		Check(stats.Count() == 0, "clearing empties the history");
		Check(stats.Last() == 0.0f, "clearing forgets the last sample");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
