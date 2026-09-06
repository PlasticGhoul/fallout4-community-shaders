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

	constexpr std::string_view kMinimalCompute =
		"RWTexture2D<float> Output : register(u0);\n"
		"[numthreads(8, 8, 1)]\n"
		"void main(uint3 id : SV_DispatchThreadID)\n"
		"{\n"
		"    Output[id.xy] = 1.0;\n"
		"}\n";

	constexpr std::string_view kMinimalVertex =
		"float4 main(uint id : SV_VertexID) : SV_POSITION\n"
		"{\n"
		"    return float4(float(id), 0.0, 0.0, 1.0);\n"
		"}\n";

	// The same implicit truncation as kTruncating, but shaped as a compute
	// shader, so that the strictness can be checked on that profile without the
	// compile failing for an unrelated reason.
	constexpr std::string_view kTruncatingCompute =
		"RWTexture2D<float> Output : register(u0);\n"
		"[numthreads(8, 8, 1)]\n"
		"void main(uint3 id : SV_DispatchThreadID)\n"
		"{\n"
		"    float3 value = float4(1.0, 2.0, 3.0, 4.0);\n"
		"    Output[id.xy] = value.x;\n"
		"}\n";

	// Refuses to compile unless WANTED is defined, so the define is proven to
	// arrive rather than merely to be accepted.
	constexpr std::string_view kNeedsDefine =
		"#ifndef WANTED\n"
		"#error WANTED was not defined\n"
		"#endif\n"
		"float4 main() : SV_TARGET\n"
		"{\n"
		"    return float4(WANTED, 0.0, 0.0, 1.0);\n"
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

	{
		const auto result = Shader::CompileComputeShader(kMinimalCompute, "compute.hlsl", "main");
		Check(result.Succeeded(), "a compute shader compiles against cs_5_0");
	}

	{
		const auto result = Shader::CompileVertexShader(kMinimalVertex, "vertex.hlsl", "main");
		Check(result.Succeeded(), "a vertex shader compiles against vs_5_0");
	}

	{
		const Shader::ShaderDefine defines[] = { { "WANTED", "2.0" } };
		const auto with = Shader::Compile(kNeedsDefine, "defined.hlsl", "main", "ps_5_0", defines);
		Check(with.Succeeded(), "a define reaches the shader");

		const auto without = Shader::Compile(kNeedsDefine, "defined.hlsl", "main", "ps_5_0");
		Check(!without.Succeeded(), "without the define the same source fails");
		Check(Contains(without.diagnostics, "WANTED"), "and says which define was missing");
	}

	{
		// Every profile is held to it, not only the pixel wrapper the original
		// check went through.
		const auto compute = Shader::Compile(kTruncatingCompute, "warn.hlsl", "main", "cs_5_0");
		Check(!compute.Succeeded(), "warnings are errors for compute too");
	}

	{
		const auto result = Shader::Compile(kMinimalVertex, "vertex.hlsl", "main", "gs_5_0");
		Check(!result.Succeeded(), "an unsupported profile is refused");

		// D3DCompile would refuse gs_5_0 by itself, so "it failed" proves
		// nothing about us. The refusal has to be ours, and it has to say which
		// profile - otherwise a typo comes back as an unrecognisable HRESULT.
		Check(
			result.diagnostics.starts_with("unsupported shader profile"),
			"and the refusal is ours, not the compiler's");
		Check(Contains(result.diagnostics, "gs_5_0"), "and it names the profile");
		Check(result.bytecode.empty(), "a refused compile produces no bytecode");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
