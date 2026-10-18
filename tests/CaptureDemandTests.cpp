#include "Bridge/CaptureDemand.h"

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

	constexpr std::uint32_t kFrameGeneration = 1;
	constexpr std::uint32_t kNeuralRendering = 2;
}

int main()
{
	Bridge::CaptureDemand demand;
	Check(!demand.Active(), "nobody wants it at first");
	Check(demand.Want(kFrameGeneration, true), "the first consumer starts with a reset");
	Check(demand.Active(), "and the capture runs");
	Check(!demand.Want(kFrameGeneration, true), "wanting again is no new start");

	Check(demand.Want(kNeuralRendering, true), "a second consumer starting gets its reset");
	Check(!demand.Want(kFrameGeneration, false), "stopping is no reset");
	Check(demand.Active(), "the capture keeps running for the other one");
	Check(demand.Want(kFrameGeneration, true), "FG resuming while NR runs gets its reset (overlay closed)");

	Check(!demand.Want(kNeuralRendering, false) && !demand.Want(kFrameGeneration, false), "both stop");
	Check(!demand.Active(), "and the capture stops");

	static_cast<void>(demand.Want(kNeuralRendering, true));
	demand.Clear();
	Check(!demand.Active(), "clear forgets everyone");
	Check(demand.Want(kNeuralRendering, true), "and the next want starts afresh");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
