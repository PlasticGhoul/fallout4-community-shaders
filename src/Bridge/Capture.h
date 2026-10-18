#pragma once

#include "Bridge/Bridge.h"
#include "Bridge/CameraConstants.h"
#include "Bridge/CaptureDemand.h"
#include "Render/Resources.h"
#include "Upscale/CameraCut.h"
#include "Upscale/EngineHooks.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Bridge
{
	/// Who needs the frame's inputs in the bridge.
	enum class Consumer : std::uint32_t
	{
		kFrameGeneration = 1,
		kNeuralRendering = 2,
	};

	/// The only Upscale::Hooks::Tap: tells the bridge where rendering starts,
	/// copies depth and motion into the slot, builds the camera and copies
	/// the picture before the HUD - in every frame some consumer wants it.
	/// Frame generation and neural rendering both read these; either runs
	/// without the other. It owns nothing of the engine's and only reads.
	class Capture final : public Upscale::Hooks::Tap
	{
	public:
		/// From a consumer's Setup: compiles CopyDepth on first use and sets
		/// the tap. False with the reason logged.
		[[nodiscard]] bool Acquire(Consumer a_consumer) noexcept;
		/// From Shutdown; the last one out removes the tap and frees the shader.
		void Release(Consumer a_consumer) noexcept;
		/// From Frame, every frame: whether this consumer needs the next
		/// frame's inputs.
		void Want(Consumer a_consumer, bool a_want) noexcept;
		/// From whoever logs once a second; writes at most once per interval
		/// however many consumers call it, and resets the counters.
		void LogSecond() noexcept;

		void AfterTemporalData() noexcept override;
		void AfterEffectRange() noexcept override;
		void AfterDynamicViewportDefault(bool a_enabled) noexcept override;

	private:
		[[nodiscard]] bool CompileShader() noexcept;
		void CaptureDepthAndMotion() noexcept;
		void SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept;
		[[nodiscard]] bool TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept;
		void Reset() noexcept;

		REX::W32::ID3D11ComputeShader* _copyDepth{ nullptr };
		Render::ConstantBuffer _constants;
		std::uint32_t _acquired{ 0 };
		CaptureDemand _demand;
		bool _reset{ true };
		bool _havePrevious{ false };
		CameraFrame _previous{};
		bool _haveCamera{ false };
		Upscale::CameraState _camera{};

		std::uint64_t _lastLog{ 0 };
		std::uint64_t _captures{ 0 };
		std::uint64_t _hudless{ 0 };
		/// Each refusal by its own reason.
		std::uint64_t _refusedDepthMotion{ 0 };
		std::uint64_t _refusedCamera{ 0 };
		std::uint64_t _refusedHudless{ 0 };
	};

	[[nodiscard]] Capture& TheCapture() noexcept;
}
