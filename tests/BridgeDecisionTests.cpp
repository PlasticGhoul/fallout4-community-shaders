#include "Bridge/Decision.h"

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

	Bridge::DecisionFacts Good()
	{
		Bridge::DecisionFacts facts{};
		facts.switchOn = true;
		facts.swapChainRequested = true;
		facts.windowed = true;
		facts.vendorId = Bridge::kVendorNvidia;
		return facts;
	}
}

int main()
{
	using Bridge::Decide;

	const auto yes = Decide(Good());
	Check(yes.build && yes.reason.empty(), "everything in place builds the bridge");

	auto facts = Good();
	facts.switchOn = false;
	facts.switchReason = "FrameGeneration/enabled is false";
	Check(Decide(facts).reason ==
			  "the switch was off when the game started (FrameGeneration/enabled is false); a restart applies it",
		"switch off, with the switch's own reason inside");

	facts = Good();
	facts.swapChainRequested = false;
	Check(Decide(facts).reason == "the game created a device without a swap chain", "no swap chain asked for");

	facts = Good();
	facts.windowed = false;
	Check(Decide(facts).reason == "exclusive fullscreen; frame generation needs borderless windowed mode",
		"exclusive fullscreen is refused by name");

	facts = Good();
	facts.vendorId = 0x1002;
	Check(Decide(facts).reason == "not an NVIDIA adapter (vendor 0x1002)", "another vendor, with its id");

	facts = Good();
	facts.enb = true;
	Check(Decide(facts).reason == "ENB is loaded", "ENB");

	facts = Good();
	facts.foreignModule = "AAAFrameGeneration.dll";
	Check(Decide(facts).reason == "AAAFrameGeneration.dll is loaded", "a foreign frame generation mod, by name");

	facts = Good();
	facts.switchOn = false;
	facts.switchReason = "no settings file yet";
	facts.windowed = false;
	facts.enb = true;
	const auto first = Decide(facts);
	Check(!first.build && first.reason.starts_with("the switch was off"),
		"the first failed check is the one reported");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
