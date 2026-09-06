#include "Shader/ShaderCompiler.h"

#include <memory>
#include <vector>

namespace Shader
{
	namespace
	{
		// Level 3 because this code runs per pixel. Warnings as errors is not
		// in here: it is the default of Compile, and the one call site that
		// compiles foreign source turns it off for itself.
		constexpr std::uint32_t kBaseFlags =
			REX::W32::D3DCOMPILE_ENABLE_STRICTNESS |
			REX::W32::D3DCOMPILE_OPTIMIZATION_LEVEL3;

		bool IsSupportedProfile(std::string_view a_profile) noexcept
		{
			return a_profile == "ps_5_0" || a_profile == "vs_5_0" || a_profile == "cs_5_0";
		}

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
		return Compile(a_source, a_sourceName, a_entryPoint, "ps_5_0");
	}

	CompileResult CompileVertexShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint)
	{
		return Compile(a_source, a_sourceName, a_entryPoint, "vs_5_0");
	}

	CompileResult CompileComputeShader(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint,
		std::span<const ShaderDefine> a_defines,
		bool a_warningsAsErrors)
	{
		return Compile(
			a_source, a_sourceName, a_entryPoint, "cs_5_0", a_defines, a_warningsAsErrors);
	}

	CompileResult Compile(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint,
		const std::string& a_profile,
		std::span<const ShaderDefine> a_defines,
		bool a_warningsAsErrors)
	{
		CompileResult result;

		if (!IsSupportedProfile(a_profile)) {
			result.diagnostics = "unsupported shader profile " + a_profile;
			return result;
		}

		// The array has to outlive the call and end in a null entry. The
		// strings belong to the caller's span; D3D_SHADER_MACRO stores pointers
		// into them rather than copying.
		std::vector<REX::W32::D3D_SHADER_MACRO> macros;
		macros.reserve(a_defines.size() + 1);
		for (const auto& define : a_defines) {
			macros.push_back({ define.name.c_str(), define.value.c_str() });
		}
		macros.push_back({ nullptr, nullptr });

		const std::uint32_t flags =
			kBaseFlags |
			(a_warningsAsErrors ? REX::W32::D3DCOMPILE_WARNINGS_ARE_ERRORS : 0u);

		REX::W32::ID3DBlob* code = nullptr;
		REX::W32::ID3DBlob* errors = nullptr;

		const auto hr = REX::W32::D3DCompile(
			a_source.data(),
			a_source.size(),
			a_sourceName.c_str(),
			a_defines.empty() ? nullptr : macros.data(),
			nullptr,  // no include handler, see the header for why
			a_entryPoint.c_str(),
			a_profile.c_str(),
			flags,
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
