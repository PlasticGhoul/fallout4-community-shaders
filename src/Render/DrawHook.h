#pragma once

#include <cstdint>

namespace Render
{
	class DrawObserver;

	/// Replaces slot 12 - DrawIndexed - in the vtable of the immediate device
	/// context, once, for the life of the process, the way the Present hook
	/// takes IDXGISwapChain::Present. Every indexed draw of the game then
	/// passes through a thunk that costs one atomic load and one comparison
	/// when nobody is watching.
	///
	/// Installed from InstallSwapChainHook, after the context has been
	/// verified against the swap chain's device. Returns true when in place.
	[[nodiscard]] bool InstallDrawHook() noexcept;

	/// Exactly one observer. A feature sets itself from Setup and clears
	/// itself from Shutdown before it releases anything; both run on the
	/// render thread, as does the thunk, so nothing races. A second feature
	/// that needs one turns this into a list - not before.
	void SetDrawObserver(DrawObserver* a_observer) noexcept;

	/// Indexed draws seen since the hook went in. The probe reads it once a
	/// second: a number that does not move while the game renders means the
	/// engine draws through a context this hook does not see.
	[[nodiscard]] std::uint64_t DrawHookCalls() noexcept;
}
