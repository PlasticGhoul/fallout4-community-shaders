#include "Render/PassStatistics.h"

#include <cstdio>
#include <string>

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

	{
		Render::PassTable table;
		const auto frame = table.Find("Frame");
		const auto overlay = table.Find("Overlay");

		Check(frame != Render::PassTable::kNoPass, "a new name gets a slot");
		Check(frame != overlay, "two names get two slots");
		Check(table.Find("Frame") == frame, "a known name gets its own slot back");

		table.Sample(frame, 0, 5.0f, 3.0f, 1);
		table.Sample(overlay, 1, 0.2f, 0.1f, 1);

		const auto results = table.Results();
		Check(results.size() == 2, "both passes are reported");
		Check(results[0].name == "Frame", "the first pass seen is reported first");
		Check(results[0].depth == 0 && results[1].depth == 1, "the depth is carried through");
		Check(Near(results[0].gpuMs, 5.0f), "the gpu time is carried through");
		Check(Near(results[1].cpuMs, 0.1f), "the cpu time is carried through");
	}

	{
		// The case that costs an afternoon when it is wrong: a pass drops out,
		// and the pass behind it must still find its own history.
		Render::PassTable table;
		const auto first = table.Find("First");
		const auto second = table.Find("Second");
		table.Sample(first, 0, 1.0f, 1.0f, 1);
		table.Sample(second, 0, 2.0f, 2.0f, 1);

		// Only the second keeps reporting.
		for (std::uint64_t frame = 2; frame <= Render::PassTable::kRetireFrames + 2; ++frame) {
			table.Sample(table.Find("Second"), 0, 2.0f, 2.0f, frame);
			table.Retire(frame);
		}

		const auto results = table.Results();
		Check(results.size() == 1, "a pass that stopped reporting is retired");
		Check(results.size() == 1 && results[0].name == "Second", "the right pass survived");
		Check(Near(results[0].gpuMs, 2.0f), "the survivor kept its own history");

		// Reaching the survivor is not enough to prove the index was rebuilt: a
		// stale index still returns a number. What proves it is that a sample
		// taken afterwards lands on the survivor's own history rather than
		// falling off the end of the table.
		const auto again = table.Find("Second");
		Check(again != Render::PassTable::kNoPass, "the survivor is still addressable");

		table.Sample(again, 0, 8.0f, 8.0f, Render::PassTable::kRetireFrames + 3);
		Check(Near(table.Results()[0].gpuMs, 8.0f), "a sample after a retire lands on the survivor");
	}

	{
		// Checked once rather than per slot: a hundred and twenty-eight lines of
		// "ok" would bury the two checks that matter below them.
		Render::PassTable table;
		bool allHandedOut = true;
		for (std::size_t i = 0; i < Render::PassTable::kMaxPasses; ++i) {
			const auto name = std::string{ "pass" } + std::to_string(i);
			allHandedOut = allHandedOut && table.Find(name) != Render::PassTable::kNoPass;
		}

		Check(allHandedOut, "every slot up to the cap is handed out");

		Check(table.Find("one too many") == Render::PassTable::kNoPass, "the cap refuses the next name");
		Check(table.Find("pass0") != Render::PassTable::kNoPass, "a known name still works at the cap");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
