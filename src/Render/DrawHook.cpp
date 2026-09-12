#include "Render/DrawHook.h"

#include "Render/ContextTable.h"
#include "Render/DrawObserver.h"
#include "Render/Renderer.h"
#include "Render/TechniqueTracker.h"

#include <REX/W32/D3D11.h>

#include <atomic>

namespace Render
{
	namespace
	{
		// ID3D11DeviceContext: IUnknown 0-2, ID3D11DeviceChild 3-6, then
		// VSSetConstantBuffers 7 ... DrawIndexed 12, Draw 13, ...,
		// DrawIndexedInstanced 20, DrawInstanced 21, ..., ExecuteCommandList
		// 58. Counted off the REX declaration and confirmed offline: GetType
		// through slot 112 answers, and slot 13 intercepts Draw.
		constexpr std::size_t kDrawIndexedSlot = 12;
		constexpr std::size_t kDrawSlot = 13;
		constexpr std::size_t kDrawIndexedInstancedSlot = 20;
		constexpr std::size_t kDrawInstancedSlot = 21;
		constexpr std::size_t kExecuteCommandListSlot = 58;

		using Context = REX::W32::ID3D11DeviceContext;
		using DrawIndexedFn = void (*)(Context*, std::uint32_t, std::uint32_t, std::int32_t);
		using DrawFn = void (*)(Context*, std::uint32_t, std::uint32_t);
		using DrawIndexedInstancedFn = void (*)(Context*, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t);
		using DrawInstancedFn = void (*)(Context*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
		using ExecuteCommandListFn = void (*)(Context*, REX::W32::ID3D11CommandList*, REX::W32::BOOL);

		ContextTable g_table;
		std::atomic<DrawObserver*> g_observer{ nullptr };
		std::atomic<std::uint64_t> g_indexed{ 0 };
		std::atomic<std::uint64_t> g_plain{ 0 };
		std::atomic<std::uint64_t> g_indexedInstanced{ 0 };
		std::atomic<std::uint64_t> g_instanced{ 0 };
		std::atomic<std::uint64_t> g_executeCommandList{ 0 };

		/// The runtime's current entry for a slot: read at call time, because
		/// the runtime swaps variants in and out underneath us.
		template <class Fn>
		Fn Embedded(Context* a_self, std::size_t a_slot) noexcept
		{
			return reinterpret_cast<Fn>(ContextTable::EmbeddedTableOf(a_self)[a_slot]);
		}

		void Surround(Context* a_self, const DrawCall& a_call, auto a_issue) noexcept
		{
			auto* const observer = g_observer.load(std::memory_order_acquire);
			if (observer == nullptr || !observer->Wants(CurrentTechniqueOf())) {
				a_issue();
				return;
			}

			observer->BeforeDraw(*a_self, a_call);
			a_issue();
			observer->AfterDraw(*a_self, a_call);
		}

		void DrawIndexed(Context* a_self, std::uint32_t a_count, std::uint32_t a_start, std::int32_t a_base)
		{
			g_indexed.fetch_add(1, std::memory_order_relaxed);
			const DrawCall call{ DrawCall::Kind::kIndexed, a_count, 1, a_start, a_base, 0 };
			Surround(a_self, call, [&] { Embedded<DrawIndexedFn>(a_self, kDrawIndexedSlot)(a_self, a_count, a_start, a_base); });
		}

		void Draw(Context* a_self, std::uint32_t a_count, std::uint32_t a_start)
		{
			g_plain.fetch_add(1, std::memory_order_relaxed);
			const DrawCall call{ DrawCall::Kind::kPlain, a_count, 1, a_start, 0, 0 };
			Surround(a_self, call, [&] { Embedded<DrawFn>(a_self, kDrawSlot)(a_self, a_count, a_start); });
		}

		void DrawIndexedInstanced(
			Context* a_self, std::uint32_t a_count, std::uint32_t a_instances, std::uint32_t a_start, std::int32_t a_base, std::uint32_t a_startInstance)
		{
			g_indexedInstanced.fetch_add(1, std::memory_order_relaxed);
			const DrawCall call{ DrawCall::Kind::kIndexedInstanced, a_count, a_instances, a_start, a_base, a_startInstance };
			Surround(a_self, call, [&] {
				Embedded<DrawIndexedInstancedFn>(a_self, kDrawIndexedInstancedSlot)(a_self, a_count, a_instances, a_start, a_base, a_startInstance);
			});
		}

		void DrawInstanced(Context* a_self, std::uint32_t a_count, std::uint32_t a_instances, std::uint32_t a_start, std::uint32_t a_startInstance)
		{
			g_instanced.fetch_add(1, std::memory_order_relaxed);
			const DrawCall call{ DrawCall::Kind::kInstanced, a_count, a_instances, a_start, 0, a_startInstance };
			Surround(a_self, call, [&] {
				Embedded<DrawInstancedFn>(a_self, kDrawInstancedSlot)(a_self, a_count, a_instances, a_start, a_startInstance);
			});
		}

		void ExecuteCommandList(Context* a_self, REX::W32::ID3D11CommandList* a_list, REX::W32::BOOL a_restore)
		{
			g_executeCommandList.fetch_add(1, std::memory_order_relaxed);
			Embedded<ExecuteCommandListFn>(a_self, kExecuteCommandListSlot)(a_self, a_list, a_restore);
		}
	}

