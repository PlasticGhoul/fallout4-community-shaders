#pragma once

// The project PCH stops at REL and REX; D3DCompile and ID3DBlob live in a
// header of their own that has to be pulled in explicitly.
#include <REX/W32/D3DCOMPILER.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Shader
{
	struct CompileResult
	{
		/// Empty when the compile failed.
		std::vector<std::uint8_t> bytecode;

		/// The compiler's own diagnostics, verbatim, so that a shader author
		/// reads what fxc would have told them.
		std::string diagnostics;

		[[nodiscard]] bool Succeeded() const noexcept { return !bytecode.empty(); }
	};

	/// Compiles one pixel shader against ps_5_0.
	///
	/// No include handler is passed: REX::W32 declares ID3DInclude as deriving
	/// from IUnknown, while the real interface (d3dcommon.h, DECLARE_INTERFACE)
	/// has no base and exactly two vtable slots. An implementation of the REX
	/// declaration would have d3dcompiler call QueryInterface where it means
	/// Open. ShaderSource splices includes before we get here instead.
	[[nodiscard]] CompileResult CompilePixelShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint);
}
