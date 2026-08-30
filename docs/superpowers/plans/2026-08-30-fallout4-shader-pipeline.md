# Teilprojekt C — Shader-Pipeline, Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eigenes HLSL zur Laufzeit übersetzen, damit einen laufenden Post-Process-Pass von
Fallout 4 ersetzen, und bei Dateiänderung ohne Spielneustart neu übersetzen.

**Architecture:** `ImageSpaceManager::effectList` führt `BSImagespaceShader`-Objekte, die zugleich
`BSShader` sind. Klassenname und Subobjekt-Offset werden aus der MSVC-RTTI des Objekts gelesen,
nicht geraten und nicht über die Adressbibliothek aufgelöst. Aus dem `BSShader` kommt die
Technik-Karte `pixelShaders`, in deren Eintrag ein `ID3D11PixelShader*` steht — dieser Zeiger wird
getauscht. Kein Engine-Hook, kein Trampolin, keine Änderung an commonlibf4.

**Tech Stack:** C++23, MSVC, CommonLibF4 (Fork `PlasticGhoul/commonlibf4`), `REX::W32` für D3D11
und `D3DCompile`, CMake 4.2+, handgeschriebene Host-Tests nach dem Muster in `tests/`.

**Spec:** `docs/superpowers/specs/2026-08-30-fallout4-shader-pipeline-design.md`

## Global Constraints

-   **Runtime:** ausschließlich Fallout 4 AE `1.11.240`. Keine Lockerung der Prüfung in
    `Runtime::IsSupported`.
-   **Compiler-Flags unseres Targets:** `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`. Eine
    Warnung ist ein Fehler. Die beiden Fremdtargets werden nicht angefasst.
-   **`<d3d11.h>` ist verboten.** D3D- und DXGI-Typen kommen aus `REX::W32`. Wer sie in einer
    eigenen Schnittstelle nennt, bindet `REX/W32/D3D11.h`, `DXGI.h` oder `D3DCOMPILER.h` selbst
    ein — der PCH bringt nur `REL` und `REX` mit.
-   **`<Windows.h>` ist ebenso zu vermeiden.** Was gebraucht wird, steht in `REX::W32`; was dort
    fehlt, wird umgangen statt nachgezogen (siehe Aufgabe 3).
-   **Trampolin bleibt aus.** `InitInfo::trampoline` und `InitInfo::hook` bleiben auf `false`.
-   **commonlibf4 wird nicht geändert.** Kein Submodul-Commit in diesem Teilprojekt.
-   **Keine `REL::ID`-Auflösung für die Katalogisierung.** `REL::ID::offset()` ruft bei unbekannter
    ID `REX::FAIL` und beendet den Prozess (`lib/commonlib-shared/src/REL/IDDB.cpp:442`).
-   **Konventionen:** Tabs, `a_`-Präfix für Parameter, `_`-Präfix für Member, anonymer Namensraum
    für Internes, Kommentare begründen statt zu beschreiben. Conventional Commits, Titel maximal
    50 Zeichen, Rumpf bei 72 umgebrochen. Code und Commits auf Englisch, Dokumente unter `docs/`
    auf Deutsch.
-   **Branch:** `port/c-shader-pipeline`. Kein direktes Bauen auf `dev`, kein Push ohne Ansage.
-   **`cmake` liegt nicht auf dem PATH.** Voller Pfad:
    `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.
    In jeder Sitzung vorsorglich `$env:VCPKG_ROOT = "C:\vcpkg"` setzen.
-   **Testdisziplin:** Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen, und der
    erwartete Fehlschlag wird **vorher benannt**. Dabei prüfen, dass die Mutation auch übersetzt —
    unter `/W4 /WX` scheitert sonst der Bau und man misst das alte Executable.

---

## File Structure

| Datei                                     | Verantwortung                                                         |
| ----------------------------------------- | --------------------------------------------------------------------- |
| `src/Shader/ShaderSource.h/.cpp`          | Datei lesen, `#include` textuell einsetzen, berührte Dateien melden   |
| `src/Shader/ShaderCompiler.h/.cpp`        | Hülle um `REX::W32::D3DCompile`, kennt weder Dateien noch Engine      |
| `src/Shader/ShaderWatcher.h/.cpp`         | `FileWatch`: Zeitstempel einer Dateimenge abfragen, Änderung melden   |
| `src/Shader/ImagespaceCatalog.h/.cpp`     | `effectList` ablaufen, RTTI auswerten, Sicherheitsnetz, Protokoll     |
| `src/Shader/ShaderOverride.h/.cpp`        | Zeiger-Tausch in einem `BSGraphics::PixelShader`, Wächter, Rücktausch |
| `src/Shader/ShaderPipeline.h/.cpp`        | Verdrahtung: Start, Watcher-Thread, `Tick()` aus `Present`            |
| `package/Shaders/FO4/ImagespaceCopy.hlsl` | Unser Ersatz-Shader                                                   |
| `tests/ShaderSourceTests.cpp`             | Host-Test zu `ShaderSource`                                           |
| `tests/ShaderCompilerTests.cpp`           | Host-Test zu `ShaderCompiler`                                         |
| `tests/ShaderWatcherTests.cpp`            | Host-Test zu `FileWatch`                                              |
| `CMakeLists.txt`                          | drei neue Test-Executables, ein Kopierschritt für die Shader          |

`src/*.cpp` wird per `file(GLOB_RECURSE … CONFIGURE_DEPENDS)` eingesammelt — neue Dateien unter
`src/Shader/` brauchen **keine** CMake-Änderung. Nur die Tests und der Kopierschritt werden
eingetragen.

---

## Task 1: `ShaderSource` — Datei lesen und `#include` auflösen

**Files:**

-   Create: `src/Shader/ShaderSource.h`, `src/Shader/ShaderSource.cpp`
-   Test: `tests/ShaderSourceTests.cpp`
-   Modify: `CMakeLists.txt` (Test-Executable `ShaderSourceTests`)

**Interfaces:**

-   Consumes: nichts.
-   Produces: `Shader::SourceText { std::string text; std::vector<std::filesystem::path> files; }`
    und
    `std::expected<Shader::SourceText, std::string> Shader::LoadSource(const std::filesystem::path& a_root, const std::filesystem::path& a_relative)`.
    Aufgabe 2 übersetzt `text`, Aufgabe 3 beobachtet `files`, Aufgabe 6 ruft `LoadSource`.

-   [ ] **Step 1: Header anlegen**

`src/Shader/ShaderSource.h`:

