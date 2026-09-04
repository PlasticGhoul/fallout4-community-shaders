#include "Render/SwapChainHook.h"

#include "Feature/FeatureSystem.h"
#include "Menu/MenuSystem.h"
#include "Render/Markers.h"
#include "Render/Renderer.h"
#include "Render/TargetInventory.h"
#include "Render/VTablePatch.h"

#include <atomic>
#include <format>

namespace Render
{
	namespace
	{
		// IUnknown occupies 0-2, IDXGIObject 3-6, IDXGIDeviceSubObject 7, so
		// IDXGISwapChain::Present is the eighth entry.
		constexpr std::size_t kPresentSlot = 8;

		// Every 600 frames is roughly every ten seconds at 60 fps: enough to
		// show the counter is alive without filling the log.
		constexpr std::uint64_t kLogInterval = 600;

		using Present_t = REX::W32::HRESULT (*)(SwapChain*, std::uint32_t, std::uint32_t);

		VTablePatch g_patch;
		Present_t g_originalPresent = nullptr;
		std::atomic<std::uint64_t> g_frames{ 0 };
		bool g_installed = false;

		REX::W32::HRESULT Present(SwapChain* a_swapChain, std::uint32_t a_syncInterval, std::uint32_t a_flags)
		{
			const auto frame = g_frames.fetch_add(1, std::memory_order_relaxed) + 1;

			if (frame % kLogInterval == 0) {
				REX::DEBUG("frame {}", frame);
			}

			// Ours runs inside the named block, so a capture shows it under
			// the marker rather than loose between frames.
			Features::TickSystem();

			// After the features: the overlay belongs on top of whatever they
			// drew.
			Menu::TickSystem();

			const auto name = std::format(L"CommunityShadersFO4 Frame {}", frame);

			// The scope wraps the chained call, so a capture shows a named block
			// per frame rather than a bare event.
			const MarkerScope scope{ name.c_str() };

			// Deliberately the remembered pointer, not IDXGISwapChain::Present.
			// If ENB, an overlay or an upscaler sits below us, calling through
			// DXGI directly would skip it.
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}
	}

	void InstallSwapChainHook() noexcept
	{
		if (g_installed) {
			return;
		}

		if (!ValidateAndLog()) {
			return;
		}

		auto* const swapChain = GetSwapChain();

		if (!g_patch.Install(swapChain, kPresentSlot, reinterpret_cast<void*>(&Present))) {
			REX::ERROR("could not patch the swapchain vtable, leaving Present alone");
			return;
		}

		// Read back from the patch rather than walking the vtable a second time:
		// that arithmetic lives in VTablePatch precisely so it exists once.
		g_originalPresent = reinterpret_cast<Present_t>(g_patch.Original());
		g_installed = true;

		REX::INFO("Present hooked, chaining to {}", g_patch.Original());

		static_cast<void>(InitMarkers());

		// Runs only once the cross-check above has passed. Without verified
		// renderer access every number the inventory reads would be worthless.
		RunTargetInventory();
	}

	std::uint64_t FrameCount() noexcept
	{
		return g_frames.load(std::memory_order_relaxed);
	}
}
