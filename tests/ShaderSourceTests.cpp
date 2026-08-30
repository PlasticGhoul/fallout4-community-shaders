#include "Shader/ShaderSource.h"

#include <cstdio>
#include <fstream>

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

	std::filesystem::path MakeRoot()
	{
		auto root = std::filesystem::temp_directory_path() / "fo4cs-shadersource-tests";
		std::filesystem::remove_all(root);
		std::filesystem::create_directories(root);
		return root;
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}

	bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		return a_haystack.find(a_needle) != std::string_view::npos;
	}
}

int main()
{
	const auto root = MakeRoot();

	// A file without includes comes back unchanged apart from the #line
	// directive that anchors it.
	WriteFile(root / "plain.hlsl", "float4 main() : SV_TARGET { return 0; }\n");
	{
		const auto result = Shader::LoadSource(root, "plain.hlsl");
		Check(result.has_value(), "a plain file loads");
		if (result.has_value()) {
			Check(Contains(result->text, "float4 main()"), "the body survives");
			Check(Contains(result->text, "#line 1 "), "a #line directive anchors the body");
			Check(result->files.size() == 1, "one file contributed");
		}
	}

	// An include is replaced by the file it names, and both files are reported.
	WriteFile(root / "common.hlsli", "#define TINT 0.5\n");
	WriteFile(
		root / "withinclude.hlsl",
		"#include \"common.hlsli\"\nfloat4 main() : SV_TARGET { return TINT; }\n");
	{
		const auto result = Shader::LoadSource(root, "withinclude.hlsl");
		Check(result.has_value(), "a file with an include loads");
		if (result.has_value()) {
			Check(Contains(result->text, "#define TINT 0.5"), "the included body is spliced in");
			Check(!Contains(result->text, "#include"), "the include line itself is gone");
			Check(result->files.size() == 2, "both files are reported");
		}
	}

	// Nesting works, and a file included twice is reported once.
	WriteFile(root / "outer.hlsli", "#include \"common.hlsli\"\n#define OUTER 1\n");
	WriteFile(
		root / "nested.hlsl",
		"#include \"outer.hlsli\"\n#include \"common.hlsli\"\n"
		"float4 main() : SV_TARGET { return OUTER; }\n");
	{
		const auto result = Shader::LoadSource(root, "nested.hlsl");
		Check(result.has_value(), "nested includes load");
		if (result.has_value()) {
			Check(Contains(result->text, "#define OUTER 1"), "the nested body is spliced in");
			Check(result->files.size() == 3, "a file included twice is listed once");
		}
	}

	// A missing file names itself in the error.
	{
		const auto result = Shader::LoadSource(root, "absent.hlsl");
		Check(!result.has_value(), "a missing file fails");
		if (!result.has_value()) {
			Check(Contains(result.error(), "absent.hlsl"), "the error names the missing file");
		}
	}

	// An include must not reach outside the shader directory.
	WriteFile(root.parent_path() / "outside.hlsli", "#define OUTSIDE 1\n");
	WriteFile(root / "escape.hlsl", "#include \"../outside.hlsli\"\n");
	{
		const auto result = Shader::LoadSource(root, "escape.hlsl");
		Check(!result.has_value(), "an include leaving the root fails");
	}

	// A cycle is refused rather than recursed into.
	WriteFile(root / "a.hlsli", "#include \"b.hlsli\"\n");
	WriteFile(root / "b.hlsli", "#include \"a.hlsli\"\n");
	WriteFile(root / "cycle.hlsl", "#include \"a.hlsli\"\n");
	{
		const auto result = Shader::LoadSource(root, "cycle.hlsl");
		Check(!result.has_value(), "an include cycle fails");
		if (!result.has_value()) {
			Check(Contains(result.error(), "cycle"), "the error says it is a cycle");
		}
	}

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
