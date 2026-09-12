#pragma once

#include <array>
#include <optional>

namespace Render
{
	/// Whether a matrix could be a world-to-clip transform at all: every entry
	/// finite, and not the whole thing zero.
	///
	/// Deliberately no cleverer than that. A heuristic that guessed at the
	/// shape of a projection would sooner or later refuse a valid one, and
	/// refusing a valid matrix costs a feature for the session while accepting
	/// an odd one costs a frame.
	[[nodiscard]] bool IsPlausibleViewProjection(const float (&a_matrix)[16]) noexcept;

	/// A world direction in homogeneous clip space through a world-to-clip
	/// matrix in the engine's convention: the point stands on the right,
	/// clip_i is the sum over j of m[i][j] d_j, and the fourth component of a
	/// direction is zero, so the translation column never enters.
	///
	/// Pure arithmetic, no engine, which is what lets the host test hold it
	/// against three frames of NiCamera::WorldPtToScreenPt3 out of the log.
	[[nodiscard]] std::array<float, 4> ClipFromWorldToCam(
		const float (&a_worldToCam)[4][4],
		const float (&a_direction)[3]) noexcept;

	/// What a full-screen pass needs to turn a pixel and its depth into a
	/// place in the world, relative to the camera.
	///
	/// A pixel's ray is forward + ndc.x * rightOverScaleX + ndc.y * upOverScaleY,
	/// unnormalised; the depth buffer's z gives d = near / (1 - z) along
	/// forward; the point is ray * d, and its height is height + that z.
	struct FogCamera
	{
		float forward[3];
		float height;
		float rightOverScaleX[3];
		float near;
		float upOverScaleY[3];
	};

	/// All of it from the world-to-clip matrix alone. Row three is forward,
	/// a unit vector, because w is forward dotted with the point; row zero is
	/// the horizontal scale times right, so right over the scale is that row
	/// divided by its own squared length, and row one the same for up; and
	/// the near plane is m[3][3] - m[2][3], because z is w minus near.
	/// Neither the camera's rotation nor its position is needed - only the
	/// height, which is the caller's to supply.
	[[nodiscard]] FogCamera FogCameraFromMatrix(
		const float (&a_worldToCam)[4][4],
		float a_cameraHeight) noexcept;

	/// The direction towards the sun in homogeneous clip space, which is what
	/// Bend's dispatch builder asks for: float4(direction, 0) through the view
	/// projection, no divide.
	///
	/// The matrix is NiCamera::worldToCam of Main::WorldRootCamera, and it is
	/// world to clip whole, rotation included, whatever its name says. On the
	/// loading screen the camera's rotation is a bare permutation of the axes,
	/// which made the matrix read as if it carried no rotation at all; the
	/// two commits that inverted the camera node's own rotation in front of it
	/// rotated twice. The engine's own WorldPtToScreenPt3 applies exactly this
	/// matrix, and rebuilding its clip coordinate from the screen position it
	/// hands back is where the three mistakes of the last session lived: it
	/// divides by the magnitude of w, so behind the camera its answer is
	/// mirrored and its depth has the wrong sign for Bend, and the forward
	/// axis that was used to give w back its sign was the wrong column.
	///
	/// Two conventions worth keeping in mind, both measured on 2026-09-06 and
	/// 2026-09-12: the camera node's world rotation holds forward in row zero,
	/// up in row one and right in row two - Gamebryo's camera looks down its
	/// local x - and the projection has no far plane, so z over w is one for
	/// every direction, ahead or behind.
	///
	/// Returns nothing only when there is no world camera or its matrix is
	/// not finite, and says which the first time.
	[[nodiscard]] std::optional<std::array<float, 4>> ProjectPoint(
		const float (&a_direction)[3]) noexcept;

	/// Writes our clip coordinate for the direction beside what the engine's
	/// WorldPtToScreenPt3 answers for a point far along it, both in the
	/// engine's convention - screen position and depth over the magnitude of
	/// w - so that a run shows the two agreeing rather than assuming it.
	///
	/// Called from the caller's own periodic tick, never once: three one-shot
	/// logs in this file have sampled a loading screen and said nothing.
	void LogProjectionSample(const float (&a_direction)[3]) noexcept;
}