	void DrawCall::Repeat(REX::W32::ID3D11DeviceContext& a_context) const noexcept
	{
		auto* const self = std::addressof(a_context);
		switch (kind) {
		case Kind::kIndexed:
			Embedded<DrawIndexedFn>(self, kDrawIndexedSlot)(self, count, start, baseVertex);
			break;
		case Kind::kPlain:
			Embedded<DrawFn>(self, kDrawSlot)(self, count, start);
			break;
		case Kind::kIndexedInstanced:
			Embedded<DrawIndexedInstancedFn>(self, kDrawIndexedInstancedSlot)(self, count, instanceCount, start, baseVertex, startInstance);
			break;
		case Kind::kInstanced:
			Embedded<DrawInstancedFn>(self, kDrawInstancedSlot)(self, count, instanceCount, start, startInstance);
			break;
		}
	}

	bool InstallDrawHook() noexcept
	{
		if (g_table.Installed()) {
			return true;
		}

		auto* const context = GetContext();
		if (context == nullptr) {
			REX::ERROR("draw hook: no device context");
			return false;
		}

		if (!ContextTable::HasEmbeddedTable(context)) {
			REX::ERROR(
				"draw hook: the context's vtable is not embedded at offset eight (points to {}), leaving it alone",
				static_cast<void*>(*reinterpret_cast<void***>(context)));
			return false;
		}

		const ContextTable::Override overrides[] = {
			{ kDrawIndexedSlot, reinterpret_cast<void*>(&DrawIndexed) },
			{ kDrawSlot, reinterpret_cast<void*>(&Draw) },
			{ kDrawIndexedInstancedSlot, reinterpret_cast<void*>(&DrawIndexedInstanced) },
			{ kDrawInstancedSlot, reinterpret_cast<void*>(&DrawInstanced) },
			{ kExecuteCommandListSlot, reinterpret_cast<void*>(&ExecuteCommandList) },
		};
		if (!g_table.Install(context, overrides)) {
			REX::ERROR("draw hook: the context table could not be installed");
			return false;
		}

		REX::INFO(
			"draw hook: context {} now dispatches through our table {}, type {}",
			static_cast<void*>(context),
			static_cast<void*>(g_table.Table()),
			static_cast<int>(context->GetType()));
		return true;
	}

	void SetDrawObserver(DrawObserver* a_observer) noexcept
	{
		g_observer.store(a_observer, std::memory_order_release);
	}

	DrawHookCounts DrawHookCountsSoFar() noexcept
	{
		DrawHookCounts counts;
		counts.indexed = g_indexed.load(std::memory_order_relaxed);
		counts.plain = g_plain.load(std::memory_order_relaxed);
		counts.indexedInstanced = g_indexedInstanced.load(std::memory_order_relaxed);
		counts.instanced = g_instanced.load(std::memory_order_relaxed);
		counts.executeCommandList = g_executeCommandList.load(std::memory_order_relaxed);
		return counts;
	}
}
