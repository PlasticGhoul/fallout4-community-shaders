#include "Render/DrawHook.h"

#include "Render/DrawObserver.h"
#include "Render/Renderer.h"
#include "Render/TechniqueTracker.h"
#include "Render/VTablePatch.h"

#include <REX/W32/D3D11.h>

#include <array>
#include <atomic>

namespace Render
{
	namespace
	{
		// ID3D11DeviceContext: IUnknown 0-2, ID3D11DeviceChild 3-6, then
		// VSSetConstantBuffers 7 ... DrawIndexed 12, Draw 13, ...,
		// DrawIndexedInstanced 20, DrawInstanced 21, ..., ExecuteCommandList
		// 58, ..., GetType 112. Counted off the REX declaration.
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

		/// One table's worth of patches and remembered originals. Two of
		/// them: the immediate context's class and the deferred one's.
		struct Table
		{
			VTablePatch drawIndexed;
			VTablePatch draw;
			VTablePatch drawIndexedInstanced;
			VTablePatch drawInstanced;
			VTablePatch executeCommandList;
			DrawIndexedFn originalDrawIndexed{ nullptr };
			DrawFn originalDraw{ nullptr };
			DrawIndexedInstancedFn originalDrawIndexedInstanced{ nullptr };
			DrawInstancedFn originalDrawInstanced{ nullptr };
			ExecuteCommandListFn originalExecuteCommandList{ nullptr };
			void** vtable{ nullptr };
			bool installed{ false };
		};

		constexpr std::size_t kImmediate = 0;
		constexpr std::size_t kDeferred = 1;
		std::array<Table, 2> g_tables{};

		std::atomic<DrawObserver*> g_observer{ nullptr };
		std::atomic<std::uint64_t> g_indexed{ 0 };
		std::atomic<std::uint64_t> g_plain{ 0 };
		std::atomic<std::uint64_t> g_indexedInstanced{ 0 };
		std::atomic<std::uint64_t> g_instanced{ 0 };
		std::atomic<std::uint64_t> g_executeCommandList{ 0 };
		std::atomic<std::uint64_t> g_onDeferredTable{ 0 };

		/// Which of the two tables a_self's class uses. The vtable pointer is
		/// the first word of any COM object.
		std::size_t TableOf(Context* a_self) noexcept
		{
			return *reinterpret_cast<void***>(a_self) == g_tables[kDeferred].vtable ? kDeferred : kImmediate;
		}

		template <std::size_t T>
		void Surround(Context* a_self, const DrawCall& a_call, auto a_issue) noexcept
		{
			if constexpr (T == kDeferred) {
				g_onDeferredTable.fetch_add(1, std::memory_order_relaxed);
			}

			auto* const observer = g_observer.load(std::memory_order_acquire);
			if (observer == nullptr || !observer->Wants(CurrentTechniqueOf())) {
				a_issue();
				return;
			}

			observer->BeforeDraw(*a_self, a_call);
			a_issue();
			observer->AfterDraw(*a_self, a_call);
		}

		template <std::size_t T>
		void DrawIndexed(Context* a_self, std::uint32_t a_count, std::uint32_t a_start, std::int32_t a_base)
		{
			g_indexed.fetch_add(1, std::memory_order_relaxed);
			DrawCall call{ DrawCall::Kind::kIndexed, a_count, 1, a_start, a_base, 0 };
			Surround<T>(a_self, call, [&] { g_tables[T].originalDrawIndexed(a_self, a_count, a_start, a_base); });
		}

		template <std::size_t T>
		void Draw(Context* a_self, std::uint32_t a_count, std::uint32_t a_start)
		{
			g_plain.fetch_add(1, std::memory_order_relaxed);
			DrawCall call{ DrawCall::Kind::kPlain, a_count, 1, a_start, 0, 0 };
			Surround<T>(a_self, call, [&] { g_tables[T].originalDraw(a_self, a_count, a_start); });
		}

		template <std::size_t T>
		void DrawIndexedInstanced(
			Context* a_self, std::uint32_t a_count, std::uint32_t a_instances, std::uint32_t a_start, std::int32_t a_base, std::uint32_t a_startInstance)
		{
			g_indexedInstanced.fetch_add(1, std::memory_order_relaxed);
			DrawCall call{ DrawCall::Kind::kIndexedInstanced, a_count, a_instances, a_start, a_base, a_startInstance };
			Surround<T>(a_self, call, [&] {
				g_tables[T].originalDrawIndexedInstanced(a_self, a_count, a_instances, a_start, a_base, a_startInstance);
			});
		}

