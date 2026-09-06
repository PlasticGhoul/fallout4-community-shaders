#pragma once

// The project PCH stops at REL and REX; D3DCompile and ID3DBlob live in a
// header of their own that has to be pulled in explicitly.
#include <REX/W32/D3DCOMPILER.h>

#include <cstdint>
#include <span>
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

	/// One /D for the compiler. Both strings must outlive the Compile call:
	/// D3D_SHADER_MACRO holds pointers into them, not copies.
	struct ShaderDefine
	{
		std::string name;
		std::string value;
	};

	/// Compiles one shader against a_profile.
	///
	/// Only ps_5_0, vs_5_0 and cs_5_0 are accepted. An unknown profile is
	/// refused with a diagnostic naming it rather than handed to D3DCompile,
	/// where a typo would come back as an unrecognisable HRESULT.
	///
	/// a_warningsAsErrors is true for everything we write ourselves, the same
	/// standard /W4 /WX holds our C++ to. It exists to be passed false at the
	/// one call site that compiles third-party source - Bend's raymarch in
	/// ScreenSpaceShadows - and nowhere else.
	///
	/// No include handler is passed: REX::W32 declares ID3DInclude as deriving
	/// from IUnknown, while the real interface (d3dcommon.h, DECLARE_INTERFACE)
	/// has no base and exactly two vtable slots. An implementation of the REX
	/// declaration would have d3dcompiler call QueryInterface where it means
	/// Open. ShaderSource splices includes before we get here instead.
	[[nodiscard]] CompileResult Compile(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint,
		const std::string& a_profile,
		std::span<const ShaderDefine> a_defines = {},
		bool a_warningsAsErrors = true);

	[[nodiscard]] CompileResult CompilePixelShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint);

	[[nodiscard]] CompileResult CompileVertexShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint);

	[[nodiscard]] CompileResult CompileComputeShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint,
		std::span<const ShaderDefine> a_defines = {},
		bool a_warningsAsErrors = true);
}