```cpp
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Shader
{
	/// One shader translation unit, with every #include already spliced in.
	struct SourceText
	{
		/// What the compiler is handed. Carries #line directives so that
		/// diagnostics keep pointing at the file the author actually wrote.
		std::string text;

		/// Every file that contributed, the root file first, each canonical.
		/// This is the set the watcher polls.
		std::vector<std::filesystem::path> files;
	};

	/// Reads a_root / a_relative and splices its `#include "..."` lines.
	///
	/// Includes are resolved relative to the including file and must stay
	/// inside a_root: a shader must not be able to read the rest of the disk.
	/// Returns the message to log instead of a translation unit when a file is
	/// missing, an include escapes a_root, or the includes form a cycle.
	[[nodiscard]] std::expected<SourceText, std::string> LoadSource(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative);
}
```

-   [ ] **Step 2: Den fehlschlagenden Test schreiben**

`tests/ShaderSourceTests.cpp`:

```cpp
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
	WriteFile(root / "withinclude.hlsl", "#include \"common.hlsli\"\nfloat4 main() : SV_TARGET { return TINT; }\n");
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
	WriteFile(root / "nested.hlsl", "#include \"outer.hlsli\"\n#include \"common.hlsli\"\nfloat4 main() : SV_TARGET { return OUTER; }\n");
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
```

-   [ ] **Step 3: Test in `CMakeLists.txt` eintragen**

Direkt vor dem abschließenden `endif()` des `FO4CS_BUILD_TESTS`-Blocks einfügen:

```cmake
    add_executable(
        ShaderSourceTests
        "${CMAKE_SOURCE_DIR}/tests/ShaderSourceTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Shader/ShaderSource.cpp"
    )

    target_include_directories(
        ShaderSourceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ShaderSourceTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ShaderSourceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ShaderSourceTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ShaderSourceTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ShaderSource COMMAND ShaderSourceTests)
```

-   [ ] **Step 4: Bauen und den Fehlschlag sehen**

```pwsh
$env:VCPKG_ROOT = "C:\vcpkg"
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . --preset FO4-Fast
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4-Fast
```

Erwartet: **Bau schlägt fehl**, `ShaderSource.cpp` existiert nicht.

-   [ ] **Step 5: Implementierung schreiben**

`src/Shader/ShaderSource.cpp`:

```cpp
#include "Shader/ShaderSource.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

namespace Shader
{
	namespace
	{
		// Deep enough for any sane shader tree, shallow enough that a cycle we
		// somehow failed to spot still terminates.
		constexpr std::size_t kMaxIncludeDepth = 16;

		void SkipSpace(std::string_view& a_view) noexcept
		{
			while (!a_view.empty() && (a_view.front() == ' ' || a_view.front() == '\t')) {
				a_view.remove_prefix(1);
			}
		}

		// Returns the file named by an `#include "..."` line, or an empty view
		// when the line is anything else. Angle brackets are deliberately not
		// handled: there is no system include path to search.
		std::string_view IncludeTarget(std::string_view a_line) noexcept
		{
			SkipSpace(a_line);
			if (!a_line.starts_with("#")) {
				return {};
			}
			a_line.remove_prefix(1);

			SkipSpace(a_line);
			if (!a_line.starts_with("include")) {
				return {};
			}
			a_line.remove_prefix(7);

			SkipSpace(a_line);
			if (!a_line.starts_with("\"")) {
				return {};
			}
			a_line.remove_prefix(1);

			const auto end = a_line.find('"');
			if (end == std::string_view::npos) {
				return {};
			}
			return a_line.substr(0, end);
		}

		std::optional<std::string> ReadWholeFile(const std::filesystem::path& a_path)
		{
			std::ifstream stream{ a_path, std::ios::binary };
			if (!stream) {
				return std::nullopt;
			}
			std::ostringstream buffer;
			buffer << stream.rdbuf();
			return buffer.str();
		}

		// Path comparison by component, not by string prefix: a plain prefix
		// test would accept a sibling directory whose name merely starts with
		// the root's name.
		bool IsInside(const std::filesystem::path& a_child, const std::filesystem::path& a_root)
		{
			const auto pair =
				std::mismatch(a_root.begin(), a_root.end(), a_child.begin(), a_child.end());
			return pair.first == a_root.end();
		}

		class Splicer
		{
		public:
			explicit Splicer(const std::filesystem::path& a_root) :
				_root(std::filesystem::weakly_canonical(a_root))
			{}

			std::expected<SourceText, std::string> Run(const std::filesystem::path& a_relative)
			{
				if (auto error = Splice(_root / a_relative); error.has_value()) {
					return std::unexpected(*error);
				}
				return std::move(_out);
			}

		private:
			std::optional<std::string> Splice(const std::filesystem::path& a_file);

			std::filesystem::path              _root;
			SourceText                         _out;
			std::vector<std::filesystem::path> _open;
		};

		std::optional<std::string> Splicer::Splice(const std::filesystem::path& a_file)
		{
			std::error_code ec;
			const auto      canonical = std::filesystem::weakly_canonical(a_file, ec);
			if (ec) {
				return std::format("cannot resolve {}", a_file.generic_string());
			}

			if (!IsInside(canonical, _root)) {
				return std::format(
					"{} lies outside the shader directory",
					canonical.generic_string());
			}

			if (std::ranges::find(_open, canonical) != _open.end()) {
				return std::format("include cycle through {}", canonical.generic_string());
			}

			if (_open.size() >= kMaxIncludeDepth) {
				return std::format("includes nested deeper than {}", kMaxIncludeDepth);
			}

			const auto content = ReadWholeFile(canonical);
			if (!content.has_value()) {
				return std::format("cannot read {}", canonical.generic_string());
			}

			if (std::ranges::find(_out.files, canonical) == _out.files.end()) {
				_out.files.push_back(canonical);
			}

			_open.push_back(canonical);

			// Forward slashes: a backslash would open an escape sequence in the
			// string literal the compiler parses out of the directive.
			const auto directive = canonical.generic_string();
			_out.text += std::format("#line 1 \"{}\"\n", directive);

			std::istringstream lines{ *content };
			std::string        line;
			std::uint32_t      lineNumber = 1;

			while (std::getline(lines, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}

				const auto target = IncludeTarget(line);
				if (target.empty()) {
					_out.text += line;
					_out.text += '\n';
				} else {
					if (auto error = Splice(canonical.parent_path() / target);
						error.has_value()) {
						return error;
					}
					// Back in this file, on the line after the include.
					_out.text += std::format("#line {} \"{}\"\n", lineNumber + 1, directive);
				}

				++lineNumber;
			}

			_open.pop_back();
			return std::nullopt;
		}
	}

	std::expected<SourceText, std::string> LoadSource(
		const std::filesystem::path& a_root,
		const std::filesystem::path& a_relative)
	{
		Splicer splicer{ a_root };
		return splicer.Run(a_relative);
	}
}
```

-   [ ] **Step 6: Bauen und Test grün sehen**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4-Fast
ctest --test-dir build/FO4-Fast --output-on-failure -R ShaderSource
```

Erwartet: PASS, `0 failure(s)`.

-   [ ] **Step 7: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `an include cycle fails` und
`the error says it is a cycle` schlagen fehl, `nested includes load` bleibt grün.

Mutation in `ShaderSource.cpp` — die Zyklusprüfung wirkungslos machen, ohne dass der Bau bricht:

```cpp
			if (false && std::ranges::find(_open, canonical) != _open.end()) {
```

Bauen und erneut `ctest … -R ShaderSource`. Erwartet: der Lauf endet mit einem Rückgabewert
ungleich null und nennt genau die beiden angekündigten Zeilen. Danach Mutation zurücknehmen,
neu bauen, erneut grün sehen.

-   [ ] **Step 8: Commit**

