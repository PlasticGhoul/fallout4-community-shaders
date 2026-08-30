#pragma once

#include <REX/W32/D3D11.h>

namespace Render
{
	/// Attaches a name that D3D debugging tools display, RenderDoc included.
	///
	/// Returns false for a null object or when D3D rejects the call. Callers
	/// are expected to report a failure once rather than per object: with
	/// hundreds of resources, a per-object message would bury the log.
	bool SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
}
