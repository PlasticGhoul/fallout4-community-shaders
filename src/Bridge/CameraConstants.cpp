#include "Bridge/CameraConstants.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Bridge
{
	namespace
	{
		using Vec3 = std::array<float, 3>;

		constexpr float kDegenerate = 1e-6f;

		float Dot(const Vec3& a_left, const Vec3& a_right) noexcept
		{
			return a_left[0] * a_right[0] + a_left[1] * a_right[1] + a_left[2] * a_right[2];
		}

		Vec3 Scaled(const Vec3& a_vector, float a_factor) noexcept
		{
			return { a_vector[0] * a_factor, a_vector[1] * a_factor, a_vector[2] * a_factor };
		}

		Vec3 Minus(const Vec3& a_left, const Vec3& a_right) noexcept
		{
			return { a_left[0] - a_right[0], a_left[1] - a_right[1], a_left[2] - a_right[2] };
		}

		std::optional<Vec3> Normalised(const Vec3& a_vector) noexcept
		{
			const auto length = std::sqrt(Dot(a_vector, a_vector));
			if (!(length > kDegenerate)) {
				return std::nullopt;
			}
			return Scaled(a_vector, 1.0f / length);
		}

		Vec3 Row(const Matrix& a_matrix, std::size_t a_row) noexcept
		{
			return { a_matrix[a_row][0], a_matrix[a_row][1], a_matrix[a_row][2] };
		}
	}

	Matrix Multiply(const Matrix& a_left, const Matrix& a_right) noexcept
	{
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				for (std::size_t k = 0; k < 4; ++k) {
					out[i][j] += a_left[i][k] * a_right[k][j];
				}
			}
		}
		return out;
	}

	Matrix Transpose(const Matrix& a_matrix) noexcept
	{
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				out[i][j] = a_matrix[j][i];
			}
		}
		return out;
	}

	std::optional<Matrix> Invert(const Matrix& a_matrix) noexcept
	{
		// Gauss-Jordan with partial pivoting, in double: a projection with
		// no far plane has entries far apart in magnitude.
		std::array<std::array<double, 8>, 4> work{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				work[i][j] = a_matrix[i][j];
			}
			work[i][4 + i] = 1.0;
		}
		for (std::size_t column = 0; column < 4; ++column) {
			std::size_t pivot = column;
			for (std::size_t row = column + 1; row < 4; ++row) {
				if (std::abs(work[row][column]) > std::abs(work[pivot][column])) {
					pivot = row;
				}
			}
			if (!(std::abs(work[pivot][column]) > 1e-12)) {
				return std::nullopt;
			}
			std::swap(work[pivot], work[column]);
			const auto divisor = work[column][column];
			for (auto& value : work[column]) {
				value /= divisor;
			}
			for (std::size_t row = 0; row < 4; ++row) {
				if (row == column) {
					continue;
				}
				const auto factor = work[row][column];
				for (std::size_t k = 0; k < 8; ++k) {
					work[row][k] -= factor * work[column][k];
				}
			}
		}
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				out[i][j] = static_cast<float>(work[i][4 + j]);
			}
		}
		return out;
	}

	std::array<float, 2> JitterFromEngineOffset(
		float a_offsetX,
		float a_offsetY,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight) noexcept
	{
		return { -a_offsetX * static_cast<float>(a_displayWidth) / 2.0f, a_offsetY * static_cast<float>(a_displayHeight) / 2.0f };
	}

	std::optional<Constants> BuildConstants(
		const CameraFrame& a_current,
		const CameraFrame* a_previous,
		float a_nearPlane,
		float a_farPlane,
		std::array<float, 2> a_jitter,
		bool a_reset) noexcept
	{
		const auto& world = a_current.worldToClip;

		// Row three is forward, because w is forward dotted with the point
		// (Render::FogCameraFromMatrix). Rows zero and one are the scaled
		// right and up, plus whatever jitter leaned them towards forward;
		// Gram-Schmidt takes that lean out.
		const auto forward = Normalised(Row(world, 3));
		if (!forward) {
			return std::nullopt;
		}
		const auto rowRight = Row(world, 0);
		const auto right = Normalised(Minus(rowRight, Scaled(*forward, Dot(rowRight, *forward))));
		if (!right) {
			return std::nullopt;
		}
		const auto rowUp = Row(world, 1);
		const auto up = Normalised(Minus(Minus(rowUp, Scaled(*forward, Dot(rowUp, *forward))), Scaled(*right, Dot(rowUp, *right))));
		if (!up) {
			return std::nullopt;
		}

		const auto& position = a_current.position;
		Matrix viewToWorld{};
		for (std::size_t i = 0; i < 3; ++i) {
			viewToWorld[i][0] = (*right)[i];
			viewToWorld[i][1] = (*up)[i];
			viewToWorld[i][2] = (*forward)[i];
			viewToWorld[i][3] = position[i];
		}
		viewToWorld[3] = { 0.0f, 0.0f, 0.0f, 1.0f };

		const auto viewToClip = Multiply(world, viewToWorld);
		const auto clipToView = Invert(viewToClip);
		const auto clipToWorld = Invert(world);
		if (!clipToView || !clipToWorld) {
			return std::nullopt;
		}
		const auto& previous = a_previous != nullptr ? a_previous->worldToClip : world;
		const auto clipToPrevious = Multiply(previous, *clipToWorld);
		const auto previousToClip = Invert(clipToPrevious);
		if (!previousToClip) {
			return std::nullopt;
		}

		const auto scaleX = viewToClip[0][0];
		const auto scaleY = viewToClip[1][1];
		if (!(std::abs(scaleX) > kDegenerate) || !(std::abs(scaleY) > kDegenerate)) {
			return std::nullopt;
		}

		Constants out{};
		out.cameraViewToClip = Transpose(viewToClip);
		out.clipToCameraView = Transpose(*clipToView);
		out.clipToPrevClip = Transpose(clipToPrevious);
		out.prevClipToClip = Transpose(*previousToClip);
		out.jitterOffset = a_jitter;
		out.cameraPos = position;
		out.cameraRight = *right;
		out.cameraUp = *up;
		out.cameraFwd = *forward;
		out.cameraNear = a_nearPlane;
		out.cameraFar = a_farPlane;
		out.cameraFOV = 2.0f * std::atan(1.0f / scaleY);
		out.cameraAspectRatio = scaleY / scaleX;
		out.reset = a_reset;
		out.diagnosticResidue = std::max({ std::abs(viewToClip[0][3]), std::abs(viewToClip[1][3]), std::abs(viewToClip[3][3]) });
		return out;
	}
}
