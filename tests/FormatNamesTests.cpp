#include "Render/FormatNames.h"

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

	void CheckName(REX::W32::DXGI_FORMAT a_format, std::string_view a_expected)
	{
		const auto actual = Render::FormatName(a_format);
		const bool passed = actual == a_expected;
		std::printf("%s  format %-4d -> %s\n",
			passed ? "ok  " : "FAIL",
			static_cast<int>(a_format),
			std::string(actual).c_str());
		if (!passed) {
			++g_failures;
		}
	}
}

int main()
{
	// Spot checks across the ranges that actually occur as render targets.
	CheckName(REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM");
	CheckName(REX::W32::DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT");
	CheckName(REX::W32::DXGI_FORMAT_R32_FLOAT, "R32_FLOAT");
	CheckName(REX::W32::DXGI_FORMAT_R8_UNORM, "R8_UNORM");
	CheckName(REX::W32::DXGI_FORMAT_UNKNOWN, "UNKNOWN");

	// An unmapped value must degrade to UNKNOWN rather than an empty string or
	// a crash. The caller logs the raw number alongside, so nothing is lost.
	CheckName(static_cast<REX::W32::DXGI_FORMAT>(9999), "UNKNOWN");

	Check(Render::BindFlagsString(0) == "-", "no flags renders as a dash");
	Check(
		Render::BindFlagsString(REX::W32::D3D11_BIND_RENDER_TARGET) == "RENDER_TARGET",
		"a single flag renders alone");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_RENDER_TARGET |
			REX::W32::D3D11_BIND_SHADER_RESOURCE) == "RENDER_TARGET|SHADER_RESOURCE",
		"two flags join in declaration order, not set order");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_SHADER_RESOURCE |
			REX::W32::D3D11_BIND_RENDER_TARGET) == "RENDER_TARGET|SHADER_RESOURCE",
		"the order of the operands does not change the result");
	Check(
		Render::BindFlagsString(
			REX::W32::D3D11_BIND_RENDER_TARGET |
			REX::W32::D3D11_BIND_SHADER_RESOURCE |
			REX::W32::D3D11_BIND_UNORDERED_ACCESS) ==
			"RENDER_TARGET|SHADER_RESOURCE|UNORDERED_ACCESS",
		"three flags join in declaration order");

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
