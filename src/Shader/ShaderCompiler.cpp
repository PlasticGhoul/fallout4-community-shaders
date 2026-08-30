#include "Shader/ShaderCompiler.h"

#include <memory>

namespace Shader
{
	namespace
	{
		// Strictness plus warnings-as-errors is the same standard we hold our
		// own C++ to with /W4 /WX. Level 3 because this code runs per pixel.
		constexpr std::uint32_t kFlags =
			REX::W32::D3DCOMPILE_ENABLE_STRICTNESS |
			REX::W32::D3DCOMPILE_WARNINGS_ARE_ERRORS |
			REX::W32::D3DCOMPILE_OPTIMIZATION_LEVEL3;

		std::string BlobToString(REX::W32::ID3DBlob* a_blob)
		{
			if (a_blob == nullptr) {
				return {};
			}

			const auto* const data = static_cast<const char*>(a_blob->GetBufferPointer());
			std::string text{ data, a_blob->GetBufferSize() };

			// The diagnostics blob counts its terminator in the size; left in,
			// it would end up inside the logged string.
			while (!text.empty() && text.back() == '\0') {
				text.pop_back();
			}

			return text;
		}
	}

	CompileResult CompilePixelShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint)
	{
		CompileResult result;

		REX::W32::ID3DBlob* code = nullptr;
		REX::W32::ID3DBlob* errors = nullptr;

		const auto hr = REX::W32::D3DCompile(
			a_source.data(),
			a_source.size(),
			a_sourceName.c_str(),
			nullptr,  // no defines until subproject D brings a descriptor scheme
			nullptr,  // no include handler, see the header for why
			a_entryPoint.c_str(),
			"ps_5_0",
			kFlags,
			0,
			std::addressof(code),
			std::addressof(errors));

		result.diagnostics = BlobToString(errors);

		if (hr >= 0 && code != nullptr) {
			const auto* const bytes = static_cast<const std::uint8_t*>(code->GetBufferPointer());
			result.bytecode.assign(bytes, bytes + code->GetBufferSize());
		}

		if (code != nullptr) {
			code->Release();
		}
		if (errors != nullptr) {
			errors->Release();
		}

		return result;
	}
}
