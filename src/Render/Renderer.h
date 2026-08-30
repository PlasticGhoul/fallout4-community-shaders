#pragma once

// The project PCH stops at REL and REX; the D3D11 and DXGI declarations are
// separate headers that RE/B/BSGraphics.h pulls in for itself. Naming those
// types in our own interface means including them here too.
#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

namespace Render
{
	// commonlibf4 declares the D3D11 and DXGI interfaces itself, under REX::W32.
	// We stay inside that namespace and never include <d3d11.h>: the
	// declarations are complete, and one type system is simpler than two.
	using Device = REX::W32::ID3D11Device;
	using Context = REX::W32::ID3D11DeviceContext;
	using SwapChain = REX::W32::IDXGISwapChain;

	/// Each of these reads through to the engine on every call rather than
	/// caching. The read is cheap, and caching would risk handing out a stale
	/// pointer after a device loss or a window change.
	///
	/// The objects belong to the engine. We neither AddRef nor Release them.
	[[nodiscard]] Device* GetDevice() noexcept;
	[[nodiscard]] Context* GetContext() noexcept;
	[[nodiscard]] SwapChain* GetSwapChain() noexcept;

	/// Confirms that the three pointers describe one coherent D3D11 object
	/// family, and logs what was found.
	///
	/// This is the safety net for the whole subproject. Every address here is
	/// resolved through REL::VariantID, which pads a missing third entry with
	/// the second - so our AE runtime reads the NG id. If that assumption were
	/// wrong we would be handed a pointer into unrelated memory, and a wrong
	/// pointer that happens to be non-null is far worse than a null one.
	[[nodiscard]] bool ValidateAndLog() noexcept;
}
