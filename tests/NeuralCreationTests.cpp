#include "Bridge/NeuralCreation.h"

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
	using Bridge::NeuralCreationGate;
	using Bridge::NeuralFeatureKey;

	const NeuralFeatureKey fastAt1440{ 2560, 1440, 2, 28 };
	const NeuralFeatureKey originalAt1440{ 2560, 1440, 1, 28 };
	const NeuralFeatureKey fastAt1080{ 1920, 1080, 2, 28 };

	NeuralCreationGate gate;
	Check(gate.MayCreate(fastAt1440, 0), "the first creation may run");

	gate.Submitted(1);
	Check(!gate.MayCreate(fastAt1440, 0), "not while the create list may still execute (timeout)");
	Check(gate.MayCreate(fastAt1440, 1), "once its fence passed");

	gate.Failed(fastAt1440);
	Check(!gate.MayCreate(fastAt1440, 1), "a failed key is not retried every frame");
	Check(gate.MayCreate(originalAt1440, 1), "another model may be tried");
	Check(gate.MayCreate(fastAt1080, 1), "another size may be tried");

	gate.Forget();
	Check(gate.MayCreate(fastAt1440, 1), "after a resize the failed key may be tried again");

	gate.Failed(fastAt1440);
	gate.Succeeded();
	Check(gate.MayCreate(fastAt1440, 1), "a success clears the failure");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
