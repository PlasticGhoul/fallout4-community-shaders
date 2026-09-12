#pragma once

namespace Features::Clouds
{
	struct Vec3
	{
		float x;
		float y;
		float z;
	};

	/// Fallout 4's unit: seventy to the metre, the figure the Skyrim template
	/// carries as GAME_UNIT_TO_M.
	inline constexpr float kUnitsPerMetre = 70.0f;
	inline constexpr float kEarthRadiusMetres = 6371000.0f;

	/// Where the sun's ray through a_relative (camera relative, z up) meets a
	/// spherical cloud shell a_cloudHeight above the planet's surface, itself
	/// a sphere of a_planetRadius under the camera. The result, camera
	/// relative, is the direction the coverage cubemap is sampled in.
	///
	/// The template's formula, rearranged so that the planet radius squared
	/// never appears: at four hundred million units it would swallow every
	/// other term in a float.
	[[nodiscard]] Vec3 CloudSampleDirection(
		Vec3 a_relative,
		Vec3 a_toSun,
		float a_planetRadius,
		float a_cloudHeight) noexcept;

	/// 1 - coverage * opacity, held in [0, 1].
	[[nodiscard]] float ShadowFactor(float a_coverage, float a_opacity) noexcept;
}
