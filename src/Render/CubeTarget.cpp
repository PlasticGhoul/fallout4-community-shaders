#include "Render/CubeTarget.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <format>

namespace Render
{
	bool CubeTarget::Create(std::uint32_t a_size, REX::W32::DXGI_FORMAT a_format, std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr || a_size == 0) {
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = a_size;
		desc.height = a_size;
		desc.mipLevels = 1;
		desc.arraySize = kFaces;
		desc.format = a_format;
		desc.sampleDesc.count = 1;
		desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		desc.bindFlags = REX::W32::D3D11_BIND_RENDER_TARGET | REX::W32::D3D11_BIND_SHADER_RESOURCE;
		desc.miscFlags = REX::W32::D3D11_RESOURCE_MISC_TEXTURECUBE;

		if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_texture)) < 0) {
			REX::ERROR("cube target {}: the texture could not be created", a_debugName);
			return false;
		}
		static_cast<void>(SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_texture), a_debugName));

		for (std::uint32_t face = 0; face < kFaces; ++face) {
			REX::W32::D3D11_RENDER_TARGET_VIEW_DESC rtv{};
			rtv.format = a_format;
			rtv.viewDimension = REX::W32::D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtv.texture2DArray.mipSlice = 0;
			rtv.texture2DArray.firstArraySlice = face;
			rtv.texture2DArray.arraySize = 1;
			if (device->CreateRenderTargetView(_texture, std::addressof(rtv), std::addressof(_faces[face])) < 0) {
				REX::ERROR("cube target {}: face {} has no render target view", a_debugName, face);
				Release();
				return false;
			}
			static_cast<void>(SetDebugName(
				reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_faces[face]),
				std::format("{}_face{}", a_debugName, face)));
		}

		REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.format = a_format;
		srv.viewDimension = REX::W32::D3D11_SRV_DIMENSION_TEXTURECUBE;
		srv.textureCube.mostDetailedMip = 0;
		srv.textureCube.mipLevels = 1;
		if (device->CreateShaderResourceView(_texture, std::addressof(srv), std::addressof(_srv)) < 0) {
			REX::ERROR("cube target {}: no shader resource view", a_debugName);
			Release();
			return false;
		}

		_size = a_size;
		return true;
	}

	void CubeTarget::Release() noexcept
	{
		if (_srv != nullptr) {
			_srv->Release();
			_srv = nullptr;
		}
		for (auto*& face : _faces) {
			if (face != nullptr) {
				face->Release();
				face = nullptr;
			}
		}
		if (_texture != nullptr) {
			_texture->Release();
			_texture = nullptr;
		}
		_size = 0;
	}

	void CubeTarget::ClearFace(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_face, float a_alpha) noexcept
	{
		if (a_face < kFaces && _faces[a_face] != nullptr) {
			const float colour[4]{ 0.0f, 0.0f, 0.0f, a_alpha };
			a_context.ClearRenderTargetView(_faces[a_face], colour);
		}
	}
}
