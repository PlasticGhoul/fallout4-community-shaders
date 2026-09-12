#pragma once

#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

#include <cstdint>
#include <string_view>

namespace Render
{
	/// A cubemap we own: six faces to draw into, one view to sample. Square,
	/// one mip, no unordered access - the coverage map F4 collects the
	/// engine's cloud draws into, and the shape F9's cubemaps will take.
	class CubeTarget
	{
	public:
		static constexpr std::uint32_t kFaces = 6;

		CubeTarget() = default;
		~CubeTarget() { Release(); }
		CubeTarget(const CubeTarget&) = delete;
		CubeTarget& operator=(const CubeTarget&) = delete;

		/// Replaces whatever was there. a_format has to be a render target
		/// format on this device; the caller checks with CheckFormatSupport.
		[[nodiscard]] bool Create(std::uint32_t a_size, REX::W32::DXGI_FORMAT a_format, std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		/// Clears one face to (0, 0, 0, a_alpha).
		void ClearFace(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_face, float a_alpha) noexcept;

		[[nodiscard]] REX::W32::ID3D11RenderTargetView* FaceRTV(std::uint32_t a_face) const noexcept
		{
			return a_face < kFaces ? _faces[a_face] : nullptr;
		}
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] std::uint32_t Size() const noexcept { return _size; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11RenderTargetView* _faces[kFaces]{};
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		std::uint32_t _size{ 0 };
	};
}
