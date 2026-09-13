#pragma once

#include "Feature/Feature.h"
#include "Features/Skylighting/OcclusionProjection.h"
#include "Render/DepthTarget.h"
#include "Render/DrawObserver.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Render/TexelProbe.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <vector>

namespace Features
{
	/// Sky visibility from the scene the engine draws, as a factor on the
	/// ambient light.
	///
	/// Three stages a frame. Capture: every shadow caster draw of
	/// BSUtilityShader is repeated into a depth target of our own, with the
	/// sun's view projection swapped for an orthographic one looking down
	/// from this frame's sky direction. Grid: a compute shader folds that
	/// map into a 3D grid of SH2 probes around the camera, a running mean
	/// over the frames. Pass: in kComposite the factor is multiplied into
	/// RT_028, which the composite applies to its ambient term.
	///
	/// The probe of 2026-09-13 found the casters' view constants in vertex
	/// slot 12, the layout the sky uses too: rotation, projection, their
	/// product, the transposed rotation, 64 bytes each, geometry relative to
	/// the camera. 166 casters a frame, all into DS_008.
	class Skylighting final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "Skylighting"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;
		void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;

	private:
		static constexpr std::uint32_t kSavedTargets = 8;
		static constexpr std::uint32_t kSavedRects = 16;

		void UpdateProbes();
		void Draw();
		void DrawDebug();
		void DrawPass(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11RenderTargetView* a_target, REX::W32::ID3D11BlendState* a_blend, float a_debugView);
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();
		[[nodiscard]] bool EnsureViewCopy(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept;
		[[nodiscard]] bool BeginFrameCapture(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		[[nodiscard]] bool FillConstants(float a_debugView) noexcept;
		[[nodiscard]] bool Outdoors() const noexcept;
		void ResetGrid() noexcept;
		void ReleaseSaved() noexcept;
		void LogProbes(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame) noexcept;

		/// The numbers behind the pictures, once a second: sixteen texels of
		/// the occlusion map, of RT_028 before the multiply, and of the HDR
		/// scene. Tone mapping made every debug view white in the first
		/// acceptance run; these say what the maps hold.
		Render::TexelProbe _occlusionProbe;
		Render::TexelProbe _ambientProbe;
		Render::TexelProbe _sceneProbe;

		Render::TechniqueFilter _casters{};
		Render::DepthTarget _occlusion;
		Render::Texture3D _probes;
		Render::Texture3D _accumFrames;
		Render::ConstantBuffer _constants;
		REX::W32::ID3D11ComputeShader* _update{ nullptr };
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11SamplerState* _comparison{ nullptr };
		REX::W32::ID3D11SamplerState* _linearClamp{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };
		REX::W32::ID3D11BlendState* _plain{ nullptr };
		REX::W32::ID3D11DepthStencilState* _depthState{ nullptr };
		Render::PhaseDispatcher::Token _probePhase{ Render::PhaseDispatcher::kNoToken };
		Render::PhaseDispatcher::Token _compositePhase{ Render::PhaseDispatcher::kNoToken };
		Render::PhaseDispatcher::Token _debugPhase{ Render::PhaseDispatcher::kNoToken };
		Util::FileWatch _watch;

		// The capture of the current frame.
		REX::W32::ID3D11Buffer* _viewConstants{ nullptr };
		std::vector<std::uint8_t> _viewImage;
		std::uint64_t _captureFrame{ 0 };
		bool _captureReady{ false };
		bool _repeating{ false };
		bool _surrounding{ false };
		bool _reportedSmallBuffer{ false };
		bool _reportedStates{ false };
		double _direction[3]{};
		/// The projection for the casters, whose positions are absolute world
		/// coordinates: the cube is centred on the eye.
		double _captureMatrix[16]{};
		/// The same projection for the grid, whose cells are relative to the
		/// eye: the cube is centred on the origin.
		double _matrix[16]{};
		double _eye[3]{};

		// What BeforeDraw put aside for AfterDraw.
		REX::W32::ID3D11RenderTargetView* _savedTargets[kSavedTargets]{};
		REX::W32::ID3D11DepthStencilView* _savedDepth{ nullptr };
		REX::W32::ID3D11Buffer* _savedView{ nullptr };
		REX::W32::ID3D11DepthStencilState* _savedDepthState{ nullptr };
		std::uint32_t _savedStencilRef{ 0 };
		REX::W32::D3D11_VIEWPORT _savedViewports[kSavedRects]{};
		std::uint32_t _savedViewportCount{ 0 };
		REX::W32::D3D11_RECT _savedScissors[kSavedRects]{};
		std::uint32_t _savedScissorCount{ 0 };

		// The grid window, computed once a frame by the probe update; the
		// pass reads the same one.
		SkyOcclusion::GridWindow _window{};
		std::int32_t _previousCell[3]{};
		bool _gridValid{ false };
		bool _wasOutdoors{ false };

		// Counters for the once-a-second line.
		std::uint64_t _frames{ 0 };
		std::uint64_t _castersSeen{ 0 };
		std::uint64_t _castersRepeated{ 0 };
		std::uint64_t _dispatches{ 0 };
		std::uint64_t _passes{ 0 };
		bool _reportedIndoors{ false };
	};
}
