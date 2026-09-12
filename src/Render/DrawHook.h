#pragma once

#include <cstdint>

namespace Render
{
	class DrawObserver;

	/// Puts a ContextTable on the immediate device context with the four draw
	/// entries - DrawIndexed, Draw, DrawIndexedInstanced, DrawInstanced - and
	/// ExecuteCommandList overridden. Every draw of the game then passes a
	/// thunk that costs one atomic increment, one atomic load and one
	/// comparison when nobody is watching, and chains to whatever variant the
	/// runtime currently holds in its own table.
	///
	/// Not a slot patch: the runtime rewrites the entries of the table it keeps
	/// inside the object at every Flush, which is how the first F4 probe came
	/// to count nothing. See ContextTable.
	///
	/// Installed from InstallSwapChainHook, after the context has been
	/// verified against the swap chain's device. Returns true when in place.
	[[nodiscard]] bool InstallDrawHook() noexcept;

	/// Exactly one observer. A feature sets itself from Setup and clears
	/// itself from Shutdown before it releases anything. The thunk may run on
	/// whichever thread the engine draws on; an observer keeps that in mind.
	/// A second feature that needs one turns this into a list - not before.
	void SetDrawObserver(DrawObserver* a_observer) noexcept;

	/// What the hook has seen since it went in, by call. The probe reads these
	/// once a second; a count that does not move while the game renders names
	/// a path the engine does not take.
	struct DrawHookCounts
	{
		std::uint64_t indexed{ 0 };
		std::uint64_t plain{ 0 };
		std::uint64_t indexedInstanced{ 0 };
		std::uint64_t instanced{ 0 };
		std::uint64_t executeCommandList{ 0 };
	};

	[[nodiscard]] DrawHookCounts DrawHookCountsSoFar() noexcept;
}
