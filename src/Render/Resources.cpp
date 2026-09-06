#include "Render/Resources.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <cstring>
#include <memory>

namespace Render
{
	Texture::~Texture()
	{
		Release();
	}

	bool Texture::Create(
		std::uint32_t a_width,
		std::uint32_t a_height,
		REX::W32::DXGI_FORMAT a_format,
		std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr) {
			REX::ERROR("texture {}: no device", a_debugName);
			return false;
		}

		if (a_width == 0 || a_height == 0) {
			REX::ERROR("texture {}: {}x{} has a zero dimension", a_debugName, a_width, a_height);
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = a_width;
		desc.height = a_height;
		desc.mipLevels = 1;
		desc.arraySize = 1;
		desc.format = a_format;
		desc.sampleDesc.count = 1;
		desc.sampleDesc.quality = 0;
		desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		desc.bindFlags =
			REX::W32::D3D11_BIND_SHADER_RESOURCE | REX::W32::D3D11_BIND_UNORDERED_ACCESS;

		if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_texture)) < 0) {
			REX::ERROR("texture {}: CreateTexture2D failed", a_debugName);
			return false;
		}

		REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.format = a_format;
		srvDesc.viewDimension = REX::W32::D3D_SRV_DIMENSION_TEXTURE2D;
		srvDesc.texture2D.mostDetailedMip = 0;
		srvDesc.texture2D.mipLevels = 1;

		if (device->CreateShaderResourceView(
				_texture, std::addressof(srvDesc), std::addressof(_srv)) < 0) {
			REX::ERROR("texture {}: the shader resource view failed", a_debugName);
			Release();
			return false;
		}

		REX::W32::D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.format = a_format;
		uavDesc.viewDimension = REX::W32::D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.texture2D.mipSlice = 0;

		if (device->CreateUnorderedAccessView(
				_texture, std::addressof(uavDesc), std::addressof(_uav)) < 0) {
			REX::ERROR("texture {}: the unordered access view failed", a_debugName);
			Release();
			return false;
		}

		_width = a_width;
		_height = a_height;

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_texture), a_debugName));

		return true;
	}

	void Texture::Release() noexcept
	{
		if (_uav != nullptr) {
			_uav->Release();
			_uav = nullptr;
		}
		if (_srv != nullptr) {
			_srv->Release();
			_srv = nullptr;
		}
		if (_texture != nullptr) {
			_texture->Release();
			_texture = nullptr;
		}

		_width = 0;
		_height = 0;
	}

	ConstantBuffer::~ConstantBuffer()
	{
		Release();
	}

	bool ConstantBuffer::Create(std::size_t a_bytes, std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr) {
			REX::ERROR("constant buffer {}: no device", a_debugName);
			return false;
		}

		if (a_bytes == 0) {
			REX::ERROR("constant buffer {}: zero bytes", a_debugName);
			return false;
		}

		const auto rounded = (a_bytes + 15) & ~static_cast<std::size_t>(15);

		REX::W32::D3D11_BUFFER_DESC desc{};
		desc.byteWidth = static_cast<std::uint32_t>(rounded);
		desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
		desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;

		if (device->CreateBuffer(std::addressof(desc), nullptr, std::addressof(_buffer)) < 0) {
			REX::ERROR("constant buffer {}: CreateBuffer failed", a_debugName);
			return false;
		}

		_bytes = rounded;

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_buffer), a_debugName));

		return true;
	}

	void ConstantBuffer::Release() noexcept
	{
		if (_buffer != nullptr) {
			_buffer->Release();
			_buffer = nullptr;
		}

		_bytes = 0;
	}

	bool ConstantBuffer::Update(const void* a_data, std::size_t a_bytes) noexcept
	{
		auto* const context = GetContext();
		if (context == nullptr || _buffer == nullptr || a_data == nullptr || a_bytes > _bytes) {
			return false;
		}

		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (context->Map(
				reinterpret_cast<REX::W32::ID3D11Resource*>(_buffer),
				0,
				REX::W32::D3D11_MAP_WRITE_DISCARD,
				0,
				std::addressof(mapped)) < 0) {
			return false;
		}

		std::memcpy(mapped.data, a_data, a_bytes);
		context->Unmap(reinterpret_cast<REX::W32::ID3D11Resource*>(_buffer), 0);

		return true;
	}
}
