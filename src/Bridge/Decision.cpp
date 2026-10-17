#include "Bridge/Decision.h"

#include <format>

namespace Bridge
{
	Verdict Decide(const DecisionFacts& a_facts)
	{
		if (!a_facts.switchOn) {
			return { false, std::format("the switch was off when the game started ({}); a restart applies it", a_facts.switchReason) };
		}
		if (!a_facts.swapChainRequested) {
			return { false, "the game created a device without a swap chain" };
		}
		if (!a_facts.windowed) {
			return { false, "exclusive fullscreen; frame generation needs borderless windowed mode" };
		}
		if (a_facts.vendorId != kVendorNvidia) {
			return { false, std::format("not an NVIDIA adapter (vendor 0x{:04X})", a_facts.vendorId) };
		}
		if (a_facts.enb) {
			return { false, "ENB is loaded" };
		}
		if (!a_facts.foreignModule.empty()) {
			return { false, std::format("{} is loaded", a_facts.foreignModule) };
		}
		return { true, {} };
	}
}
