#pragma once

#include <cstdint>

namespace Features::SkyOcclusion
{
	/// The cube the sky is seen from, in game units: the template's
	/// occlusionDistance. Centred on the camera.
	inline constexpr double kOcclusionDistance = 10000.0;
	inline constexpr std::uint32_t kOcclusionMapSize = 2048;
	/// The probe grid: cells over 10000 x 10000 x 5000 units, the template's
	/// ARRAY_DIM and ARRAY_SIZE. Both shaders carry the same numbers.
	inline constexpr std::uint32_t kGridSize[3] = { 256, 256, 128 };
	inline constexpr double kGridExtent[3] = { 10000.0, 10000.0, 5000.0 };

	/// The sky direction of a frame: the template's R2 sequence on the unit
	/// disc, the radius scaled so that the zenith angle stays within
	/// a_maxZenith (radians), lifted onto the sphere. Points down (z < 0) -
	/// the way the occlusion camera looks.
	void SkyDirection(std::uint32_t a_index, double a_seed, double a_maxZenith, double (&a_out)[3]) noexcept;

	/// Orthographic projection looking along a_direction at a cube of edge
	/// kOcclusionDistance around a_centre, rows as the engine's vertex
	/// shaders read them (clip_i = sum_j M[i][j] p_j): x and y span the cube
	/// face in [-1, 1], z runs 0 at the near face to 1 at the far face,
	/// w is 1. Sixteen doubles, row major.
	void OcclusionMatrix(const double (&a_centre)[3], const double (&a_direction)[3], double (&a_out)[16]) noexcept;

	struct GridWindow
	{
		/// Origin of the camera's cell, relative to the eye.
		double cellOrigin[3];
		/// Where the ring buffer starts, in [0, kGridSize).
		std::int32_t arrayOrigin[3];
		/// previous cell - current cell, per axis: which edge of the grid is
		/// new this frame. Zero while the camera stays in its cell.
		std::int32_t validMargin[3];
	};

	/// The template's GetCommonBufferData. a_previousCell is the cell of
	/// the last frame and is updated to this frame's.
	GridWindow WindowFor(const double (&a_eye)[3], std::int32_t (&a_previousCell)[3]) noexcept;
}
