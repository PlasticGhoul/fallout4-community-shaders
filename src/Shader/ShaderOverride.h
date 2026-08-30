#pragma once

#include <RE/B/BSGraphics.h>
#include <REX/W32/D3D11.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace Shader
{
	/// Owns the replacement of one technique's pixel shader.
	///
	/// The engine's own shader is remembered but never referenced and never
	/// released - the same rule subproject B1 set for device, context and swap
	/// chain. Our own shader is ours alone.
	class PixelShaderOverride
	{
	public:
		/// Remembers the slot and whatever the engine put in it.
		void Adopt(RE::BSGraphics::PixelShader* a_slot) noexcept;

		/// Creates a shader from the bytecode and writes it into the slot. The
		/// previous own shader stays alive until the new one is in place.
		bool Install(
			std::span<const std::uint8_t> a_bytecode,
			std::string_view a_debugName) noexcept;

		/// Puts the engine's own shader back.
		void Restore() noexcept;

		/// Re-applies our shader if something else ended up in the slot. Cheap
		/// enough to run every frame: one comparison in the common case.
		void Guard() noexcept;

		[[nodiscard]] bool Adopted() const noexcept { return _slot != nullptr; }
		[[nodiscard]] bool Installed() const noexcept { return _ours != nullptr; }

	private:
		RE::BSGraphics::PixelShader* _slot{ nullptr };
		REX::W32::ID3D11PixelShader* _original{ nullptr };
		REX::W32::ID3D11PixelShader* _ours{ nullptr };
	};
}
