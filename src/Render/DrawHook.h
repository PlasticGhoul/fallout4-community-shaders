#pragma once

#include <cstdint>

namespace Render
{
	class DrawObserver;

	/// Replaces the four draw entries - DrawIndexed, Draw,
	/// DrawIndexedInstanced, DrawInstanced - and ExecuteCommandList in the
	/// vtable of the immediate device context, once, for the life of the
	/// process, the way the Present hook takes IDXGISwapChain::Present. A
	/// deferred context of our own is created for its vtable alone: the
	/// runtime gives deferred contexts a class of their own, and if the
	/// engine records its frame on such contexts, that is the table its draws
	/// go through. Every draw of the game then passes a thunk that costs one
	/// atomic increment, one atomic load and one comparison when nobody is
	/// watching.
	///
	/// Installed from InstallSwapChainHook, after the context has been
	/// verified against the swap chain's device. Returns true when the
	/// immediate context's table is in place.
	[[nodiscard]] bool InstallDrawHook() noexcept;

	/// Exactly one observer. A feature sets itself from Setup and clears
	/// itself from Shutdown before it releases anything. The thunk may run on
	/// whichever thread the engine draws on; an observer keeps that in mind.
	/// A second feature that needs one turns this into a list - not before.
	void SetDrawObserver(DrawObserver* a_observer) noexcept;

	/// What the hook has seen since it went in, by call and by table. The
	/// probe reads these once a second; a count that does not move while the
	/// game renders names a path the engine does not take.
	struct DrawHookCounts
	{
		std::uint64_t indexed{ 0 };
		std::uint64_t plain{ 0 };
		std::uint64_t indexedInstanced{ 0 };
		std::uint64_t instanced{ 0 };
		std::uint64_t executeCommandList{ 0 };
		std::uint64_t onDeferredTable{ 0 };
	};

	[[nodiscard]] DrawHookCounts DrawHookCountsSoFar() noexcept;
}
