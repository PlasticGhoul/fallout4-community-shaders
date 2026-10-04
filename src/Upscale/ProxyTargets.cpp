#include "Upscale/ProxyTargets.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <format>

// After doodlum's fo4test upscaler (src/Upscaling.cpp: UpdateRenderTarget,
// OverrideRenderTarget, ResetRenderTarget and the sampler functions, commit
// ce587f5, GPL-3.0), on our types and with refusals logged instead of thrown.
namespace Upscale
{
	namespace
	{
		/// The engine's 320 sampler states; doodlum's SamplerStates singleton.
		constexpr std::uint64_t kSamplerStatesId = 2704455;

		REX::W32::ID3D11SamplerState** EngineSamplers() noexcept
		{
			return reinterpret_cast<REX::W32::ID3D11SamplerState**>(REL::ID(kSamplerStatesId).address());
		}

		template <class T>
		void ReleaseInto(T*& a_object) noexcept
		{
			if (a_object != nullptr) {
				a_object->Release();
				a_object = nullptr;
			}
		}

		void CopyCorner(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11Texture2D* a_destination,
			REX::W32::ID3D11Texture2D* a_source,
			REX::W32::ID3D11Texture2D* a_sizeOf) noexcept
		{
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			a_sizeOf->GetDesc(std::addressof(desc));
			const REX::W32::D3D11_BOX box{ 0, 0, 0, desc.width, desc.height, 1 };
			a_context.CopySubresourceRegion(
				reinterpret_cast<REX::W32::ID3D11Resource*>(a_destination), 0, 0, 0, 0,
				reinterpret_cast<REX::W32::ID3D11Resource*>(a_source), 0, std::addressof(box));
		}
	}

	ProxyTargets::~ProxyTargets()
	{
		ReleaseProxies();
		ReleaseSamplers();
	}

