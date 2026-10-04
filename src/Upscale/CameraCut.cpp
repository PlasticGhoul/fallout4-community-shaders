#include "Upscale/CameraCut.h"

#include <algorithm>
#include <cmath>

namespace Upscale
{
	bool IsCameraCut(const CameraState& a_previous, const CameraState& a_current, const CutThresholds& a_limits) noexcept
	{
		double squared = 0.0;
		double dot = 0.0;
		double lengthPrevious = 0.0;
		double lengthCurrent = 0.0;
		for (int i = 0; i < 3; ++i) {
			const auto delta = a_current.position[i] - a_previous.position[i];
			squared += delta * delta;
			dot += a_previous.forward[i] * a_current.forward[i];
			lengthPrevious += a_previous.forward[i] * a_previous.forward[i];
			lengthCurrent += a_current.forward[i] * a_current.forward[i];
		}
		if (std::sqrt(squared) > a_limits.distance) {
			return true;
		}
		const auto lengths = std::sqrt(lengthPrevious * lengthCurrent);
		if (lengths > 0.0) {
			const auto cosine = std::clamp(dot / lengths, -1.0, 1.0);
			const auto degrees = std::acos(cosine) * 180.0 / 3.14159265358979323846;
			if (degrees > a_limits.degrees) {
				return true;
			}
		}
		return std::abs(a_current.fovDegrees - a_previous.fovDegrees) > a_limits.fovDegrees;
	}
}
