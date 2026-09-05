#pragma once

#include <REX/W32/D3D11.h>

#include <string>

namespace Render
{
	/// Attaches a name that D3D debugging tools display, RenderDoc included.
	///
	/// Returns false for a null object or when D3D rejects the call. Callers
	/// are expected to report a failure once rather than per object: with
	/// hundreds of resources, a per-object message would bury the log.
	bool SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;

	/// Reads back a name set that way. Returns an empty string for a null
	/// object or one that carries no name.
	///
	/// This is what makes a bound resource legible: the inventory from
	/// subproject B2 named all 267 of them, so a view found on the pipeline can
	/// be reported as FO4_RT_042 rather than as an address. The engine itself
	/// names nothing.
	[[nodiscard]] std::string GetDebugName(REX::W32::ID3D11DeviceChild* a_object) noexcept;

	/// The same for a view, following it to the resource it addresses. Views
	/// are unnamed; their resources are the ones the inventory labelled.
	[[nodiscard]] std::string GetViewTargetName(REX::W32::ID3D11View* a_view) noexcept;
}
