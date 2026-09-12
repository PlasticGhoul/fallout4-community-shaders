#include "Render/Camera.h"

#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>

#include <cmath>
#include <cstring>

namespace Render
{
	namespace
	{
		bool g_loggedRefusal = false;

		/// Far enough that the sun behaves as a directional light and near
		/// enough to stay well inside the range of a float. Only the log's
		/// comparison against the engine needs a point; the projection itself
		/// takes the direction.
		constexpr float kSunDistance = 100000.0f;
	}

	bool IsPlausibleViewProjection(const float (&a_matrix)[16]) noexcept
	{
		bool anyNonZero = false;

		for (const auto value : a_matrix) {
			if (!std::isfinite(value)) {
				return false;
			}
			if (value != 0.0f) {
				anyNonZero = true;
			}
		}

		return anyNonZero;
	}

	std::array<float, 4> ClipFromWorldToCam(
		const float (&a_worldToCam)[4][4],
		const float (&a_direction)[3]) noexcept
	{
		std::array<float, 4> clip{};
		for (std::size_t row = 0; row < 4; ++row) {
			clip[row] =
				a_worldToCam[row][0] * a_direction[0] +
				a_worldToCam[row][1] * a_direction[1] +
				a_worldToCam[row][2] * a_direction[2];
		}
		return clip;
	}

	std::optional<std::array<float, 4>> ProjectPoint(const float (&a_direction)[3]) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR("world camera: Main::WorldRootCamera returned nothing");
			}
			return std::nullopt;
		}

		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));

		if (!IsPlausibleViewProjection(matrix)) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR("world camera: worldToCam is empty or not finite");
			}
			return std::nullopt;
		}

		// The whole matrix, applied to the direction, and nothing else. It is
		// the matrix the engine's own WorldPtToScreenPt3 applies, so there is
		// no convention left to get wrong between here and Bend: w is the
		// depth of the direction with its sign, x over w and y over w are the
		// light's place on screen - mirrored through the centre when it is
		// behind, which is what Bend expects beside a negative w - and z over
		// w is one on either side, the depth of infinity under a projection
		// with no far plane.
		return ClipFromWorldToCam(world->worldToCam, a_direction);
	}

	void LogProjectionSample(const float (&a_direction)[3]) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return;
		}

		const auto& transform = world->GetWorldTransform();
		const auto& rotate = transform.rotate;

		// Row zero is forward, row one up, row two right - the frames of
		// 2026-09-06 only add up that way. Kept in the log because it is the
		// one line that says what the player was looking at.
		REX::INFO(
			"world camera rotate: [{:.3f} {:.3f} {:.3f}] [{:.3f} {:.3f} {:.3f}] "
			"[{:.3f} {:.3f} {:.3f}], at [{:.1f} {:.1f} {:.1f}]",
			rotate.entry[0][0], rotate.entry[0][1], rotate.entry[0][2],
			rotate.entry[1][0], rotate.entry[1][1], rotate.entry[1][2],
			rotate.entry[2][0], rotate.entry[2][1], rotate.entry[2][2],
			transform.translate.x, transform.translate.y, transform.translate.z);

		// The engine's own routine over a point far along the direction, as
		// the arbiter. It divides by the magnitude of w, so behind the camera
		// its position is mirrored and its depth negative; our clip coordinate
		// is written out in the same convention so that the two lines can be
		// laid side by side. They differ legitimately in the fourth place of
		// the depth, where the stand-in point's near plane term sits - 15 over
		// a hundred thousand times w.
		const RE::NiPoint3 far{
			transform.translate.x + a_direction[0] * kSunDistance,
			transform.translate.y + a_direction[1] * kSunDistance,
			transform.translate.z + a_direction[2] * kSunDistance
		};

		float ex = 0.0f;
		float ey = 0.0f;
		float ez = 0.0f;
		static_cast<void>(world->WorldPtToScreenPt3(far, ex, ey, ez, 1.0e-5f));

		const auto clip = ClipFromWorldToCam(world->worldToCam, a_direction);
		const auto magnitude = std::abs(clip[3]);

		if (magnitude > 0.0f) {
			REX::INFO(
				"sun on screen: engine [{:.3f} {:.3f}] depth {:.4f}, "
				"ours [{:.3f} {:.3f}] depth {:.4f}, w {:.4f}, so it is {}",
				ex,
				ey,
				ez,
				(clip[0] / magnitude) * 0.5f + 0.5f,
				(clip[1] / magnitude) * 0.5f + 0.5f,
				clip[2] / magnitude,
				clip[3],
				clip[3] >= 0.0f ? "ahead" : "behind");
		} else {
			REX::INFO(
				"sun on screen: engine [{:.3f} {:.3f}] depth {:.4f}, "
				"ours in the plane of the screen, w exactly zero",
				ex,
				ey,
				ez);
		}
	}
}
