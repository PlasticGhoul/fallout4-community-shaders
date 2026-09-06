#include "Render/Camera.h"

#include "Render/Renderer.h"

#include <RE/B/BSGraphics.h>
#include <RE/N/NiCamera.h>

#include <REX/W32/DXGI.h>

#include <cmath>
#include <cstring>
#include <memory>

namespace Render
{
	namespace
	{
		bool g_reported = false;
		bool g_refused = false;

		/// Confirms that the singleton we resolved is the renderer state the
		/// game is actually drawing with, by comparing its idea of the screen
		/// against the swap chain's.
		///
		/// Run once. A mismatch means the id points at something else, and
		/// reading a matrix out of it would be reading somebody else's memory
		/// that happens to be mapped.
		bool CrossCheck(const RE::BSGraphics::State& a_state) noexcept
		{
			auto* const swapChain = GetSwapChain();
			if (swapChain == nullptr) {
				return false;
			}

			REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
			if (swapChain->GetDesc(std::addressof(desc)) < 0) {
				return false;
			}

			const bool matches =
				a_state.screenWidth == desc.bufferDesc.width &&
				a_state.screenHeight == desc.bufferDesc.height;

			REX::INFO(
				"camera state: screen {}x{}, swap chain {}x{}, back buffer {}x{} - {}",
				a_state.screenWidth,
				a_state.screenHeight,
				desc.bufferDesc.width,
				desc.bufferDesc.height,
				a_state.backBufferWidth,
				a_state.backBufferHeight,
				matches ? "agreed" : "MISMATCH, refusing the camera");

			return matches;
		}
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

	namespace
	{
		void LogMatrix(const char* a_what, const void* a_rows) noexcept
		{
			float m[16]{};
			std::memcpy(m, a_rows, sizeof(m));

			REX::INFO(
				"{}: [{:.4f} {:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f} {:.4f}] "
				"[{:.4f} {:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f} {:.4f}]",
				a_what,
				m[0], m[1], m[2], m[3],
				m[4], m[5], m[6], m[7],
				m[8], m[9], m[10], m[11],
				m[12], m[13], m[14], m[15]);
		}

		/// Row vector convention: a point goes in on the left, so the view is
		/// applied before the projection and the product is view * proj.
		void Multiply(const float (&a_lhs)[16], const float (&a_rhs)[16], float (&a_out)[16])
		{
			for (int row = 0; row < 4; ++row) {
				for (int column = 0; column < 4; ++column) {
					float sum = 0.0f;
					for (int k = 0; k < 4; ++k) {
						sum += a_lhs[row * 4 + k] * a_rhs[k * 4 + column];
					}
					a_out[row * 4 + column] = sum;
				}
			}
		}
	}

	void LogCameraMatrices() noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return;
		}

		const auto& camera = state->cameraState;
		const auto& view = camera.camViewData;

		LogMatrix("camera view", std::addressof(view.viewMat));
		LogMatrix("camera proj", std::addressof(view.projMat));
		LogMatrix("camera viewProj", std::addressof(view.viewProjMat));

		// The three places the world camera could be instead of cameraState,
		// covered in one pass so that finding it costs one run rather than
		// three. The cache is what the engine keeps per camera; the reference
		// camera is the scene node the state was built from, and its
		// worldToCam is the view matrix in another form.
		REX::INFO(
			"camera cache holds {}, reference camera {}",
			state->cameraDataCache.size(),
			camera.referenceCamera != nullptr ? "present" : "null");

		if (!state->cameraDataCache.empty()) {
			LogMatrix(
				"cache[0] viewProj",
				std::addressof(state->cameraDataCache[0].camViewData.viewProjMat));
		}

		if (camera.referenceCamera != nullptr) {
			LogMatrix("reference worldToCam", std::addressof(camera.referenceCamera->worldToCam));
		}
	}

	std::optional<std::array<float, 16>> ViewProjectionComputed() noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr || g_refused) {
			return std::nullopt;
		}

		const auto& view = state->cameraState.camViewData;

		float viewMatrix[16]{};
		float projMatrix[16]{};
		std::memcpy(viewMatrix, std::addressof(view.viewMat), sizeof(viewMatrix));
		std::memcpy(projMatrix, std::addressof(view.projMat), sizeof(projMatrix));

		if (!IsPlausibleViewProjection(viewMatrix) || !IsPlausibleViewProjection(projMatrix)) {
			return std::nullopt;
		}

		float product[16]{};
		Multiply(viewMatrix, projMatrix, product);

		std::array<float, 16> result{};
		for (std::size_t i = 0; i < result.size(); ++i) {
			result[i] = product[i];
		}

		return result;
	}

	void LogProjection() noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr || g_refused) {
			return;
		}

		const auto& view = state->cameraState.camViewData;

		float projection[16]{};
		std::memcpy(projection, std::addressof(view.projMat), sizeof(projection));

		REX::INFO(
			"projection rows: [{:.4f} {:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f} {:.4f}] "
			"[{:.4f} {:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f} {:.4f}]",
			projection[0], projection[1], projection[2], projection[3],
			projection[4], projection[5], projection[6], projection[7],
			projection[8], projection[9], projection[10], projection[11],
			projection[12], projection[13], projection[14], projection[15]);

		REX::INFO(
			"view depth range [{:.4f} {:.4f}], viewport [{:.1f} {:.1f} {:.1f} {:.1f}]",
			view.viewDepthRange.x,
			view.viewDepthRange.y,
			view.viewPort.left,
			view.viewPort.top,
			view.viewPort.right,
			view.viewPort.bottom);
	}

	std::optional<std::array<float, 16>> ViewProjection() noexcept
	{
		if (g_refused) {
			return std::nullopt;
		}

		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return std::nullopt;
		}

		if (!g_reported) {
			g_reported = true;
			if (!CrossCheck(*state)) {
				// Once and for good. A singleton that is not the one we want
				// will not become it later, and retrying would mean reading
				// unknown memory every frame.
				g_refused = true;
				return std::nullopt;
			}
		}

		// viewProjMat is four __m128 rows. Copied out as plain floats rather
		// than read through an intrinsic: this runs once a frame, and sixteen
		// loads are easier to be sure about than a shuffle.
		float matrix[16]{};
		std::memcpy(
			matrix,
			std::addressof(state->cameraState.camViewData.viewProjMat),
			sizeof(matrix));

		if (!IsPlausibleViewProjection(matrix)) {
			return std::nullopt;
		}

		std::array<float, 16> result{};
		for (std::size_t i = 0; i < result.size(); ++i) {
			result[i] = matrix[i];
		}

		return result;
	}
}
