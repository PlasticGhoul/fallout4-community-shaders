#include "Render/Resolution.h"

#include "Render/Targets.h"

#include <atomic>

namespace Render::Resolution
{
	namespace
	{
		// Written and read on the render thread; the flags are atomic so that
		// the overlay, which may read Current() for display, sees a whole
		// state and never half of one.
		std::atomic<bool> g_published{ false };
		std::atomic<bool> g_depthFull{ false };
		Extent g_display{};
		Extent g_render{};
	}

	Frame Current() noexcept
	{
		if (g_published.load(std::memory_order_acquire)) {
			return Frame{ g_display, g_render, g_depthFull.load(std::memory_order_acquire) };
		}
		Extent size{};
		if (auto* const texture = Targets::RenderTargetTexture(Targets::kSceneHDR); texture != nullptr) {
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(std::addressof(desc));
			size = Extent{ desc.width, desc.height };
		}
		return Frame{ size, size, false };
	}

	void Publish(Extent a_display, Extent a_render) noexcept
	{
		g_display = a_display;
		g_render = a_render;
		g_published.store(true, std::memory_order_release);
	}

	void SetDepthFull(bool a_full) noexcept
	{
		g_depthFull.store(a_full, std::memory_order_release);
	}

	void Withdraw() noexcept
	{
		g_published.store(false, std::memory_order_release);
		g_depthFull.store(false, std::memory_order_release);
	}
}
