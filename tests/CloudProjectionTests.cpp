#include "Features/CloudShadows/CloudProjection.h"

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

	bool Near(float a_value, float a_expected, float a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}

	constexpr float kRadius = Features::Clouds::kEarthRadiusMetres * Features::Clouds::kUnitsPerMetre;
}

int main()
{
	using namespace Features::Clouds;

	{
		// Sun in the zenith: the shadow of a cloud falls straight down, so the
		// sample direction stands right above the point, at cloud height.
		const auto v = CloudSampleDirection({ 100.0f, 200.0f, -50.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 140000.0f);
		Check(Near(v.x, 100.0f, 0.5f), "zenith: x stays");
		Check(Near(v.y, 200.0f, 0.5f), "zenith: y stays");
		Check(Near(v.z, 140000.0f, 1.0f), "zenith: z is the cloud height");
	}

	{
		// Sun 45 degrees up along +x, a point a thousand units below the
		// camera, clouds a thousand units above it: the sample point lies two
		// thousand units along x at cloud height. Small numbers so the
		// planet's curvature stays below the tolerance.
		const float s = 0.70710678f;
		const auto v = CloudSampleDirection({ 0.0f, 0.0f, -1000.0f }, { s, 0.0f, s }, kRadius, 1000.0f);
		Check(Near(v.x, 2000.0f, 0.5f), "45 degrees: offset equals the height climbed");
		Check(Near(v.y, 0.0f, 0.01f), "45 degrees: no sideways drift");
		Check(Near(v.z, 1000.0f, 0.5f), "45 degrees: at cloud height");
	}

	{
		// Behind the camera is no different from in front.
		const auto ahead = CloudSampleDirection({ 5000.0f, 0.0f, -1000.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 2000.0f);
		const auto behind = CloudSampleDirection({ -5000.0f, 0.0f, -1000.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 2000.0f);
		Check(Near(ahead.x, 5000.0f, 0.5f) && Near(behind.x, -5000.0f, 0.5f), "sign of x survives");
		Check(Near(ahead.z, behind.z, 0.01f), "same height either way");
	}

	{
		// Cloud height zero puts the shell at the camera's height.
		const auto v = CloudSampleDirection({ 300.0f, 0.0f, -700.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 0.0f);
		Check(Near(v.z, 0.0f, 0.5f), "height zero: the shell is at the camera");
	}

	{
		Check(Near(ShadowFactor(1.0f, 0.5f), 0.5f, 1e-6f), "full cover at opacity 0.5 halves the light");
		Check(Near(ShadowFactor(0.5f, 4.0f), 0.0f, 1e-6f), "opacity 4 clamps at black");
		Check(Near(ShadowFactor(0.0f, 4.0f), 1.0f, 1e-6f), "no cover leaves the light alone");
		Check(Near(ShadowFactor(0.25f, 1.0f), 0.75f, 1e-6f), "a quarter cover at opacity 1 takes a quarter");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
