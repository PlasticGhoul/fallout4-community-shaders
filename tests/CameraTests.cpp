#include "Render/Camera.h"

#include <cstdio>
#include <limits>

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

	// A real one, read out of a running frame: perspective, row major, the
	// shape a view projection has.
	constexpr float kPlausible[16] = {
		1.29f, 0.0f, 0.0f, 0.0f,
		0.0f, 2.30f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.1f, 0.0f
	};
}

int main()
{
	{
		Check(Render::IsPlausibleViewProjection(kPlausible), "a perspective matrix is plausible");
	}

	{
		// What an uninitialised camera looks like, and the state the crash of
		// 2026-09-06 went through: everything zero, so every projected point
		// lands on the same pixel and the sweep is meaningless.
		constexpr float zero[16]{};
		Check(!Render::IsPlausibleViewProjection(zero), "an all-zero matrix is not");
	}

	{
		float withNaN[16]{};
		for (int i = 0; i < 16; ++i) {
			withNaN[i] = kPlausible[i];
		}
		withNaN[5] = std::numeric_limits<float>::quiet_NaN();
		Check(!Render::IsPlausibleViewProjection(withNaN), "a matrix carrying a NaN is not");
	}

	{
		float withInfinity[16]{};
		for (int i = 0; i < 16; ++i) {
			withInfinity[i] = kPlausible[i];
		}
		withInfinity[10] = std::numeric_limits<float>::infinity();
		Check(!Render::IsPlausibleViewProjection(withInfinity), "and neither is one with an infinity");
	}

	{
		// A single non-zero entry is enough to pass the zero test but not to be
		// a transform. The check is deliberately not cleverer than this: a
		// heuristic that guesses at the shape of a projection would sooner or
		// later refuse a valid one, and refusing a valid matrix costs a
		// feature while accepting an odd one costs a frame.
		float almostZero[16]{};
		almostZero[0] = 1.0f;
		Check(
			Render::IsPlausibleViewProjection(almostZero),
			"one non-zero entry is accepted, the check is not a shape test");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
