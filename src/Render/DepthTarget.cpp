#include "Render/DepthTarget.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <REX/W32/DXGI.h>

#include <format>

namespace Render
{
	bool DepthTarget::Create(std::uint32_t a_size, std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr || a_size == 0) {
			REX::ERROR("depth target {}: no device or a zero size", a_debugName);
			return false;
		}

		// Typeless, so that the same texture is D32_FLOAT to the depth view
		// and R32_FLOAT to the shader - the engine's own R24G8 targets are
		// built the same way.
		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = a_size;
		desc.height = a_size;
		desc.mipLevels = 1;
		desc.arraySize = 1;
		desc.format = REX::W32::DXGI_FORMAT_R32_TYPELESS;
		desc.sampleDesc.count = 1;
		desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		desc.bindFlags = REX::W32::D3D11_BIND_DEPTH_STENCIL | REX::W32::D3D11_BIND_SHADER_RESOURCE;
		if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_texture)) < 0) {
			REX::ERROR("depth target {}: the texture could not be created", a_debugName);
			return false;
		}
		static_cast<void>(SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_texture), a_debugName));

		REX::W32::D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
		dsv.format = REX::W32::DXGI_FORMAT_D32_FLOAT;
		dsv.viewDimension = REX::W32::D3D11_DSV_DIMENSION_TEXTURE2D;
		dsv.texture2D.mipSlice = 0;
		if (device->CreateDepthStencilView(_texture, std::addressof(dsv), std::addressof(_dsv)) < 0) {
			REX::ERROR("depth target {}: no depth stencil view", a_debugName);
			Release();
			return false;
		}
		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_dsv), std::format("{}_dsv", a_debugName)));

		REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.format = REX::W32::DXGI_FORMAT_R32_FLOAT;
		srv.viewDimension = REX::W32::D3D_SRV_DIMENSION_TEXTURE2D;
		srv.texture2D.mostDetailedMip = 0;
		srv.texture2D.mipLevels = 1;
		if (device->CreateShaderResourceView(_texture, std::addressof(srv), std::addressof(_srv)) < 0) {
			REX::ERROR("depth target {}: no shader resource view", a_debugName);
			Release();
			return false;
		}

		_size = a_size;
		return true;
	}

	void DepthTarget::Release() noexcept
	{
		if (_srv != nullptr) {
			_srv->Release();
			_srv = nullptr;
		}
		if (_dsv != nullptr) {
			_dsv->Release();
			_dsv = nullptr;
		}
		if (_texture != nullptr) {
			_texture->Release();
			_texture = nullptr;
		}
		_size = 0;
	}

	void DepthTarget::Clear(REX::W32::ID3D11DeviceContext& a_context, float a_depth) noexcept
	{
		if (_dsv != nullptr) {
			a_context.ClearDepthStencilView(_dsv, REX::W32::D3D11_CLEAR_DEPTH, a_depth, 0);
		}
	}
}
