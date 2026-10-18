#include "Bridge/NeuralDecision.h"

#include <cmath>
#include <cstdio>
#include <string_view>

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

	bool Is(const char* a_reason, std::string_view a_expected)
	{
		return a_reason != nullptr && std::string_view{ a_reason } == a_expected;
	}
}

int main()
{
	using Bridge::MakeNeuralOptions;
	using Bridge::NeuralBlocker;
	using Bridge::NeuralView;

	const auto defaults = MakeNeuralOptions({ "fast", "1", "off", 1.0, 0.7, 0.3, 0.5, true });
	Check(defaults.preset == 2 && defaults.style == 0 && defaults.maskBlendMode == 2 && defaults.view == NeuralView::kOff,
		"defaults map to preset B, style 0, blend Min");
	Check(defaults.intensity == 1.0f && defaults.structure == 0.7f && defaults.tone == 0.3f && defaults.skin == 0.5f && defaults.autoMask,
		"the sliders pass through");

	const auto original = MakeNeuralOptions({ "original", "3", "mask", 1.0, 1.0, 1.0, 1.0, false });
	Check(original.preset == 1, "original is preset A");
	Check(original.style == 2, "style 3 is NGX style 2");
	Check(original.view == NeuralView::kMask, "mask");
	Check(!original.autoMask && original.maskBlendMode == 0, "no characters, no auto mask, blend None");

	const auto unknown = MakeNeuralOptions({ "huh", "9", "??", 2.0, -1.0, std::nan(""), 0.5, true });
	Check(unknown.preset == 2, "an unknown model falls back to B");
	Check(unknown.style == 0 && unknown.view == NeuralView::kOff, "unknown style and view fall back");
	Check(unknown.intensity == 1.0f && unknown.structure == 0.0f, "out-of-range sliders are clamped to 0..1");
	Check(unknown.tone == 0.3f, "NaN falls back to the default");

	Check(MakeNeuralOptions({ "fast", "2", "split", 0.0, 0.0, 0.0, 0.0, true }).view == NeuralView::kSplit, "split");
	Check(MakeNeuralOptions({ "fast", "2", "split", 0.0, 0.0, 0.0, 0.0, true }).style == 1, "style 2 is NGX style 1");

	Check(NeuralBlocker({ false, false, false, true, true }) == nullptr, "the open overlay does not block");
	Check(NeuralBlocker({ false, false, false, false, true }) == nullptr, "nothing in the way");
	Check(Is(NeuralBlocker({ true, true, true, false, false }), "loading screen"), "the loading screen first");
	Check(Is(NeuralBlocker({ false, true, true, false, true }), "Pip-Boy"), "then the Pip-Boy");
	Check(Is(NeuralBlocker({ false, false, true, false, true }), "a game menu is open"), "then menus");
	Check(Is(NeuralBlocker({ false, false, false, false, false }), "the game's TAA is off, so it writes no motion vectors"),
		"no TAA, no motion vectors");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
