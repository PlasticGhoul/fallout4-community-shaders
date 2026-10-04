#include "Upscale/CameraCut.h"

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

	Upscale::CameraState At(double a_x, double a_yawDegrees, double a_fov)
	{
		const double radians = a_yawDegrees * 3.14159265358979323846 / 180.0;
		return Upscale::CameraState{ { a_x, 0.0, 0.0 }, { std::sin(radians), std::cos(radians), 0.0 }, a_fov };
	}
}

int main()
{
	using Upscale::IsCameraCut;
	const auto start = At(-69481.0, 0.0, 90.0);

	Check(!IsCameraCut(start, start), "a camera standing still is no cut");
	Check(!IsCameraCut(start, At(-69481.0 + 20.0, 0.0, 90.0)), "a vertibird's twenty units a frame is no cut");
	Check(IsCameraCut(start, At(-69481.0 + 3000.0, 0.0, 90.0)), "three thousand units in a frame is a cut");
	Check(!IsCameraCut(start, At(-69481.0, 10.0, 90.0)), "a flick of ten degrees is no cut");
	Check(IsCameraCut(start, At(-69481.0, 120.0, 90.0)), "a hundred and twenty degrees is a cut");
	Check(!IsCameraCut(start, At(-69481.0, 0.0, 88.0)), "sights easing in by two degrees is no cut");
	Check(IsCameraCut(start, At(-69481.0, 0.0, 55.0)), "a jump of thirty-five degrees in field of view is a cut");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
