#pragma once

namespace Bridge
{
	/// Detours a function at its prolog. a_original holds the function on
	/// entry and the trampoline to its original body on success. Lives in the
	/// native half because Detours needs <Windows.h>.
	[[nodiscard]] bool AttachPrologDetour(void** a_original, void* a_replacement) noexcept;
}
