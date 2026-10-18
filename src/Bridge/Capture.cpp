#include "Bridge/Capture.h"

#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/Resolution.h"
#include "Render/SwapChainHook.h"
#include "Render/Targets.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>

#include <cmath>

namespace Bridge
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// FO4_RT_029, R16G16_FLOAT: the engine's motion vectors, in uv units.
		constexpr std::size_t kMotionVectors = 29;

		/// The engine's camera planes; DLSS reads the same two floats.
		constexpr std::uint64_t kCameraNear = 2712882;
		constexpr std::uint64_t kCameraFar = 2712883;

		/// Field for field the cbuffer CopyDepth of CopyDepth.hlsl.
		struct CopyDepthConstants
		{
			std::uint32_t outputSize[2];
			std::uint32_t inputSize[2];
		};
		static_assert(sizeof(CopyDepthConstants) == 16);

		std::uint32_t Groups(std::uint32_t a_pixels) noexcept
		{
			return (a_pixels + 7) / 8;
		}

		std::uint32_t Bit(Consumer a_consumer) noexcept
		{
			return static_cast<std::uint32_t>(a_consumer);
		}
	}

	Capture& TheCapture() noexcept
	{
		static Capture capture;
		return capture;
	}

	bool Capture::Acquire(Consumer a_consumer) noexcept
	{
		if (_acquired == 0) {
			Reset();
			// The shader ships in the FrameGeneration addon, which neural
			// rendering requires anyway: Streamline comes with it.
			if (!CompileShader()) {
				return false;
			}
			if (!_constants.Create(sizeof(CopyDepthConstants), "FO4CS_CB_CaptureCopyDepth")) {
				REX::ERROR("capture: the constant buffer could not be created");
				_copyDepth->Release();
				_copyDepth = nullptr;
				return false;
			}
			Upscale::Hooks::SetTap(this);
		}
		_acquired |= Bit(a_consumer);
		return true;
	}

	void Capture::Release(Consumer a_consumer) noexcept
	{
		_acquired &= ~Bit(a_consumer);
		Want(a_consumer, false);
		if (_acquired != 0) {
			return;
		}
		Upscale::Hooks::SetTap(nullptr);
		if (_copyDepth != nullptr) {
			_copyDepth->Release();
			_copyDepth = nullptr;
		}
		_constants.Release();
		Reset();
	}

	void Capture::Want(Consumer a_consumer, bool a_want) noexcept
	{
		if (a_want && (_acquired & Bit(a_consumer)) != 0) {
			_wanted |= Bit(a_consumer);
		} else {
			_wanted &= ~Bit(a_consumer);
		}
		const bool active = _wanted != 0;
		if (active && !_active) {
			// History from before an off stretch is not this frame's.
			_reset = true;
		}
		_active = active;
	}

	void Capture::Reset() noexcept
	{
		_wanted = 0;
		_active = false;
		_reset = true;
		_havePrevious = false;
		_haveCamera = false;
		_captures = 0;
		_hudless = 0;
		_refusedDepthMotion = 0;
		_refusedCamera = 0;
		_refusedHudless = 0;
	}

	void Capture::LogSecond() noexcept
	{
		const auto frame = Render::FrameCount();
		if (frame - _lastLog < kLogInterval) {
			return;
		}
		_lastLog = frame;
		REX::INFO(
			"capture: {} depth+motion / {} hud-less, refused {} depth+motion / {} camera / {} hud-less",
			_captures, _hudless, _refusedDepthMotion, _refusedCamera, _refusedHudless);
		_captures = 0;
		_hudless = 0;
		_refusedDepthMotion = 0;
		_refusedCamera = 0;
		_refusedHudless = 0;
	}

	bool Capture::CompileShader() noexcept
	{
		constexpr const char* kFile = "FrameGeneration/CopyDepth.hlsl";
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			REX::ERROR("capture: no device");
			return false;
		}
		const auto text = Shader::LoadSource(Util::DataDirectory() / "Shaders" / "FO4", kFile);
		if (!text) {
			REX::ERROR("capture: {}", text.error());
			return false;
		}
		const auto result = Shader::CompileComputeShader(text->text, kFile, "main");
		if (!result.Succeeded()) {
			REX::ERROR("capture: {}", result.diagnostics);
			return false;
		}
		if (device->CreateComputeShader(result.bytecode.data(), result.bytecode.size(), nullptr, std::addressof(_copyDepth)) < 0) {
			REX::ERROR("capture: CreateComputeShader failed");
			return false;
		}
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_copyDepth), "FO4CS_CS_CaptureCopyDepth"));
		return true;
	}

	void Capture::AfterTemporalData() noexcept
	{
		Bridge::OnRenderStart();
	}

	void Capture::AfterEffectRange() noexcept
	{
		if (_active) {
			const Render::PassScope pass{ "Capture/DepthMotion"sv };
			CaptureDepthAndMotion();
		}
	}

	void Capture::CaptureDepthAndMotion() noexcept
	{
		Bridge::InputSlot slot{};
		auto* const context = Render::GetContext();
		auto* const motion = Render::Targets::RenderTargetTexture(kMotionVectors);
		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		if (!Bridge::CurrentSlot(slot) || context == nullptr || motion == nullptr || depth == nullptr || _copyDepth == nullptr) {
			++_refusedDepthMotion;
			return;
		}
		const auto frame = Render::Resolution::Current();
		const auto depthExtent = Render::Resolution::DepthExtent(frame);
		const auto motionExtent = frame.render;

		REX::W32::D3D11_TEXTURE2D_DESC motionDesc{};
		motion->GetDesc(std::addressof(motionDesc));
		if (motionDesc.format != REX::W32::DXGI_FORMAT_R16G16_FLOAT || motionExtent.width > slot.width || motionExtent.height > slot.height ||
			depthExtent.width > slot.width || depthExtent.height > slot.height) {
			++_refusedDepthMotion;
			return;
		}

		const REX::W32::D3D11_BOX box{ 0, 0, 0, motionExtent.width, motionExtent.height, 1 };
		context->CopySubresourceRegion(static_cast<REX::W32::ID3D11Resource*>(slot.motion), 0, 0, 0, 0, motion, 0, std::addressof(box));

		// Depth goes out at the motion vectors' size: DLSS-G and DLSS-NR take
		// the two as one pair, and while DLSS upscales, the depth bound here
		// is DLSS's display-size one (DLSS::AfterEffectRange runs first).
		const CopyDepthConstants constants{ { motionExtent.width, motionExtent.height }, { depthExtent.width, depthExtent.height } };
		static_cast<void>(_constants.Update(std::addressof(constants), sizeof(constants)));
		auto* const buffer = _constants.Buffer();
		auto* uav = static_cast<REX::W32::ID3D11UnorderedAccessView*>(slot.depthUAV);
		context->CSSetShaderResources(0, 1, std::addressof(depth));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(buffer));
		context->CSSetShader(_copyDepth, nullptr, 0);
		context->Dispatch(Groups(motionExtent.width), Groups(motionExtent.height), 1);
		REX::W32::ID3D11ShaderResourceView* noView = nullptr;
		REX::W32::ID3D11UnorderedAccessView* noUAV = nullptr;
		REX::W32::ID3D11Buffer* noBuffer = nullptr;
		context->CSSetShaderResources(0, 1, std::addressof(noView));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(noUAV), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(noBuffer));
		context->CSSetShader(nullptr, nullptr, 0);

		++_captures;
		SubmitCamera(motionExtent.width, motionExtent.height, motionExtent.width, motionExtent.height);
	}

	bool Capture::TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept
	{
		const auto camera = Render::FogCameraFromMatrix(a_worldToCam, a_position[2]);
		const auto right = camera.rightOverScaleX;
		const auto halfWidth = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		const Upscale::CameraState current{
			{ a_position[0], a_position[1], a_position[2] },
			{ camera.forward[0], camera.forward[1], camera.forward[2] },
			2.0 * std::atan(halfWidth) * 180.0 / 3.14159265358979323846,
		};
		const bool cut = _haveCamera && Upscale::IsCameraCut(_camera, current);
		_camera = current;
		_haveCamera = true;
		return cut;
	}

	void Capture::SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (world == nullptr || state == nullptr) {
			return;
		}
		const auto& translate = world->GetWorldTransform().translate;
		const float position[3]{ translate.x, translate.y, translate.z };

		CameraFrame current{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				current.worldToClip[i][j] = world->worldToCam[i][j];
			}
		}
		current.position = { position[0], position[1], position[2] };

		if (TakeCameraCut(world->worldToCam, position)) {
			_reset = true;
		}

		const auto nearPlane = *reinterpret_cast<const float*>(REL::ID(kCameraNear).address());
		const auto farPlane = *reinterpret_cast<const float*>(REL::ID(kCameraFar).address());
		const auto jitter = JitterFromEngineOffset(state->offsetX, state->offsetY, state->screenWidth, state->screenHeight);
		// After a reset the frame before is not this one's history.
		const auto* const previous = _havePrevious && !_reset ? &_previous : nullptr;
		const auto built = BuildConstants(current, previous, nearPlane, farPlane, jitter, _reset);
		_previous = current;
		_havePrevious = true;
		if (!built) {
			++_refusedCamera;
			return;
		}
		FrameInputs inputs{};
		inputs.constants = *built;
		inputs.depthWidth = a_depthWidth;
		inputs.depthHeight = a_depthHeight;
		inputs.motionWidth = a_motionWidth;
		inputs.motionHeight = a_motionHeight;
		SubmitFrameInputs(inputs);
		_reset = false;
	}

	void Capture::AfterDynamicViewportDefault(bool a_enabled) noexcept
	{
		// doodlum takes the picture at the call that switches the dynamic
		// viewport off; the other calls of this site are not the end of
		// post-processing.
		if (a_enabled || !_active) {
			return;
		}
		const Render::PassScope pass{ "Capture/Hudless"sv };
		// Straight from the proxy's back buffer: that is what the game got
		// from GetBuffer(0), and what doodlum reads as renderTargets[0]. In
		// Fallout 4 RT_000's texture is null at this point (run of 2026-10-17).
		auto* const backBuffer = static_cast<REX::W32::ID3D11Texture2D*>(BackBuffer());
		InputSlot slot{};
		auto* const context = Render::GetContext();
		if (!CurrentSlot(slot) || context == nullptr || backBuffer == nullptr) {
			++_refusedHudless;
			return;
		}
		auto* const hudless = static_cast<REX::W32::ID3D11Texture2D*>(slot.hudless);
		context->CopyResource(hudless, backBuffer);
		SubmitHudless();
		++_hudless;
	}
}
