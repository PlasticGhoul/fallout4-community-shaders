#include "Features/Skylighting/OcclusionProjection.h"

#include <algorithm>
#include <cmath>

namespace Features::SkyOcclusion
{
	namespace
	{
		constexpr double kTau = 6.283185307179586;

		double Fract(double a_value) noexcept
		{
			return a_value - std::floor(a_value);
		}

		void Normalise(double (&a_v)[3]) noexcept
		{
			const auto length = std::sqrt(a_v[0] * a_v[0] + a_v[1] * a_v[1] + a_v[2] * a_v[2]);
			if (length > 0.0) {
				a_v[0] /= length;
				a_v[1] /= length;
				a_v[2] /= length;
			}
		}

		void Cross(const double (&a_a)[3], const double (&a_b)[3], double (&a_out)[3]) noexcept
		{
			a_out[0] = a_a[1] * a_b[2] - a_a[2] * a_b[1];
			a_out[1] = a_a[2] * a_b[0] - a_a[0] * a_b[2];
			a_out[2] = a_a[0] * a_b[1] - a_a[1] * a_b[0];
		}

		double Dot(const double (&a_a)[3], const double (&a_b)[3]) noexcept
		{
			return a_a[0] * a_b[0] + a_a[1] * a_b[1] + a_a[2] * a_b[2];
		}

		std::int32_t Wrap(std::int32_t a_value, std::int32_t a_size) noexcept
		{
			const auto r = a_value % a_size;
			return r < 0 ? r + a_size : r;
		}
	}

	void SkyDirection(std::uint32_t a_index, double a_seed, double a_maxZenith, double (&a_out)[3]) noexcept
	{
		// The template's R2 sequence: two irrationals stepped per frame.
		const double u = Fract(a_seed + a_index * 0.245122333753);
		const double v = Fract(a_seed + a_index * 0.430159709002);
		// Uniform on a disc of radius sin(maxZenith): sqrt for area, the
		// radius so that asin(r) never exceeds the zenith angle. The template
		// scales u by sin(maxZenith) before the root, which at thirty degrees
		// lets directions out to forty-five; this keeps the promise the
		// setting makes.
		const double r = std::sin(a_maxZenith) * std::sqrt(u);
		const double phi = kTau * v;
		a_out[0] = -r * std::cos(phi);
		a_out[1] = -r * std::sin(phi);
		a_out[2] = -std::sqrt(std::max(0.0, 1.0 - r * r));
		Normalise(a_out);
	}

	void OcclusionMatrix(const double (&a_centre)[3], const double (&a_direction)[3], double (&a_out)[16]) noexcept
	{
		double forward[3]{ a_direction[0], a_direction[1], a_direction[2] };
		Normalise(forward);

		// A reference that is never parallel to the look direction: y when
		// looking nearly straight down, z otherwise.
		double reference[3]{ 0.0, 0.0, 1.0 };
		if (std::abs(forward[2]) > 0.99) {
			reference[1] = 1.0;
			reference[2] = 0.0;
		}
		double right[3]{};
		Cross(reference, forward, right);
		Normalise(right);
		double up[3]{};
		Cross(forward, right, up);

		const double half = kOcclusionDistance * 0.5;
		// Rows: x = right . (p - c) / half, y = up . (p - c) / half,
		// z = (forward . (p - c) + half) / (2 half), w = 1.
		for (int j = 0; j < 3; ++j) {
			a_out[0 * 4 + j] = right[j] / half;
			a_out[1 * 4 + j] = up[j] / half;
			a_out[2 * 4 + j] = forward[j] / kOcclusionDistance;
			a_out[3 * 4 + j] = 0.0;
		}
		a_out[0 * 4 + 3] = -Dot(right, a_centre) / half;
		a_out[1 * 4 + 3] = -Dot(up, a_centre) / half;
		a_out[2 * 4 + 3] = 0.5 - Dot(forward, a_centre) / kOcclusionDistance;
		a_out[3 * 4 + 3] = 1.0;
	}

	GridWindow WindowFor(const double (&a_eye)[3], std::int32_t (&a_previousCell)[3]) noexcept
	{
		GridWindow window{};
		for (int axis = 0; axis < 3; ++axis) {
			const double cellSize = kGridExtent[axis] / kGridSize[axis];
			const auto cell = static_cast<std::int32_t>(std::round(a_eye[axis] / cellSize));
			window.cellOrigin[axis] = cell * cellSize - a_eye[axis];
			window.validMargin[axis] = a_previousCell[axis] - cell;
			window.arrayOrigin[axis] = Wrap(
				cell - static_cast<std::int32_t>(kGridSize[axis] / 2),
				static_cast<std::int32_t>(kGridSize[axis]));
			a_previousCell[axis] = cell;
		}
		return window;
	}

	std::uint32_t NextCapturePhase(std::uint32_t a_phase, std::uint64_t a_lastFrame, std::uint64_t a_frame) noexcept
	{
		if (a_phase == kNoCapturePhase || a_frame != a_lastFrame + 1 || a_phase + 1 >= kCaptureCycle) {
			return 0;
		}
		return a_phase + 1;
	}

	std::uint32_t CaptureBucket(std::uint64_t a_key) noexcept
	{
		// splitmix64's finaliser: pointers share their low bits, so the key
		// is mixed before it is divided.
		a_key ^= a_key >> 30;
		a_key *= 0xBF58476D1CE4E5B9ull;
		a_key ^= a_key >> 27;
		a_key *= 0x94D049BB133111EBull;
		a_key ^= a_key >> 31;
		return static_cast<std::uint32_t>(a_key % kCaptureCycle);
	}
}
