#include "Shader/ShaderCompiler.h"

#include <cstdio>

namespace
{
	int g_failures = 0;

	void Check(bool a_passed, const char* a_what)
	{
		std::printf("%s  %s\n", a_passed ? "ok  " : "FAIL", a_what);
		if (!a_passed) {
			++g_failures;
		}
	}

	bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		return a_haystack.find(a_needle) != std::string_view::npos;
	}

	constexpr std::string_view kValid =
		"Texture2D<float4> SourceTexture : register(t0);\n"
		"SamplerState SourceSampler : register(s0);\n"
		"float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET\n"
		"{\n"
		"    return SourceTexture.Sample(SourceSampler, uv);\n"
		"}\n";

	// The undeclared identifier sits on line 3 so the diagnostics can be
	// checked for the line number, not merely for the word "error".
	constexpr std::string_view kUndeclaredOnLineThree =
		"float4 main() : SV_TARGET\n"
		"{\n"
		"    return NoSuchSymbol;\n"
		"}\n";

	// Implicit truncation is a warning. With warnings as errors it must fail.
	constexpr std::string_view kTruncating =
		"float4 main() : SV_TARGET\n"
		"{\n"
		"    float3 value = float4(1.0, 2.0, 3.0, 4.0);\n"
		"    return float4(value, 1.0);\n"
		"}\n";
}

int main()
{
	{
		const auto result = Shader::CompilePixelShader(kValid, "valid.hlsl", "main");
		Check(result.Succeeded(), "valid HLSL compiles");
		if (result.Succeeded()) {
			const bool magic = result.bytecode.size() > 4 &&
			                   result.bytecode[0] == 'D' && result.bytecode[1] == 'X' &&
			                   result.bytecode[2] == 'B' && result.bytecode[3] == 'C';
			Check(magic, "the bytecode starts with the DXBC magic");
		}
	}

	{
		const auto result =
			Shader::CompilePixelShader(kUndeclaredOnLineThree, "broken.hlsl", "main");
		Check(!result.Succeeded(), "an undeclared identifier fails the compile");
		Check(!result.diagnostics.empty(), "the failure carries diagnostics");
		Check(Contains(result.diagnostics, "broken.hlsl"), "the diagnostics name the source");
		Check(Contains(result.diagnostics, "(3"), "the diagnostics carry the line number");
	}

	{
		const auto result = Shader::CompilePixelShader(kTruncating, "warn.hlsl", "main");
		Check(!result.Succeeded(), "a warning is treated as an error");
	}

	{
		const auto result = Shader::CompilePixelShader(kValid, "valid.hlsl", "no_such_entry");
		Check(!result.Succeeded(), "a missing entry point fails");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
