#include "Render/Camera.h"

#include <array>
#include <cmath>
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

	bool Near(float a_value, float a_expected, float a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}

	// NiCamera::worldToCam as it read on the loading screen of 2026-09-06,
	// with the camera at [0 0 128] and a rotation that only permutes the
	// axes: forward is world y, up is world z, right is world x. The two
	// scales are the horizontal and vertical projection at 2560x1440, the
	// -271.2 is 2.1187 times 128, and z = y - 15 is the near plane with no
	// far plane behind it.
	constexpr float kLoadingScreen[4][4] = {
		{ 1.1918f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 2.1187f, -271.2f },
		{ 0.0f, 1.0f, 0.0f, -15.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f }
	};

	constexpr float kScaleX = 1.1918f;
	constexpr float kScaleY = 2.1187f;
	constexpr float kNear = 15.0f;

	/// A world-to-clip matrix from the rows of a camera node's world rotation,
	/// the way the loading screen matrix is built: row zero of the rotation is
	/// forward, row one up, row two right. This is what the engine's own
	/// WorldPtToScreenPt3 was found to apply, and the frames below are the
	/// measurements it was found on.
	void BuildWorldToCam(
		const float (&a_forward)[3],
		const float (&a_up)[3],
		const float (&a_right)[3],
		float (&a_out)[4][4])
	{
		for (int column = 0; column < 3; ++column) {
			a_out[0][column] = kScaleX * a_right[column];
			a_out[1][column] = kScaleY * a_up[column];
			a_out[2][column] = a_forward[column];
			a_out[3][column] = a_forward[column];
		}

		// The translation column plays no part for a direction. It is filled
		// in so the matrix is a whole one and not a corner of one.
		a_out[0][3] = 0.0f;
		a_out[1][3] = 0.0f;
		a_out[2][3] = -kNear;
		a_out[3][3] = 0.0f;
	}

	/// What the engine reports: normalised screen coordinates and a depth,
	/// all divided by the magnitude of w. That is the convention the log
	/// lines were written in, and the one the expectations below are in.
	struct EngineAnswer
	{
		float x;
		float y;
		float depth;
	};

	EngineAnswer AsEngine(const std::array<float, 4>& a_clip)
	{
		const auto magnitude = std::abs(a_clip[3]);
		return {
			(a_clip[0] / magnitude) * 0.5f + 0.5f,
			(a_clip[1] / magnitude) * 0.5f + 0.5f,
			a_clip[2] / magnitude
		};
	}

	/// One frame out of the log of 2026-09-06 19:31, Sanctuary: the camera
	/// node's world rotation, the direction towards the sun, and what
	/// NiCamera::WorldPtToScreenPt3 answered for a point a hundred thousand
	/// units along that direction.
	struct LoggedFrame
	{
		const char* what;
		float rotation[3][3];
		float towardsSun[3];
		EngineAnswer engine;
	};

	// The engine's depth carries the near plane of the finite stand-in point
	// - (100000 w - 15) / |100000 w| - which a pure direction does not. What
	// the two share is the sign and the magnitude of about one.
	constexpr LoggedFrame kFrames[] = {
		{ "19:32:00, sun ahead",
			{ { -0.682f, -0.731f, -0.003f }, { -0.002f, -0.002f, 1.000f }, { -0.731f, 0.682f, 0.000f } },
			{ 0.404f, -0.682f, 0.610f },
			{ -1.555f, 3.432f, 0.9994f } },
		{ "19:31:52, sun behind",
			{ { 0.707f, 0.707f, 0.038f }, { -0.027f, -0.027f, 0.999f }, { 0.707f, -0.707f, 0.000f } },
			{ 0.412f, -0.680f, 0.607f },
			{ 3.279f, 4.423f, -1.0009f } },
	};

	// The frame the picture went dark in. The engine's depth of -1.3305 says
	// the stand-in point sat 45 units behind the plane of the screen, so w
	// for the direction was -0.00045: the sun was crossing that plane.
	constexpr LoggedFrame kCrossing = {
		"19:31:57, the crossing",
		{ { 0.847f, 0.531f, 0.026f }, { -0.022f, -0.014f, 1.000f }, { 0.531f, -0.847f, 0.000f } },
		{ 0.407f, -0.681f, 0.609f },
		{ 1041.751f, 1421.474f, -1.3305f }
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

	{
		// Straight ahead on the loading screen: the centre of the screen, in
		// front, at the depth of infinity. Nothing about this can be argued
		// with, so it is the check that catches a transposed matrix first.
		const float forward[3]{ 0.0f, 1.0f, 0.0f };
		const auto clip = Render::ClipFromWorldToCam(kLoadingScreen, forward);

		Check(Near(clip[0], 0.0f, 1e-6f), "loading screen, forward: x is zero");
		Check(Near(clip[1], 0.0f, 1e-6f), "loading screen, forward: y is zero");
		Check(Near(clip[2], 1.0f, 1e-6f), "loading screen, forward: z is one");
		Check(Near(clip[3], 1.0f, 1e-6f), "loading screen, forward: w is one");
	}

	{
		// Straight behind: w flips sign and z with it, so z over w stays one.
		// Bend takes the sign of w for "behind" and z over w for the depth of
		// the light, and both have to hold on this side too.
		const float behind[3]{ 0.0f, -1.0f, 0.0f };
		const auto clip = Render::ClipFromWorldToCam(kLoadingScreen, behind);

		Check(Near(clip[3], -1.0f, 1e-6f), "loading screen, behind: w is minus one");
		Check(Near(clip[2] / clip[3], 1.0f, 1e-6f), "loading screen, behind: z over w is still one");
	}

	{
		// Straight to the right: in the plane of the screen, w exactly zero.
		// This is the case Bend clamps, and a translation term leaking into a
		// direction would show up here as a w that is not zero.
		const float right[3]{ 1.0f, 0.0f, 0.0f };
		const auto clip = Render::ClipFromWorldToCam(kLoadingScreen, right);

		Check(Near(clip[0], kScaleX, 1e-6f), "loading screen, right: x is the horizontal scale");
		Check(Near(clip[3], 0.0f, 1e-6f), "loading screen, right: w is zero");
	}

	{
		// Up: the vertical scale, and y positive. Bend maps y over w with a
		// minus, so a sun above the horizon has to come out positive here to
		// land in the upper half of the picture there.
		const float up[3]{ 0.0f, 0.0f, 1.0f };
		const auto clip = Render::ClipFromWorldToCam(kLoadingScreen, up);

		Check(Near(clip[1], kScaleY, 1e-6f), "loading screen, up: y is the vertical scale");
	}

	for (const auto& frame : kFrames) {
		float worldToCam[4][4]{};
		BuildWorldToCam(frame.rotation[0], frame.rotation[1], frame.rotation[2], worldToCam);

		const auto clip = Render::ClipFromWorldToCam(worldToCam, frame.towardsSun);
		const auto ours = AsEngine(clip);

		// The inputs are logged to three places; the quotient of two of them
		// at w of 0.17 moves in the second place. A twentieth is well inside
		// what a wrong axis produces - that misses by whole screens.
		char what[128];

		std::snprintf(what, sizeof(what), "%s: x %.3f against the engine's %.3f", frame.what, ours.x, frame.engine.x);
		Check(Near(ours.x, frame.engine.x, 0.05f), what);

		std::snprintf(what, sizeof(what), "%s: y %.3f against the engine's %.3f", frame.what, ours.y, frame.engine.y);
		Check(Near(ours.y, frame.engine.y, 0.05f), what);

		std::snprintf(what, sizeof(what), "%s: w %.3f has the sign of the engine's depth %.4f", frame.what, clip[3], frame.engine.depth);
		Check((clip[3] > 0.0f) == (frame.engine.depth > 0.0f), what);

		std::snprintf(what, sizeof(what), "%s: z over w is one, whichever side", frame.what);
		Check(Near(clip[2] / clip[3], 1.0f, 1e-5f), what);
	}

	{
		// The sun behind the player: what Bend is handed is the real quotient,
		// which is the engine's mirrored answer with its sign turned. Handing
		// it the engine's answer unturned is the second of the three mistakes
		// this test exists to keep out.
		const auto& frame = kFrames[1];
		float worldToCam[4][4]{};
		BuildWorldToCam(frame.rotation[0], frame.rotation[1], frame.rotation[2], worldToCam);

		const auto clip = Render::ClipFromWorldToCam(worldToCam, frame.towardsSun);
		const auto engineNdcX = frame.engine.x * 2.0f - 1.0f;

		Check(
			Near(clip[0] / clip[3], -engineNdcX, 0.1f),
			"sun behind: x over w is the engine's answer mirrored, not repeated");
	}

	{
		float worldToCam[4][4]{};
		BuildWorldToCam(kCrossing.rotation[0], kCrossing.rotation[1], kCrossing.rotation[2], worldToCam);

		const auto clip = Render::ClipFromWorldToCam(worldToCam, kCrossing.towardsSun);

		// Three decimals in the rotation leave about a thousandth in w, so
		// the crossing can only be placed, not measured. The column the code
		// used to take gave -0.29 here, which is not a crossing at all.
		Check(Near(clip[3], 0.0f, 0.005f), "the crossing: w is within a thousandth of zero");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
