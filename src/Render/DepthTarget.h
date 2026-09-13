#pragma once

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <string_view>

namespace Render
{
	/// A depth buffer we own: one view to draw into, one to sample. Square,
	/// R32_TYPELESS, no stencil, one mip. What F5 repeats the engine's shadow
	/// casters into; the shape F6's volumes will read from.
	class DepthTarget
	{
	public:
		DepthTarget() = default;
		~DepthTarget() { Release(); }
		DepthTarget(const DepthTarget&) = delete;
		DepthTarget& operator=(const DepthTarget&) = delete;

		/// Replaces whatever was there.
		[[nodiscard]] bool Create(std::uint32_t a_size, std::string_view a_debugName) noexcept;
		/// Idempotent.
		void Release() noexcept;
		void Clear(REX::W32::ID3D11DeviceContext& a_context, float a_depth) noexcept;

		[[nodiscard]] REX::W32::ID3D11DepthStencilView* DSV() const noexcept { return _dsv; }
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] std::uint32_t Size() const noexcept { return _size; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11DepthStencilView* _dsv{ nullptr };
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		std::uint32_t _size{ 0 };
	};
}
