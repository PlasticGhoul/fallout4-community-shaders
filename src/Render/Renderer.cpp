#include "Render/Renderer.h"

#include <RE/B/BSGraphics.h>

namespace Render
{
	Device* GetDevice() noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		return data != nullptr ? data->device : nullptr;
	}

	Context* GetContext() noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		return data != nullptr ? data->context : nullptr;
	}

	SwapChain* GetSwapChain() noexcept
	{
		const auto* const window = RE::BSGraphics::GetCurrentRendererWindow();
		return window != nullptr ? window->swapChain : nullptr;
	}

	bool ValidateAndLog() noexcept
	{
		auto* const device = GetDevice();
		auto* const context = GetContext();
		auto* const swapChain = GetSwapChain();

		REX::INFO("renderer: device {}, context {}, swapchain {}",
			static_cast<const void*>(device),
			static_cast<const void*>(context),
			static_cast<const void*>(swapChain));

		if (device == nullptr || context == nullptr || swapChain == nullptr) {
			REX::ERROR("renderer is not ready, refusing to install");
			return false;
		}

		// Both GetDevice calls hand back a reference we own and must drop.
		Device* fromContext = nullptr;
		context->GetDevice(&fromContext);

		Device* fromSwapChain = nullptr;
		swapChain->GetDevice(REX::W32::IID_ID3D11Device, reinterpret_cast<void**>(&fromSwapChain));

		const bool coherent = fromContext == device && fromSwapChain == device;

		if (!coherent) {
			REX::ERROR(
				"device mismatch: engine {}, via context {}, via swapchain {}. "
				"Address resolution is wrong for this runtime; refusing to install.",
				static_cast<const void*>(device),
				static_cast<const void*>(fromContext),
				static_cast<const void*>(fromSwapChain));
		}

		if (fromContext != nullptr) {
			fromContext->Release();
		}
		if (fromSwapChain != nullptr) {
			fromSwapChain->Release();
		}

		if (!coherent) {
			return false;
		}

		REX::INFO("device cross-check passed, feature level {:#x}",
			static_cast<std::uint32_t>(device->GetFeatureLevel()));

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) >= 0) {
			REX::INFO("swapchain {}x{}, format {}, buffers {}",
				desc.bufferDesc.width,
				desc.bufferDesc.height,
				static_cast<std::uint32_t>(desc.bufferDesc.format),
				desc.bufferCount);
		} else {
			REX::WARN("could not read the swapchain description");
		}

		return true;
	}
}
