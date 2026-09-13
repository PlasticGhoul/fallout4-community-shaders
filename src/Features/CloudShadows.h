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

		/// Cloud layers a frame can carry; the weather format has 32, the
		/// probe saw nine drawn.
		static constexpr std::uint32_t kMaxLayers = 32;

		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();
		[[nodiscard]] bool EnsureConstantCopies(REX::W32::ID3D11DeviceContext& a_context, REX::W32::ID3D11Buffer* a_engine) noexcept;
		[[nodiscard]] bool EnsureGeometryCopies(REX::W32::ID3D11Buffer* a_engine) noexcept;
		void ReadStagedConstants() noexcept;
		void WriteFaceConstants(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		[[nodiscard]] bool ReadLayerGeometry(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_layer) noexcept;
		void WriteFaceGeometry(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_layer) noexcept;
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

		// The sky's vertex constants, kRing - 1 frames old: copied into staging at the
		// first clouds draw, read back from Present, written out with one
		// face's matrices each. Sized from the engine's own buffer.
		/// Copies go into a ring and are read kRing - 1 frames later. A read
		/// with DO_NOT_WAIT one frame after the copy failed in about half the
		/// frames at 180 fps - the GPU runs that far behind - and a layer left
		/// out for a frame was the flicker the user saw. Two frames later
		/// still failed for seconds at a time whenever the GPU was the
		/// bottleneck: D3D11 lets the CPU queue three frames, so the copy from
		/// two frames ago need not have run yet. Six is the depth the profiler
		/// keeps its timestamp queries in flight for, for the same reason.
		static constexpr std::uint32_t kRing = 6;
		static constexpr std::uint32_t WriteSlot(std::uint64_t a_frame) noexcept { return static_cast<std::uint32_t>(a_frame % kRing); }
		static constexpr std::uint32_t ReadSlot(std::uint64_t a_frame) noexcept { return static_cast<std::uint32_t>((a_frame + 1) % kRing); }

		REX::W32::ID3D11Buffer* _staging[kRing]{};
		std::uint64_t _stagingWrittenFrame[kRing]{};
		REX::W32::ID3D11Buffer* _faceConstants[kCapturedFaces]{};
		std::vector<std::uint8_t> _image;
		std::uint32_t _constantBytes{ 0 };
		std::uint64_t _stagingFrame{ 0 };
		std::uint64_t _facesWrittenFrame{ 0 };
		bool _imageReady{ false };
		std::uint32_t _mapFailures{ 0 };

		// The layer's geometry constants - slot 2, world view projection in
		// front, the layer's world matrix behind it - copied per draw, read a
		// frame later at the same draw of the next frame, and written out
		// with the face's projection times that world matrix.
		REX::W32::ID3D11Buffer* _layerStaging[kMaxLayers][kRing]{};
		std::uint64_t _layerWrittenFrame[kMaxLayers][kRing]{};
		std::vector<std::uint8_t> _layerImage[kMaxLayers];
		bool _layerReady[kMaxLayers]{};
		REX::W32::ID3D11Buffer* _geometryConstants[kCapturedFaces]{};
		REX::W32::ID3D11Buffer* _savedGeometry{ nullptr };
		std::uint32_t _geometryBytes{ 0 };
		std::uint32_t _layerIndex{ 0 };
		std::uint32_t _layer{ 0 };
		std::uint64_t _layerFrame{ 0 };
		std::uint32_t _layersReady{ 0 };

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
