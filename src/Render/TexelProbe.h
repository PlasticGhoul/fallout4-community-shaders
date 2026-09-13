#pragma once

#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace Render
{
	/// Sixteen texels of a texture, read back to the CPU a few frames later:
	/// the numbers behind a picture, for when a debug view cannot be trusted
	/// - tone mapping turned every one of F5's views white and nobody could
	/// say whether the map underneath was empty.
	///
	/// Capture copies the texels along the texture's diagonal into a staging
	/// texture of a ring; Read maps the copy made kRing - 1 frames earlier
	/// with DO_NOT_WAIT, the lesson of F4. Formats have to match the source.
	///
	/// Colour textures only. D3D11 copies a depth-stencil resource whole or
	/// not at all: a boxed CopySubresourceRegion from one is dropped without
	/// an error, and Read then hands back the staging texture's uninitialised
	/// memory - which F5 took for an empty map, twice. A depth map is
	/// surveyed through a full-size staging copy instead.
	class TexelProbe
	{
	public:
		static constexpr std::uint32_t kTexels = 16;
		static constexpr std::uint32_t kRing = 6;

		TexelProbe() = default;
		~TexelProbe() { Release(); }
		TexelProbe(const TexelProbe&) = delete;
		TexelProbe& operator=(const TexelProbe&) = delete;

		[[nodiscard]] bool Create(REX::W32::DXGI_FORMAT a_format, std::uint32_t a_bytesPerTexel, std::string_view a_debugName) noexcept;
		void Release() noexcept;

		/// Copies texel (i * width / 16, i * height / 16) for i in 0..15 into
		/// this frame's staging texture.
		void Capture(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11Resource* a_source,
			std::uint32_t a_width,
			std::uint32_t a_height,
			std::uint64_t a_frame) noexcept;

		/// The texels captured kRing - 1 frames before a_frame, as raw bytes
		/// (kTexels * bytesPerTexel). False when nothing was captured there
		/// yet or the map would have waited.
		[[nodiscard]] bool Read(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame, std::span<std::uint8_t> a_out) noexcept;

		[[nodiscard]] bool Valid() const noexcept { return _staging[0] != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _staging[kRing]{};
		std::uint64_t _written[kRing]{};
		std::uint32_t _bytesPerTexel{ 0 };
	};
}
