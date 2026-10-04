#pragma once

#include <REX/W32/D3D11.h>

#include <RE/B/BSGraphics.h>

#include <array>
#include <cstddef>

namespace Upscale
{
	/// The smaller stand-ins doodlum's upscaler puts under the deferred
	/// composite while the engine renders below display size, and the 320
	/// engine sampler states with a mip bias for the world passes.
	///
	/// The composite reads the g-buffer and light targets as if they filled
	/// their textures; with the dynamic resolution they hold the picture in
	/// their top left render rectangle only. For the composite's duration the
	/// engine's entries are swapped for textures of the render size, the
	/// rectangle copied in, and copied back afterwards.
	class ProxyTargets
	{
	public:
		/// doodlum's twelve: the g-buffer, both light targets, SSAO and the
		/// scene targets the composite reads.
		static constexpr std::array<std::size_t, 12> kSlots{ 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61 };
		static constexpr std::size_t kSamplerStates = 320;

		ProxyTargets() = default;
		~ProxyTargets();

		ProxyTargets(const ProxyTargets&) = delete;
		ProxyTargets& operator=(const ProxyTargets&) = delete;

		/// Recreates the stand-ins when the ratios change; none at ratio one.
		[[nodiscard]] bool Update(float a_ratioX, float a_ratioY) noexcept;
		void Swap(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		void Restore(REX::W32::ID3D11DeviceContext& a_context) noexcept;
		[[nodiscard]] bool Swapped() const noexcept { return _swapped; }

		/// Recreates the biased samplers when the bias changes.
		[[nodiscard]] bool UpdateSamplers(float a_mipBias) noexcept;
		void BiasSamplers() noexcept;
		void RestoreSamplers() noexcept;

		/// Restores first if anything is swapped.
		void Release(REX::W32::ID3D11DeviceContext* a_context) noexcept;

	private:
		void ReleaseProxies() noexcept;
		void ReleaseSamplers() noexcept;

		std::array<RE::BSGraphics::RenderTarget, 101> _original{};
		std::array<RE::BSGraphics::RenderTarget, 101> _proxy{};
		std::array<std::uint32_t, 100> _originalWidth{};
		std::array<std::uint32_t, 100> _originalHeight{};
		float _ratioX{ 1.0f };
		float _ratioY{ 1.0f };
		bool _swapped{ false };

		std::array<REX::W32::ID3D11SamplerState*, kSamplerStates> _engineSamplers{};
		std::array<REX::W32::ID3D11SamplerState*, kSamplerStates> _biasedSamplers{};
		float _mipBias{ 0.0f };
		bool _haveBias{ false };
		bool _biased{ false };
	};
}
