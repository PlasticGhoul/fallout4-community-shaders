#include "Render/TexelProbe.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <cstring>
#include <format>

namespace Render
{
	bool TexelProbe::Create(REX::W32::DXGI_FORMAT a_format, std::uint32_t a_bytesPerTexel, std::string_view a_debugName) noexcept
	{
		Release();
		auto* const device = GetDevice();
		if (device == nullptr || a_bytesPerTexel == 0) {
			return false;
		}
		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = kTexels;
		desc.height = 1;
		desc.mipLevels = 1;
		desc.arraySize = 1;
		desc.format = a_format;
		desc.sampleDesc.count = 1;
		desc.usage = REX::W32::D3D11_USAGE_STAGING;
		desc.bindFlags = 0;
		desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
		for (std::uint32_t i = 0; i < kRing; ++i) {
			if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_staging[i])) < 0) {
				REX::ERROR("texel probe {}: staging texture {} could not be created", a_debugName, i);
				Release();
				return false;
			}
			static_cast<void>(SetDebugName(
				reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_staging[i]), std::format("{}_{}", a_debugName, i)));
		}
		_bytesPerTexel = a_bytesPerTexel;
		return true;
	}

	void TexelProbe::Release() noexcept
	{
		for (auto*& staging : _staging) {
			if (staging != nullptr) {
				staging->Release();
				staging = nullptr;
			}
		}
		for (auto& written : _written) {
			written = 0;
		}
		_bytesPerTexel = 0;
	}

	void TexelProbe::Capture(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11Resource* a_source,
		std::uint32_t a_width,
		std::uint32_t a_height,
		std::uint64_t a_frame) noexcept
	{
		if (!Valid() || a_source == nullptr || a_width == 0 || a_height == 0) {
			return;
		}
		const auto slot = static_cast<std::uint32_t>(a_frame % kRing);
		for (std::uint32_t i = 0; i < kTexels; ++i) {
			const std::uint32_t x = static_cast<std::uint32_t>((static_cast<std::uint64_t>(i) * a_width) / kTexels);
			const std::uint32_t y = static_cast<std::uint32_t>((static_cast<std::uint64_t>(i) * a_height) / kTexels);
			const REX::W32::D3D11_BOX box{ x, y, 0, x + 1, y + 1, 1 };
			a_context.CopySubresourceRegion(_staging[slot], 0, i, 0, 0, a_source, 0, std::addressof(box));
		}
		_written[slot] = a_frame;
	}

	bool TexelProbe::Read(REX::W32::ID3D11DeviceContext& a_context, std::uint64_t a_frame, std::span<std::uint8_t> a_out) noexcept
	{
		if (!Valid() || a_out.size() < static_cast<std::size_t>(kTexels) * _bytesPerTexel) {
			return false;
		}
		const auto slot = static_cast<std::uint32_t>((a_frame + 1) % kRing);
		if (_written[slot] == 0) {
			return false;
		}
		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (a_context.Map(_staging[slot], 0, REX::W32::D3D11_MAP_READ, REX::W32::D3D11_MAP_FLAG_DO_NOT_WAIT, std::addressof(mapped)) < 0) {
			return false;
		}
		std::memcpy(a_out.data(), mapped.data, static_cast<std::size_t>(kTexels) * _bytesPerTexel);
		a_context.Unmap(_staging[slot], 0);
		return true;
	}
}
