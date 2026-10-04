#pragma once

#include "Feature/Feature.h"
#include "Render/Extent.h"
#include "Render/TexelProbe.h"
#include "Upscale/CameraCut.h"
#include "Upscale/EngineHooks.h"
#include "Upscale/Ngx.h"
#include "Upscale/Passes.h"
#include "Upscale/ProxyTargets.h"
#include "Upscale/UpscaleMath.h"

#include <cstdint>

namespace Features
{
	/// NVIDIA DLSS in place of the engine's temporal anti-aliasing. The
	/// feature is the handler of Upscale::Hooks: at the start of a frame it
	/// sets the jitter (and, upscaling, the resolution ratios), keeps the
	/// engine's TAA out of the chain, and after RenderEffectRange hands the
	/// HDR scene, depth and dilated motion vectors to NGX on D3D11.
	///
	/// TAA's switch is forced on while this runs: the engine produces jitter
	/// and motion vectors only with TAA enabled. Both that switch and FXAA go
	/// back to what they were in Shutdown.
	class DLSS final : public Feature, public Upscale::Hooks::Handler
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "DLSS"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		void AfterTemporalData() noexcept override;
		[[nodiscard]] bool AllowTemporalAA() noexcept override;
		void AfterEffectRange() noexcept override;
		void BeforeDynamicViewportDefault() noexcept override;
		void AroundWorldPass(bool a_before) noexcept override;
		void AroundComposite(bool a_before) noexcept override;
		void AroundLensFlare(bool a_before) noexcept override;
		void AfterReflectionsSetup() noexcept override;

	private:
		[[nodiscard]] bool MenuForcesNative() const noexcept;
		[[nodiscard]] bool TakeCameraCut() noexcept;
		void LogMotionProbe() noexcept;
		void SetRatios(float a_x, float a_y) noexcept;
		/// The depth at display size in place of DS_002's view, for the
		/// passes that read the whole picture's depth after the composite
		/// stand-ins or after DLSS (doodlum's OverrideDepth / ResetDepth).
		void OverrideDepth() noexcept;
		void ResetDepth() noexcept;

		Upscale::Ngx _ngx;
		Upscale::Passes _passes;
		Upscale::ProxyTargets _proxies;
		Render::TexelProbe _motionProbe;

		bool _upscaling{ false };
		float _ratioX{ 1.0f };
		float _ratioY{ 1.0f };
		REX::W32::ID3D11ShaderResourceView* _savedDepthView{ nullptr };
		bool _depthOverridden{ false };
		std::uint64_t _fullDepthFrame{ UINT64_MAX };

		Upscale::Mode _mode{ Upscale::Mode::kDLAA };
		Upscale::Preset _preset{ Upscale::Preset::kDefault };
		Render::Extent _render{};
		Render::Extent _display{};
		Upscale::Jitter _jitter{ 0.0f, 0.0f };
		bool _active{ false };
		bool _wasNative{ true };
		bool _reset{ true };
		bool _enb{ false };
		bool _handlerSet{ false };

		bool _haveCamera{ false };
		Upscale::CameraState _camera{};

		/// What Setup found, given back in Shutdown.
		bool _taaWas{ false };
		bool _fxaaWas{ false };
		bool _engineSwitched{ false };

		// Counters for the once-a-second line.
		std::uint64_t _frames{ 0 };
		std::uint64_t _evaluations{ 0 };
		std::uint64_t _failedEvaluations{ 0 };
		std::uint64_t _resets{ 0 };
		std::uint64_t _composites{ 0 };
		std::uint64_t _lensFlares{ 0 };
		std::uint64_t _reflections{ 0 };
	};
}
