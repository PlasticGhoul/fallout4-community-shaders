#include "Render/Camera.h"

#include "Render/Renderer.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiTransform.h>

#include <REX/W32/DXGI.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

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

	namespace
	{
		/// Where a_matrix puts a_vector, in pixels, and how far in front it is.
		/// Row vector on the left, no w divide until the end - the same way
		/// Bend takes its light coordinate.
		void ReportCandidate(
			const char* a_what,
			const float (&a_matrix)[16],
			const float (&a_vector)[4],
			std::uint32_t a_width,
			std::uint32_t a_height) noexcept
		{
			float clip[4]{};
			for (int column = 0; column < 4; ++column) {
				clip[column] =
					a_vector[0] * a_matrix[0 * 4 + column] +
					a_vector[1] * a_matrix[1 * 4 + column] +
					a_vector[2] * a_matrix[2 * 4 + column] +
					a_vector[3] * a_matrix[3 * 4 + column];
			}

			if (clip[3] == 0.0f) {
				REX::INFO("{}: w is zero, no position", a_what);
				return;
			}

			const auto x = ((clip[0] / clip[3]) * 0.5f + 0.5f) * static_cast<float>(a_width);
			const auto y = ((clip[1] / clip[3]) * -0.5f + 0.5f) * static_cast<float>(a_height);

			REX::INFO("{}: pixel [{:.0f} {:.0f}], w {:.4f}", a_what, x, y, clip[3]);
		}
	}

	void ProbeCameraMatrices(std::uint32_t a_width, std::uint32_t a_height) noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return;
		}

		const auto& view = state->cameraState.camViewData;

		float direction[4]{};
		std::memcpy(direction, std::addressof(view.viewDir), sizeof(float) * 3);
		direction[3] = 0.0f;

		REX::INFO(
			"probe: viewDir [{:.4f} {:.4f} {:.4f}] must land at [{} {}]",
			direction[0],
			direction[1],
			direction[2],
			a_width / 2,
			a_height / 2);

		const auto probe = [&](const char* a_what, const void* a_rows) {
			float m[16]{};
			std::memcpy(m, a_rows, sizeof(m));
			ReportCandidate(a_what, m, direction, a_width, a_height);
		};

		probe("probe state viewProj", std::addressof(view.viewProjMat));
		probe("probe state viewProjUnjittered", std::addressof(view.viewProjUnjittered));
		probe("probe state currentViewProjUnjittered", std::addressof(view.currentViewProjUnjittered));

		{
			float viewMatrix[16]{};
			float projMatrix[16]{};
			float product[16]{};
			std::memcpy(viewMatrix, std::addressof(view.viewMat), sizeof(viewMatrix));
			std::memcpy(projMatrix, std::addressof(view.projMat), sizeof(projMatrix));
			Multiply(viewMatrix, projMatrix, product);
			ReportCandidate("probe state view x proj", product, direction, a_width, a_height);
		}

		const auto cached_count = static_cast<std::uint32_t>(
			state->cameraDataCache.size() < 8 ? state->cameraDataCache.size() : 8);

		for (std::uint32_t i = 0; i < cached_count; ++i) {
			const auto& cached = state->cameraDataCache[i].camViewData;

			const auto name = std::string{ "probe cache[" } + std::to_string(i) + "] viewProj";
			probe(name.c_str(), std::addressof(cached.viewProjMat));

			float viewMatrix[16]{};
			float projMatrix[16]{};
			float product[16]{};
			std::memcpy(viewMatrix, std::addressof(cached.viewMat), sizeof(viewMatrix));
			std::memcpy(projMatrix, std::addressof(cached.projMat), sizeof(projMatrix));
			Multiply(viewMatrix, projMatrix, product);

			const auto productName =
				std::string{ "probe cache[" } + std::to_string(i) + "] view x proj";
			ReportCandidate(productName.c_str(), product, direction, a_width, a_height);
		}
	}

	namespace
	{
		/// The four numbers a perspective projection needs, however they were
		/// arrived at. Scale is the diagonal, shear the off-centre term, which
		/// is zero for a symmetric frustum and not for a split-screen or a
		/// jittered one.
		struct Perspective
		{
			float scaleX{ 0.0f };
			float scaleY{ 0.0f };
			float shearX{ 0.0f };
			float shearY{ 0.0f };
			float depthScale{ 1.0f };
		};

		bool g_loggedPerspective = false;
		bool g_loggedRefusal = false;

		/// Three rows that are unit length and mutually perpendicular. A camera
		/// basis is, and a half-written or foreign matrix is not - so this is
		/// the cheapest thing that can tell them apart.
		[[nodiscard]] bool IsOrthonormal(const float (&a_axes)[3][3]) noexcept
		{
			const auto dot = [&a_axes](int a_lhs, int a_rhs) {
				return a_axes[a_lhs][0] * a_axes[a_rhs][0] +
				       a_axes[a_lhs][1] * a_axes[a_rhs][1] +
				       a_axes[a_lhs][2] * a_axes[a_rhs][2];
			};

			for (int row = 0; row < 3; ++row) {
				const auto length = dot(row, row);
				if (!std::isfinite(length) || length < 0.99f || length > 1.01f) {
					return false;
				}
			}

			return std::abs(dot(0, 1)) < 0.01f && std::abs(dot(0, 2)) < 0.01f &&
			       std::abs(dot(1, 2)) < 0.01f;
		}

		/// Fallout 4's fDefaultWorldFOV, horizontal, in degrees. Only used when
		/// the frustum cannot be trusted, and said so in the log when it is.
		constexpr float kFallbackHorizontalFov = 70.0f;

		[[nodiscard]] bool FromFrustum(
			const RE::NiFrustum& a_frustum,
			float a_aspect,
			Perspective& a_out) noexcept
		{
			const float values[6]{
				a_frustum.left, a_frustum.right, a_frustum.top,
				a_frustum.bottom, a_frustum.near, a_frustum.far
			};

			for (const auto value : values) {
				if (!std::isfinite(value)) {
					return false;
				}
			}

			const auto width = a_frustum.right - a_frustum.left;
			const auto height = a_frustum.top - a_frustum.bottom;

			if (a_frustum.ortho || a_frustum.near <= 0.0f || a_frustum.far <= a_frustum.near ||
				width <= 0.0f || height <= 0.0f) {
				return false;
			}

			// The frustum has to describe the picture we are drawing. An aspect
			// that disagrees with the render target means this is somebody
			// else's camera, and that is the whole failure this code exists to
			// avoid repeating.
			const auto frustumAspect = width / height;
			if (frustumAspect < a_aspect * 0.98f || frustumAspect > a_aspect * 1.02f) {
				return false;
			}

			a_out.scaleX = 2.0f * a_frustum.near / width;
			a_out.scaleY = 2.0f * a_frustum.near / height;
			a_out.shearX = (a_frustum.right + a_frustum.left) / width;
			a_out.shearY = (a_frustum.top + a_frustum.bottom) / height;
			a_out.depthScale = a_frustum.far / (a_frustum.far - a_frustum.near);

			return true;
		}

		[[nodiscard]] Perspective FromFallbackFov(float a_aspect) noexcept
		{
			constexpr auto kDegreesToRadians = 0.01745329252f;
			const auto tanHalf = std::tan(kFallbackHorizontalFov * 0.5f * kDegreesToRadians);

			Perspective perspective;
			perspective.scaleX = 1.0f / tanHalf;
			perspective.scaleY = perspective.scaleX * a_aspect;

			return perspective;
		}
	}

	std::optional<std::array<float, 4>> ProjectPoint(const float (&a_direction)[3]) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR("world camera: Main::WorldRootCamera returned nothing");
			}
			return std::nullopt;
		}

		// A direction, never a point. The camera node reads as being at
		// [0 0 128] while the sun's node sits at eighty thousand units out:
		// Fallout 4 rebases the world around the camera, so those two are not
		// in the same space and subtracting one from the other carries the
		// player's absolute position in as an error the size of the signal.
		//
		// A direction is immune to all of it. With the fourth component zero
		// every translation term of the projection drops out, and what is left
		// is the rotation and the two scales.
		float projection[16]{};
		for (int row = 0; row < 4; ++row) {
			for (int column = 0; column < 4; ++column) {
				projection[row * 4 + column] = world->worldToCam[row][column];
			}
		}

		if (!IsPlausibleViewProjection(projection)) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR("world camera: worldToCam is empty or not finite");
			}
			return std::nullopt;
		}

		// worldToCam holds no rotation - it is camera space to clip, in
		// Bethesda's axis order with the near plane at 15. The orientation
		// lives in the camera node's own world transform, and inverting it is
		// the library's arithmetic rather than a convention to guess at.
		const RE::NiPoint3 direction{ a_direction[0], a_direction[1], a_direction[2] };
		const auto inverse = world->GetWorldTransform().Invert();
		const auto inCamera = inverse.rotate * direction;

		std::array<float, 4> clip{};
		for (int row = 0; row < 4; ++row) {
			clip[static_cast<std::size_t>(row)] =
				projection[row * 4 + 0] * inCamera.x +
				projection[row * 4 + 1] * inCamera.y +
				projection[row * 4 + 2] * inCamera.z;
		}

		return clip;
	}

	void LogProjectionSample(const float (&a_direction)[3]) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			return;
		}

		const auto& transform = world->GetWorldTransform();
		const auto& rotate = transform.rotate;

		// A rotation that only permutes a vector is an axis convention, not an
		// orientation. Aiming at the sun produced exactly that - the world
		// direction came back with its components cycled - so this says plainly
		// whether the camera node carries the player's heading at all.
		REX::INFO(
			"world camera rotate: [{:.3f} {:.3f} {:.3f}] [{:.3f} {:.3f} {:.3f}] "
			"[{:.3f} {:.3f} {:.3f}], at [{:.1f} {:.1f} {:.1f}]",
			rotate.entry[0][0], rotate.entry[0][1], rotate.entry[0][2],
			rotate.entry[1][0], rotate.entry[1][1], rotate.entry[1][2],
			rotate.entry[2][0], rotate.entry[2][1], rotate.entry[2][2],
			transform.translate.x, transform.translate.y, transform.translate.z);

		// The engine's own routine, as the arbiter. It uses the same matrix and
		// the same port the game does, with the game's conventions, so its
		// answer settles what ours should be without another convention to
		// guess at. A point far along the direction stands in for the sun.
		const RE::NiPoint3 far{
			transform.translate.x + a_direction[0] * 100000.0f,
			transform.translate.y + a_direction[1] * 100000.0f,
			transform.translate.z + a_direction[2] * 100000.0f
		};

		float ex = 0.0f;
		float ey = 0.0f;
		float ez = 0.0f;
		const auto onScreen = world->WorldPtToScreenPt3(far, ex, ey, ez, 1.0e-5f);

		REX::INFO(
			"engine says the sun is at [{:.3f} {:.3f}] depth {:.4f}, in front: {}",
			ex,
			ey,
			ez,
			onScreen ? "yes" : "no");
	}

	std::optional<std::array<float, 4>> ProjectDirection(
		const float (&a_direction)[3],
		std::uint32_t a_width,
		std::uint32_t a_height) noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr || g_refused || a_width == 0 || a_height == 0) {
			return std::nullopt;
		}

		// Not BSGraphics::State::cameraState. At the point in the frame where
		// this runs, that describes the sun's shadow camera and not the
		// player's: its viewDir and the sun light's direction agree to four
		// places, which is why every light direction came out along the view
		// axis and the sweep never moved. Main::WorldRootCamera is the camera
		// the picture is drawn with, and it is a call into the game rather than
		// a field to be guessed at. Its id was checked offline against
		// version-1-11-240-0.bin.
		auto* const world = RE::Main::WorldRootCamera();
		if (world == nullptr) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR("world camera: Main::WorldRootCamera returned nothing");
			}
			return std::nullopt;
		}

		// Rows of worldToCam are the camera's axes in world space; the
		// translation sits in the fourth column and a direction ignores it.
		float axes[3][3]{};
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				axes[row][column] = world->worldToCam[row][column];
			}
		}

		if (!IsOrthonormal(axes)) {
			// Named, with the numbers. A silent refusal is how the last run
			// looked like a success while the feature did nothing at all, and
			// a check that cannot say which one it was is no better than none.
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR(
					"world camera basis is not orthonormal: "
					"[{:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f}]",
					axes[0][0], axes[0][1], axes[0][2],
					axes[1][0], axes[1][1], axes[1][2],
					axes[2][0], axes[2][1], axes[2][2]);

				// The transposed reading, in case worldToCam is stored the
				// other way round - one look tells which, instead of a run.
				REX::ERROR(
					"the same read transposed: "
					"[{:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f}] [{:.4f} {:.4f} {:.4f}]",
					axes[0][0], axes[1][0], axes[2][0],
					axes[0][1], axes[1][1], axes[2][1],
					axes[0][2], axes[1][2], axes[2][2]);
			}
			return std::nullopt;
		}

		// The check that is not circular. Reading viewDir out of the same
		// struct as the matrix it belongs to proves nothing - it lands in the
		// centre by construction, whichever camera it is. This asks something
		// the failure would fail: the camera we draw with must not be looking
		// straight down the sun, and the shadow camera does exactly that.
		const auto& shadowView = state->cameraState.camViewData;
		float sunAxis[3]{};
		std::memcpy(sunAxis, std::addressof(shadowView.viewDir), sizeof(sunAxis));

		const auto alignment =
			axes[2][0] * sunAxis[0] + axes[2][1] * sunAxis[1] + axes[2][2] * sunAxis[2];

		if (alignment > 0.999f || alignment < -0.999f) {
			if (!g_loggedRefusal) {
				g_loggedRefusal = true;
				REX::ERROR(
					"world camera looks straight down the sun ({:.4f}) - this is the shadow "
					"camera again, refusing it",
					alignment);
			}
			return std::nullopt;
		}

		const auto aspect = static_cast<float>(a_width) / static_cast<float>(a_height);

		Perspective perspective;
		const bool fromFrustum = FromFrustum(world->viewFrustum, aspect, perspective);
		if (!fromFrustum) {
			perspective = FromFallbackFov(aspect);
		}

		const auto toView = [&axes](const float (&a_world)[3], float (&a_out)[3]) {
			for (int row = 0; row < 3; ++row) {
				a_out[row] =
					axes[row][0] * a_world[0] +
					axes[row][1] * a_world[1] +
					axes[row][2] * a_world[2];
			}
		};

		const auto toClip = [&perspective](const float (&a_view)[3], std::array<float, 4>& a_out) {
			a_out[0] = a_view[0] * perspective.scaleX + a_view[2] * perspective.shearX;
			a_out[1] = a_view[1] * perspective.scaleY + a_view[2] * perspective.shearY;
			a_out[2] = a_view[2] * perspective.depthScale;
			a_out[3] = a_view[2];
		};

		float inView[3]{};
		toView(a_direction, inView);

		std::array<float, 4> clip{};
		toClip(inView, clip);

		if (!g_loggedPerspective) {
			g_loggedPerspective = true;

			// No self-check here any more. The one that was here asked whether
			// viewDir lands in the centre, and viewDir is the third column of
			// the very matrix it was tested against, so it always did - a
			// tautology dressed as a test. What is worth writing down is what
			// was actually used, and the sun's place on screen, which the
			// player can check against the sky.
			REX::INFO(
				"camera basis right [{:.3f} {:.3f} {:.3f}] up [{:.3f} {:.3f} {:.3f}] "
				"forward [{:.3f} {:.3f} {:.3f}], sun alignment {:.3f}",
				axes[0][0], axes[0][1], axes[0][2],
				axes[1][0], axes[1][1], axes[1][2],
				axes[2][0], axes[2][1], axes[2][2],
				alignment);

			REX::INFO(
				"projection built {}: scale [{:.4f} {:.4f}], shear [{:.4f} {:.4f}], near {:.1f}",
				fromFrustum ? "from the world camera frustum" : "from the fallback field of view",
				perspective.scaleX,
				perspective.scaleY,
				perspective.shearX,
				perspective.shearY,
				world->viewFrustum.near);
		}

		return clip;
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