	bool ProxyTargets::Update(float a_ratioX, float a_ratioY) noexcept
	{
		if (a_ratioX == _ratioX && a_ratioY == _ratioY) {
			return true;
		}
		ReleaseProxies();
		_ratioX = a_ratioX;
		_ratioY = a_ratioY;
		if (a_ratioX == 1.0f && a_ratioY == 1.0f) {
			return true;
		}

		auto* const data = RE::BSGraphics::GetRendererData();
		auto* const device = Render::GetDevice();
		if (data == nullptr || device == nullptr) {
			return false;
		}
		for (const auto slot : kSlots) {
			const auto& original = data->renderTargets[slot];
			auto& proxy = _proxy[slot];
			if (original.texture == nullptr) {
				continue;
			}
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			original.texture->GetDesc(std::addressof(desc));
			desc.width = static_cast<std::uint32_t>(static_cast<float>(desc.width) * a_ratioX);
			desc.height = static_cast<std::uint32_t>(static_cast<float>(desc.height) * a_ratioY);
			if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(proxy.texture)) < 0) {
				REX::ERROR("DLSS: the stand-in for RT_{:03} could not be created", slot);
				ReleaseProxies();
				return false;
			}
			// No render target view: doodlum's call passes the view pointer's
			// value instead of its address, so his stand-ins never had one -
			// and that is the arrangement that was played.
			if (original.srView != nullptr) {
				REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC view{};
				original.srView->GetDesc(std::addressof(view));
				if (device->CreateShaderResourceView(proxy.texture, std::addressof(view), std::addressof(proxy.srView)) < 0) {
					REX::ERROR("DLSS: the stand-in view for RT_{:03} could not be created", slot);
					ReleaseProxies();
					return false;
				}
			}
			if (original.uaView != nullptr) {
				REX::W32::D3D11_UNORDERED_ACCESS_VIEW_DESC view{};
				original.uaView->GetDesc(std::addressof(view));
				if (device->CreateUnorderedAccessView(proxy.texture, std::addressof(view), std::addressof(proxy.uaView)) < 0) {
					REX::ERROR("DLSS: the stand-in UAV for RT_{:03} could not be created", slot);
					ReleaseProxies();
					return false;
				}
			}
			static_cast<void>(Render::SetDebugName(
				reinterpret_cast<REX::W32::ID3D11DeviceChild*>(proxy.texture), std::format("FO4CS_PROXY_RT_{:03}", slot)));
		}
		REX::INFO("DLSS: stand-ins at {:.3f} x {:.3f}", a_ratioX, a_ratioY);
		return true;
	}

	void ProxyTargets::Swap(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (_swapped || data == nullptr || manager == nullptr) {
			return;
		}
		for (const auto slot : kSlots) {
			if (_proxy[slot].texture == nullptr || data->renderTargets[slot].texture == nullptr) {
				continue;
			}
			_original[slot] = data->renderTargets[slot];
			CopyCorner(a_context, _proxy[slot].texture, _original[slot].texture, _proxy[slot].texture);
			data->renderTargets[slot] = _proxy[slot];
		}
		// What code that asks the manager for a target's size gets told.
		for (std::size_t i = 0; i < _originalWidth.size(); ++i) {
			auto& properties = manager->renderTargetData[i];
			_originalWidth[i] = properties.width;
			_originalHeight[i] = properties.height;
			properties.width = static_cast<std::uint32_t>(static_cast<float>(properties.width) * _ratioX);
			properties.height = static_cast<std::uint32_t>(static_cast<float>(properties.height) * _ratioY);
		}
		_swapped = true;
	}

	void ProxyTargets::Restore(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		auto* const data = RE::BSGraphics::GetRendererData();
		auto* const manager = RE::BSGraphics::RenderTargetManager::GetSingleton();
		if (!_swapped || data == nullptr || manager == nullptr) {
			return;
		}
		for (const auto slot : kSlots) {
			if (_proxy[slot].texture == nullptr || _original[slot].texture == nullptr) {
				continue;
			}
			data->renderTargets[slot] = _original[slot];
			CopyCorner(a_context, _original[slot].texture, _proxy[slot].texture, _proxy[slot].texture);
		}
		for (std::size_t i = 0; i < _originalWidth.size(); ++i) {
			manager->renderTargetData[i].width = _originalWidth[i];
			manager->renderTargetData[i].height = _originalHeight[i];
		}
		_swapped = false;
	}

	bool ProxyTargets::UpdateSamplers(float a_mipBias) noexcept
	{
		auto* const engine = EngineSamplers();
		auto* const device = Render::GetDevice();
		if (engine == nullptr || device == nullptr) {
			return false;
		}
		// The engine's own, taken afresh each frame: what BiasSamplers swaps
		// out and RestoreSamplers puts back.
		for (std::size_t i = 0; i < kSamplerStates; ++i) {
			_engineSamplers[i] = engine[i];
		}
		if (_haveBias && a_mipBias == _mipBias) {
			return true;
		}
		ReleaseSamplers();
		_mipBias = a_mipBias;
		_haveBias = true;
		for (std::size_t i = 0; i < kSamplerStates; ++i) {
			if (_engineSamplers[i] == nullptr) {
				continue;
			}
			REX::W32::D3D11_SAMPLER_DESC desc{};
			_engineSamplers[i]->GetDesc(std::addressof(desc));
			// Only the anisotropic ones, the material samplers; 8x rather
			// than 16x, as doodlum found it pays for itself at render size.
			if (desc.filter == REX::W32::D3D11_FILTER_ANISOTROPIC) {
				desc.maxAnisotropy = 8;
				desc.mipLODBias = a_mipBias;
			}
			if (device->CreateSamplerState(std::addressof(desc), std::addressof(_biasedSamplers[i])) < 0) {
				REX::ERROR("DLSS: biased sampler {} could not be created", i);
				ReleaseSamplers();
				return false;
			}
		}
		return true;
	}

	void ProxyTargets::BiasSamplers() noexcept
	{
		auto* const engine = EngineSamplers();
		if (engine == nullptr || !_haveBias || _biased) {
			return;
		}
		for (std::size_t i = 0; i < kSamplerStates; ++i) {
			if (_biasedSamplers[i] != nullptr) {
				engine[i] = _biasedSamplers[i];
			}
		}
		_biased = true;
	}

	void ProxyTargets::RestoreSamplers() noexcept
	{
		auto* const engine = EngineSamplers();
		if (engine == nullptr || !_biased) {
			return;
		}
		for (std::size_t i = 0; i < kSamplerStates; ++i) {
			engine[i] = _engineSamplers[i];
		}
		_biased = false;
	}

	void ProxyTargets::ReleaseProxies() noexcept
	{
		for (auto& proxy : _proxy) {
			ReleaseInto(proxy.uaView);
			ReleaseInto(proxy.srView);
			ReleaseInto(proxy.rtView);
			ReleaseInto(proxy.texture);
		}
		_ratioX = 1.0f;
		_ratioY = 1.0f;
	}

	void ProxyTargets::ReleaseSamplers() noexcept
	{
		for (auto*& sampler : _biasedSamplers) {
			ReleaseInto(sampler);
		}
		_haveBias = false;
	}

	void ProxyTargets::Release(REX::W32::ID3D11DeviceContext* a_context) noexcept
	{
		if (_swapped && a_context != nullptr) {
			Restore(*a_context);
		}
		RestoreSamplers();
		ReleaseProxies();
		ReleaseSamplers();
	}
}
