#pragma once

#include "Feature/Feature.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Features
{
	/// Exponential height fog, drawn onto the finished HDR scene.
	///
	/// The second pass of our own and the first behind the opaque scene: it
	/// runs from Phase::kAfterOpaque, reads the scene depth, and blends fog
	/// onto Targets::kSceneHDR. The game's own fog stays and is dimmed through
	/// fogState.clamp, a value in engine memory that Shutdown hands back.
	///
	/// The dimming is written from Phase::kBeforeComposite, not from Present.
	/// The probe of 2026-09-12 wrote from Present and found the engine's own
	/// copy back in the value in 2 of 69 samples at the composite - one frame
	/// of full game fog each time, a flash at the horizon. Written in the
	/// composite's own SetupTechnique, just before it reads, the window is as
	/// small as it gets; the log counts how often the engine still gets in.
	class ExponentialHeightFog final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ExponentialHeightFog"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureBlendState();

		/// Dims the game's fog by the vanillaFog setting, remembering what
		/// the engine had so that a value the engine did not refresh is not
		/// multiplied a second time, and so that Shutdown can restore it.
		void ApplyVanillaFog();
		void RestoreVanillaFog() noexcept;

		/// The clamp the engine meant, whether or not we are dimming it.
		[[nodiscard]] float VanillaClamp(float a_current) const noexcept;

		Render::ConstantBuffer _constants;
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11BlendState* _alpha{ nullptr };
		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };
		Render::PhaseDispatcher::Token _vanillaPhase{ Render::PhaseDispatcher::kNoToken };

		std::uint64_t _draws{ 0 };
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };
		bool _reportedNoCamera{ false };

		/// Two things the once-a-second line reports, both about whether the
		/// frame is the frame the trace measured: the technique that fired the
		/// anchor and how often it changed, and in how many frames the clamp
		/// at draw time was no longer ours.
		std::uint32_t _anchorTechnique{ 0 };
		std::uint32_t _anchorChanges{ 0 };
		std::uint32_t _clampForeignFrames{ 0 };
		std::uint32_t _framesSinceLog{ 0 };

		float _clampOriginal{ 0.0f };
		float _clampWritten{ 0.0f };
		bool _clampOverridden{ false };
	};
}
