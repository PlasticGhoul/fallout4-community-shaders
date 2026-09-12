#pragma once

#include "Feature/Feature.h"
#include "Render/CubeTarget.h"
#include "Render/DrawObserver.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <vector>

namespace Features
{
	/// Shadows of the clouds the engine draws, multiplied onto the direct
	/// light from Phase::kBeforeComposite.
	///
	/// Two halves. The capture watches the engine's BSSkyClouds draws through
	/// the draw hook and repeats each of them into five faces of an A8
	/// cubemap of our own, with the sky's view projection swapped for one
	/// face at a time and a blend state that composites the shader's alpha -
	/// the coverage - and nothing else. The engine's shaders, geometry and
	/// textures are untouched. The pass reconstructs each pixel's position
	/// from the depth buffer, casts it along the sun onto a spherical cloud
	/// shell and samples the cubemap there.
	///
	/// This is the spec's Weg B. The probe of 2026-09-12 saw the engine draw
	/// its clouds nine times a frame into RT_004 and never into its cubemap,
	/// and found the sky's view projection in vertex constant buffer 12
	/// without a translation: the sky is camera centred, so a face needs
	/// only its axes.
	class CloudShadows final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "CloudShadows"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;
		void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;

	private:
		/// Five faces: every direction but straight down.
		static constexpr std::uint32_t kCapturedFaces = 5;

		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();
		[[nodiscard]] bool EnsureConstantCopies(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept;
		void ReadStagedConstants() noexcept;
		void WriteFaceConstants(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		void ReleaseSaved() noexcept;

		Render::TechniqueFilter _clouds{};
		Render::CubeTarget _coverage;
		Render::ConstantBuffer _constants;
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };
		REX::W32::ID3D11BlendState* _plain{ nullptr };
		REX::W32::ID3D11BlendState* _alphaComposite{ nullptr };
		REX::W32::ID3D11RasterizerState* _cullNone{ nullptr };
		REX::W32::ID3D11SamplerState* _linearClamp{ nullptr };
		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };

		// The sky's vertex constants, a frame old: copied into staging at the
		// first clouds draw, read back from Present, written out with one
		// face's matrices each. Sized from the engine's own buffer.
		REX::W32::ID3D11Buffer* _staging{ nullptr };
		REX::W32::ID3D11Buffer* _faceConstants[kCapturedFaces]{};
		std::vector<std::uint8_t> _image;
		std::uint32_t _constantBytes{ 0 };
		std::uint64_t _stagingFrame{ 0 };
		std::uint64_t _facesWrittenFrame{ 0 };
		std::uint64_t _copiedFrame{ 0 };
		bool _imageReady{ false };

		// Saved around one repeated draw, on the render thread.
		/// All eight: the clouds technique binds RT_004 and RT_029, the motion
		/// vectors, and TAA needs the second one from every layer.
		static constexpr std::uint32_t kSavedTargets = 8;
		REX::W32::ID3D11RenderTargetView* _savedTargets[kSavedTargets]{};
		REX::W32::ID3D11DepthStencilView* _savedDepth{ nullptr };
		REX::W32::ID3D11BlendState* _savedBlend{ nullptr };
		REX::W32::ID3D11RasterizerState* _savedRasterizer{ nullptr };
		REX::W32::ID3D11Buffer* _savedConstants{ nullptr };
		float _savedFactor[4]{};
		std::uint32_t _savedMask{ 0 };
		REX::W32::D3D11_VIEWPORT _savedViewports[16]{};
		std::uint32_t _savedViewportCount{ 16 };
		bool _mainView{ false };
		bool _repeating{ false };

		std::uint64_t _faceClearedFrame[Render::CubeTarget::kFaces]{};
		std::uint32_t _capturedDraws{ 0 };
		std::uint32_t _repeatedDraws{ 0 };
		std::uint64_t _draws{ 0 };
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };
		bool _reportedLowSun{ false };
	};
}
