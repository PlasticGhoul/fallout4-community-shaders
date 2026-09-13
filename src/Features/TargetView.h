#pragma once

#include "Feature/Feature.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Features
{
	/// One of the engine's render targets laid over the picture, chosen by
	/// its slot number, one channel or all three.
	///
	/// A tool, not an effect, like FrameTrace: off by default, owns nothing
	/// of the engine's, draws from Phase::kAfterOpaque so that the sky does
	/// not paint over it. Born as the probe of F5 - what does the composite
	/// read from RT_028? - and kept because every screen space feature after
	/// it will ask the same kind of question.
	class TargetView final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "TargetView"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();

		Render::ConstantBuffer _constants;
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11SamplerState* _pointClamp{ nullptr };
		REX::W32::ID3D11BlendState* _plain{ nullptr };
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };
		Util::FileWatch _watch;
		std::uint64_t _draws{ 0 };
	};
}
