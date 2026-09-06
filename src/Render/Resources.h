#pragma once

#include <REX/W32/D3D11.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Render
{
	/// A texture we own: Texture2D plus a shader resource view and an unordered
	/// access view, which is what a screen space pass needs and all it needs.
	///
	/// Both views are made up front rather than on demand. A pass that fails to
	/// bind halfway through is harder to reason about than one that never
	/// started.
	class Texture
	{
	public:
		Texture() = default;
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		/// a_format is a DXGI format. BSGraphics::Format is the DXGI enum, so
		/// the values in render-targets.md can be used as they stand.
		///
		/// Replaces whatever was there, which is how a resolution change is
		/// answered.
		[[nodiscard]] bool Create(
			std::uint32_t a_width,
			std::uint32_t a_height,
			REX::W32::DXGI_FORMAT a_format,
			std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] REX::W32::ID3D11UnorderedAccessView* UAV() const noexcept { return _uav; }
		[[nodiscard]] std::uint32_t Width() const noexcept { return _width; }
		[[nodiscard]] std::uint32_t Height() const noexcept { return _height; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		REX::W32::ID3D11UnorderedAccessView* _uav{ nullptr };
		std::uint32_t _width{ 0 };
		std::uint32_t _height{ 0 };
	};

	/// A dynamic constant buffer, written with MAP_WRITE_DISCARD.
	///
	/// Discard rather than a default buffer with UpdateSubresource, because the
	/// Bend sweep rewrites this between dispatches of the same frame: discard
	/// hands out fresh storage each time instead of waiting on the last one.
	class ConstantBuffer
	{
	public:
		ConstantBuffer() = default;
		~ConstantBuffer();

		ConstantBuffer(const ConstantBuffer&) = delete;
		ConstantBuffer& operator=(const ConstantBuffer&) = delete;

		/// a_bytes is rounded up to a multiple of sixteen, which D3D11 wants.
		[[nodiscard]] bool Create(std::size_t a_bytes, std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		/// False when the buffer does not exist, when a_bytes exceeds it, or
		/// when the map fails.
		[[nodiscard]] bool Update(const void* a_data, std::size_t a_bytes) noexcept;

		[[nodiscard]] REX::W32::ID3D11Buffer* Buffer() const noexcept { return _buffer; }

	private:
		REX::W32::ID3D11Buffer* _buffer{ nullptr };
		std::size_t _bytes{ 0 };
	};
}