```bash
git add src/Shader/ShaderSource.h src/Shader/ShaderSource.cpp tests/ShaderSourceTests.cpp CMakeLists.txt
git commit -m "feat: read shader sources and splice includes"
```

---

## Task 2: `ShaderCompiler` — HLSL zur Laufzeit übersetzen

**Files:**

-   Create: `src/Shader/ShaderCompiler.h`, `src/Shader/ShaderCompiler.cpp`
-   Test: `tests/ShaderCompilerTests.cpp`
-   Modify: `CMakeLists.txt` (Test-Executable `ShaderCompilerTests`)

**Interfaces:**

-   Consumes: nichts. Das Modul kennt weder Dateien noch die Engine.
-   Produces:
    `Shader::CompileResult { std::vector<std::uint8_t> bytecode; std::string diagnostics; bool Succeeded() const; }`
    und
    `Shader::CompileResult Shader::CompilePixelShader(std::string_view a_source, const std::string& a_sourceName, const std::string& a_entryPoint)`.
    Aufgabe 6 gibt `bytecode` an `PixelShaderOverride::Install` weiter.

-   [ ] **Step 1: Header anlegen**

`src/Shader/ShaderCompiler.h`:

```cpp
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
		std::string_view   a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint);
}
```

-   [ ] **Step 2: Den fehlschlagenden Test schreiben**

`tests/ShaderCompilerTests.cpp`:

```cpp
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
```

-   [ ] **Step 3: Test in `CMakeLists.txt` eintragen**

Wie in Aufgabe 1, mit `ShaderCompilerTests`, Quelle
`"${CMAKE_SOURCE_DIR}/src/Shader/ShaderCompiler.cpp"` und `add_test(NAME ShaderCompiler COMMAND ShaderCompilerTests)`.
`d3dcompiler.lib` kommt über `CommonLibF4::CommonLibF4` mit — es wird von `commonlib-shared`
bereits `PUBLIC` gelinkt, hier ist nichts zusätzlich einzutragen.

-   [ ] **Step 4: Bauen und den Fehlschlag sehen**

Erwartet: **Bau schlägt fehl**, `ShaderCompiler.cpp` existiert nicht.

-   [ ] **Step 5: Implementierung schreiben**

`src/Shader/ShaderCompiler.cpp`:

```cpp
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
			std::string       text{ data, a_blob->GetBufferSize() };

			// The diagnostics blob counts its terminator in the size; left in,
			// it would end up inside the logged string.
			while (!text.empty() && text.back() == '\0') {
				text.pop_back();
			}

			return text;
		}
	}

	CompileResult CompilePixelShader(
		std::string_view   a_source,
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
```

-   [ ] **Step 6: Bauen und Test grün sehen**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4-Fast
ctest --test-dir build/FO4-Fast --output-on-failure -R ShaderCompiler
```

-   [ ] **Step 7: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `a warning is treated as an error` schlägt fehl, alle
übrigen Zeilen bleiben grün.

Mutation: `REX::W32::D3DCOMPILE_WARNINGS_ARE_ERRORS` aus `kFlags` entfernen. Das übersetzt sauber,
der Test also misst wirklich das neue Executable. Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 8: Commit**

```bash
git add src/Shader/ShaderCompiler.h src/Shader/ShaderCompiler.cpp tests/ShaderCompilerTests.cpp CMakeLists.txt
git commit -m "feat: compile hlsl at runtime"
```

---

## Task 3: `FileWatch` — Änderungen an der Dateimenge erkennen

**Files:**

-   Create: `src/Shader/ShaderWatcher.h`, `src/Shader/ShaderWatcher.cpp`
-   Test: `tests/ShaderWatcherTests.cpp`
-   Modify: `CMakeLists.txt` (Test-Executable `ShaderWatcherTests`)

**Interfaces:**

-   Consumes: die `files`-Menge aus `Shader::SourceText` (Aufgabe 1).
-   Produces: `class Shader::FileWatch` mit
    `void Reset(std::span<const std::filesystem::path> a_files)` und `[[nodiscard]] bool Poll()`.
    Aufgabe 6 ruft beides aus dem Watcher-Thread.

Der Thread selbst gehört **nicht** hierher: was hier steht, ist reine Logik und deshalb auf dem
Host prüfbar. Den Thread startet Aufgabe 6.

-   [ ] **Step 1: Header anlegen**

`src/Shader/ShaderWatcher.h`:

```cpp
#pragma once

#include <filesystem>
#include <span>
#include <utility>
#include <vector>

namespace Shader
{
	/// Polls the modification times of a fixed set of files.
	///
	/// Deliberately polling rather than ReadDirectoryChangesW: REX::W32 declares
	/// neither that function nor FindFirstChangeNotification, so the event-based
	/// route would mean including <Windows.h> and running two type systems side
	/// by side. For a handful of files, asking is cheaper than the apparatus.
	class FileWatch
	{
	public:
		/// Replaces the watched set and takes the current timestamps as the
		/// baseline, so the next Poll only reports changes made from now on.
		void Reset(std::span<const std::filesystem::path> a_files);

		/// True when at least one watched file changed since the previous call.
		///
		/// A file that cannot be read right now - an editor may still hold it
		/// open - keeps its old timestamp and is tried again next time. A file
		/// that vanished is treated the same way: never an exception.
		[[nodiscard]] bool Poll();

	private:
		std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> _entries;
	};
}
```

-   [ ] **Step 2: Den fehlschlagenden Test schreiben**

`tests/ShaderWatcherTests.cpp`:

```cpp
#include "Shader/ShaderWatcher.h"

