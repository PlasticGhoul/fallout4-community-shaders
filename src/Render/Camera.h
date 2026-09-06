#pragma once

#include <array>
#include <optional>

namespace Render
{
	/// Whether a matrix could be a view projection at all: every entry finite,
	/// and not the whole thing zero.
	///
	/// Deliberately no cleverer than that. A heuristic that guessed at the
	/// shape of a projection would sooner or later refuse a valid one, and
	/// refusing a valid matrix costs a feature for the session while accepting
	/// an odd one costs a frame.
	[[nodiscard]] bool IsPlausibleViewProjection(const float (&a_matrix)[16]) noexcept;

	/// The current camera's view projection, row major, or nothing.
	///
	/// Read from BSGraphics::State::GetSingleton()->cameraState, which is a
	/// **value** member of the singleton: no pointer supplied by the engine is
	/// dereferenced on the way.
	///
	/// That is the whole point of taking this route.
	/// BSGraphics::RendererData::shadowState is declared as a pointer at offset
	/// zero and is not one in Fallout 4 AE 1.11.240 - it read as 0x1B70, which
	/// is the offset of RendererShadowState inside BSGraphics::Context, and
	/// following it crashed the game at 0x1B70 + 0x7B0. commonlibf4 describes
	/// that field the way it describes BSShader's: plausibly, and wrongly.
	///
	/// The first successful read cross-checks the singleton against the swap
	/// chain and logs what it found, the same safety net B1 put in front of the
	/// device: a wrong pointer that happens to be readable is far worse than a
	/// null one.
	[[nodiscard]] std::optional<std::array<float, 16>> ViewProjection() noexcept;

	/// Writes the projection matrix and the engine's own depth range to the
	/// log, once per call.
	///
	/// This is the measurement that settles which end of the depth buffer is
	/// near. Bend's raymarch has to be told, and getting it backwards makes a
	/// ray that leaves the screen read the border colour as a surface right in
	/// front of the camera - a shadow around the edge of the picture, which is
	/// what the first run showed. The projection is logged rather than the
	/// combined view projection because the view rotation would bury the two
	/// terms that carry the answer.
	void LogProjection() noexcept;

	/// The view projection worked out here from the view and the projection,
	/// rather than read from the field that claims to hold it already.
	///
	/// It exists because the field does not hold it. A light direction
	/// multiplied by what viewProjMat contains comes out zero in x, y and w
	/// whatever the direction is, which puts the light at the exact centre of
	/// the screen every frame and makes the sweep independent of where the sun
	/// actually is.
	[[nodiscard]] std::optional<std::array<float, 16>> ViewProjectionComputed() noexcept;

	/// Writes all four matrices ViewData carries - view, projection, the
	/// combined one and the unjittered combined one - so that which of them is
	/// filled can be read off rather than guessed at.
	void LogCameraMatrices() noexcept;

	/// Tries every matrix the camera state and its cache can offer, and reports
	/// for each where it puts the camera's own view direction.
	///
	/// This is the check that should have been written first. A view projection
	/// is not recognised by its shape - the one this code trusted looks like a
	/// matrix and is not one - but by what it does, and there is exactly one
	/// thing it must do that can be verified without a known scene point:
	/// carry viewDir to the centre of the screen, in front of the camera. Any
	/// candidate that does is the one; every candidate that does not is out,
	/// whatever its entries look like.
	void ProbeCameraMatrices(std::uint32_t a_width, std::uint32_t a_height) noexcept;

	/// A world direction in homogeneous clip space, which is what Bend's
	/// dispatch builder wants for a directional light.
	///
	/// Built here from the camera's view matrix and its view frustum rather
	/// than read from a projection matrix, because Fallout 4 keeps none for the
	/// camera it draws the world with. Everything cameraState offers has a zero
	/// row where the depth belongs, and the projections in its cache expect a
	/// different axis order - they answer with the view direction's y where a
	/// depth should be.
	///
	/// The view matrix is sound and was checked: viewDir times it gives
	/// (0, 0, 1) to four places. The frustum supplies the rest, and left,
	/// right, top and bottom are far harder for a header to misdescribe than
	/// sixteen anonymous floats.
	///
	/// Returns nothing when neither the frustum nor the fallback can be
	/// trusted. The first success logs what it used and where it puts the
	/// camera's own view direction, which has to be the centre of the screen.
	[[nodiscard]] std::optional<std::array<float, 4>> ProjectDirection(
		const float (&a_direction)[3],
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept;

	/// A world point in homogeneous clip space, through the world camera's own
	/// worldToCam.
	///
	/// That matrix is world to clip, not world to camera - its top left is a
	/// projection with Bethesda's axis order, x to x, z to y and y to depth,
	/// and the ratio of its two scales is the aspect of the screen exactly.
	/// Reading its three by three corner as a camera basis was a mistake; it
	/// was never one, and the engine's own WorldPtToScreenPt3 takes the whole
	/// thing for precisely this purpose.
	///
	/// Applying all sixteen entries to a point needs no basis, no frustum and
	/// no field of view, and gets the perspective divide right by construction.
	/// Bend takes a point as readily as a direction - float4(position, 1) - and
	/// the sun, a hundred and twenty thousand units out, behaves as a
	/// directional light regardless.
	[[nodiscard]] std::optional<std::array<float, 4>> ProjectPoint(
		const float (&a_direction)[3]) noexcept;

	/// Writes where the sun lands on screen under both readings of the camera
	/// rotation, the inverted one and the plain one.
	///
	/// Called from the caller's own periodic tick, never once: three one-shot
	/// logs in this file have sampled a loading screen and said nothing.
	void LogProjectionSample(const float (&a_direction)[3]) noexcept;
}