		template <std::size_t T>
		void DrawInstanced(Context* a_self, std::uint32_t a_count, std::uint32_t a_instances, std::uint32_t a_start, std::uint32_t a_startInstance)
		{
			g_instanced.fetch_add(1, std::memory_order_relaxed);
			DrawCall call{ DrawCall::Kind::kInstanced, a_count, a_instances, a_start, 0, a_startInstance };
			Surround<T>(a_self, call, [&] { g_tables[T].originalDrawInstanced(a_self, a_count, a_instances, a_start, a_startInstance); });
		}

		template <std::size_t T>
		void ExecuteCommandList(Context* a_self, REX::W32::ID3D11CommandList* a_list, REX::W32::BOOL a_restore)
		{
			g_executeCommandList.fetch_add(1, std::memory_order_relaxed);
			g_tables[T].originalExecuteCommandList(a_self, a_list, a_restore);
		}

		template <std::size_t T>
		bool InstallTable(Context* a_context, const char* a_what) noexcept
		{
			auto& table = g_tables[T];
			if (table.installed) {
				return true;
			}
			if (a_context == nullptr) {
				return false;
			}

			table.vtable = *reinterpret_cast<void***>(a_context);

			const bool ok =
				table.drawIndexed.Install(a_context, kDrawIndexedSlot, reinterpret_cast<void*>(&DrawIndexed<T>)) &&
				table.draw.Install(a_context, kDrawSlot, reinterpret_cast<void*>(&Draw<T>)) &&
				table.drawIndexedInstanced.Install(a_context, kDrawIndexedInstancedSlot, reinterpret_cast<void*>(&DrawIndexedInstanced<T>)) &&
				table.drawInstanced.Install(a_context, kDrawInstancedSlot, reinterpret_cast<void*>(&DrawInstanced<T>)) &&
				table.executeCommandList.Install(a_context, kExecuteCommandListSlot, reinterpret_cast<void*>(&ExecuteCommandList<T>));

			if (!ok) {
				REX::ERROR("draw hook: could not patch the {} context's vtable", a_what);
				return false;
			}

			table.originalDrawIndexed = reinterpret_cast<DrawIndexedFn>(table.drawIndexed.Original());
			table.originalDraw = reinterpret_cast<DrawFn>(table.draw.Original());
			table.originalDrawIndexedInstanced = reinterpret_cast<DrawIndexedInstancedFn>(table.drawIndexedInstanced.Original());
			table.originalDrawInstanced = reinterpret_cast<DrawInstancedFn>(table.drawInstanced.Original());
			table.originalExecuteCommandList = reinterpret_cast<ExecuteCommandListFn>(table.executeCommandList.Original());
			table.installed = true;

			REX::INFO(
				"draw hook: {} context vtable {} patched, type {}",
				a_what,
				static_cast<void*>(table.vtable),
				static_cast<int>(a_context->GetType()));
			return true;
		}
	}

	void DrawCall::Repeat(REX::W32::ID3D11DeviceContext& a_context) const noexcept
	{
		const auto& table = g_tables[TableOf(std::addressof(a_context))];
		if (!table.installed) {
			return;
		}
		switch (kind) {
		case Kind::kIndexed:
			table.originalDrawIndexed(std::addressof(a_context), count, start, baseVertex);
			break;
		case Kind::kPlain:
			table.originalDraw(std::addressof(a_context), count, start);
			break;
		case Kind::kIndexedInstanced:
			table.originalDrawIndexedInstanced(std::addressof(a_context), count, instanceCount, start, baseVertex, startInstance);
			break;
		case Kind::kInstanced:
			table.originalDrawInstanced(std::addressof(a_context), count, instanceCount, start, startInstance);
			break;
		}
	}

	bool InstallDrawHook() noexcept
	{
		auto* const immediate = GetContext();
		if (immediate == nullptr) {
			REX::ERROR("draw hook: no device context");
			return false;
		}

		if (!InstallTable<kImmediate>(immediate, "immediate")) {
			return false;
		}

		// A deferred context of our own, made and dropped for its vtable.
		// Same class as any deferred context the engine records on, so the
		// patch reaches those too - or the same class as the immediate one,
		// in which case there is nothing more to do.
		auto* const device = GetDevice();
		Context* deferred = nullptr;
		if (device != nullptr && device->CreateDeferredContext(0, std::addressof(deferred)) >= 0 && deferred != nullptr) {
			if (*reinterpret_cast<void***>(deferred) == g_tables[kImmediate].vtable) {
				REX::INFO("draw hook: deferred contexts share the immediate context's vtable");
			} else {
				static_cast<void>(InstallTable<kDeferred>(deferred, "deferred"));
			}
			deferred->Release();
		} else {
			REX::WARN("draw hook: no deferred context could be created, its vtable stays unpatched");
		}

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
		counts.onDeferredTable = g_onDeferredTable.load(std::memory_order_relaxed);
		return counts;
	}
}