#include <chrono>
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

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}

	// The timestamp is set explicitly rather than by sleeping: the test stays
	// deterministic and does not depend on filesystem timestamp granularity.
	void AgeForward(const std::filesystem::path& a_path)
	{
		const auto now = std::filesystem::last_write_time(a_path);
		std::filesystem::last_write_time(a_path, now + std::chrono::seconds{ 5 });
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-filewatch-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	const auto first = root / "first.hlsl";
	const auto second = root / "second.hlsli";
	WriteFile(first, "one\n");
	WriteFile(second, "two\n");

	const std::vector<std::filesystem::path> files{ first, second };

	Shader::FileWatch watch;
	watch.Reset(files);

	Check(!watch.Poll(), "nothing changed right after Reset");

	AgeForward(second);
	Check(watch.Poll(), "a changed file is reported");
	Check(!watch.Poll(), "the same change is not reported twice");

	AgeForward(first);
	AgeForward(second);
	Check(watch.Poll(), "two files changing at once report once");
	Check(!watch.Poll(), "and then go quiet again");

	std::filesystem::remove(second);
	bool threw = false;
	try {
		static_cast<void>(watch.Poll());
	} catch (...) {
		threw = true;
	}
	Check(!threw, "a deleted file does not throw");

	// An empty watch set is the normal state before the first shader loads.
	Shader::FileWatch empty;
	Check(!empty.Poll(), "an empty watch reports nothing");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 3: Test in `CMakeLists.txt` eintragen**

Wie in Aufgabe 1, mit `ShaderWatcherTests`, Quelle
`"${CMAKE_SOURCE_DIR}/src/Shader/ShaderWatcher.cpp"` und
`add_test(NAME ShaderWatcher COMMAND ShaderWatcherTests)`.

-   [ ] **Step 4: Bauen und den Fehlschlag sehen**

Erwartet: **Bau schlägt fehl**, `ShaderWatcher.cpp` existiert nicht.

-   [ ] **Step 5: Implementierung schreiben**

`src/Shader/ShaderWatcher.cpp`:

```cpp
#include "Shader/ShaderWatcher.h"

namespace Shader
{
	namespace
	{
		// A file that cannot be stat'ed right now yields the caller's previous
		// value, so a locked or missing file is neither a change nor a throw.
		std::filesystem::file_time_type TimestampOr(
			const std::filesystem::path&           a_path,
			std::filesystem::file_time_type        a_fallback)
		{
			std::error_code ec;
			const auto      stamp = std::filesystem::last_write_time(a_path, ec);
			return ec ? a_fallback : stamp;
		}
	}

	void FileWatch::Reset(std::span<const std::filesystem::path> a_files)
	{
		_entries.clear();
		_entries.reserve(a_files.size());

		for (const auto& file : a_files) {
			_entries.emplace_back(file, TimestampOr(file, std::filesystem::file_time_type{}));
		}
	}

	bool FileWatch::Poll()
	{
		bool changed = false;

		// Every entry is visited even after the first hit: the timestamps all
		// have to be brought up to date, or the next Poll would report the
		// same change again.
		for (auto& [path, stamp] : _entries) {
			const auto current = TimestampOr(path, stamp);
			if (current != stamp) {
				stamp = current;
				changed = true;
			}
		}

		return changed;
	}
}
```

-   [ ] **Step 6: Bauen und Test grün sehen**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4-Fast
ctest --test-dir build/FO4-Fast --output-on-failure -R ShaderWatcher
```

-   [ ] **Step 7: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `the same change is not reported twice` und
`and then go quiet again` schlagen fehl; `a changed file is reported` bleibt grün.

Mutation in `FileWatch::Poll`: die Zeile `stamp = current;` entfernen. Übersetzt sauber, weil
`stamp` weiterhin gelesen wird.

Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 8: Commit**

```bash
git add src/Shader/ShaderWatcher.h src/Shader/ShaderWatcher.cpp tests/ShaderWatcherTests.cpp CMakeLists.txt
git commit -m "feat: notice changes to shader files"
```

---

## Task 4: Der Ersatz-Shader und seine Auslieferung

**Files:**

-   Create: `package/Shaders/FO4/ImagespaceCopy.hlsl`
-   Modify: `CMakeLists.txt` (Kopierschritt im `FO4CS_DEPLOY_DIR`-Block)

**Interfaces:**

-   Consumes: nichts.
-   Produces: die Datei `ImagespaceCopy.hlsl` unter `<Data>/Shaders/FO4/`, die Aufgabe 6 lädt.
    Eintrittspunkt `main`, Profil `ps_5_0`.

-   [ ] **Step 1: Den Shader schreiben**

`package/Shaders/FO4/ImagespaceCopy.hlsl`:

```hlsl
// Replacement for the engine's simplest image space pass.
//
// The tint is not an effect, it is the evidence: subproject C is finished when
// a screenshot shows it. The pass reads one texture and writes one colour, so
// it needs to know nothing about the rest of the engine's bindings. Which slot
// this actually lands in is decided at runtime by the imagespace catalog.
Texture2D<float4> SourceTexture : register(t0);
SamplerState SourceSampler : register(s0);

static const float3 ProofTint = float3(1.0, 0.6, 0.6);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    float4 source = SourceTexture.Sample(SourceSampler, uv);
    return float4(source.rgb * ProofTint, source.a);
}
```

-   [ ] **Step 2: Kopierschritt eintragen**

In `CMakeLists.txt`, innerhalb des bestehenden `if(FO4CS_DEPLOY_DIR)`-Blocks, nach dem
vorhandenen `add_custom_command` für die DLL:

```cmake
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E copy_directory_if_different
            "${CMAKE_SOURCE_DIR}/package/Shaders/FO4"
            "${FO4CS_DEPLOY_DIR}/Shaders/FO4"
        COMMENT "Deploying shaders to ${FO4CS_DEPLOY_DIR}/Shaders/FO4"
        VERBATIM
    )
```

Nur `package/Shaders/FO4` wird kopiert. Die 153 geerbten Skyrim-Dateien eine Ebene darüber bleiben
liegen, wo sie sind — sie sind das Ziel von Teilprojekt F, nicht von C.

-   [ ] **Step 3: Bauen und die Auslieferung prüfen**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
Test-Path "F:/SteamLibrary/steamapps/common/Fallout 4/Data/Shaders/FO4/ImagespaceCopy.hlsl"
```

Erwartet: `True`.

-   [ ] **Step 4: Der Shader muss für sich allein übersetzbar sein**

Diese Datei ist die einzige, deren Fehler man erst im Spiel sähe. Deshalb hier einmal
gegenprüfen — der `ShaderCompilerTests`-Aufbau aus Aufgabe 2 leistet das bereits, also genügt der
Nachweis über das laufende Spiel in Aufgabe 7. Kein eigener Test.

-   [ ] **Step 5: Commit**

```bash
git add package/Shaders/FO4/ImagespaceCopy.hlsl CMakeLists.txt
git commit -m "feat: add our replacement image space shader"
```

---

## Task 5: `ImagespaceCatalog` — Pässe finden und belegen

**Files:**

-   Create: `src/Shader/ImagespaceCatalog.h`, `src/Shader/ImagespaceCatalog.cpp`
-   Test: keiner auf dem Host. Engine-Typen und ein laufender Renderer sind Voraussetzung; belegt
    wird über das Log in Aufgabe 7.

**Interfaces:**

-   Consumes: nichts aus früheren Aufgaben.
-   Produces:
    `struct Shader::ImagespacePass { std::string className; RE::BSShader* shader; RE::BSGraphics::PixelShader* slot; std::uint32_t techniqueID; }`
    und `[[nodiscard]] std::vector<Shader::ImagespacePass> Shader::RunImagespaceCatalog()`.
    Aufgabe 6 wählt aus dieser Liste ihren Ziel-Slot.

-   [ ] **Step 1: Header anlegen**

`src/Shader/ImagespaceCatalog.h`:

```cpp
#pragma once

#include <RE/B/BSGraphics.h>
#include <RE/B/BSShader.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Shader
{
	/// One image space pass, as found in ImageSpaceManager::effectList and
	/// proven to be what it looks like.
	struct ImagespacePass
	{
		/// Read from the object's own RTTI, e.g. "BSImagespaceShaderCopy".
		std::string className;

		/// The BSShader subobject, already corrected for multiple inheritance.
		RE::BSShader* shader{ nullptr };

		/// The single technique entry, when the pass has exactly one pixel
		/// shader. Null when it has none or more than one - we only replace
		/// what we can name unambiguously.
		RE::BSGraphics::PixelShader* slot{ nullptr };

		std::uint32_t techniqueID{ 0 };
	};

	/// Walks the effect list, proves each entry, and logs a table of what it
	/// found. Writes nothing to the engine.
	[[nodiscard]] std::vector<ImagespacePass> RunImagespaceCatalog();
}
```

