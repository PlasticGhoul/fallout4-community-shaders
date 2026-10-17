#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Bridge
{
	using Matrix = std::array<std::array<float, 4>, 4>;

	/// One frame of the engine's camera in its own convention: clip =
	/// worldToClip * (p, 1), the point on the right (Render::ClipFromWorldToCam).
	/// worldToClip is NiCamera::worldToCam of Main::WorldRootCamera, world to
	/// clip whole, whatever its name says.
	struct CameraFrame
	{
		Matrix worldToClip{};
		std::array<float, 3> position{};
	};

	/// Field for field what sl::Constants needs, already in Streamline's
	/// convention - a row vector on the left (sl_consts.h, clipToPrevClip),
	/// so each matrix is the transpose of its column form.
	struct Constants
	{
		Matrix cameraViewToClip{};
		Matrix clipToCameraView{};
		Matrix clipToPrevClip{};
		Matrix prevClipToClip{};
		std::array<float, 2> jitterOffset{};
		/// RT_029 holds uv units, already normalised (F5.5a, first run).
		std::array<float, 2> mvecScale{ 1.0f, 1.0f };
		std::array<float, 3> cameraPos{};
		std::array<float, 3> cameraUp{};
		std::array<float, 3> cameraRight{};
		std::array<float, 3> cameraFwd{};
		float cameraNear{ 0.0f };
		float cameraFar{ 0.0f };
		/// Vertical, in radians.
		float cameraFOV{ 0.0f };
		float cameraAspectRatio{ 0.0f };
		bool reset{ false };
		/// Largest translation left in the derived projection; near zero when
		/// the position and the matrix describe the same camera. Logged, never
		/// sent: the first run shows whether NiCamera's translation is the one
		/// the matrix was built from.
		float diagnosticResidue{ 0.0f };
	};

	[[nodiscard]] std::optional<Matrix> Invert(const Matrix& a_matrix) noexcept;
	[[nodiscard]] Matrix Multiply(const Matrix& a_left, const Matrix& a_right) noexcept;
	[[nodiscard]] Matrix Transpose(const Matrix& a_matrix) noexcept;

	/// In render pixels from BSGraphics::State::offsetX/Y: the inverse of
	/// Upscale::ToEngineOffset, relative to the display size like it.
	[[nodiscard]] std::array<float, 2> JitterFromEngineOffset(
		float a_offsetX,
		float a_offsetY,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight) noexcept;

	/// Nothing when a matrix cannot be inverted or the axes degenerate.
	/// Without a previous frame (the first one, or after a reset) the current
	/// one stands for both.
	[[nodiscard]] std::optional<Constants> BuildConstants(
		const CameraFrame& a_current,
		const CameraFrame* a_previous,
		float a_nearPlane,
		float a_farPlane,
		std::array<float, 2> a_jitter,
		bool a_reset) noexcept;
}
