#include "Bridge/PrologDetour.h"

#include "Bridge/Native/NativeSdk.h"

#include <detours/detours.h>

namespace Bridge
{
	bool AttachPrologDetour(void** a_original, void* a_replacement) noexcept
	{
		if (DetourTransactionBegin() != NO_ERROR) {
			return false;
		}
		DetourUpdateThread(GetCurrentThread());
		if (DetourAttach(a_original, a_replacement) != NO_ERROR) {
			DetourTransactionAbort();
			return false;
		}
		return DetourTransactionCommit() == NO_ERROR;
	}
}
