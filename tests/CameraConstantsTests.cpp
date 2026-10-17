#include "Bridge/CameraConstants.h"

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

	bool Near(float a_left, float a_right, float a_tolerance = 1e-4f)
	{
		return std::abs(a_left - a_right) <= a_tolerance;
	}

	bool Near(const Bridge::Matrix& a_left, const Bridge::Matrix& a_right, float a_tolerance = 1e-4f)
	{
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				if (!Near(a_left[i][j], a_right[i][j], a_tolerance)) {
					return false;
				}
			}
		}
		return true;
	}

	using Vec3 = std::array<float, 3>;

	float Dot(const Vec3& a_left, const Vec3& a_right)
	{
		return a_left[0] * a_right[0] + a_left[1] * a_right[1] + a_left[2] * a_right[2];
	}

	/// World to view written out by hand, independent of the code under
	/// test: rows right, up, forward, translation minus axis dot position.
	Bridge::Matrix View(const Vec3& a_right, const Vec3& a_up, const Vec3& a_forward, const Vec3& a_position)
	{
		return { { { a_right[0], a_right[1], a_right[2], -Dot(a_right, a_position) },
			{ a_up[0], a_up[1], a_up[2], -Dot(a_up, a_position) },
			{ a_forward[0], a_forward[1], a_forward[2], -Dot(a_forward, a_position) },
			{ 0.0f, 0.0f, 0.0f, 1.0f } } };
	}

	/// Fallout 4's projection has no far plane: z = w - near (Render::Camera.h).
	Bridge::Matrix Projection(float a_scaleX, float a_scaleY, float a_nearPlane)
	{
		return { { { a_scaleX, 0.0f, 0.0f, 0.0f },
			{ 0.0f, a_scaleY, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, -a_nearPlane },
			{ 0.0f, 0.0f, 1.0f, 0.0f } } };
	}

	std::array<float, 4> Apply(const Bridge::Matrix& a_columnForm, const std::array<float, 4>& a_point)
	{
		std::array<float, 4> out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				out[i] += a_columnForm[i][j] * a_point[j];
			}
		}
		return out;
	}

	/// A row vector on the left, as Streamline multiplies.
	std::array<float, 4> ApplyRow(const std::array<float, 4>& a_point, const Bridge::Matrix& a_rowForm)
	{
		std::array<float, 4> out{};
		for (std::size_t j = 0; j < 4; ++j) {
			for (std::size_t i = 0; i < 4; ++i) {
				out[j] += a_point[i] * a_rowForm[i][j];
			}
		}
		return out;
	}
}

int main()
{
	using namespace Bridge;

	const Matrix identity{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
	const Matrix sample{ { { 2, 0, 0, 1 }, { 0, 3, 0, 2 }, { 0, 0, 4, 3 }, { 0, 0, 0, 1 } } };
	const auto inverse = Invert(sample);
	Check(inverse.has_value() && Near(Multiply(sample, *inverse), identity), "Invert gives a matrix that multiplies back to the identity");
	const Matrix singular{ { { 1, 2, 3, 4 }, { 2, 4, 6, 8 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
	Check(!Invert(singular).has_value(), "a singular matrix is refused, not inverted into nonsense");

	const Vec3 right{ 1, 0, 0 };
	const Vec3 up{ 0, 0, 1 };
	const Vec3 forward{ 0, 1, 0 };
	const Vec3 position{ 100, -50, 20 };
	const float scaleX = 1.0f;
	const float scaleY = 16.0f / 9.0f;
	const auto projection = Projection(scaleX, scaleY, 10.0f);
	const CameraFrame current{ Multiply(projection, View(right, up, forward, position)), position };

	const auto built = BuildConstants(current, nullptr, 10.0f, 353840.0f, { 0.25f, -0.125f }, true);
	Check(built.has_value(), "a plain camera builds");
	if (built) {
		Check(Near(built->cameraViewToClip, Transpose(projection)), "the projection comes back, transposed for Streamline");
		Check(Near(Multiply(built->cameraViewToClip, built->clipToCameraView), identity), "clipToCameraView is its inverse");
		Check(Near(built->cameraFOV, 2.0f * std::atan(1.0f / scaleY)), "the vertical field of view from the projection");
		Check(Near(built->cameraAspectRatio, scaleY / scaleX), "the aspect ratio from the projection");
		Check(Near(built->cameraPos[0], 100.0f) && Near(built->cameraPos[1], -50.0f) && Near(built->cameraPos[2], 20.0f), "the camera position");
		Check(Near(built->cameraRight[0], 1.0f) && Near(built->cameraUp[2], 1.0f) && Near(built->cameraFwd[1], 1.0f), "the three axes");
		Check(Near(built->clipToPrevClip, identity) && Near(built->prevClipToClip, identity), "without a previous frame there is no motion");
		Check(Near(built->jitterOffset[0], 0.25f) && Near(built->jitterOffset[1], -0.125f), "the jitter is passed through");
		Check(built->reset && Near(built->cameraNear, 10.0f) && Near(built->cameraFar, 353840.0f), "reset and the planes");
	}

	const Vec3 moved{ 100, -45, 20 };
	const CameraFrame previous{ Multiply(projection, View(right, up, forward, moved)), moved };
	const auto motion = BuildConstants(current, &previous, 10.0f, 353840.0f, { 0.0f, 0.0f }, false);
	Check(motion.has_value(), "two frames build");
	if (motion) {
		const std::array<float, 4> world{ 130, 200, 35, 1 };
		const auto clipNow = Apply(current.worldToClip, world);
		const auto clipThen = Apply(previous.worldToClip, world);
		const auto mapped = ApplyRow(clipNow, motion->clipToPrevClip);
		Check(Near(mapped[0], clipThen[0], 1e-2f) && Near(mapped[1], clipThen[1], 1e-2f) && Near(mapped[3], clipThen[3], 1e-2f),
			"clipToPrevClip takes a point's clip position now to where it was");
		Check(Near(Multiply(motion->clipToPrevClip, motion->prevClipToClip), identity, 1e-3f), "prevClipToClip undoes it");
	}

	// A jittered projection tilts row zero towards forward; the axes must
	// still come out orthonormal.
	auto jittered = current;
	for (std::size_t j = 0; j < 4; ++j) {
		jittered.worldToClip[0][j] += 0.0004f * jittered.worldToClip[3][j];
	}
	const auto tilted = BuildConstants(jittered, nullptr, 10.0f, 353840.0f, { 0.0f, 0.0f }, false);
	Check(tilted.has_value() && Near(Dot(tilted->cameraRight, tilted->cameraFwd), 0.0f) && Near(tilted->cameraRight[0], 1.0f),
		"right stays perpendicular to forward when row zero leans on it");

	const CameraFrame flat{ Matrix{}, position };
	Check(!BuildConstants(flat, nullptr, 10.0f, 353840.0f, { 0.0f, 0.0f }, false).has_value(), "a zero matrix is refused");

	const auto jitter = JitterFromEngineOffset(2.0f * -0.25f / 2560.0f, 2.0f * -0.125f / 1440.0f, 2560, 1440);
	Check(Near(jitter[0], 0.25f) && Near(jitter[1], -0.125f), "the engine offset turns back into DLSS's jitter (Upscale::ToEngineOffset inverted)");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
