#include "Render/Resolution.h"

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
	using namespace Render;
	const Resolution::Frame quality{ Extent{ 2560, 1440 }, Extent{ 1707, 960 }, false };

	Check(Resolution::DepthExtent(quality) == Extent{ 1707, 960 }, "before the swap, depth covers the render rectangle");
	auto swapped = quality;
	swapped.depthFull = true;
	Check(Resolution::DepthExtent(swapped) == Extent{ 2560, 1440 }, "while the full depth is bound, it covers the display");

	const auto scale = Resolution::RenderToTexture(quality, Extent{ 2560, 1440 });
	Check(std::abs(scale[0] - 1707.0f / 2560.0f) < 1e-6f && std::abs(scale[1] - 960.0f / 1440.0f) < 1e-6f,
		"a uv over the render rectangle maps into the full texture's corner");
	const auto proxy = Resolution::RenderToTexture(quality, Extent{ 1707, 960 });
	Check(proxy[0] == 1.0f && proxy[1] == 1.0f, "a texture of the render size is used whole");

	Check(Resolution::RenderPart(quality, Extent{ 2560, 1440 }) == Extent{ 1707, 960 },
		"a full size target holds the picture in its render rectangle");
	Check(Resolution::RenderPart(quality, Extent{ 1280, 720 }) == Extent{ 853, 480 },
		"a half size target like RT_028 in half of that");
	const Resolution::Frame native{ Extent{ 2560, 1440 }, Extent{ 2560, 1440 }, false };
	Check(Resolution::RenderPart(native, Extent{ 1280, 720 }) == Extent{ 1280, 720 }, "without DLSS a target is used whole");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
