#include "Features/Skylighting/OcclusionProjection.h"

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

	void Apply(const double (&a_m)[16], const double (&a_p)[3], double a_w, double (&a_out)[4])
	{
		for (int i = 0; i < 4; ++i) {
			a_out[i] = a_m[i * 4 + 0] * a_p[0] + a_m[i * 4 + 1] * a_p[1] + a_m[i * 4 + 2] * a_p[2] + a_m[i * 4 + 3] * a_w;
		}
	}

	constexpr double kPi = 3.14159265358979323846;
}

int main()
{
	using namespace Features::SkyOcclusion;

	// Directions: down, unit, within the zenith angle, not all the same.
	{
		bool allDown = true;
		bool allUnit = true;
		bool within = true;
		double first[3]{};
		double second[3]{};
		SkyDirection(0, 0.37, kPi / 2.0, first);
		SkyDirection(1, 0.37, kPi / 2.0, second);
		for (std::uint32_t i = 0; i < 64; ++i) {
			double d[3]{};
			SkyDirection(i, 0.37, kPi / 2.0, d);
			allDown = allDown && d[2] < 0.0;
			allUnit = allUnit && Near(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), 1.0, 1e-9);
		}
		for (std::uint32_t i = 0; i < 64; ++i) {
			double d[3]{};
			SkyDirection(i, 0.37, 30.0 * kPi / 180.0, d);
			within = within && std::acos(-d[2]) <= 30.0 * kPi / 180.0 + 1e-9;
		}
		Check(allDown, "every direction points down");
		Check(allUnit, "every direction is a unit vector");
		Check(within, "at 30 degrees no direction leaves the cone");
		Check(!Near(first[0], second[0], 1e-6) || !Near(first[1], second[1], 1e-6), "consecutive directions differ");
	}

	// The matrix: centre to the middle, near face to 0, far face to 1, the
	// cube's edge to the clip edge, a vector's w stays 0.
	{
		const double centre[3]{ 100.0, -200.0, 50.0 };
		const double direction[3]{ 0.0, 0.0, -1.0 };
		double m[16]{};
		OcclusionMatrix(centre, direction, m);
		double out[4]{};
		Apply(m, centre, 1.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && Near(out[2], 0.5, 1e-9) && Near(out[3], 1.0, 1e-9),
			"the centre lands in the middle at half depth");
		const double nearPoint[3]{ 100.0, -200.0, 50.0 + 5000.0 };  // against the direction: towards the sky
		Apply(m, nearPoint, 1.0, out);
		Check(Near(out[2], 0.0, 1e-9), "the near face is depth 0");
		const double farPoint[3]{ 100.0, -200.0, 50.0 - 5000.0 };
		Apply(m, farPoint, 1.0, out);
		Check(Near(out[2], 1.0, 1e-9), "the far face is depth 1");
		const double edge[3]{ 100.0 + 5000.0, -200.0, 50.0 };
		Apply(m, edge, 1.0, out);
		Check(Near(std::abs(out[0]), 1.0, 1e-9) || Near(std::abs(out[1]), 1.0, 1e-9), "the cube's edge is the clip edge");
		Check(m[12] == 0.0 && m[13] == 0.0 && m[14] == 0.0 && m[15] == 1.0, "the last row is 0 0 0 1");
		Apply(m, direction, 0.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && out[2] > 0.0 && Near(out[3], 0.0, 1e-9),
			"the direction itself maps to positive depth and no offset");
	}

	// A tilted direction still gives an orthonormal frame: along it depth
	// only, across it the clip edge.
	{
		const double centre[3]{};
		const double s = std::sqrt(0.5);
		const double direction[3]{ s, 0.0, -s };
		double m[16]{};
		OcclusionMatrix(centre, direction, m);
		const double along[3]{ s * 5000.0, 0.0, -s * 5000.0 };
		double out[4]{};
		Apply(m, along, 1.0, out);
		Check(Near(out[0], 0.0, 1e-9) && Near(out[1], 0.0, 1e-9) && Near(out[2], 1.0, 1e-9), "along the tilted direction: depth 1, no offset");
		const double across[3]{ 0.0, 5000.0, 0.0 };
		Apply(m, across, 1.0, out);
		Check(Near(std::abs(out[0]), 1.0, 1e-9) || Near(std::abs(out[1]), 1.0, 1e-9), "across it: the clip edge");
	}

	// The window: standing still, one cell along x, a leap.
	{
		std::int32_t cell[3]{ 0, 0, 0 };
		const double eye[3]{ 12.0, -7.0, 3.0 };
		auto window = WindowFor(eye, cell);
		const double cellSize = kGridExtent[0] / kGridSize[0];
		Check(window.validMargin[0] == 0 && window.validMargin[1] == 0 && window.validMargin[2] == 0, "still: nothing is new");
		Check(std::abs(window.cellOrigin[0]) <= cellSize * 0.5 + 1e-9 && std::abs(window.cellOrigin[1]) <= cellSize * 0.5 + 1e-9,
			"still: the cell origin is within half a cell of the eye");
		Check(window.arrayOrigin[0] >= 0 && window.arrayOrigin[0] < 256, "the array origin is inside the grid");
		const auto originBefore = window.arrayOrigin[0];
		const double moved[3]{ 12.0 + cellSize, -7.0, 3.0 };
		window = WindowFor(moved, cell);
		Check(window.validMargin[0] == -1 && window.validMargin[1] == 0, "one cell along x: the far x edge is new");
		Check(window.arrayOrigin[0] == (originBefore + 1) % 256, "and the ring buffer moved by one");
		const double leapt[3]{ 12.0 + cellSize * 301.0, -7.0, 3.0 };
		window = WindowFor(leapt, cell);
		Check(window.validMargin[0] <= -256, "a leap makes the whole grid new");
	}

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