-   [ ] **Step 2: Implementierung schreiben**

`src/Shader/ImagespaceCatalog.cpp`:

```cpp
#include "Shader/ImagespaceCatalog.h"

#include <RE/I/ImageSpaceEffect.h>
#include <RE/I/ImageSpaceManager.h>
#include <REX/W32/RTTI.h>

#include <format>
#include <optional>

namespace Shader
{
	namespace
	{
		// MSVC's type descriptor: a vftable pointer, a spare, then the
		// decorated name as a plain null terminated string.
		struct TypeDescriptor
		{
			const void* vftable;
			void*       spare;
			char        name[1];
		};

		struct ObjectInfo
		{
			std::string   className;
			std::uint32_t subobjectOffset{ 0 };
		};

		// MSVC stores a pointer to the complete object locator immediately
		// before the first entry of every polymorphic vtable.
		const REX::W32::RTTICompleteObjectLocator* LocatorOf(const void* a_object) noexcept
		{
			const auto* const vtable = *static_cast<const void* const*>(a_object);
			if (vtable == nullptr) {
				return nullptr;
			}
			return *(static_cast<const REX::W32::RTTICompleteObjectLocator* const*>(vtable) - 1);
		}

		// Reads class name and subobject offset out of the object itself.
		//
		// Deliberately not a comparison against RE::VTABLE ids: REL::ID::offset
		// calls REX::FAIL for an id the address library does not know, which
		// ends the process. With 162 imagespace classes that is a real risk and
		// an unnecessary one - the compiler already wrote both answers into the
		// binary.
		std::optional<ObjectInfo> Describe(const void* a_object) noexcept
		{
			if (a_object == nullptr) {
				return std::nullopt;
			}

			const auto* const locator = LocatorOf(a_object);
			if (locator == nullptr || locator->signature != 1) {
				return std::nullopt;
			}

			// The locator records its own RVA. Recomputing the module base from
			// it and comparing against the game module proves the vtable really
			// belongs to Fallout4.exe rather than to whatever the pointer
			// happened to land in.
			const auto base = reinterpret_cast<std::uintptr_t>(locator) - locator->self;
			if (base != REX::FModule::GetExecutingModule().GetBaseAddress()) {
				return std::nullopt;
			}

			const auto* const descriptor =
				reinterpret_cast<const TypeDescriptor*>(base + locator->typeDescriptor);

			std::string_view decorated{ descriptor->name };
			if (!decorated.starts_with(".?AV")) {
				return std::nullopt;
			}

			decorated.remove_prefix(4);
			if (decorated.ends_with("@@")) {
				decorated.remove_suffix(2);
			}

			return ObjectInfo{ std::string{ decorated }, locator->offset };
		}

		// Stage two of the safety net: the address computed from the subobject
		// offset must itself carry a locator of the same class whose offset is
		// zero. That proves the arithmetic from both ends.
		RE::BSShader* ShaderBaseOf(const RE::ImageSpaceEffect* a_effect, const ObjectInfo& a_info) noexcept
		{
			const auto address =
				reinterpret_cast<std::uintptr_t>(a_effect) - a_info.subobjectOffset;
			auto* const candidate = reinterpret_cast<RE::BSShader*>(address);

			const auto whole = Describe(candidate);
			if (!whole.has_value()) {
				return nullptr;
			}
			if (whole->className != a_info.className || whole->subobjectOffset != 0) {
				return nullptr;
			}

			return candidate;
		}

		// Stage three: the fields have to look like a BSShader before we
		// believe any of this.
		bool LooksPlausible(const RE::BSShader* a_shader) noexcept
		{
			if (a_shader->shaderType < 0 || a_shader->shaderType > 0x40) {
				return false;
			}
			if (a_shader->fxpFilename == nullptr) {
				return false;
			}
			const auto techniques = a_shader->pixelShaders.size();
			return techniques > 0 && techniques < 64;
		}
	}

	std::vector<ImagespacePass> RunImagespaceCatalog()
	{
		std::vector<ImagespacePass> found;

		auto* const manager = RE::ImageSpaceManager::GetSingleton();
		if (manager == nullptr) {
			REX::ERROR("no image space manager, skipping the catalog");
			return found;
		}

		if (manager->effectList.empty()) {
			REX::WARN("the image space effect list is still empty");
			return found;
		}

		REX::INFO("=== image space catalog ===");
		REX::INFO("{} entries in the effect list", manager->effectList.size());

		std::uint32_t rejected = 0;

		for (auto* const effect : manager->effectList) {
			const auto info = Describe(effect);
			if (!info.has_value()) {
				++rejected;
				continue;
			}

			auto* const shader = ShaderBaseOf(effect, *info);
			if (shader == nullptr || !LooksPlausible(shader)) {
				REX::WARN("{}: rejected by the safety net", info->className);
				++rejected;
				continue;
			}

			ImagespacePass pass;
			pass.className = info->className;
			pass.shader = shader;

			// Only an unambiguous single technique is worth recording as a
			// target; anything else we log but do not offer for replacement.
			if (shader->pixelShaders.size() == 1) {
				auto* const entry = *shader->pixelShaders.begin();
				pass.slot = entry;
				pass.techniqueID = entry->id;
			}

			std::string techniques;
			for (auto* const entry : shader->pixelShaders) {
				techniques += std::format(
					"{}{}@{}",
					techniques.empty() ? "" : ",",
					entry->id,
					static_cast<const void*>(entry->shader));
			}

			REX::INFO(
				"{:<44} +{:<4} type {:<3} fxp {:<28} ps [{}]",
				pass.className,
				info->subobjectOffset,
				shader->shaderType,
				shader->fxpFilename,
				techniques);

			found.push_back(std::move(pass));
		}

		REX::INFO("{} passes described, {} rejected", found.size(), rejected);
		REX::INFO("=== end of catalog ===");

		return found;
	}
}
```

-   [ ] **Step 3: Bauen**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4-Fast
```

Erwartet: übersetzt ohne Warnung. Wo `/W4 /WX` an einem commonlibf4-Header anschlägt, wird die
Warnung **eng auf unserem Target** unterdrückt, mit einem Kommentar, der den Header nennt — nie
durch Lockern von `/WX`.

-   [ ] **Step 4: Commit**

```bash
git add src/Shader/ImagespaceCatalog.h src/Shader/ImagespaceCatalog.cpp
git commit -m "feat: catalog the engine image space passes"
```

---

## Task 6: `ShaderOverride` und die Verdrahtung

**Files:**

-   Create: `src/Shader/ShaderOverride.h`, `src/Shader/ShaderOverride.cpp`
-   Create: `src/Shader/ShaderPipeline.h`, `src/Shader/ShaderPipeline.cpp`
-   Modify: `src/Render/SwapChainHook.cpp`, `src/XSEPlugin.cpp`
-   Test: keiner auf dem Host; belegt wird im Spiel in Aufgabe 7.

**Interfaces:**

-   Consumes: `Shader::LoadSource` (1), `Shader::CompilePixelShader` (2), `Shader::FileWatch` (3),
    `Shader::RunImagespaceCatalog` (5), `Render::GetDevice` und `Render::SetDebugName` aus B1/B2.
-   Produces: `void Shader::StartPipeline()` und `void Shader::TickPipeline()`.

-   [ ] **Step 1: `ShaderOverride.h` anlegen**

```cpp
#pragma once

