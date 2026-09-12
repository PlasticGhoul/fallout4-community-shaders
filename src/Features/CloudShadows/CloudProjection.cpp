#include "Features/CloudShadows/CloudProjection.h"

#include <algorithm>
#include <cmath>

namespace Features::Clouds
{
	Vec3 CloudSampleDirection(Vec3 a_relative, Vec3 a_toSun, float a_planetRadius, float a_cloudHeight) noexcept
	{
		const auto R = a_planetRadius;
		const auto H = a_cloudHeight;

		// u = relative + (0, 0, R) is the point seen from the planet's centre.
		// The ray u + s t meets the shell of radius R + H where
		//   t^2 + 2 (u.s) t + (|u|^2 - (R+H)^2) = 0.
		// |u|^2 - (R+H)^2 expands to |rel|^2 + 2 R (rel.z - H) - H^2; the R^2
		// terms cancel on paper, so they are left out of the arithmetic. What
		// remains is still (u.s)^2 against c at ten to the seventeenth, which
		// a float resolves to about ten units: double here, float in the
		// shader, where the debug view shows whether that is enough.
		const double dotUS =
			static_cast<double>(a_relative.x) * a_toSun.x +
			static_cast<double>(a_relative.y) * a_toSun.y +
			(static_cast<double>(a_relative.z) + R) * a_toSun.z;
		const double lengthSquared =
			static_cast<double>(a_relative.x) * a_relative.x +
			static_cast<double>(a_relative.y) * a_relative.y +
			static_cast<double>(a_relative.z) * a_relative.z;
		const double c = lengthSquared + 2.0 * R * (static_cast<double>(a_relative.z) - H) - static_cast<double>(H) * H;

		const double discriminant = std::max(dotUS * dotUS - c, 0.0);
		const auto t = static_cast<float>(-dotUS + std::sqrt(discriminant));

		return { a_relative.x + a_toSun.x * t, a_relative.y + a_toSun.y * t, a_relative.z + a_toSun.z * t };
	}

	float ShadowFactor(float a_coverage, float a_opacity) noexcept
	{
		return std::clamp(1.0f - a_coverage * a_opacity, 0.0f, 1.0f);
	}
}
