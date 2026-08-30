#include "Shader/ShaderOverride.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <memory>

namespace Shader
{
	void PixelShaderOverride::Adopt(RE::BSGraphics::PixelShader* a_slot) noexcept
	{
		_slot = a_slot;
		_original = a_slot != nullptr ? a_slot->shader : nullptr;
	}

	bool PixelShaderOverride::Install(
		std::span<const std::uint8_t> a_bytecode,
		std::string_view a_debugName) noexcept
	{
		if (_slot == nullptr || a_bytecode.empty()) {
			return false;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			REX::ERROR("no device, cannot create the replacement shader");
			return false;
		}

		REX::W32::ID3D11PixelShader* created = nullptr;
		const auto hr = device->CreatePixelShader(
			a_bytecode.data(),
			a_bytecode.size(),
			nullptr,
			std::addressof(created));

		if (hr < 0 || created == nullptr) {
			REX::ERROR("CreatePixelShader failed with 0x{:08X}", static_cast<std::uint32_t>(hr));
			return false;
		}

		static_cast<void>(Render::SetDebugName(created, a_debugName));

		// The old one is released only after the slot points at the new one, so
		// there is no instant at which the engine could bind a dead shader.
		auto* const previous = _ours;
		_ours = created;
		_slot->shader = created;

		if (previous != nullptr) {
			previous->Release();
		}

		REX::INFO(
			"installed {} in place of {}",
			static_cast<const void*>(created),
			static_cast<const void*>(_original));

		return true;
	}

	void PixelShaderOverride::Restore() noexcept
	{
		if (_slot == nullptr) {
			return;
		}

		_slot->shader = _original;

		if (_ours != nullptr) {
			_ours->Release();
			_ours = nullptr;
		}
	}

	void PixelShaderOverride::Guard() noexcept
	{
		if (_slot == nullptr || _ours == nullptr) {
			return;
		}

		auto* const current = _slot->shader;
		if (current == _ours) {
			return;
		}

		// Something replaced what we put there. If it is not the shader we
		// remembered either, the engine reloaded its own - adopt the new one so
		// that a later Restore puts back something valid rather than a stale
		// pointer.
		if (current != _original) {
			REX::INFO(
				"the engine put {} in our slot, adopting it as the original",
				static_cast<const void*>(current));
			_original = current;
		}

		_slot->shader = _ours;
	}
}
