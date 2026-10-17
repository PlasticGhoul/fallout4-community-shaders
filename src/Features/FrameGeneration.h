#pragma once

#include "Bridge/Bridge.h"
#include "Bridge/CameraConstants.h"
#include "Feature/Feature.h"
#include "Render/Extent.h"
#include "Render/Resources.h"
#include "Upscale/CameraCut.h"
#include "Upscale/EngineHooks.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <string>

namespace Features
{
	/// NVIDIA DLSS Frame Generation through the D3D12 bridge (src/Bridge).
	/// Whether the bridge exists was decided when the game created its device;
	/// this feature switches generation on and off within it, captures what
	/// DLSS-G needs from the engine's frame - depth, motion vectors, the
	/// picture before the HUD - and hands it over with the camera. It owns
	/// nothing of the engine's and changes nothing in it: it only reads.
	class FrameGeneration final : public Feature, public Upscale::Hooks::Tap
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameGeneration"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		void AfterTemporalData() noexcept override;
		void AfterEffectRange() noexcept override;
		void AfterDynamicViewportDefault(bool a_enabled) noexcept override;

	private:
		/// Why generation is off this frame; nullptr when nothing stands in the way.
		[[nodiscard]] const char* Blocker() const noexcept;
		[[nodiscard]] bool CompileShader() noexcept;
		void CaptureDepthAndMotion() noexcept;
		void SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept;
		[[nodiscard]] bool TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept;

		REX::W32::ID3D11ComputeShader* _copyDepth{ nullptr };
		Render::ConstantBuffer _constants;
		bool _tapSet{ false };

		bool _generating{ false };
		std::string _blocker{ "<start>" };
		bool _reset{ true };
		bool _havePrevious{ false };
		Bridge::CameraFrame _previous{};
		bool _haveCamera{ false };
		Upscale::CameraState _camera{};

		// For the once-a-second line.
		std::uint64_t _frames{ 0 };
		std::uint64_t _captures{ 0 };
		std::uint64_t _hudless{ 0 };
		/// What FO4_RT_000's texture was at the HUD-less point: none, the
		/// proxy's back buffer, or something else.
		std::uint64_t _rt0Null{ 0 };
		std::uint64_t _rt0IsBackBuffer{ 0 };
		std::uint64_t _rt0Other{ 0 };
		/// Each refusal by its own reason.
		std::uint64_t _refusedDepthMotion{ 0 };
		std::uint64_t _refusedCamera{ 0 };
		std::uint64_t _refusedHudless{ 0 };
		std::uint64_t _viewportCalls[2]{ 0, 0 };
		float _matrixLean[2]{ 0.0f, 0.0f };
		float _residue{ 0.0f };
		Render::Extent _depthSource{};
		Render::Extent _motionSize{};
	};
}