#include <RE/B/BSGraphics.h>
#include <REX/W32/D3D11.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace Shader
{
	/// Owns the replacement of one technique's pixel shader.
	///
	/// The engine's own shader is remembered but never referenced and never
	/// released - the same rule subproject B1 set for device, context and swap
	/// chain. Our own shader is ours alone.
	class PixelShaderOverride
	{
	public:
		/// Remembers the slot and whatever the engine put in it.
		void Adopt(RE::BSGraphics::PixelShader* a_slot) noexcept;

		/// Creates a shader from the bytecode and writes it into the slot. The
		/// previous own shader stays alive until the new one is in place.
		bool Install(std::span<const std::uint8_t> a_bytecode, std::string_view a_debugName) noexcept;

		/// Puts the engine's own shader back.
		void Restore() noexcept;

		/// Re-applies our shader if something else ended up in the slot. Cheap
		/// enough to run every frame: one comparison in the common case.
		void Guard() noexcept;

		[[nodiscard]] bool Installed() const noexcept { return _ours != nullptr; }

	private:
		RE::BSGraphics::PixelShader* _slot{ nullptr };
		REX::W32::ID3D11PixelShader* _original{ nullptr };
		REX::W32::ID3D11PixelShader* _ours{ nullptr };
	};
}
```

-   [ ] **Step 2: `ShaderOverride.cpp` schreiben**

```cpp
#include "Shader/ShaderOverride.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

namespace Shader
{
	void PixelShaderOverride::Adopt(RE::BSGraphics::PixelShader* a_slot) noexcept
	{
		_slot = a_slot;
		_original = a_slot != nullptr ? a_slot->shader : nullptr;
	}

	bool PixelShaderOverride::Install(
		std::span<const std::uint8_t> a_bytecode,
		std::string_view              a_debugName) noexcept
	{
		if (_slot == nullptr || a_bytecode.empty()) {
			return false;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			REX::ERROR("no device, cannot create the replacement shader");
			return false;
		}

		REX::W32::ID3D11PixelShader* created = nullptr;
		const auto                   hr = device->CreatePixelShader(
            a_bytecode.data(),
            a_bytecode.size(),
            nullptr,
            std::addressof(created));

		if (hr < 0 || created == nullptr) {
			REX::ERROR("CreatePixelShader failed with 0x{:08X}", static_cast<std::uint32_t>(hr));
			return false;
		}

		static_cast<void>(Render::SetDebugName(created, a_debugName));

		// The old one is released only after the slot points at the new one, so
		// there is no instant at which the engine could bind a dead shader.
		auto* const previous = _ours;
		_ours = created;
		_slot->shader = created;

		if (previous != nullptr) {
			previous->Release();
		}

		REX::INFO(
			"installed {} in place of {}",
			static_cast<const void*>(created),
			static_cast<const void*>(_original));

		return true;
	}

	void PixelShaderOverride::Restore() noexcept
	{
		if (_slot == nullptr) {
			return;
		}

		_slot->shader = _original;

		if (_ours != nullptr) {
			_ours->Release();
			_ours = nullptr;
		}
	}

	void PixelShaderOverride::Guard() noexcept
	{
		if (_slot == nullptr || _ours == nullptr) {
			return;
		}

		auto* const current = _slot->shader;
		if (current == _ours) {
			return;
		}

		// Something replaced what we put there. If it is not the shader we
		// remembered either, the engine reloaded its own - adopt the new one so
		// that a later Restore puts back something valid rather than a stale
		// pointer.
		if (current != _original) {
			REX::INFO(
				"the engine put {} in our slot, adopting it as the original",
				static_cast<const void*>(current));
			_original = current;
		}

		_slot->shader = _ours;
	}
}
```

-   [ ] **Step 3: `ShaderPipeline.h` anlegen**

```cpp
#pragma once

namespace Shader
{
	/// Starts the watcher thread. Called once, from kGameDataReady.
	void StartPipeline() noexcept;

	/// Called from Present, on the render thread. Runs the catalog on the first
	/// frames, picks up freshly compiled bytecode, and guards the slot.
	void TickPipeline() noexcept;
}
```

-   [ ] **Step 4: `ShaderPipeline.cpp` schreiben**

```cpp
#include "Shader/ShaderPipeline.h"

