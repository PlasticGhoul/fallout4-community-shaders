#pragma once

#include "Feature/Feature.h"
#include "Render/DrawObserver.h"

#include <REX/W32/D3D11.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Features
{
	/// The probe of F5: what the sun's shadow casters bind, once a second.
	///
	/// Watches every BSUtilityShader draw. Those into the shadow map are
	/// counted, and for the first of every 180th frame the vertex stage's
	/// constant buffers are listed and dumped, with a hint wherever sixty-four
	/// bytes end in the row 0 0 0 1 - the signature of a projection matrix.
	/// Draws into other depth targets are counted by target and technique,
	/// which answers whether rain draws an occlusion map of its own.
	///
	/// A blocking map is used on purpose: once a second, in a tool that is
	/// removed after its run, a stall is cheaper than the staging ring the
	/// features carry.
	class ShadowProbe final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ShadowProbe"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;
		void AfterDraw(REX::W32::ID3D11DeviceContext& a_context, const Render::DrawCall& a_call) noexcept override;

	private:
		static constexpr std::uint32_t kSlots = 14;
		static constexpr std::uint32_t kDumpBytes = 256;
		static constexpr std::size_t kOtherSlots = 13;
		static constexpr std::size_t kTechniques = 16;

		[[nodiscard]] bool EnsureStaging(std::uint32_t a_slot, std::uint32_t a_bytes) noexcept;
		void DumpBuffers(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame) noexcept;
		void RememberTechnique(std::uint32_t a_technique) noexcept;
		void ReadSun() noexcept;

		Render::TechniqueFilter _utility{};
		std::array<REX::W32::ID3D11Buffer*, kSlots> _staging{};
		std::array<std::vector<std::uint8_t>, kSlots> _last{};

		std::uint64_t _frame{ 0 };
		std::uint64_t _frames{ 0 };
		std::uint32_t _perFrameShadow{ 0 };
		std::uint32_t _perFrameOther{ 0 };
		std::uint32_t _lastFrameShadow{ 0 };
		std::uint32_t _lastFrameOther{ 0 };
		std::uint64_t _shadowDraws{ 0 };
		std::uint64_t _otherDraws{ 0 };
		std::array<std::uint64_t, kOtherSlots> _otherBySlot{};
		std::array<std::uint32_t, kTechniques> _otherTechniques{};
		std::size_t _otherTechniqueCount{ 0 };
		float _sun[3]{};
		bool _dumpedThisSecond{ false };
	};
}
