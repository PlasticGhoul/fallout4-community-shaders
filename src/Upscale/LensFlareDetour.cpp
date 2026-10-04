// Compiled without the precompiled header (CMakeLists.txt): Detours needs
// <Windows.h>, and nothing else in the plugin may see it.
#include <Windows.h>

#include <detours/detours.h>

namespace Upscale
{
	/// BSImagespaceShaderLensFlare's render function is reached through a
	/// pointer handed to a registration at startup - one lea, no call site -
	/// so it is the one engine function hooked at its prolog.
	bool AttachLensFlare(void** a_original, void* a_replacement) noexcept
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
