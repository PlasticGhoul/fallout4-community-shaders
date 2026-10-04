#include "Upscale/UpscaleMath.h"

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

	bool Near(double a_value, double a_expected, double a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}
}

int main()
{
	using namespace Upscale;
	const Render::Extent qhd{ 2560, 1440 };
	const Render::Extent fhd{ 1920, 1080 };

	Check(ModeFromName("dlaa") == Mode::kDLAA && ModeFromName("ultra_performance") == Mode::kUltraPerformance,
		"mode names as the settings write them");
	Check(!ModeFromName("ultra").has_value(), "an unknown name is no mode");

	Check(RenderSize(qhd, Mode::kDLAA) == qhd, "DLAA renders at the display size");
	Check(RenderSize(qhd, Mode::kQuality) == Render::Extent{ 1707, 960 }, "Quality at 1440p: 1707x960");
	Check(RenderSize(qhd, Mode::kBalanced) == Render::Extent{ 1485, 835 }, "Balanced at 1440p: 1485x835");
	Check(RenderSize(qhd, Mode::kPerformance) == Render::Extent{ 1280, 720 }, "Performance at 1440p: 1280x720");
	Check(RenderSize(qhd, Mode::kUltraPerformance) == Render::Extent{ 853, 480 }, "Ultra Performance at 1440p: 853x480");
	Check(RenderSize(fhd, Mode::kQuality) == Render::Extent{ 1280, 720 }, "Quality at 1080p: 1280x720");

	Check(JitterPhaseCount(qhd, qhd) == 8, "eight phases at native resolution");
	Check(JitterPhaseCount(Render::Extent{ 1280, 720 }, qhd) == 32, "thirty-two at half resolution");

	Check(Near(Halton(1, 2), 0.5, 1e-12) && Near(Halton(2, 2), 0.25, 1e-12) && Near(Halton(3, 2), 0.75, 1e-12),
		"Halton base 2: 1/2, 1/4, 3/4");
	Check(Near(Halton(1, 3), 1.0 / 3.0, 1e-12) && Near(Halton(2, 3), 2.0 / 3.0, 1e-12) && Near(Halton(3, 3), 1.0 / 9.0, 1e-12),
		"Halton base 3: 1/3, 2/3, 1/9");

	bool inRange = true;
	bool repeats = true;
	for (std::uint32_t frame = 0; frame < 64; ++frame) {
		const auto j = JitterOffset(frame, 8);
		inRange = inRange && j.x >= -0.5f && j.x < 0.5f && j.y >= -0.5f && j.y < 0.5f;
		const auto again = JitterOffset(frame + 8, 8);
		repeats = repeats && j.x == again.x && j.y == again.y;
	}
	Check(inRange, "jitter stays within half a pixel");
	Check(repeats, "the sequence repeats after its phase count");
	const auto first = JitterOffset(0, 8);
	Check(Near(first.x, 0.0, 1e-6) && Near(first.y, 1.0 / 3.0 - 0.5, 1e-6), "frame 0 is Halton index 1, minus one half");

	const auto offset = ToEngineOffset(Jitter{ 0.25f, -0.5f }, qhd);
	Check(Near(offset.x, -0.5 / 2560.0, 1e-9), "engine x offset is negated, in NDC");
	Check(Near(offset.y, -1.0 / 1440.0, 1e-9), "engine y offset keeps its sign, in NDC");

	Check(Near(MipBias(qhd, qhd), -1.0, 1e-6), "native: bias minus one");
	Check(Near(MipBias(Render::Extent{ 1280, 720 }, qhd), -2.0, 1e-6), "half resolution: bias minus two");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
