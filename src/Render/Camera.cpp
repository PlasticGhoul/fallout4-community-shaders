#include "Render/Camera.h"

#include "Render/Renderer.h"

#include <RE/B/BSGraphics.h>

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