#include "Shader/ImagespaceCatalog.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderOverride.h"
#include "Shader/ShaderSource.h"
#include "Shader/ShaderWatcher.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace Shader
{
	namespace
	{
		constexpr auto kShaderFile = "ImagespaceCopy.hlsl";
		constexpr auto kEntryPoint = "main";
		constexpr auto kDebugName = "FO4CS_PS_ImagespaceCopy"sv;

		// The pass we want, and the rule for picking a stand-in when it is not
		// there: the first proven pass with exactly one technique.
		constexpr auto kPreferredClass = "BSImagespaceShaderCopy"sv;

		// The effect list may not be populated on the very first frame. Asking
		// once a second for ten seconds is generous, and spacing the attempts
		// out keeps a persistently empty list from filling the log with the
		// same warning six hundred times.
		constexpr std::uint64_t kCatalogInterval = 60;
		constexpr std::uint64_t kCatalogAttempts = 10;

		constexpr auto kPollInterval = std::chrono::milliseconds{ 500 };

		std::filesystem::path ShaderRoot()
		{
			// Derived from the game module rather than the working directory:
			// the working directory is not ours to rely on.
			const std::filesystem::path exe = REX::FModule::GetExecutingModule().GetFileName();
			return exe.parent_path() / "Data" / "Shaders" / "FO4";
		}

		struct Pending
		{
			std::vector<std::uint8_t> bytecode;
			bool                      valid{ false };
		};

		std::mutex          g_mutex;
		Pending             g_pending;
		std::atomic<bool>   g_hasPending{ false };
		std::atomic<bool>   g_stop{ false };
		std::atomic<bool>   g_armed{ false };

		PixelShaderOverride g_override;
		std::uint64_t       g_frames = 0;
		std::uint64_t       g_catalogTries = 0;
		bool                g_catalogDone = false;

		// Reads, splices and compiles. Creating the D3D object is left to the
		// render thread: keeping every D3D call on one thread is one fewer
		// assumption to be wrong about.
		void CompileAndPublish(FileWatch& a_watch)
		{
			const auto source = LoadSource(ShaderRoot(), kShaderFile);
			if (!source.has_value()) {
				REX::WARN("{}", source.error());
				return;
			}

			a_watch.Reset(source->files);

			const auto compiled = CompilePixelShader(source->text, kShaderFile, kEntryPoint);
			if (!compiled.diagnostics.empty()) {
				REX::WARN("shader diagnostics:\n{}", compiled.diagnostics);
			}

			if (!compiled.Succeeded()) {
				// Whatever is installed stays installed. A typo must not be
				// able to produce a black screen.
				REX::ERROR("{} did not compile, keeping the shader in place", kShaderFile);
				return;
			}

			{
				const std::scoped_lock lock{ g_mutex };
				g_pending.bytecode = compiled.bytecode;
				g_pending.valid = true;
			}
			g_hasPending.store(true, std::memory_order_release);
		}

		void WatcherLoop()
		{
			FileWatch watch;
			bool      loadedOnce = false;

			while (!g_stop.load(std::memory_order_acquire)) {
				if (g_armed.load(std::memory_order_acquire)) {
					if (!loadedOnce) {
						CompileAndPublish(watch);
						loadedOnce = true;
					} else if (watch.Poll()) {
						REX::INFO("{} changed, recompiling", kShaderFile);
						CompileAndPublish(watch);
					}
				}

				std::this_thread::sleep_for(kPollInterval);
			}
		}

		// Picks the pass to replace: the preferred class if the catalog found
		// it with a single technique, otherwise the first pass that has one.
		const ImagespacePass* ChoosePass(const std::vector<ImagespacePass>& a_passes)
		{
			for (const auto& pass : a_passes) {
				if (pass.className == kPreferredClass && pass.slot != nullptr) {
					return std::addressof(pass);
				}
			}

			for (const auto& pass : a_passes) {
				if (pass.slot != nullptr) {
					REX::WARN(
						"{} was not available, falling back to {}",
						kPreferredClass,
						pass.className);
					return std::addressof(pass);
				}
			}

			return nullptr;
		}

		void RunCatalogOnce()
		{
			const auto passes = RunImagespaceCatalog();
			if (passes.empty()) {
				return;
			}

			const auto* const chosen = ChoosePass(passes);
			if (chosen == nullptr) {
				REX::ERROR("no pass with a single technique, nothing to replace");
				g_catalogDone = true;
				return;
			}

			REX::INFO(
				"replacing {} technique {}",
				chosen->className,
				chosen->techniqueID);

			g_override.Adopt(chosen->slot);
			g_catalogDone = true;
			g_armed.store(true, std::memory_order_release);
		}
	}

	void StartPipeline() noexcept
	{
		REX::INFO("shader root is {}", ShaderRoot().generic_string());

		// Detached rather than joined: F4SE gives no unload path to join in,
		// and the loop owns nothing the process needs back.
		std::thread{ WatcherLoop }.detach();
	}

	void TickPipeline() noexcept
	{
		++g_frames;

		if (!g_catalogDone && g_catalogTries < kCatalogAttempts &&
			g_frames % kCatalogInterval == 0) {
			++g_catalogTries;
			RunCatalogOnce();

			if (!g_catalogDone && g_catalogTries == kCatalogAttempts) {
				REX::ERROR(
					"no usable image space pass after {} attempts, giving up",
					kCatalogAttempts);
			}
		}

		if (g_hasPending.exchange(false, std::memory_order_acq_rel)) {
			std::vector<std::uint8_t> bytecode;
			{
				const std::scoped_lock lock{ g_mutex };
				bytecode = std::move(g_pending.bytecode);
				g_pending.valid = false;
			}

			static_cast<void>(g_override.Install(bytecode, kDebugName));
		}

		g_override.Guard();
	}
}
```

-   [ ] **Step 5: In den Present-Hook einhängen**

In `src/Render/SwapChainHook.cpp`, im `Present`-Ersatz, direkt nach dem Frame-Zähler und **vor**
dem `MarkerScope` — so liegt unsere Arbeit innerhalb des benannten Blocks im Capture:

```cpp
			Shader::TickPipeline();
```

Dazu oben `#include "Shader/ShaderPipeline.h"`.

-   [ ] **Step 6: Beim Spielstart starten**

In `src/XSEPlugin.cpp`, im `kGameDataReady`-Zweig, nach `Render::InstallSwapChainHook();`:

```cpp
			Shader::StartPipeline();
```

Dazu oben `#include "Shader/ShaderPipeline.h"`.

-   [ ] **Step 7: Bauen und ausliefern**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: alle vier Host-Tests grün, `verify-plugin.ps1` ohne Beanstandung.

-   [ ] **Step 8: Commit**

```bash
git add src/Shader/ShaderOverride.h src/Shader/ShaderOverride.cpp src/Shader/ShaderPipeline.h src/Shader/ShaderPipeline.cpp src/Render/SwapChainHook.cpp src/XSEPlugin.cpp
git commit -m "feat: replace an image space pixel shader"
```

---

## Task 7: Erster Nachweis im Spiel

**Files:** keine Änderung erwartet. Was hier auffällt, wird in Aufgabe 8 nachgezogen.

**Interfaces:** keine.

Diese Aufgabe braucht den Nutzer. Anweisungen an ihn bleiben konkret: welche Datei, welcher Knopf,
was danach passieren soll.

-   [ ] **Step 1: Spiel über `f4se_loader.exe` starten**

Nicht über Steam — sonst lädt kein F4SE-Plugin und es entsteht nicht einmal der Logordner. Ins
Spiel laden, bis die Spielwelt sichtbar ist.

-   [ ] **Step 2: Das Log lesen**

`C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`

Erwartet, in dieser Reihenfolge:

-   `shader root is …/Data/Shaders/FO4`
-   `=== image space catalog ===` mit einer Zeilenzahl größer null
-   je Pass eine Zeile mit Klassenname, `+offset`, `type`, `fxp` und den Technik-IDs
-   `N passes described, M rejected`
-   `replacing BSImagespaceShaderCopy technique …` — oder die Rückfall-Warnung
-   `installed 0x… in place of 0x…`

Bleibt es bei `the image space effect list is still empty` — höchstens zehnmal, im Abstand von je
60 Frames — ist die Annahme aus Abschnitt 11 der Spec widerlegt; dann in Aufgabe 8 den Zeitpunkt
nach hinten schieben statt die Annahme zu beschönigen.

-   [ ] **Step 3: Den Farbstich sehen**

Erwartet: das Bild ist sichtbar rötlich getönt. Screenshot machen.

Ist das Bild stattdessen schwarz, verzerrt oder unverändert, sagt das Log, welcher Pass getroffen
wurde — der gewählte Pass ist dann der falsche, und der Katalog nennt die Alternativen. Das ist
der in der Spec vorgesehene Rückfallweg, kein Fehlschlag des Ansatzes.

-   [ ] **Step 4: Gegenprobe ohne Datei**

`Data/Shaders/FO4/ImagespaceCopy.hlsl` umbenennen, Spiel neu starten. Erwartet: Bild normal, im
Log `cannot read …ImagespaceCopy.hlsl`, kein Absturz. Datei zurückbenennen.

-   [ ] **Step 5: RenderDoc-Capture**

