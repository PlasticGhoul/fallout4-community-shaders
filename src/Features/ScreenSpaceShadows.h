#pragma once

#include "Feature/Feature.h"
#include "Features/ScreenSpaceShadows/BendDispatch.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace RE
{
	class Sky;
}

namespace Features
{
	/// Contact shadows from the sun, ray marched through the scene depth.
	///
	/// The first pass of our own. It runs from Render::FramePhase, the one
	/// point in the frame where the g-buffer is complete and kDFLight has
	/// written its direct light but the composite has not drawn - so the mask
	/// can be multiplied onto FO4_RT_058 and FO4_RT_059 without replacing a
	/// single engine permutation.
	class ScreenSpaceShadows : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ScreenSpaceShadows"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		/// What one frame's sweep needs, worked out on the CPU before anything
		/// is bound.
		struct RaymarchGeometry
		{
			float lightProjection[4]{};
			Bend::DispatchPlan plan;
		};

		/// Runs from the frame phase, on the render thread.
		void Draw();

		[[nodiscard]] bool BuildGeometry(const RE::Sky& a_sky, RaymarchGeometry& a_out);
		void RayMarch(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11ShaderResourceView* a_depth,
			const RaymarchGeometry& a_geometry);
		void Modulate(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11RenderTargetView* a_diffuse,
			REX::W32::ID3D11RenderTargetView* a_specular);

		[[nodiscard]] bool CompileRaymarch(std::uint32_t a_sampleCount);
		[[nodiscard]] bool CompileModulate();
		[[nodiscard]] bool EnsureMask();
		[[nodiscard]] bool EnsureSampler();
		[[nodiscard]] bool EnsureBlendState();

		/// The sample count the shader was last built for, scaled by resolution
		/// and quantised to eights so that a wobbling resolution does not
		/// recompile it every frame.
		[[nodiscard]] std::uint32_t ScaledSampleCount() const;

		Render::Texture _mask;
		Render::ConstantBuffer _constants;

		REX::W32::ID3D11ComputeShader* _raymarch{ nullptr };
		REX::W32::ID3D11PixelShader* _modulate{ nullptr };
		REX::W32::ID3D11SamplerState* _pointBorder{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };

		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };

		std::uint32_t _compiledSampleCount{ 0 };
		std::uint64_t _draws{ 0 };

		/// The phase's hit count as of the last check, and the frame it was
		/// checked on. A hit count that stops moving while we are subscribed is
		/// the only symptom of the vtable entry having been overwritten - see
		/// the ordering note in FramePhase.h - so it is worth one comparison a
		/// second.
		std::uint64_t _lastHits{ 0 };
		std::uint64_t _frames{ 0 };

		/// Whether the phase has ever fired. Before it has, silence is the
		/// normal state rather than a fault: the composite does not run in the
		/// main menu or on a loading screen.
		bool _everFired{ false };
		bool _reportedStall{ false };

		/// Logged once rather than per frame: a sun that is not there is the
		/// normal state indoors, not a fault.
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };

		/// The light's world rotation and the sun node's position, written once
		/// per session. Which column of that matrix is the direction is not
		/// established for Fallout 4, and one look at the log settles it
		/// instead of three game starts.
		bool _loggedDirection{ false };

		/// The four matrices ViewData carries, written once so that which of
		/// them actually holds the view projection can be read off.
		bool _loggedMatrices{ false };
	};
}
