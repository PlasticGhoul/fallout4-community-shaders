#pragma once

namespace Upscale
{
	/// What a frame's camera is, for telling a cut from movement.
	struct CameraState
	{
		double position[3];
		double forward[3];
		double fovDegrees;
	};

	/// A frame at 180 fps moves a sprinting player three units and a
	/// vertibird twenty; five hundred is a teleport, a load or a cut to a
	/// dialogue camera. Thirty degrees in one frame is beyond a mouse flick,
	/// five degrees of field of view beyond the ease into iron sights.
	struct CutThresholds
	{
		double distance{ 500.0 };
		double degrees{ 30.0 };
		double fovDegrees{ 5.0 };
	};

	/// True when the history DLSS keeps no longer describes this frame.
	[[nodiscard]] bool IsCameraCut(
		const CameraState& a_previous,
		const CameraState& a_current,
		const CutThresholds& a_limits = {}) noexcept;
}
