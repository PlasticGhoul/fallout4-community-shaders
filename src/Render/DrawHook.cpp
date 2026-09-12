#include "Render/DrawHook.h"

#include "Render/DrawObserver.h"
#include "Render/Renderer.h"
#include "Render/TechniqueTracker.h"
#include "Render/VTablePatch.h"

#include <REX/W32/D3D11.h>

#include <atomic>

namespace Render
{
	namespace
	{
		// ID3D11DeviceContext: IUnknown 0-2, ID3D11DeviceChild 3-6,
		// VSSetConstantBuffers 7, PSSetShaderResources 8, PSSetShader 9,
		// PSSetSamplers 10, VSSetShader 11, DrawIndexed 12.
		constexpr std::size_t kDrawIndexedSlot = 12;

		using DrawIndexedFn = void (*)(REX::W32::ID3D11DeviceContext*, std::uint32_t, std::uint32_t, std::int32_t);

		VTablePatch g_patch;
		DrawIndexedFn g_original = nullptr;
		std::atomic<DrawObserver*> g_observer{ nullptr };
		std::atomic<std::uint64_t> g_calls{ 0 };
		bool g_installed = false;

		void DrawIndexed(
			REX::W32::ID3D11DeviceContext* a_self,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex)
		{
			g_calls.fetch_add(1, std::memory_order_relaxed);

			auto* const observer = g_observer.load(std::memory_order_acquire);
			if (observer == nullptr || !observer->Wants(CurrentTechniqueOf())) {
				g_original(a_self, a_indexCount, a_startIndex, a_baseVertex);
				return;
			}

			observer->BeforeDraw(*a_self);
			g_original(a_self, a_indexCount, a_startIndex, a_baseVertex);
			observer->AfterDraw(*a_self, a_indexCount, a_startIndex, a_baseVertex);
		}
	}

	bool InstallDrawHook() noexcept
	{
		if (g_installed) {
			return true;
		}

		auto* const context = GetContext();
		if (context == nullptr) {
			REX::ERROR("draw hook: no device context");
			return false;
		}

		if (!g_patch.Install(context, kDrawIndexedSlot, reinterpret_cast<void*>(&DrawIndexed))) {
			REX::ERROR("draw hook: could not patch the device context vtable");
			return false;
		}

		g_original = reinterpret_cast<DrawIndexedFn>(g_patch.Original());
		g_installed = true;
		REX::INFO("draw hook: DrawIndexed patched, chaining to {}", g_patch.Original());
		return true;
	}

	void SetDrawObserver(DrawObserver* a_observer) noexcept
	{
		g_observer.store(a_observer, std::memory_order_release);
	}

	std::uint64_t DrawHookCalls() noexcept
	{
		return g_calls.load(std::memory_order_relaxed);
	}
}