RenderDoc starten, **Capture Child Processes** einschalten, `f4se_loader.exe` als Programm
angeben, im Spiel einen Frame aufnehmen. Die `.rdc` liegt unter `%TEMP%\RenderDoc\`.

Die Prüfung übernimmt **nicht** der Nutzer über die Oberfläche, sondern die Zeichenkettensuche in
der Datei — nach `FO4CS_PS_ImagespaceCopy`. So ist es in B1 und B2 zweimal zuverlässig gelaufen.

-   [ ] **Step 6: Befunde festhalten**

Alles Beobachtete roh notieren, bevor irgendetwas geändert wird. Es ist das Rohmaterial für
Aufgabe 9.

Kein Commit in dieser Aufgabe.

---

## Task 8: Hot-Reload und Robustheit belegen

**Files:** je nach Befund aus Aufgabe 7 Nachbesserungen an `src/Shader/*`.

**Interfaces:** keine neuen.

-   [ ] **Step 1: Tönung im laufenden Spiel ändern**

Spiel läuft. In `Data/Shaders/FO4/ImagespaceCopy.hlsl` `ProofTint` auf
`float3(0.6, 0.6, 1.0)` ändern und speichern. Alt-Tab ins Spiel.

Erwartet, innerhalb von rund einer Sekunde: das Bild wird bläulich, und im Log stehen
`ImagespaceCopy.hlsl changed, recompiling` sowie eine neue `installed …`-Zeile. **Kein Neustart.**

-   [ ] **Step 2: Syntaxfehler einbauen**

In derselben Datei `return float4(source.rgb * ProofTint, source.a);` durch
`return float4(source.rgb * ProofTint, source.a)` ersetzen — das fehlende Semikolon. Speichern,
Alt-Tab.

Erwartet: das Bild bleibt bläulich, das Spiel läuft, und im Log steht der Compilerfehler mit
Dateiname und Zeilennummer, gefolgt von `did not compile, keeping the shader in place`.

-   [ ] **Step 3: Fehler beheben**

Semikolon zurück, speichern, Alt-Tab. Erwartet: Übersetzung gelingt, Bild bleibt bläulich, eine
neue `installed …`-Zeile.

-   [ ] **Step 4: Tönung auf den Ausgangswert zurücksetzen**

`ProofTint` wieder auf `float3(1.0, 0.6, 0.6)`, auch in `package/Shaders/FO4/ImagespaceCopy.hlsl`
im Repo, damit ausgeliefertes und versioniertes HLSL übereinstimmen.

-   [ ] **Step 5: Nachbesserungen aus Aufgabe 7 und 8 einarbeiten**

Falls nötig. Danach Host-Tests erneut laufen lassen:

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
```

-   [ ] **Step 6: Commit**

Nur wenn Aufgabe 7 oder 8 Änderungen erzwungen haben:

```bash
git add -A
git commit -m "fix: <was die Spielprüfung erzwungen hat>"
```

---

## Task 9: Befunddokument, Roadmap, Abschluss

**Files:**

-   Create: `docs/fallout4-port/imagespace-passes.md`
-   Modify: `docs/fallout4-port/ROADMAP.md`

**Interfaces:** keine.

-   [ ] **Step 1: Befunddokument schreiben**

`docs/fallout4-port/imagespace-passes.md`, das Gegenstück zu `render-targets.md`. Inhalt aus dem
Katalog-Protokoll von Aufgabe 7:

-   Zahl der Einträge in `effectList`, davon belegt und verworfen.
-   Eine Tabelle: Klassenname, Subobjekt-Offset, `shaderType`, `fxpFilename`, Technik-IDs.
-   Der gemessene Offset des `ImageSpaceEffect`-Subobjekts — die Zahl, die in Abschnitt 11 der
    Spec noch eine Annahme war.
-   Welcher Pass ersetzt wurde und warum dieser.

-   [ ] **Step 2: Roadmap nachziehen**

In `docs/fallout4-port/ROADMAP.md`:

-   Zeile C in der Zerlegungstabelle auf **abgeschlossen** setzen.
-   Einen Abschnitt „Aus Teilprojekt C bestätigt" nach dem Muster der Abschnitte zu A, B1 und B2
    anlegen, mit den gemessenen Werten.
-   Die beiden Korrekturen aufnehmen: C schaltet das Trampolin **nicht** ein und braucht
    `REL::THook` nicht; `BSRenderPass` wird für C **nicht** gebraucht und bleibt eine offene Lücke
    für später.
-   Den dritten Rückgabe-Befund an commonlib-shared vermerken: `REX::W32::ID3DInclude` erbt
    fälschlich von `IUnknown`.

-   [ ] **Step 3: Vollständigen Lauf gegen den kanonischen Preset**

```pwsh
& "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: sechs Tests grün (`Runtime`, `VTablePatch`, `FormatNames`, `ShaderSource`,
`ShaderCompiler`, `ShaderWatcher`), Plugin-Prüfung ohne Beanstandung.

-   [ ] **Step 4: Commit**

```bash
git add docs/fallout4-port/imagespace-passes.md docs/fallout4-port/ROADMAP.md
git commit -m "docs: record subproject c acceptance"
```

-   [ ] **Step 5: Abschluss**

`superpowers:finishing-a-development-branch` aufrufen. Erwartete Wahl nach bisherigem Muster:
lokal nach `dev` mergen (Fast-Forward), Feature-Branch löschen, Push **nur** auf ausdrückliche
Ansage.

---

## Self-Review

**Spec-Abdeckung**

| Spec-Abschnitt                       | Aufgabe        |
| ------------------------------------ | -------------- |
| 5.1 `ShaderSource`                   | 1              |
| 5.2 `ShaderCompiler`                 | 2              |
| 5.3 `ImagespaceCatalog`              | 5              |
| 5.4 `ShaderOverride`                 | 6              |
| 5.5 `ShaderWatcher`                  | 3              |
| 3 Kopierschritt im Build             | 4              |
| 6 Ablauf und Zeitpunkte              | 6 (Steps 4–6)  |
| 7 Nebenläufigkeit und Besitz         | 6 (Steps 2, 4) |
| 8 Sicherheitsnetz, drei Stufen       | 5 (Step 2)     |
| 9 Fehlerbehandlung, alle sechs Fälle | 6 (Step 4), 8  |
| 10.1 Host-Tests                      | 1, 2, 3        |
| 10.3 Abnahmekriterien 1–5            | 7, 8           |
| 12 Befunddokument und Korrekturen    | 9              |

**Abweichungen von der Spec, bewusst und begründet**

-   Die Spec beschreibt in 7 einen Watcher-Thread, der bereits `CreatePixelShader` aufruft. Der
    Plan lässt den Thread bei `D3DCompile` aufhören und legt die D3D-Erzeugung auf den
    Render-Thread. Das gibt dieselbe Nebenläufigkeit her und stützt sich auf eine Annahme weniger
    — die freie Threadsicherheit von `ID3D11Device` muss dann gar nicht mehr tragen.

**Typkonsistenz geprüft:** `SourceText.files` (1) ist `std::vector<std::filesystem::path>` und
passt auf `FileWatch::Reset(std::span<const std::filesystem::path>)` (3). `CompileResult.bytecode`
(2) ist `std::vector<std::uint8_t>` und passt auf
`PixelShaderOverride::Install(std::span<const std::uint8_t>, std::string_view)` (6).
`ImagespacePass::slot` (5) ist `RE::BSGraphics::PixelShader*` und passt auf
`PixelShaderOverride::Adopt(RE::BSGraphics::PixelShader*)` (6).

**Platzhalter:** keine. Jeder Codeschritt enthält den Code, jeder Prüfschritt das erwartete
Ergebnis, jeder Mutationsschritt den vorher benannten Fehlschlag.
