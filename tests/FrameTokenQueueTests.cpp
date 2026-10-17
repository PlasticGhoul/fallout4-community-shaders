#include "Bridge/FrameTokenQueue.h"

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
	Bridge::FrameTokenQueue queue;
	Check(!queue.Pop().has_value(), "an empty queue hands out nothing, never an invented index");

	queue.Push(10);
	queue.Push(11);
	Check(queue.Pop() == 10u, "the oldest frame comes first");
	Check(queue.Pop() == 11u, "then the next");
	Check(!queue.Pop().has_value(), "and then nothing");

	for (std::uint32_t index = 20; index < 25; ++index) {
		queue.Push(index);
	}
	Check(queue.Dropped() == 2, "beyond three in flight the oldest are dropped and counted");
	Check(queue.Pop() == 22u, "what remains starts at the oldest kept");
	Check(queue.Pop() == 23u && queue.Pop() == 24u, "in order");

	queue.Push(30);
	queue.Clear();
	Check(!queue.Pop().has_value() && queue.Dropped() == 2, "Clear empties the queue and keeps the count");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
