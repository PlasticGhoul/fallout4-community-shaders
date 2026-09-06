# Teilprojekt F2 — Screen-Space Shadows: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Der erste eigene Renderpass des Ports — ein Kontaktschatten, der die Szenentiefe liest,
eine Maske erzeugt und die beiden Lichtziele der Engine damit dämpft.

**Architecture:** Ein vtable-Patch auf `BSDFCompositeShader::SetupTechnique` gibt uns einmal je
Frame Kontrolle zwischen G-Buffer und Composite. Dort läuft Bends Raymarch als Compute-Shader über
`FO4_DS_002` in eine eigene `R8_UNORM`-Maske, und eine Vollbildzeichnung mit Multiplikationsmischung
trägt sie auf `FO4_RT_058` und `FO4_RT_059` auf. Keine Engine-Permutation wird ersetzt.

**Tech Stack:** C++23, MSVC, CommonLibF4 (`REX::W32`-D3D11, kein `<d3d11.h>`), `D3DCompile`,
ImGui-freies Feature, HLSL `cs_5_0` / `vs_5_0` / `ps_5_0`, Bend SSS (Apache-2.0).

**Spec:** `docs/superpowers/specs/2026-09-06-fallout4-f2-screen-space-shadows-design.md`

## Global Constraints

-   **C++23, MSVC, `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`.** Eine neue Warnung aus einem
    Fremdheader wird eng auf unserem Ziel unterdrückt, mit einem Kommentar, der den Header nennt —
    nie durch Lockern von `/WX`.
-   **`<d3d11.h>`, `<dxgi.h>` und `<Windows.h>` sind verboten.** D3D- und DXGI-Typen kommen aus
    `REX::W32`; der PCH bringt nur `REL` und `REX` mit, also `REX/W32/D3D11.h`, `DXGI.h`,
    `D3DCOMPILER.h` selbst einbinden. Fehlendes USER32 steht in `src/Menu/Win32.h`.
-   **Kein ImGui in `src/Features/`.** Ein Feature bekommt seine Oberfläche allein dadurch, daß es
    Einstellungen deklariert.
-   **Einstellungspfade sind `"Block/Key"`**, ein Schrägstrich, beide Hälften nicht leer — der Store
    adressiert als JSON-Pointer. Ganze Zahlen werden als `double` gespeichert; `REX::TJsonSetting`
    liest keinen Ganzzahltyp je aus der Datei.
-   **Einstellungen werden bei jeder Verwendung frisch über `Settings::Get*` gelesen.** Es gibt keine
    Änderungsbenachrichtigung.
-   **`Setup` schlägt nur an Dingen fehl, die wieder fehlschlügen.** Warten auf die Engine gehört in
    `Frame`.
-   **`Shutdown` läuft im laufenden Spiel**, nicht nur beim Beenden, und muß nach einem halb
    gescheiterten `Setup` aufrufbar sein.
-   **Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen.** Vorher wird geprüft, daß der
    Bau geglückt ist — eine Mutation, die nicht übersetzt, hinterläßt das alte Executable. Eine
    Mutation, die nicht fällt, ist ein Befund über den Test. Mutationen mit dem Edit-Werkzeug setzen
    **und zurücknehmen**, nie mit `git checkout`.
-   **Commits:** Conventional Commits, Titel höchstens 50 Zeichen, Rumpf auf 72 umbrochen, Englisch.
    Dokumente unter `docs/` sind Deutsch.
-   **Branch:** `port/f2-screen-space-shadows`, bereits angelegt. Nicht nach `dev` mergen, bevor die
    Abnahme steht.
-   **Bauen:**
    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure
    ```
    Für ein einzelnes Ziel `--target <Name>`. Der Preset `FO4-Fast` läuft aus einer normalen Shell
    **nicht**.
-   **Quelltext niemals durch eine Shell-Pipeline schreiben.** Für Dateiinhalte Write und Edit; Bash
    ist für Bauen, Testen, Git und Suchen.

---

## Dateiübersicht

| Datei                                                                                   | Verantwortung                                                        |
| --------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| `src/Shader/ShaderCompiler.{h,cpp}`                                                     | **ändern** — Profil, Defines, abschaltbare Warnungs-als-Fehler       |
| `src/Render/PhaseDispatcher.{h,cpp}`                                                    | **neu** — wer wird einmal je Frame gerufen; kennt kein D3D           |
| `src/Render/FramePhase.{h,cpp}`                                                         | **neu** — der vtable-Patch, der den Dispatcher antreibt              |
| `src/Render/Targets.{h,cpp}`                                                            | **neu** — Engine-Ziele über ihren Slot                               |
| `src/Render/Resources.{h,cpp}`                                                          | **neu** — eigene Textur und Konstantenpuffer                         |
| `src/Render/StateGuard.{h,cpp}`                                                         | **neu** — Pipeline-Zustand sichern und zurückgeben                   |
| `src/Render/FullscreenPass.{h,cpp}`                                                     | **neu** — Dreieck ohne Vertexpuffer                                  |
| `src/Features/ScreenSpaceShadows/bend_sss_cpu.h`                                        | **neu, übernommen** — Bends CPU-Teil, unverändert                    |
| `src/Features/ScreenSpaceShadows/BendDispatch.{h,cpp}`                                  | **neu** — die eine Übersetzungseinheit, die den Kopf einbindet       |
| `src/Features/ScreenSpaceShadows.{h,cpp}`                                               | **neu** — das Feature                                                |
| `src/Feature/FeatureSystem.cpp`                                                         | **ändern** — Registrierung, Phase installieren                       |
| `src/XSEPlugin.cpp`                                                                     | **ändern** — `InstallFramePhase` vor dem Present-Hook                |
| `package/Shaders/FO4/Fullscreen.hlsl`                                                   | **neu** — geteilter Vollbild-Vertex-Shader, Basis                    |
| `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl`    | **neu** — angepaßt                                                   |
| `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli` | **neu, übernommen**                                                  |
| `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/Modulate.hlsl`      | **neu** — der Pixel-Shader der Modulation                            |
| `tests/ShaderCompilerTests.cpp`                                                         | **ändern** — Profile, Defines, abgelehntes Profil                    |
| `tests/PhaseDispatcherTests.cpp`                                                        | **neu**                                                              |
| `tests/BendDispatchTests.cpp`                                                           | **neu**                                                              |
| `CMakeLists.txt`                                                                        | **ändern** — Quellen und zwei Testziele                              |
| `tools/verify-package.ps1`                                                              | **ändern** — `Shaders/FO4/Fullscreen.hlsl` in der Basis erwarten     |
| `docs/fallout4-port/ROADMAP.md`                                                         | **ändern** — Status und der Abschnitt „Aus Teilprojekt F2 bestätigt" |

---

## Task 1: Der Übersetzer lernt Profile, Defines und fremden Quelltext

**Files:**

-   Modify: `src/Shader/ShaderCompiler.h`
-   Modify: `src/Shader/ShaderCompiler.cpp`
-   Test: `tests/ShaderCompilerTests.cpp`

**Interfaces:**

-   Consumes: nichts.
-   Produces:

    ```cpp
    namespace Shader
    {
        struct ShaderDefine { std::string name; std::string value; };

        CompileResult Compile(
            std::string_view a_source,
            const std::string& a_sourceName,
            const std::string& a_entryPoint,
            const std::string& a_profile,
            std::span<const ShaderDefine> a_defines = {},
            bool a_warningsAsErrors = true);

        CompileResult CompilePixelShader(std::string_view, const std::string&, const std::string&);
        CompileResult CompileVertexShader(std::string_view, const std::string&, const std::string&);
        CompileResult CompileComputeShader(
            std::string_view, const std::string&, const std::string&,
            std::span<const ShaderDefine> = {}, bool a_warningsAsErrors = true);
    }
    ```

**Warum `a_warningsAsErrors`:** Bends `bend_sss_gpu.hlsli` ist fremder Quelltext, rechnet durchgehend
in `half` und wird unter `D3DCOMPILE_WARNINGS_ARE_ERRORS` mit hoher Wahrscheinlichkeit nicht
übersetzen. Das ist dieselbe Lage wie ein fremder C++-Header unter `/W4 /WX`, und die Regel ist
dieselbe: eng abschalten, an genau der Aufrufstelle, mit einem Kommentar, der den fremden Quelltext
nennt — nicht die Voreinstellung für alle senken.

-   [ ] **Step 1: Die fehlschlagenden Tests schreiben**

An `tests/ShaderCompilerTests.cpp` anhängen. Die Quelltexte oben zu den vorhandenen `constexpr`
in den anonymen Namensraum:

```cpp
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

	// Refuses to compile unless WANTED is defined, so the define is proven to
	// arrive rather than merely accepted.
	constexpr std::string_view kNeedsDefine =
		"#ifndef WANTED\n"
		"#error WANTED was not defined\n"
		"#endif\n"
		"float4 main() : SV_TARGET\n"
		"{\n"
		"    return float4(WANTED, 0.0, 0.0, 1.0);\n"
		"}\n";

	// A warning, not an error: implicit truncation. With warnings as errors it
	// must fail, without them it must succeed.
	constexpr std::string_view kWarningOnly =
		"float4 main() : SV_TARGET\n"
		"{\n"
		"    float3 value = float4(1.0, 2.0, 3.0, 4.0);\n"
		"    return float4(value, 1.0);\n"
		"}\n";
```

Und in `main()`, hinter den vorhandenen Blöcken:

```cpp
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
		const auto strict =
			Shader::Compile(kWarningOnly, "warn.hlsl", "main", "ps_5_0", {}, true);
		Check(!strict.Succeeded(), "warnings are errors by default");

		const auto lenient =
			Shader::Compile(kWarningOnly, "warn.hlsl", "main", "ps_5_0", {}, false);
		Check(lenient.Succeeded(), "and can be relaxed for foreign source");
	}

	{
		const auto result = Shader::Compile(kMinimalVertex, "vertex.hlsl", "main", "gs_5_0");
		Check(!result.Succeeded(), "an unsupported profile is refused");
		Check(Contains(result.diagnostics, "gs_5_0"), "and the refusal names the profile");
		Check(result.bytecode.empty(), "a refused compile produces no bytecode");
	}
```

-   [ ] **Step 2: Bauen und laufen lassen, um den Fehlschlag zu sehen**

```pwsh
cmake --build --preset FO4 --target ShaderCompilerTests
```

Erwartet: **Übersetzungsfehler**, `Compile`, `CompileComputeShader`, `CompileVertexShader` und
`ShaderDefine` sind nicht deklariert. Das ist der Fehlschlag, nicht ein fehlgeschlagener Test.

-   [ ] **Step 3: Den Header erweitern**

`src/Shader/ShaderCompiler.h` — Einbindungen um `<span>` ergänzen, dann hinter `CompileResult`:

```cpp
	/// One /D for the compiler. Both strings must outlive the Compile call:
	/// D3D_SHADER_MACRO holds pointers, not copies.
	struct ShaderDefine
	{
		std::string name;
		std::string value;
	};

	/// Compiles one shader against a_profile.
	///
	/// Only ps_5_0, vs_5_0 and cs_5_0 are accepted. An unknown profile is
	/// refused with a diagnostic naming it rather than handed to D3DCompile:
	/// a typo would otherwise come back as an unrecognisable HRESULT.
	///
	/// a_warningsAsErrors is true for everything we write ourselves, the same
	/// standard /W4 /WX holds our C++ to. It exists to be passed false at the
	/// one call site that compiles third-party source - see the Bend raymarch
	/// in ScreenSpaceShadows - and nowhere else.
	[[nodiscard]] CompileResult Compile(
		std::string_view a_source,
		const std::string& a_sourceName,
		const std::string& a_entryPoint,
		const std::string& a_profile,
		std::span<const ShaderDefine> a_defines = {},
		bool a_warningsAsErrors = true);

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
```

Der Kommentarblock über `CompilePixelShader`, der den fehlenden Include-Handler begründet, wandert
über `Compile` — er gilt jetzt für alle drei.

-   [ ] **Step 4: Die Umsetzung**

`src/Shader/ShaderCompiler.cpp` — `kFlags` in zwei Teile trennen und `Compile` einführen:

```cpp
		constexpr std::uint32_t kBaseFlags =
			REX::W32::D3DCOMPILE_ENABLE_STRICTNESS |
			REX::W32::D3DCOMPILE_OPTIMIZATION_LEVEL3;

		bool IsSupportedProfile(std::string_view a_profile) noexcept
		{
			return a_profile == "ps_5_0" || a_profile == "vs_5_0" || a_profile == "cs_5_0";
		}
```

`CompilePixelShader` ruft nur noch durch:

```cpp
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

		// The array has to outlive the call and be null terminated. The strings
		// belong to the caller's span; D3D_SHADER_MACRO stores pointers.
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
```

Einbindungen in der `.cpp` um `<vector>` ergänzen.

-   [ ] **Step 5: Bauen und alle Tests laufen lassen**

```pwsh
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure -R ShaderCompiler
```

Erwartet: alle Prüfungen `ok`, `0 failure(s)`. Insbesondere muß der vorhandene `kTruncating`-Test
weiter fehlschlagen — die Voreinstellung hat sich nicht geändert.

-   [ ] **Step 6: Die Tests brechen, um zu belegen, daß sie greifen**

Drei Mutationen, einzeln, jede mit dem Edit-Werkzeug gesetzt und zurückgenommen. Vor jedem Lauf
prüfen, daß der Bau geglückt ist.

| Mutation                                                               | Muß fallen                                              |
| ---------------------------------------------------------------------- | ------------------------------------------------------- |
| In `Compile` `a_defines.empty() ? nullptr : macros.data()` → `nullptr` | „a define reaches the shader"                           |
| `IsSupportedProfile` gibt immer `true` zurück                          | „an unsupported profile is refused" und die zwei danach |
| `flags` ignoriert `a_warningsAsErrors` und nimmt immer `kBaseFlags`    | „warnings are errors by default"                        |

-   [ ] **Step 7: Commit**

```bash
git add src/Shader/ShaderCompiler.h src/Shader/ShaderCompiler.cpp tests/ShaderCompilerTests.cpp
git commit -m "feat: teach the compiler profiles and defines"
```

---

## Task 2: Wer einmal je Frame gerufen wird

**Files:**

-   Create: `src/Render/PhaseDispatcher.h`
-   Create: `src/Render/PhaseDispatcher.cpp`
-   Create: `tests/PhaseDispatcherTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts.
-   Produces:

    ```cpp
    namespace Render
    {
        class PhaseDispatcher
        {
        public:
            using Token = std::uint32_t;
            static constexpr Token kNoToken = 0;
            static constexpr std::size_t kMaxSubscribers = 16;

            Token Subscribe(std::string_view a_name, std::function<void()> a_callback);
            void Unsubscribe(Token a_token) noexcept;
            [[nodiscard]] std::size_t Count() const noexcept;
            bool Dispatch(std::uint64_t a_frame);
        };
    }
    ```

**Warum getrennt vom Patch:** F1 hat gelernt, daß ein Zustandsautomat, der den Renderer anfaßt,
nicht mehr ohne Spiel prüfbar ist — der Versuch, `PassScope` direkt in `FeatureRegistry` zu
benutzen, hat deren Tests zerrissen. Diese Klasse kennt deshalb weder D3D noch die Engine. Ein
festes Feld statt eines `std::vector` ist kein Geiz: es macht Eintragen während eines laufenden
Durchlaufs ungefährlich, weil nichts umzieht, während ein `std::function` gerade ausgeführt wird.

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

`tests/PhaseDispatcherTests.cpp`:

```cpp
#include "Render/PhaseDispatcher.h"

#include <cstdio>
#include <string>

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
}

int main()
{
	{
		Render::PhaseDispatcher dispatcher;
		Check(dispatcher.Count() == 0, "a fresh dispatcher has no subscribers");
		Check(!dispatcher.Dispatch(1), "and dispatching to nobody does not run");
	}

	{
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		const auto token = dispatcher.Subscribe("counter", [&runs] { ++runs; });

		Check(token != Render::PhaseDispatcher::kNoToken, "subscribing hands out a token");
		Check(dispatcher.Count() == 1, "and counts the subscriber");

		Check(dispatcher.Dispatch(1), "the first call of a frame dispatches");
		Check(runs == 1, "and runs the subscriber once");

		Check(!dispatcher.Dispatch(1), "the second call of the same frame does not");
		Check(runs == 1, "and does not run it again");

		Check(dispatcher.Dispatch(2), "the first call of the next frame does");
		Check(runs == 2, "and runs it a second time");
	}

	{
		// Frame zero is a legitimate frame number, not "nothing seen yet".
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		static_cast<void>(dispatcher.Subscribe("counter", [&runs] { ++runs; }));

		Check(dispatcher.Dispatch(0), "frame zero dispatches");
		Check(!dispatcher.Dispatch(0), "and does not dispatch twice");
		Check(runs == 1, "so the subscriber ran exactly once");
	}

	{
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		const auto token = dispatcher.Subscribe("counter", [&runs] { ++runs; });

		static_cast<void>(dispatcher.Dispatch(1));
		dispatcher.Unsubscribe(token);

		Check(dispatcher.Count() == 0, "unsubscribing removes the subscriber");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(runs == 1, "and it does not run again");
	}

	{
		Render::PhaseDispatcher dispatcher;
		std::string order;
		static_cast<void>(dispatcher.Subscribe("first", [&order] { order += "a"; }));
		static_cast<void>(dispatcher.Subscribe("second", [&order] { order += "b"; }));

		static_cast<void>(dispatcher.Dispatch(1));
		Check(order == "ab", "subscribers run in subscription order");
	}

	{
		// The one that would crash a naive implementation: a subscriber that
		// removes itself while it is the thing being executed.
		Render::PhaseDispatcher dispatcher;
		int runs = 0;
		Render::PhaseDispatcher::Token self = Render::PhaseDispatcher::kNoToken;
		self = dispatcher.Subscribe("suicide", [&] {
			++runs;
			dispatcher.Unsubscribe(self);
		});

		static_cast<void>(dispatcher.Dispatch(1));
		Check(runs == 1, "a self-removing subscriber runs once");
		Check(dispatcher.Count() == 0, "and is gone afterwards");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(runs == 1, "and stays gone");
	}

	{
		// Removing a later subscriber from an earlier one must take effect in
		// this very dispatch, not only in the next.
		Render::PhaseDispatcher dispatcher;
		int second = 0;
		Render::PhaseDispatcher::Token later = Render::PhaseDispatcher::kNoToken;
		static_cast<void>(dispatcher.Subscribe("first", [&] { dispatcher.Unsubscribe(later); }));
		later = dispatcher.Subscribe("second", [&second] { ++second; });

		static_cast<void>(dispatcher.Dispatch(1));
		Check(second == 0, "a subscriber removed during dispatch does not run");
	}

	{
		// Subscribing from inside a dispatch must not run the newcomer in the
		// same frame, and must not disturb the one running.
		Render::PhaseDispatcher dispatcher;
		int newcomer = 0;
		static_cast<void>(dispatcher.Subscribe("adder", [&] {
			static_cast<void>(dispatcher.Subscribe("newcomer", [&newcomer] { ++newcomer; }));
		}));

		static_cast<void>(dispatcher.Dispatch(1));
		Check(newcomer == 0, "a subscriber added during dispatch waits for the next frame");

		static_cast<void>(dispatcher.Dispatch(2));
		Check(newcomer == 1, "and runs then");
	}

	{
		Render::PhaseDispatcher dispatcher;
		bool allTaken = true;
		for (std::size_t i = 0; i < Render::PhaseDispatcher::kMaxSubscribers; ++i) {
			allTaken = allTaken &&
			           dispatcher.Subscribe("filler", [] {}) != Render::PhaseDispatcher::kNoToken;
		}

		Check(allTaken, "every slot up to the cap is handed out");
		Check(
			dispatcher.Subscribe("one too many", [] {}) == Render::PhaseDispatcher::kNoToken,
			"the cap refuses the next subscriber");
	}

	{
		Render::PhaseDispatcher dispatcher;
		dispatcher.Unsubscribe(Render::PhaseDispatcher::kNoToken);
		dispatcher.Unsubscribe(4711);
		Check(dispatcher.Count() == 0, "unsubscribing an unknown token is harmless");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 2: Das Testziel in CMake eintragen**

In `CMakeLists.txt`, hinter dem `ProfilerStatsTests`-Block und im selben Muster:

```cmake
    add_executable(
        PhaseDispatcherTests
        "${CMAKE_SOURCE_DIR}/tests/PhaseDispatcherTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/PhaseDispatcher.cpp"
    )

    target_include_directories(
        PhaseDispatcherTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(PhaseDispatcherTests PRIVATE cxx_std_23)
    target_precompile_headers(
        PhaseDispatcherTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(PhaseDispatcherTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            PhaseDispatcherTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME PhaseDispatcher COMMAND PhaseDispatcherTests)
```

Die neue Quelldatei außerdem in die Quellenliste des Hauptziels aufnehmen, dort wo die übrigen
`src/Render/*.cpp` stehen.

-   [ ] **Step 3: Bauen, um den Fehlschlag zu sehen**

```pwsh
cmake --build --preset FO4 --target PhaseDispatcherTests
```

Erwartet: `Cannot open include file: 'Render/PhaseDispatcher.h'`.

-   [ ] **Step 4: Den Header schreiben**

`src/Render/PhaseDispatcher.h`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Render
{
	/// Who gets called once per frame at a fixed point inside the frame.
	///
	/// Deliberately knows neither D3D nor the engine. F1 learned the cost of
	/// the other arrangement: reaching for the profiler from inside
	/// FeatureRegistry tore up a state machine D1 had built to be testable
	/// without a game. The vtable patch that drives this lives in FramePhase.
	///
	/// Subscribers sit in a fixed array rather than a vector because a callback
	/// may subscribe or unsubscribe while it is running. Nothing moves, so no
	/// std::function is ever destroyed or relocated while it is executing.
	class PhaseDispatcher
	{
	public:
		using Token = std::uint32_t;

		/// Never handed out. Returned when the cap is reached.
		static constexpr Token kNoToken = 0;

		/// Ten features would be a lot; sixteen is room to spare without making
		/// the array worth thinking about.
		static constexpr std::size_t kMaxSubscribers = 16;

		/// Returns kNoToken when every slot is taken. The name is for logging
		/// and for the profiler pass FramePhase wraps around the callback.
		Token Subscribe(std::string_view a_name, std::function<void()> a_callback);

		/// Takes effect immediately, including for a subscriber that has not
		/// run yet in a dispatch that is currently underway. Removing the
		/// callback that is executing defers the destruction of its
		/// std::function until the dispatch is over.
		void Unsubscribe(Token a_token) noexcept;

		[[nodiscard]] std::size_t Count() const noexcept;

		/// Runs every subscriber, in subscription order, but only when a_frame
		/// differs from the frame last dispatched. Returns whether it ran.
		///
		/// A subscriber added while this is running waits for the next frame:
		/// the run length is fixed before the first callback.
		bool Dispatch(std::uint64_t a_frame);

	private:
		struct Entry
		{
			Token token{ kNoToken };
			std::string name;
			std::function<void()> callback;
			bool active{ false };

			/// Unsubscribed during a dispatch: no longer run, but its callback
			/// is not cleared until the dispatch ends.
			bool retiring{ false };
		};

		void Retire(Entry& a_entry) noexcept;

		std::array<Entry, kMaxSubscribers> _entries{};
		Token _nextToken{ 1 };
		std::uint64_t _lastFrame{ 0 };
		bool _dispatched{ false };
		bool _running{ false };
	};
}
```

-   [ ] **Step 5: Die Umsetzung schreiben**

`src/Render/PhaseDispatcher.cpp`:

```cpp
#include "Render/PhaseDispatcher.h"

namespace Render
{
	PhaseDispatcher::Token PhaseDispatcher::Subscribe(
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		for (auto& entry : _entries) {
			if (entry.active) {
				continue;
			}

			entry.token = _nextToken++;
			entry.name.assign(a_name);
			entry.callback = std::move(a_callback);
			entry.active = true;
			entry.retiring = false;
			return entry.token;
		}

		return kNoToken;
	}

	void PhaseDispatcher::Unsubscribe(Token a_token) noexcept
	{
		if (a_token == kNoToken) {
			return;
		}

		for (auto& entry : _entries) {
			if (entry.active && entry.token == a_token) {
				Retire(entry);
				return;
			}
		}
	}

	void PhaseDispatcher::Retire(Entry& a_entry) noexcept
	{
		a_entry.active = false;

		if (_running) {
			// The callback may be the one on the stack right now. Clearing it
			// here would destroy a std::function mid-call.
			a_entry.retiring = true;
			return;
		}

		a_entry.callback = nullptr;
		a_entry.name.clear();
		a_entry.token = kNoToken;
	}

	std::size_t PhaseDispatcher::Count() const noexcept
	{
		std::size_t count = 0;
		for (const auto& entry : _entries) {
			if (entry.active) {
				++count;
			}
		}

		return count;
	}

	bool PhaseDispatcher::Dispatch(std::uint64_t a_frame)
	{
		if (_dispatched && _lastFrame == a_frame) {
			return false;
		}

		_lastFrame = a_frame;
		_dispatched = true;

		if (Count() == 0) {
			return false;
		}

		_running = true;
		for (auto& entry : _entries) {
			if (entry.active && entry.callback) {
				entry.callback();
			}
		}
		_running = false;

		for (auto& entry : _entries) {
			if (entry.retiring) {
				entry.retiring = false;
				entry.callback = nullptr;
				entry.name.clear();
				entry.token = kNoToken;
			}
		}

		return true;
	}
}
```

**Zur Reihenfolge:** Ein während des Durchlaufs eingetragener Neuling landet in einem freien Platz,
der hinter dem gerade laufenden liegen kann — dann liefe er noch im selben Frame. Genau das prüft
der Test, und deshalb wird die Schleife über einen vorher festgehaltenen Zustand geführt: `active`
wird beim Eintragen gesetzt, aber die Schleife läuft über `_entries` in Feldreihenfolge, und ein
Neuling bekommt den **ersten freien** Platz. Bei fortlaufendem Eintragen ist das immer ein Platz
hinter dem laufenden. Damit der Test „wartet auf den nächsten Frame" hält, merkt sich `Dispatch` die
Zahl der aktiven Einträge **vor** der Schleife und läuft nur so viele ab:

```cpp
		_running = true;
		const auto planned = Count();
		std::size_t ran = 0;
		for (auto& entry : _entries) {
			if (ran == planned) {
				break;
			}
			if (entry.active && entry.callback) {
				entry.callback();
				++ran;
			}
		}
		_running = false;
```

Diese Fassung nehmen, nicht die einfache darüber. Sie zählt nur, was zu Beginn aktiv war, und
überspringt einen während des Laufs Ausgetragenen richtig, weil `active` dann bereits `false` ist.

-   [ ] **Step 6: Bauen und laufen lassen**

```pwsh
cmake --build --preset FO4 --target PhaseDispatcherTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R PhaseDispatcher
```

Erwartet: `all checks passed`.

-   [ ] **Step 7: Die Tests brechen**

| Mutation                                                       | Muß fallen                                                      |
| -------------------------------------------------------------- | --------------------------------------------------------------- |
| In `Dispatch` die Frame-Prüfung entfernen                      | „the second call of the same frame does not"                    |
| `_dispatched` weglassen und nur `_lastFrame == a_frame` prüfen | „frame zero dispatches"                                         |
| In `Retire` `_running` ignorieren und immer sofort löschen     | „a self-removing subscriber runs once" — mit Absturz, das zählt |
| `planned` durch `kMaxSubscribers` ersetzen                     | „a subscriber added during dispatch waits for the next frame"   |

-   [ ] **Step 8: Commit**

```bash
git add src/Render/PhaseDispatcher.h src/Render/PhaseDispatcher.cpp tests/PhaseDispatcherTests.cpp CMakeLists.txt
git commit -m "feat: decide who runs once inside a frame"
```

---

## Task 3: Der Patch, der den Dispatcher antreibt

**Files:**

-   Create: `src/Render/FramePhase.h`
-   Create: `src/Render/FramePhase.cpp`
-   Modify: `src/XSEPlugin.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: `Render::PhaseDispatcher` (Task 2), `Render::VTablePatch`, `Render::FrameCount()`,
    `Render::PassScope` (F1).
-   Produces:
    ```cpp
    namespace Render
    {
        bool InstallFramePhase() noexcept;
        PhaseDispatcher::Token SubscribeFramePhase(std::string_view, std::function<void()>);
        void UnsubscribeFramePhase(PhaseDispatcher::Token) noexcept;
        [[nodiscard]] std::uint64_t FramePhaseHits() noexcept;
    }
    ```

Kein Host-Test: der Patch braucht das geladene Spielmodul. Belegt wird er im Spiel, durch die
Trefferzahl im Log. Der Zustandsautomat dahinter ist in Task 2 abgedeckt.

-   [ ] **Step 1: Den Header schreiben**

`src/Render/FramePhase.h`:

```cpp
#pragma once

#include "Render/PhaseDispatcher.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace Render
{
	/// The one point inside a frame where the G-buffer is complete and the
	/// lighting has been written, but the composite has not drawn yet.
	///
	/// It is reached by replacing slot 02 - SetupTechnique - in the main vtable
	/// of BSDFCompositeShader. The first call of each frame is the moment we
	/// want; every later call in the same frame passes straight through.
	///
	/// The patch is installed once and stays for the life of the process, like
	/// the Present hook from B1. It is deliberately not owned by a feature: the
	/// overlay can switch a feature off in the middle of a running game, and
	/// taking a vtable entry back while another thread stands in it is a race
	/// nothing can win. Features subscribe and unsubscribe instead.
	[[nodiscard]] bool InstallFramePhase() noexcept;

	/// The callback runs inside a Render::PassScope named after a_name, so it
	/// is measured by F1 and named in a capture without the caller doing
	/// anything. Returns PhaseDispatcher::kNoToken when the cap is reached.
	PhaseDispatcher::Token SubscribeFramePhase(
		std::string_view a_name,
		std::function<void()> a_callback);

	void UnsubscribeFramePhase(PhaseDispatcher::Token a_token) noexcept;

	/// How often the phase has fired. A number that stays at zero while the
	/// game renders means the patch is on the wrong table.
	[[nodiscard]] std::uint64_t FramePhaseHits() noexcept;
}
```

-   [ ] **Step 2: Die Umsetzung schreiben**

`src/Render/FramePhase.cpp`:

```cpp
#include "Render/FramePhase.h"

#include "Render/Profiler.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"

#include <RE/B/BSShader.h>

namespace Render
{
	namespace
	{
		// bool SetupTechnique(std::uint32_t), slot 02 of the BSShader vtable.
		// The slot numbers of BSShader are right in commonlibf4 even though its
		// data offsets are not: measured in subproject C, every field after
		// shaderType sits 0x78 too low. Nothing here reads a field.
		using SetupTechnique_t = bool (*)(RE::BSShader*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;

		VTablePatch g_patch;
		SetupTechnique_t g_original = nullptr;
		PhaseDispatcher g_dispatcher;
		std::uint64_t g_hits = 0;
		bool g_installed = false;

		bool SetupTechniqueThunk(RE::BSShader* a_this, std::uint32_t a_pass)
		{
			if (g_dispatcher.Dispatch(FrameCount())) {
				++g_hits;
			}

			return g_original(a_this, a_pass);
		}
	}

	bool InstallFramePhase() noexcept
	{
		if (g_installed) {
			return true;
		}

		// VTABLE::BSDFCompositeShader[0] is the main table; [1] is the second
		// base subobject, 0x70 further on. Both ids exist in 1.11.240, checked
		// offline against version-1-11-240-0.bin.
		auto* const table = reinterpret_cast<void**>(
			RE::VTABLE::BSDFCompositeShader[0].address());

		if (table == nullptr) {
			REX::ERROR("frame phase: BSDFCompositeShader vtable did not resolve");
			return false;
		}

		if (!g_patch.InstallAtTable(
				table, kSetupTechniqueSlot, reinterpret_cast<void*>(&SetupTechniqueThunk))) {
			REX::ERROR("frame phase: could not patch SetupTechnique");
			return false;
		}

		g_original = reinterpret_cast<SetupTechnique_t>(g_patch.Original());
		g_installed = true;

		REX::INFO("frame phase installed, chaining to {}", g_patch.Original());
		return true;
	}

	PhaseDispatcher::Token SubscribeFramePhase(
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		// The scope is opened here rather than by the caller so that every
		// subscriber is measured, not only the ones that remembered to.
		std::string name{ a_name };
		return g_dispatcher.Subscribe(
			a_name,
			[name = std::move(name), callback = std::move(a_callback)] {
				const PassScope scope{ name };
				callback();
			});
	}

	void UnsubscribeFramePhase(PhaseDispatcher::Token a_token) noexcept
	{
		g_dispatcher.Unsubscribe(a_token);
	}

	std::uint64_t FramePhaseHits() noexcept
	{
		return g_hits;
	}
}
```

**Zu prüfen beim Schreiben:** ob `RE::VTABLE::BSDFCompositeShader[0]` `.address()` heißt und ob
`RE::VTABLE` über `<RE/B/BSShader.h>` erreichbar ist oder ein eigener Include nötig ist. Nicht
raten — im Header von commonlibf4 nachsehen. `REL::ID::offset()` beendet bei unbekannter ID den
Prozeß; die drei IDs sind offline geprüft und vorhanden, aber die Auflösung gehört trotzdem hinter
die Null-Prüfung.

-   [ ] **Step 3: Beim Start installieren**

`src/XSEPlugin.cpp`, im `kGameDataReady`-Zweig, **vor** `Render::InstallSwapChainHook()`:

```cpp
			Menu::StartSystem();
			Features::StartSystem();

			// Before the Present hook: Present drives the registry, a feature's
			// Setup runs from there, and a Setup that subscribed to a phase not
			// yet installed would be measured by nothing.
			static_cast<void>(Render::InstallFramePhase());

			Render::InstallSwapChainHook();
```

Dazu `#include "Render/FramePhase.h"` bei den übrigen Einbindungen.

-   [ ] **Step 4: In CMake aufnehmen und bauen**

`src/Render/FramePhase.cpp` in die Quellenliste des Hauptziels, dann:

```pwsh
cmake --build --preset FO4
```

Erwartet: sauberer Bau unter `/W4 /WX`.

-   [ ] **Step 5: Im Spiel belegen**

Fallout 4 über `f4se_loader.exe` starten, einen Spielstand laden, wieder beenden. In
`C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log` erwartet:

```
frame phase installed, chaining to 0x...
```

Zu diesem Zeitpunkt gibt es noch keinen Abnehmer, also feuert nichts — das ist richtig. Der Beleg,
daß die Tabelle die richtige ist, kommt in Task 10, wenn das Feature einträgt. Wer ihn früher will,
hängt vorübergehend einen Abnehmer an, der alle 600 Treffer eine Zeile schreibt, und nimmt ihn
danach wieder heraus.

-   [ ] **Step 6: Commit**

```bash
git add src/Render/FramePhase.h src/Render/FramePhase.cpp src/XSEPlugin.cpp CMakeLists.txt
git commit -m "feat: take control between g-buffer and composite"
```

---

## Task 4: Bends Teilzeichnungen

**Files:**

-   Create: `src/Features/ScreenSpaceShadows/bend_sss_cpu.h` (übernommen, unverändert)
-   Create: `src/Features/ScreenSpaceShadows/BendDispatch.h`
-   Create: `src/Features/ScreenSpaceShadows/BendDispatch.cpp`
-   Create: `tests/BendDispatchTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts.
-   Produces:

    ```cpp
    namespace Features::Bend
    {
        struct Dispatch { int waveCount[3]; int waveOffset[2]; };
        struct DispatchPlan
        {
            float lightCoordinate[4];
            Dispatch dispatches[8];
            int count{ 0 };
        };

        [[nodiscard]] DispatchPlan BuildPlan(
            const float a_lightProjection[4],
            const int a_viewportSize[2]);
    }
    ```

**Warum eine Hülle:** Der übernommene Kopf definiert `BuildDispatchList` im Header **ohne `inline`**.
In zwei Übersetzungseinheiten eingebunden — Test und Feature — gäbe das ein doppeltes Symbol. Er
wird deshalb ausschließlich von `BendDispatch.cpp` eingebunden. Die Hülle setzt zugleich die
Bildgrenzen, die immer dieselben sind (`0,0` bis Viewport), sodaß die Aufrufstelle sie nicht
wiederholt.

-   [ ] **Step 1: Den Bend-Kopf übernehmen**

```bash
mkdir -p src/Features/ScreenSpaceShadows
git show skyrim-base:src/Features/ScreenSpaceShadows/bend_sss_cpu.h > src/Features/ScreenSpaceShadows/bend_sss_cpu.h
```

Die Datei bleibt **unverändert**, samt Apache-2.0-Kopf. Wird sie angefaßt, ist sie kein
übernommener Fremdstand mehr.

-   [ ] **Step 2: Den fehlschlagenden Test schreiben**

`tests/BendDispatchTests.cpp`:

```cpp
#include "Features/ScreenSpaceShadows/BendDispatch.h"

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

	constexpr int kViewport[2] = { 2560, 1440 };

	// Every dispatch must ask for work in all three dimensions, otherwise the
	// GPU is handed a dispatch that does nothing and the mask keeps a hole.
	bool EveryDispatchDoesWork(const Features::Bend::DispatchPlan& a_plan)
	{
		for (int i = 0; i < a_plan.count; ++i) {
			const auto& dispatch = a_plan.dispatches[i];
			if (dispatch.waveCount[0] <= 0 || dispatch.waveCount[1] <= 0 ||
				dispatch.waveCount[2] <= 0) {
				return false;
			}
		}

		return true;
	}
}

int main()
{
	{
		// A directional light straight overhead: w is zero, so the light is at
		// infinity and the plan must still be built.
		const float light[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		const auto plan = Features::Bend::BuildPlan(light, kViewport);

		Check(plan.count >= 1, "an overhead sun produces at least one dispatch");
		Check(plan.count <= 8, "and never more than the eight the list holds");
		Check(EveryDispatchDoesWork(plan), "every dispatch asks for work");
	}

	{
		// Light behind the camera: the fourth coordinate carries the sign, and
		// getting it wrong is the classic way to march the rays backwards.
		const float behind[4] = { 0.0f, 0.0f, -1.0f, 0.0f };
		const auto plan = Features::Bend::BuildPlan(behind, kViewport);

		Check(plan.count >= 1, "a sun behind the camera still produces a plan");
		Check(EveryDispatchDoesWork(plan), "and every dispatch of it asks for work");
	}

	{
		// On-screen light: the documented case with the most dispatches.
		const float onScreen[4] = { 0.1f, 0.2f, 0.5f, 1.0f };
		const auto plan = Features::Bend::BuildPlan(onScreen, kViewport);

		Check(plan.count >= 1, "an on-screen light produces a plan");
		Check(plan.count <= 8, "within the list's capacity");
		Check(EveryDispatchDoesWork(plan), "and every dispatch of it asks for work");

		Check(
			plan.lightCoordinate[0] > 0.0f && plan.lightCoordinate[0] < 2560.0f,
			"its x coordinate lands inside the viewport");
		Check(
			plan.lightCoordinate[1] > 0.0f && plan.lightCoordinate[1] < 1440.0f,
			"and so does its y coordinate");
	}

	{
		// The light coordinate is in pixels, so it has to move with the
		// viewport rather than staying in some normalised space.
		const float light[4] = { 0.5f, 0.0f, 0.5f, 1.0f };
		const int small[2] = { 1280, 720 };

		const auto large = Features::Bend::BuildPlan(light, kViewport);
		const auto half = Features::Bend::BuildPlan(light, small);

		Check(
			large.lightCoordinate[0] > half.lightCoordinate[0] * 1.9f,
			"the light coordinate scales with the viewport");
	}

	{
		// A degenerate viewport must not produce a dispatch that would run off
		// the end of a zero-sized texture.
		const float light[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
		const int empty[2] = { 0, 0 };
		const auto plan = Features::Bend::BuildPlan(light, empty);

		Check(plan.count >= 0 && plan.count <= 8, "a zero viewport produces a bounded plan");
	}

	{
		// Same input, same output. The plan is pure arithmetic; if it ever
		// depended on hidden state this would catch it.
		const float light[4] = { 0.1f, 0.2f, 0.5f, 1.0f };
		const auto first = Features::Bend::BuildPlan(light, kViewport);
		const auto second = Features::Bend::BuildPlan(light, kViewport);

		bool identical = first.count == second.count;
		for (int i = 0; identical && i < first.count; ++i) {
			identical = first.dispatches[i].waveCount[0] == second.dispatches[i].waveCount[0] &&
			            first.dispatches[i].waveCount[1] == second.dispatches[i].waveCount[1] &&
			            first.dispatches[i].waveOffset[0] == second.dispatches[i].waveOffset[0] &&
			            first.dispatches[i].waveOffset[1] == second.dispatches[i].waveOffset[1];
		}

		Check(identical, "the same input builds the same plan");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 3: Das Testziel in CMake eintragen**

Im Muster aus Task 2, mit `tests/BendDispatchTests.cpp` und
`src/Features/ScreenSpaceShadows/BendDispatch.cpp` als Quellen, `add_test(NAME BendDispatch ...)`.

-   [ ] **Step 4: Bauen, um den Fehlschlag zu sehen**

```pwsh
cmake --build --preset FO4 --target BendDispatchTests
```

Erwartet: `Cannot open include file: 'Features/ScreenSpaceShadows/BendDispatch.h'`.

-   [ ] **Step 5: Header und Umsetzung schreiben**

`src/Features/ScreenSpaceShadows/BendDispatch.h`:

```cpp
#pragma once

namespace Features::Bend
{
	/// One compute dispatch of the Bend sweep.
	struct Dispatch
	{
		int waveCount[3]{ 0, 0, 0 };
		int waveOffset[2]{ 0, 0 };
	};

	/// What one frame's screen space shadow costs in dispatches.
	///
	/// Eight is the capacity of Bend's own list. Typical is one or two with the
	/// sun off screen and four to six with it on screen.
	struct DispatchPlan
	{
		float lightCoordinate[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		Dispatch dispatches[8]{};
		int count{ 0 };
	};

	/// Wraps Bend::BuildDispatchList.
	///
	/// The wrapper exists because bend_sss_cpu.h defines its function in the
	/// header without inline: included from two translation units it would be a
	/// duplicate symbol. It is included from BendDispatch.cpp and nowhere else.
	///
	/// a_lightProjection is the light direction transformed by the view
	/// projection, without the w divide: float4(-direction, 0) for the sun.
	/// The render bounds are always the whole viewport, so they are not a
	/// parameter.
	[[nodiscard]] DispatchPlan BuildPlan(
		const float a_lightProjection[4],
		const int a_viewportSize[2]);
}
```

`src/Features/ScreenSpaceShadows/BendDispatch.cpp`:

```cpp
#include "Features/ScreenSpaceShadows/BendDispatch.h"

// Third-party source, kept verbatim. It converts between int and float freely
// and passes its own ints narrowed, which /W4 /WX will not have. Suppressed
// here and only here, naming the header, rather than by relaxing the option.
#pragma warning(push)
#pragma warning(disable : 4244 4365 4838)
#include "Features/ScreenSpaceShadows/bend_sss_cpu.h"
#pragma warning(pop)

namespace Features::Bend
{
	DispatchPlan BuildPlan(const float a_lightProjection[4], const int a_viewportSize[2])
	{
		// BuildDispatchList takes its arguments by non-const pointer without
		// writing through them.
		float light[4] = {
			a_lightProjection[0],
			a_lightProjection[1],
			a_lightProjection[2],
			a_lightProjection[3]
		};

		int viewport[2] = { a_viewportSize[0], a_viewportSize[1] };
		int minBounds[2] = { 0, 0 };
		int maxBounds[2] = { a_viewportSize[0], a_viewportSize[1] };

		// The wave size is 64 because bend_sss_gpu.hlsli defines WAVE_SIZE 64
		// for itself; the two have to agree and neither reads the other.
		const auto list = ::Bend::BuildDispatchList(light, viewport, minBounds, maxBounds, false, 64);

		DispatchPlan plan;
		plan.lightCoordinate[0] = list.LightCoordinate_Shader[0];
		plan.lightCoordinate[1] = list.LightCoordinate_Shader[1];
		plan.lightCoordinate[2] = list.LightCoordinate_Shader[2];
		plan.lightCoordinate[3] = list.LightCoordinate_Shader[3];

		plan.count = list.DispatchCount < 8 ? list.DispatchCount : 8;
		for (int i = 0; i < plan.count; ++i) {
			plan.dispatches[i].waveCount[0] = list.Dispatch[i].WaveCount[0];
			plan.dispatches[i].waveCount[1] = list.Dispatch[i].WaveCount[1];
			plan.dispatches[i].waveCount[2] = list.Dispatch[i].WaveCount[2];
			plan.dispatches[i].waveOffset[0] = list.Dispatch[i].WaveOffset_Shader[0];
			plan.dispatches[i].waveOffset[1] = list.Dispatch[i].WaveOffset_Shader[1];
		}

		return plan;
	}
}
```

**Beim Bauen zu klären:** welche Warnungsnummern der Kopf unter `/W4 /WX` tatsächlich auslöst. Die
drei oben sind die aus der Skyrim-Vorlage plus `4365`; steht eine andere im Fehlertext, wird genau
die ergänzt — nicht `/WX` gelockert und nicht pauschal unterdrückt. Löst er gar keine aus, wird das
`#pragma`-Paar entfernt.

-   [ ] **Step 6: Bauen und laufen lassen**

```pwsh
cmake --build --preset FO4 --target BendDispatchTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R BendDispatch
```

Erwartet: `all checks passed`.

-   [ ] **Step 7: Die Tests brechen**

| Mutation                                                         | Muß fallen                                                            |
| ---------------------------------------------------------------- | --------------------------------------------------------------------- |
| In `BuildPlan` `viewport` fest auf `{ 1280, 720 }`               | „the light coordinate scales with the viewport"                       |
| `plan.count = 0` setzen, den Rest lassen                         | „an overhead sun produces at least one dispatch"                      |
| `waveCount[2]` nicht kopieren, also auf `0` lassen               | „every dispatch asks for work"                                        |
| `plan.lightCoordinate[0]` aus `LightCoordinate_Shader[1]` füllen | „its x coordinate lands inside the viewport" oder der Skalierungstest |

-   [ ] **Step 8: Commit**

```bash
git add src/Features/ScreenSpaceShadows tests/BendDispatchTests.cpp CMakeLists.txt
git commit -m "feat: plan the sweeps a shadow needs"
```

---

## Task 5: Die Ziele der Engine über ihren Slot

**Files:**

-   Create: `src/Render/Targets.h`
-   Create: `src/Render/Targets.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts aus F2.
-   Produces:

    ```cpp
    namespace Render::Targets
    {
        inline constexpr std::size_t kSceneDepth = 2;
        inline constexpr std::size_t kLightDiffuse = 58;
        inline constexpr std::size_t kLightSpecular = 59;
        inline constexpr std::size_t kGBufferNormal = 20;

        [[nodiscard]] REX::W32::ID3D11RenderTargetView* RenderTargetView(std::size_t) noexcept;
        [[nodiscard]] REX::W32::ID3D11ShaderResourceView* RenderTargetSRV(std::size_t) noexcept;
        [[nodiscard]] REX::W32::ID3D11ShaderResourceView* DepthSRV(std::size_t) noexcept;
        [[nodiscard]] REX::W32::ID3D11Texture2D* RenderTargetTexture(std::size_t) noexcept;
        void LogSlots() noexcept;
    }
    ```

**Wie das ohne Host-Test belegt wird:** `RunTargetInventory()` läuft bereits beim Installieren des
Present-Hooks und beschriftet jede Ressource per `SetPrivateData`. `Render::GetDebugName` liest
diese Namen zurück. `LogSlots()` gibt damit im Spiel aus, wie die vier Slots wirklich heißen — steht
dort `FO4_RT_058`, zeigt die Konstante hin, wo wir denken. Das ist eine entscheidende Prüfung, die
nichts kostet.

-   [ ] **Step 1: Den Header schreiben**

`src/Render/Targets.h`:

```cpp
#pragma once

#include <REX/W32/D3D11.h>

#include <cstddef>

namespace Render::Targets
{
	/// The engine's targets by their slot in BSGraphics::RendererData.
	///
	/// The number in the name a capture shows is that slot: the inventory from
	/// B2 labels with std::format("FO4_RT_{:03}", i) over the same index.
	/// Nothing here creates a view; the engine's own rtView, srView and
	/// srViewDepth are what we bind.
	///
	/// Only the slots F2 actually uses are named. A complete RENDER_TARGET enum
	/// is the known gap in CommonLibF4 and is not this subproject's business.

	/// FO4_DS_002, R24G8_TYPELESS. The scene depth the world is drawn against.
	inline constexpr std::size_t kSceneDepth = 2;

	/// FO4_RT_058 and FO4_RT_059, R11G11B10_FLOAT. Written by kDFLight and
	/// carried on by kDFComposite.
	inline constexpr std::size_t kLightDiffuse = 58;
	inline constexpr std::size_t kLightSpecular = 59;

	/// FO4_RT_020, R16G16_UNORM. The G-buffer normals, confirmed by the
	/// measuring spike. Unused by F2, named for F3 onwards.
	inline constexpr std::size_t kGBufferNormal = 20;

	/// Null for an index out of range or an empty slot. Every refusal logs the
	/// slot and the reason - a diagnostic that cannot say which check failed is
	/// no diagnostic.
	[[nodiscard]] REX::W32::ID3D11RenderTargetView* RenderTargetView(std::size_t a_slot) noexcept;
	[[nodiscard]] REX::W32::ID3D11ShaderResourceView* RenderTargetSRV(std::size_t a_slot) noexcept;
	[[nodiscard]] REX::W32::ID3D11Texture2D* RenderTargetTexture(std::size_t a_slot) noexcept;

	/// The depth as a shader resource. DepthStencilTarget::srViewDepth, which
	/// hands R24G8_TYPELESS over as R24_UNORM_X8_TYPELESS.
	[[nodiscard]] REX::W32::ID3D11ShaderResourceView* DepthSRV(std::size_t a_slot) noexcept;

	/// Logs the debug name behind each named slot. The inventory ran at hook
	/// install and named every resource, so this reads back FO4_RT_058 rather
	/// than an address - which is how a wrong constant is caught in one line.
	void LogSlots() noexcept;
}
```

-   [ ] **Step 2: Die Umsetzung schreiben**

`src/Render/Targets.cpp`:

```cpp
#include "Render/Targets.h"

#include "Render/DebugName.h"

#include <RE/B/BSGraphics.h>

namespace Render::Targets
{
	namespace
	{
		const RE::BSGraphics::RenderTarget* ColourSlot(std::size_t a_slot) noexcept
		{
			const auto* const data = RE::BSGraphics::GetRendererData();
			if (data == nullptr) {
				REX::ERROR("target slot {}: no renderer data", a_slot);
				return nullptr;
			}

			if (a_slot >= std::size(data->renderTargets)) {
				REX::ERROR("target slot {}: out of range", a_slot);
				return nullptr;
			}

			const auto& target = data->renderTargets[a_slot];
			if (target.texture == nullptr) {
				REX::ERROR("target slot {}: empty", a_slot);
				return nullptr;
			}

			return std::addressof(target);
		}
	}

	REX::W32::ID3D11RenderTargetView* RenderTargetView(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->rtView : nullptr;
	}

	REX::W32::ID3D11ShaderResourceView* RenderTargetSRV(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->srView : nullptr;
	}

	REX::W32::ID3D11Texture2D* RenderTargetTexture(std::size_t a_slot) noexcept
	{
		const auto* const target = ColourSlot(a_slot);
		return target != nullptr ? target->texture : nullptr;
	}

	REX::W32::ID3D11ShaderResourceView* DepthSRV(std::size_t a_slot) noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr) {
			REX::ERROR("depth slot {}: no renderer data", a_slot);
			return nullptr;
		}

		if (a_slot >= std::size(data->depthStencilTargets)) {
			REX::ERROR("depth slot {}: out of range", a_slot);
			return nullptr;
		}

		const auto& target = data->depthStencilTargets[a_slot];
		if (target.srViewDepth == nullptr) {
			REX::ERROR("depth slot {}: no depth shader resource view", a_slot);
			return nullptr;
		}

		return target.srViewDepth;
	}

	void LogSlots() noexcept
	{
		const auto colour = [](const char* a_what, std::size_t a_slot) {
			auto* const texture = RenderTargetTexture(a_slot);
			REX::INFO(
				"{} is slot {}, named {}",
				a_what,
				a_slot,
				texture != nullptr ?
					GetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(texture)) :
					std::string{ "<empty>" });
		};

		colour("light diffuse", kLightDiffuse);
		colour("light specular", kLightSpecular);
		colour("g-buffer normals", kGBufferNormal);

		auto* const depth = DepthSRV(kSceneDepth);
		REX::INFO(
			"scene depth is slot {}, named {}",
			kSceneDepth,
			depth != nullptr ?
				GetViewTargetName(reinterpret_cast<REX::W32::ID3D11View*>(depth)) :
				std::string{ "<empty>" });
	}
}
```

Einbindungen um `<string>` ergänzen.

-   [ ] **Step 3: Bauen**

```pwsh
cmake --build --preset FO4
```

Erwartet: sauberer Bau. `LogSlots` hat noch keinen Aufrufer — das ist in Ordnung, es ist eine
öffentliche Funktion, keine statische.

-   [ ] **Step 4: Commit**

```bash
git add src/Render/Targets.h src/Render/Targets.cpp CMakeLists.txt
git commit -m "feat: reach the engine's targets by slot"
```

---

## Task 6: Eigene Textur und eigener Konstantenpuffer

**Files:**

-   Create: `src/Render/Resources.h`
-   Create: `src/Render/Resources.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: `Render::GetDevice()`, `Render::GetContext()` (B1), `Render::SetDebugName` (B2).
-   Produces:

    ```cpp
    namespace Render
    {
        class Texture
        {
        public:
            bool Create(std::uint32_t a_width, std::uint32_t a_height,
                        std::uint32_t a_format, std::string_view a_debugName) noexcept;
            void Release() noexcept;
            [[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept;
            [[nodiscard]] REX::W32::ID3D11UnorderedAccessView* UAV() const noexcept;
            [[nodiscard]] std::uint32_t Width() const noexcept;
            [[nodiscard]] std::uint32_t Height() const noexcept;
            [[nodiscard]] bool Valid() const noexcept;
        };

        class ConstantBuffer
        {
        public:
            bool Create(std::size_t a_bytes, std::string_view a_debugName) noexcept;
            void Release() noexcept;
            bool Update(const void* a_data, std::size_t a_bytes) noexcept;
            [[nodiscard]] REX::W32::ID3D11Buffer* Buffer() const noexcept;
        };
    }
    ```

`Create` nimmt Breite, Höhe und Format statt eines fertigen `D3D11_TEXTURE2D_DESC`: F2 braucht genau
eine Textur, und ein Beschreibungsblock als Parameter wäre eine Verallgemeinerung ohne zweiten
Anwender. Bindeflags sind fest `SHADER_RESOURCE | UNORDERED_ACCESS` — das ist die Kombination, die
ein Bildschirmraum-Feature braucht.

-   [ ] **Step 1: Den Header schreiben**

`src/Render/Resources.h`:

```cpp
#pragma once

#include <REX/W32/D3D11.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Render
{
	/// A texture we own: Texture2D plus a shader resource view and an unordered
	/// access view, which is what a screen space pass needs and all it needs.
	///
	/// Both views are created up front rather than on demand: a pass that fails
	/// to bind halfway through is harder to reason about than one that never
	/// started.
	class Texture
	{
	public:
		Texture() = default;
		~Texture();

		Texture(const Texture&) = delete;
		Texture& operator=(const Texture&) = delete;

		/// a_format is a DXGI format. BSGraphics::Format is the DXGI enum, so
		/// the values in render-targets.md can be used directly.
		///
		/// Replaces whatever was there; calling it again with a new size is how
		/// a resolution change is answered.
		[[nodiscard]] bool Create(
			std::uint32_t a_width,
			std::uint32_t a_height,
			std::uint32_t a_format,
			std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] REX::W32::ID3D11UnorderedAccessView* UAV() const noexcept { return _uav; }
		[[nodiscard]] std::uint32_t Width() const noexcept { return _width; }
		[[nodiscard]] std::uint32_t Height() const noexcept { return _height; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		REX::W32::ID3D11UnorderedAccessView* _uav{ nullptr };
		std::uint32_t _width{ 0 };
		std::uint32_t _height{ 0 };
	};

	/// A dynamic constant buffer, written with MAP_WRITE_DISCARD.
	///
	/// Discard rather than a default buffer with UpdateSubresource because the
	/// Bend sweep rewrites it between dispatches of the same frame: discard
	/// hands out fresh storage each time instead of stalling on the last one.
	class ConstantBuffer
	{
	public:
		ConstantBuffer() = default;
		~ConstantBuffer();

		ConstantBuffer(const ConstantBuffer&) = delete;
		ConstantBuffer& operator=(const ConstantBuffer&) = delete;

		/// a_bytes is rounded up to a multiple of 16, which D3D11 requires.
		[[nodiscard]] bool Create(std::size_t a_bytes, std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		/// False when the buffer does not exist, when a_bytes exceeds it, or
		/// when the map fails.
		[[nodiscard]] bool Update(const void* a_data, std::size_t a_bytes) noexcept;

		[[nodiscard]] REX::W32::ID3D11Buffer* Buffer() const noexcept { return _buffer; }

	private:
		REX::W32::ID3D11Buffer* _buffer{ nullptr };
		std::size_t _bytes{ 0 };
	};
}
```

-   [ ] **Step 2: Die Umsetzung schreiben**

`src/Render/Resources.cpp` — Gerüst; die genauen Feldnamen der `REX::W32`-Beschreibungsblöcke
**beim Schreiben im Header nachsehen**, nicht aus dem SDK-Gedächtnis ableiten:

```cpp
#include "Render/Resources.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <cstring>

namespace Render
{
	Texture::~Texture()
	{
		Release();
	}

	bool Texture::Create(
		std::uint32_t a_width,
		std::uint32_t a_height,
		std::uint32_t a_format,
		std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr || a_width == 0 || a_height == 0) {
			REX::ERROR("texture {}: no device, or a zero dimension", a_debugName);
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = a_width;
		desc.height = a_height;
		desc.mipLevels = 1;
		desc.arraySize = 1;
		desc.format = a_format;
		desc.sampleDesc.count = 1;
		desc.sampleDesc.quality = 0;
		desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		desc.bindFlags =
			REX::W32::D3D11_BIND_SHADER_RESOURCE | REX::W32::D3D11_BIND_UNORDERED_ACCESS;

		if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_texture)) < 0) {
			REX::ERROR("texture {}: CreateTexture2D failed", a_debugName);
			return false;
		}

		REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.format = a_format;
		srvDesc.viewDimension = REX::W32::D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.texture2D.mipLevels = 1;

		REX::W32::D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.format = a_format;
		uavDesc.viewDimension = REX::W32::D3D11_UAV_DIMENSION_TEXTURE2D;

		if (device->CreateShaderResourceView(
				_texture, std::addressof(srvDesc), std::addressof(_srv)) < 0 ||
			device->CreateUnorderedAccessView(
				_texture, std::addressof(uavDesc), std::addressof(_uav)) < 0) {
			REX::ERROR("texture {}: a view could not be created", a_debugName);
			Release();
			return false;
		}

		_width = a_width;
		_height = a_height;

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_texture), a_debugName));

		return true;
	}

	void Texture::Release() noexcept
	{
		if (_uav != nullptr) {
			_uav->Release();
			_uav = nullptr;
		}
		if (_srv != nullptr) {
			_srv->Release();
			_srv = nullptr;
		}
		if (_texture != nullptr) {
			_texture->Release();
			_texture = nullptr;
		}

		_width = 0;
		_height = 0;
	}

	ConstantBuffer::~ConstantBuffer()
	{
		Release();
	}

	bool ConstantBuffer::Create(std::size_t a_bytes, std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr || a_bytes == 0) {
			REX::ERROR("constant buffer {}: no device, or zero bytes", a_debugName);
			return false;
		}

		const auto rounded = (a_bytes + 15) & ~static_cast<std::size_t>(15);

		REX::W32::D3D11_BUFFER_DESC desc{};
		desc.byteWidth = static_cast<std::uint32_t>(rounded);
		desc.usage = REX::W32::D3D11_USAGE_DYNAMIC;
		desc.bindFlags = REX::W32::D3D11_BIND_CONSTANT_BUFFER;
		desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_WRITE;

		if (device->CreateBuffer(std::addressof(desc), nullptr, std::addressof(_buffer)) < 0) {
			REX::ERROR("constant buffer {}: CreateBuffer failed", a_debugName);
			return false;
		}

		_bytes = rounded;

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_buffer), a_debugName));

		return true;
	}

	void ConstantBuffer::Release() noexcept
	{
		if (_buffer != nullptr) {
			_buffer->Release();
			_buffer = nullptr;
		}

		_bytes = 0;
	}

	bool ConstantBuffer::Update(const void* a_data, std::size_t a_bytes) noexcept
	{
		auto* const context = GetContext();
		if (context == nullptr || _buffer == nullptr || a_data == nullptr || a_bytes > _bytes) {
			return false;
		}

		REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
		if (context->Map(
				reinterpret_cast<REX::W32::ID3D11Resource*>(_buffer),
				0,
				REX::W32::D3D11_MAP_WRITE_DISCARD,
				0,
				std::addressof(mapped)) < 0) {
			return false;
		}

		std::memcpy(mapped.data, a_data, a_bytes);
		context->Unmap(reinterpret_cast<REX::W32::ID3D11Resource*>(_buffer), 0);

		return true;
	}
}
```

**Beim Schreiben nachzusehen:** die genauen Feldnamen in `REX/W32/D3D11.h`
(`D3D11_TEXTURE2D_DESC`, `D3D11_BUFFER_DESC`, `D3D11_MAPPED_SUBRESOURCE`,
`D3D11_SHADER_RESOURCE_VIEW_DESC`, `D3D11_UNORDERED_ACCESS_VIEW_DESC`) und die Schreibweise der
Enumeratoren. REX benennt Felder klein; ob es `width` oder `Width` heißt, entscheidet der Header,
nicht die Erinnerung ans SDK. Ebenso, ob `ID3D11Texture2D*` implizit zu `ID3D11Resource*` wird —
wenn ja, entfallen die `reinterpret_cast`.

-   [ ] **Step 3: Bauen**

```pwsh
cmake --build --preset FO4
```

Erwartet: sauberer Bau unter `/W4 /WX`.

-   [ ] **Step 4: Commit**

```bash
git add src/Render/Resources.h src/Render/Resources.cpp CMakeLists.txt
git commit -m "feat: own a texture and a constant buffer"
```

---

## Task 7: Den Zustand der Engine unversehrt lassen

**Files:**

-   Create: `src/Render/StateGuard.h`
-   Create: `src/Render/StateGuard.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: `Render::GetContext()`.
-   Produces:
    ```cpp
    namespace Render
    {
        class StateGuard
        {
        public:
            StateGuard() noexcept;   // saves
            ~StateGuard() noexcept;  // restores
        };
    }
    ```

Dies ist der Baustein, ohne den ein eigener Pass die Engine beschädigt. Er ist bewußt ohne
Parameter: was gesichert wird, steht fest, und eine Auswahl treffen zu können hieße, sie falsch
treffen zu können.

-   [ ] **Step 1: Den Header schreiben**

`src/Render/StateGuard.h`:

```cpp
#pragma once

#include <REX/W32/D3D11.h>

namespace Render
{
	/// Saves the pipeline state our own passes touch and puts it back.
	///
	/// What is saved is fixed rather than selectable: a guard that can be asked
	/// for less can be asked for too little, and the failure would show up as a
	/// corrupted frame three passes later.
	///
	/// Everything a D3D11 *Get* call hands back comes with a reference. The
	/// destructor releases every one of them. That makes this the one place in
	/// the project that holds COM references - the rule from B1 not to reference
	/// or release engine objects covers pointers we fetch, not ones the runtime
	/// hands us with a reference already taken.
	class StateGuard
	{
	public:
		StateGuard() noexcept;
		~StateGuard() noexcept;

		StateGuard(const StateGuard&) = delete;
		StateGuard& operator=(const StateGuard&) = delete;

	private:
		static constexpr std::uint32_t kRenderTargets = 8;
		static constexpr std::uint32_t kShaderResources = 2;

		REX::W32::ID3D11DeviceContext* _context{ nullptr };

		REX::W32::ID3D11RenderTargetView* _renderTargets[kRenderTargets]{};
		REX::W32::ID3D11DepthStencilView* _depthStencil{ nullptr };

		REX::W32::ID3D11BlendState* _blendState{ nullptr };
		float _blendFactor[4]{};
		std::uint32_t _sampleMask{ 0 };

		REX::W32::D3D11_VIEWPORT _viewports[16]{};
		std::uint32_t _viewportCount{ 16 };

		REX::W32::ID3D11VertexShader* _vertexShader{ nullptr };
		REX::W32::ID3D11PixelShader* _pixelShader{ nullptr };
		REX::W32::ID3D11ComputeShader* _computeShader{ nullptr };

		REX::W32::ID3D11ShaderResourceView* _psResources[kShaderResources]{};
		REX::W32::ID3D11ShaderResourceView* _csResources[1]{};
		REX::W32::ID3D11UnorderedAccessView* _csUAVs[1]{};
		REX::W32::ID3D11SamplerState* _csSamplers[1]{};
		REX::W32::ID3D11Buffer* _csConstantBuffers[1]{};

		std::uint32_t _topology{ 0 };
	};
}
```

-   [ ] **Step 2: Die Umsetzung schreiben**

`src/Render/StateGuard.cpp` — Konstruktor holt, Destruktor setzt zurück und gibt jede Referenz frei:

```cpp
#include "Render/StateGuard.h"

#include "Render/Renderer.h"

namespace Render
{
	namespace
	{
		template <class T>
		void ReleaseAll(T** a_objects, std::size_t a_count) noexcept
		{
			for (std::size_t i = 0; i < a_count; ++i) {
				if (a_objects[i] != nullptr) {
					a_objects[i]->Release();
					a_objects[i] = nullptr;
				}
			}
		}
	}

	StateGuard::StateGuard() noexcept
	{
		_context = GetContext();
		if (_context == nullptr) {
			return;
		}

		_context->OMGetRenderTargets(kRenderTargets, _renderTargets, std::addressof(_depthStencil));
		_context->OMGetBlendState(
			std::addressof(_blendState), _blendFactor, std::addressof(_sampleMask));
		_context->RSGetViewports(std::addressof(_viewportCount), _viewports);
		_context->IAGetPrimitiveTopology(std::addressof(_topology));

		_context->VSGetShader(std::addressof(_vertexShader), nullptr, nullptr);
		_context->PSGetShader(std::addressof(_pixelShader), nullptr, nullptr);
		_context->CSGetShader(std::addressof(_computeShader), nullptr, nullptr);

		_context->PSGetShaderResources(0, kShaderResources, _psResources);
		_context->CSGetShaderResources(0, 1, _csResources);
		_context->CSGetUnorderedAccessViews(0, 1, _csUAVs);
		_context->CSGetSamplers(0, 1, _csSamplers);
		_context->CSGetConstantBuffers(1, 1, _csConstantBuffers);
	}

	StateGuard::~StateGuard() noexcept
	{
		if (_context == nullptr) {
			return;
		}

		_context->OMSetRenderTargets(kRenderTargets, _renderTargets, _depthStencil);
		_context->OMSetBlendState(_blendState, _blendFactor, _sampleMask);
		_context->RSSetViewports(_viewportCount, _viewports);
		_context->IASetPrimitiveTopology(_topology);

		_context->VSSetShader(_vertexShader, nullptr, 0);
		_context->PSSetShader(_pixelShader, nullptr, 0);
		_context->CSSetShader(_computeShader, nullptr, 0);

		_context->PSSetShaderResources(0, kShaderResources, _psResources);
		_context->CSSetShaderResources(0, 1, _csResources);

		std::uint32_t noCounts = 0;
		_context->CSSetUnorderedAccessViews(0, 1, _csUAVs, std::addressof(noCounts));
		_context->CSSetSamplers(0, 1, _csSamplers);
		_context->CSSetConstantBuffers(1, 1, _csConstantBuffers);

		ReleaseAll(_renderTargets, kRenderTargets);
		if (_depthStencil != nullptr) {
			_depthStencil->Release();
			_depthStencil = nullptr;
		}
		if (_blendState != nullptr) {
			_blendState->Release();
			_blendState = nullptr;
		}
		if (_vertexShader != nullptr) {
			_vertexShader->Release();
			_vertexShader = nullptr;
		}
		if (_pixelShader != nullptr) {
			_pixelShader->Release();
			_pixelShader = nullptr;
		}
		if (_computeShader != nullptr) {
			_computeShader->Release();
			_computeShader = nullptr;
		}

		ReleaseAll(_psResources, kShaderResources);
		ReleaseAll(_csResources, 1);
		ReleaseAll(_csUAVs, 1);
		ReleaseAll(_csSamplers, 1);
		ReleaseAll(_csConstantBuffers, 1);
	}
}
```

**Beim Schreiben nachzusehen:** ob `IAGetPrimitiveTopology` in `REX::W32::ID3D11DeviceContext`
deklariert ist und wie der Topologie-Typ dort heißt. Fehlt er, entfällt das Paar und der Kommentar
im Header nennt das — ein Zustand, den wir setzen und nicht zurückgeben können, gehört benannt, nicht
verschwiegen.

-   [ ] **Step 3: Bauen**

```pwsh
cmake --build --preset FO4
```

-   [ ] **Step 4: Commit**

```bash
git add src/Render/StateGuard.h src/Render/StateGuard.cpp CMakeLists.txt
git commit -m "feat: hand the engine its pipeline back"
```

---

## Task 8: Das Dreieck ohne Vertexpuffer

**Files:**

-   Create: `src/Render/FullscreenPass.h`
-   Create: `src/Render/FullscreenPass.cpp`
-   Create: `package/Shaders/FO4/Fullscreen.hlsl`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: `Shader::CompileVertexShader` (Task 1), `Shader::LoadSource` (C), `Render::GetDevice`,
    `Render::GetContext`, `Util::DataDirectory` (E2).
-   Produces:

    ```cpp
    namespace Render
    {
        [[nodiscard]] bool InitFullscreenPass() noexcept;
        void ReleaseFullscreenPass() noexcept;
        void DrawFullscreen() noexcept;
    }
    ```

-   [ ] **Step 1: Den Shader schreiben**

`package/Shaders/FO4/Fullscreen.hlsl`:

```hlsl
// One oversized triangle that covers the screen, built from the vertex index
// alone. No vertex buffer, no input layout, three vertices - and no seam down
// the middle the way two triangles have.
//
// Shared by every feature that draws a full-screen pass, which is why it lives
// in the base rather than in any feature's addon.

struct VertexOutput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VertexOutput main(uint id : SV_VertexID)
{
	VertexOutput output;

	// id 0 -> (0,0), id 1 -> (2,0), id 2 -> (0,2)
	output.uv = float2((id << 1) & 2, id & 2);

	// uv (0,0) is the top left of the screen, so y is flipped.
	output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);

	return output;
}
```

-   [ ] **Step 2: Den Header schreiben**

`src/Render/FullscreenPass.h`:

```cpp
#pragma once

namespace Render
{
	/// The vertex shader every full-screen pass shares: one oversized triangle
	/// built from SV_VertexID, no vertex buffer and no input layout.
	///
	/// Compiled once from Data/Shaders/FO4/Fullscreen.hlsl. Idempotent; a
	/// failure is logged once and not retried, because a shader that would not
	/// compile will not compile a second time.
	[[nodiscard]] bool InitFullscreenPass() noexcept;

	void ReleaseFullscreenPass() noexcept;

	/// Binds the shared vertex shader and the triangle list topology, then
	/// draws three vertices. The caller owns everything else: the pixel shader,
	/// the targets, the blend state.
	void DrawFullscreen() noexcept;
}
```

-   [ ] **Step 3: Die Umsetzung schreiben**

`src/Render/FullscreenPass.cpp`:

```cpp
#include "Render/FullscreenPass.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

namespace Render
{
	namespace
	{
		REX::W32::ID3D11VertexShader* g_vertexShader = nullptr;
		bool g_refused = false;
	}

	bool InitFullscreenPass() noexcept
	{
		if (g_vertexShader != nullptr) {
			return true;
		}
		if (g_refused) {
			return false;
		}

		const auto root = Util::DataDirectory() / "Shaders" / "FO4";
		const auto source = Shader::LoadSource(root, "Fullscreen.hlsl");
		if (!source) {
			REX::ERROR("fullscreen pass: {}", source.error());
			g_refused = true;
			return false;
		}

		const auto compiled =
			Shader::CompileVertexShader(source->text, "Fullscreen.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("fullscreen pass: {}", compiled.diagnostics);
			g_refused = true;
			return false;
		}

		auto* const device = GetDevice();
		if (device == nullptr ||
			device->CreateVertexShader(
				compiled.bytecode.data(),
				compiled.bytecode.size(),
				nullptr,
				std::addressof(g_vertexShader)) < 0) {
			REX::ERROR("fullscreen pass: the vertex shader could not be created");
			g_refused = true;
			return false;
		}

		static_cast<void>(SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(g_vertexShader),
			"FO4CS_VS_Fullscreen"));

		REX::INFO("fullscreen pass ready");
		return true;
	}

	void ReleaseFullscreenPass() noexcept
	{
		if (g_vertexShader != nullptr) {
			g_vertexShader->Release();
			g_vertexShader = nullptr;
		}

		g_refused = false;
	}

	void DrawFullscreen() noexcept
	{
		auto* const context = GetContext();
		if (context == nullptr || g_vertexShader == nullptr) {
			return;
		}

		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(REX::W32::D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->VSSetShader(g_vertexShader, nullptr, 0);
		context->Draw(3, 0);
	}
}
```

**Anmerkung zum Besitz:** `ReleaseFullscreenPass` gehört nicht in ein Feature-`Shutdown` — der Shader
ist geteilt. Er wird für die Prozeßlaufzeit gehalten, wie der Present-Hook. Die Funktion existiert
für die Vollständigkeit der Schnittstelle und wird in F2 nicht gerufen; das ist im Header vermerkt.

-   [ ] **Step 4: Bauen**

```pwsh
cmake --build --preset FO4
```

-   [ ] **Step 5: Commit**

```bash
git add src/Render/FullscreenPass.h src/Render/FullscreenPass.cpp package/Shaders/FO4/Fullscreen.hlsl CMakeLists.txt
git commit -m "feat: draw a triangle over the screen"
```

---

## Task 9: Die Shader des Features

**Files:**

-   Create: `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli`
-   Create: `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl`
-   Create: `package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/Modulate.hlsl`

**Interfaces:**

-   Consumes: nichts im C++.
-   Produces: drei Dateien, die Task 10 lädt und übersetzt. Der Konstantenpuffer auf `b1` ist der
    Vertrag zwischen `RaymarchCS.hlsl` und `ScreenSpaceShadows::RaymarchConstants` aus Task 10 —
    Reihenfolge und Ausrichtung müssen Feld für Feld übereinstimmen.

-   [ ] **Step 1: Bends GPU-Teil übernehmen**

```bash
mkdir -p package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows
cp "features/Screen-Space Shadows/Shaders/ScreenSpaceShadows/bend_sss_gpu.hlsli" \
   package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli
```

**Unverändert**, samt Apache-2.0-Kopf. `features/` bleibt, wie es ist — es ist Rohmaterial, kein
Auslieferungsbaum.

-   [ ] **Step 2: Den Raymarch schreiben**

`package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl`:

```hlsl
// Screen space contact shadows, Bend Studio's sweep.
//
// SAMPLE_COUNT is a compile-time constant the caller defines: the shader sizes
// a groupshared array from it. WAVE_SIZE is not - bend_sss_gpu.hlsli sets it to
// 64 itself, and BendDispatch.cpp passes the same 64 to BuildDispatchList.

// Relative to this file, not to the shader root: Shader::LoadSource splices
// with canonical.parent_path() / target (ShaderSource.cpp:152), and both files
// sit in the same directory.
#include "bend_sss_gpu.hlsli"

// FO4_DS_002 is R24G8_TYPELESS and DepthStencilTarget::srViewDepth hands it
// over as R24_UNORM_X8_TYPELESS, so depth arrives as a unorm float. This is the
// branch the Skyrim original kept for the game's own depth buffer; its
// TERRAIN_BLENDING alternative described a texture this port does not have and
// is gone rather than carried dead.
Texture2D<unorm float> DepthTexture : register(t0);

RWTexture2D<unorm float> OutputTexture : register(u0);

// Point sampling, clamped to a border of the far depth value, so that rays
// leaving the screen read "nothing in the way" rather than the edge pixel.
SamplerState PointBorderSampler : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 LightCoordinate;
	int2 WaveOffset;

	float FarDepthValue;
	float NearDepthValue;

	float2 InvDepthTextureSize;
	float2 DynamicRes;

	float SurfaceThickness;
	float BilinearThreshold;
	float ShadowContrast;
	float Padding;
};

[numthreads(WAVE_SIZE, 1, 1)] void main(
	int3 groupID : SV_GroupID,
	int groupThreadID : SV_GroupThreadID)
{
	DispatchParameters parameters;
	parameters.SetDefaults();

	parameters.LightCoordinate = LightCoordinate;
	parameters.WaveOffset = WaveOffset;

	// Read from the buffer rather than hard-coded: which end of the depth range
	// is near is a one-line experiment on the CPU side if the first run shows
	// the shadows pointing the wrong way.
	parameters.FarDepthValue = FarDepthValue;
	parameters.NearDepthValue = NearDepthValue;

	parameters.InvDepthTextureSize = InvDepthTextureSize;
	parameters.DepthTexture = DepthTexture;
	parameters.OutputTexture = OutputTexture;
	parameters.PointBorderSampler = PointBorderSampler;

	parameters.SurfaceThickness = SurfaceThickness;
	parameters.BilinearThreshold = BilinearThreshold;
	parameters.ShadowContrast = ShadowContrast;

	parameters.DynamicRes = DynamicRes;
	parameters.UsePrecisionOffset = true;

	WriteScreenSpaceShadow(parameters, groupID, groupThreadID);
}
```

-   [ ] **Step 3: Den Modulationsshader schreiben**

`package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/Modulate.hlsl`:

```hlsl
// Multiplies the shadow mask onto the engine's two light targets.
//
// The multiplication itself is the blend state - ZERO / SRC_COLOR - so this
// shader only has to hand the same mask value to both outputs. Writing
// dest * src in the shader would need the destination as an input, and
// FO4_RT_058 and FO4_RT_059 carry no unordered access view to read it through.

Texture2D<float> ShadowMask : register(t0);

struct PixelInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

struct PixelOutput
{
	float4 diffuse : SV_TARGET0;
	float4 specular : SV_TARGET1;
};

PixelOutput main(PixelInput input)
{
	// Point sampled at the pixel centre: the mask is the same size as the
	// targets, so there is nothing to interpolate.
	const float shadow = ShadowMask.Load(int3(input.position.xy, 0));

	PixelOutput output;
	output.diffuse = float4(shadow, shadow, shadow, 1.0);
	output.specular = float4(shadow, shadow, shadow, 1.0);

	return output;
}
```

-   [ ] **Step 4: Übersetzen prüfen, ohne das Spiel**

Ein kurzer Prüflauf über `ShaderCompilerTests` ist nicht der Weg — die Dateien liegen auf der
Platte, nicht im Test. Statt dessen `fxc` aus dem Windows-SDK, falls vorhanden:

```pwsh
fxc /T cs_5_0 /E main /D SAMPLE_COUNT=64 `
    package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl /Fo $env:TEMP\raymarch.cso
fxc /T ps_5_0 /E main `
    package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/Modulate.hlsl /Fo $env:TEMP\modulate.cso
fxc /T vs_5_0 /E main package/Shaders/FO4/Fullscreen.hlsl /Fo $env:TEMP\fullscreen.cso
```

Ist `fxc` nicht auf dem PATH, entfällt der Schritt und die Übersetzung wird im Spiel belegt — das
Log führt Compilerfehler mit Datei und Zeile. **Erwartet:** `Fullscreen` und `Modulate` ohne
Warnung; `RaymarchCS` möglicherweise mit Warnungen aus `bend_sss_gpu.hlsli`, was genau der Grund
für `a_warningsAsErrors = false` an dieser einen Aufrufstelle ist.

-   [ ] **Step 5: Commit**

```bash
git add package/Features/ScreenSpaceShadows package/Shaders/FO4
git commit -m "feat: write the shaders a contact shadow needs"
```

---

## Task 10: Das Feature

**Files:**

-   Create: `src/Features/ScreenSpaceShadows.h`
-   Create: `src/Features/ScreenSpaceShadows.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: alles aus Task 1 bis 9.
-   Produces: ein registriertes `Features::ScreenSpaceShadows`.

Der Konstantenpuffer muß Feld für Feld zu `RaymarchCS.hlsl` aus Task 9 passen:

```cpp
	struct alignas(16) RaymarchConstants
	{
		float lightCoordinate[4];  //  0
		std::int32_t waveOffset[2];  // 16
		float farDepthValue;         // 24
		float nearDepthValue;        // 28
		float invDepthTextureSize[2];  // 32
		float dynamicRes[2];           // 40
		float surfaceThickness;        // 48
		float bilinearThreshold;       // 52
		float shadowContrast;          // 56
		float padding;                 // 60
	};
	static_assert(sizeof(RaymarchConstants) == 64);
```

-   [ ] **Step 1: Den Header schreiben**

`src/Features/ScreenSpaceShadows.h`:

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Features/ScreenSpaceShadows/BendDispatch.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace RE
{
	class Sky;
}

namespace Features
{
	/// Contact shadows from the sun, ray marched through the scene depth.
	///
	/// The first pass of our own. It runs from Render::FramePhase, which is the
	/// one point in the frame where the G-buffer is complete and kDFLight has
	/// written its direct light but the composite has not drawn - so the mask
	/// can be multiplied onto FO4_RT_058 and FO4_RT_059 without replacing a
	/// single engine permutation.
	class ScreenSpaceShadows : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ScreenSpaceShadows"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		/// What one frame's sweep needs, worked out on the CPU before anything
		/// is bound. Nested rather than free so that it stays with the three
		/// methods that name it.
		struct RaymarchGeometry
		{
			float lightProjection[4]{};
			Features::Bend::DispatchPlan plan;
		};

		/// Runs from the frame phase, on the render thread.
		void Draw();

		[[nodiscard]] bool BuildGeometry(const RE::Sky& a_sky, RaymarchGeometry& a_out);
		void RayMarch(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11ShaderResourceView* a_depth,
			const RaymarchGeometry& a_geometry);
		void Modulate(
			REX::W32::ID3D11DeviceContext& a_context,
			REX::W32::ID3D11RenderTargetView* a_diffuse,
			REX::W32::ID3D11RenderTargetView* a_specular);

		[[nodiscard]] bool CompileRaymarch(std::uint32_t a_sampleCount);
		[[nodiscard]] bool CompileModulate();
		[[nodiscard]] bool EnsureMask();
		[[nodiscard]] bool EnsureSampler();
		[[nodiscard]] bool EnsureBlendState();

		/// The sample count the shader was last built for. Quantised to eights
		/// so that a wobbling resolution does not recompile every frame.
		[[nodiscard]] std::uint32_t ScaledSampleCount() const;

		Render::Texture _mask;
		Render::ConstantBuffer _constants;

		REX::W32::ID3D11ComputeShader* _raymarch{ nullptr };
		REX::W32::ID3D11PixelShader* _modulate{ nullptr };
		REX::W32::ID3D11SamplerState* _pointBorder{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };

		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };

		std::uint32_t _compiledSampleCount{ 0 };
		std::uint64_t _frames{ 0 };

		/// Logged once rather than per frame: a sun that is not there is the
		/// normal state indoors, not a fault.
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };

		/// The light's world rotation and the sun node's position, written once
		/// per session. Which column of that matrix is the direction is not
		/// established, and one look at the log settles it instead of three
		/// game starts.
		bool _loggedDirection{ false };
	};
}
```

-   [ ] **Step 2: `Declare` und die Registrierung**

`src/Features/ScreenSpaceShadows.cpp`, `Declare`:

```cpp
	void ScreenSpaceShadows::Declare()
	{
		Settings::DeclareFeature("ScreenSpaceShadows", true)
			.Label("feature.screen_space_shadows.name", "Screen-Space Shadows")
			.Help(
				"feature.screen_space_shadows.help",
				"Contact shadows from the sun, ray marched through the depth buffer. "
				"Adds the fine shadows a shadow map is too coarse to carry.");

		Settings::DeclareSlider("ScreenSpaceShadows/sampleCount", 1.0, 1.0, 4.0)
			.Label("feature.screen_space_shadows.sample_count", "Sample Count")
			.Help(
				"feature.screen_space_shadows.sample_count_help",
				"Multiplier for the number of samples along a ray. Higher reaches further, "
				"and costs more. Scales with the render resolution.");

		Settings::DeclareSlider("ScreenSpaceShadows/surfaceThickness", 0.02, 0.005, 0.05)
			.Label("feature.screen_space_shadows.surface_thickness", "Surface Thickness")
			.Help(
				"feature.screen_space_shadows.surface_thickness_help",
				"How thick surfaces are assumed to be. Lower gives thinner, more precise "
				"shadows.");

		Settings::DeclareSlider("ScreenSpaceShadows/bilinearThreshold", 0.02, 0.02, 1.0)
			.Label("feature.screen_space_shadows.bilinear_threshold", "Bilinear Threshold")
			.Help(
				"feature.screen_space_shadows.bilinear_threshold_help",
				"Depth difference at which an edge stops being smoothed across.");

		Settings::DeclareSlider("ScreenSpaceShadows/shadowContrast", 1.0, 0.0, 4.0)
			.Label("feature.screen_space_shadows.shadow_contrast", "Shadow Contrast")
			.Help(
				"feature.screen_space_shadows.shadow_contrast_help",
				"How hard the transition into shadow is. Higher gives sharper edges.");
	}
```

In `src/Feature/FeatureSystem.cpp`, in `RegisterAll`, **vor** `ImagespaceTint` — die Reihenfolge ist
Abbaureihenfolge rückwärts, und der, der in Engine-Speicher schreibt, soll zuerst zurückgeben:

```cpp
			// Subscribes to the frame phase and owns D3D resources, but writes
			// into no engine memory - so it goes back after ImagespaceTint's
			// pointer does.
			TheRegistry().Register(std::make_unique<ScreenSpaceShadows>());
```

Dazu `#include "Features/ScreenSpaceShadows.h"`.

-   [ ] **Step 3: `Setup`, `Frame` und `Shutdown`**

```cpp
	namespace
	{
		constexpr auto kRaymarchFile = "ScreenSpaceShadows/RaymarchCS.hlsl";
		constexpr auto kModulateFile = "ScreenSpaceShadows/Modulate.hlsl";
		constexpr std::uint32_t kMaskFormat = 61;  // DXGI_FORMAT_R8_UNORM

		/// Field for field the cbuffer PerFrame of RaymarchCS.hlsl. HLSL packs
		/// a float2 so that it does not straddle a sixteen byte boundary, and
		/// this layout is chosen so that none of them does - which is why the
		/// two offsets agree without a single padding field beyond the last.
		struct alignas(16) RaymarchConstants
		{
			float lightCoordinate[4];      //  0
			std::int32_t waveOffset[2];    // 16
			float farDepthValue;           // 24
			float nearDepthValue;          // 28
			float invDepthTextureSize[2];  // 32
			float dynamicRes[2];           // 40
			float surfaceThickness;        // 48
			float bilinearThreshold;       // 52
			float shadowContrast;          // 56
			float padding;                 // 60
		};
		static_assert(sizeof(RaymarchConstants) == 64);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}
	}

	bool ScreenSpaceShadows::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;

		if (!Render::InitFullscreenPass()) {
			return false;
		}

		if (!CompileModulate() || !CompileRaymarch(ScaledSampleCount())) {
			return false;
		}

		if (!EnsureSampler() || !EnsureBlendState()) {
			return false;
		}

		if (!_constants.Create(sizeof(RaymarchConstants), "FO4CS_CB_ScreenSpaceShadows")) {
			return false;
		}

		// The mask is sized from the swap chain, which exists by now: Setup runs
		// from Present. A resolution change is answered in Draw, not here.
		if (!EnsureMask()) {
			return false;
		}

		Render::Targets::LogSlots();

		_phase = Render::SubscribeFramePhase("ScreenSpaceShadows", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("ScreenSpaceShadows: the frame phase has no room left");
			return false;
		}

		return true;
	}

	void ScreenSpaceShadows::Frame()
	{
		// Recompiling on the render thread, without a watcher thread of its own:
		// compiling costs milliseconds and only happens on a change. The thread
		// ImagespaceTint keeps carries the bug D2 found, where a file missing on
		// the first try empties the watch for the rest of the session.
		if (_watch.Poll()) {
			REX::INFO("ScreenSpaceShadows: shader changed, recompiling");
			static_cast<void>(CompileRaymarch(_compiledSampleCount));
		}

		const auto wanted = ScaledSampleCount();
		if (wanted != _compiledSampleCount) {
			static_cast<void>(CompileRaymarch(wanted));
		}
	}

	void ScreenSpaceShadows::Shutdown()
	{
		// First, so that no callback can be in flight while the resources it
		// uses are released.
		Render::UnsubscribeFramePhase(_phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		if (_raymarch != nullptr) {
			_raymarch->Release();
			_raymarch = nullptr;
		}
		if (_modulate != nullptr) {
			_modulate->Release();
			_modulate = nullptr;
		}
		if (_pointBorder != nullptr) {
			_pointBorder->Release();
			_pointBorder = nullptr;
		}
		if (_multiply != nullptr) {
			_multiply->Release();
			_multiply = nullptr;
		}

		_mask.Release();
		_constants.Release();
		_compiledSampleCount = 0;

		// The shared full-screen vertex shader is deliberately not released:
		// it belongs to the process, not to this feature.
	}
```

`Shutdown` läuft auf dem Render-Thread, aus `Registry::Tick`, also aus `Present` — und der Rückruf
läuft ebenfalls auf dem Render-Thread, aus `SetupTechnique`. Beide können nicht gleichzeitig laufen,
weshalb das Austragen vor dem Freigeben genügt und keine Sperre nötig ist. **Das gehört als
Kommentar in den Quelltext**, weil es die Annahme ist, die den Entwurf trägt.

-   [ ] **Step 4: `Draw`**

```cpp
	void ScreenSpaceShadows::Draw()
	{
		++_frames;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("ScreenSpaceShadows: no sun, standing down");
				_reportedNoSun = true;
			}
			return;
		}
		_reportedNoSun = false;

		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const diffuse = Render::Targets::RenderTargetView(Render::Targets::kLightDiffuse);
		auto* const specular = Render::Targets::RenderTargetView(Render::Targets::kLightSpecular);

		if (depth == nullptr || diffuse == nullptr || specular == nullptr) {
			if (!_reportedNoTargets) {
				REX::ERROR("ScreenSpaceShadows: a target is missing, standing down");
				_reportedNoTargets = true;
			}
			return;
		}

		if (!EnsureMask() || _raymarch == nullptr || _modulate == nullptr) {
			return;
		}

		auto* const context = Render::GetContext();
		if (context == nullptr) {
			return;
		}

		const Render::StateGuard guard;

		// Unbind first: DS_002 still hangs on the output merger as a depth
		// stencil view, and binding it as a shader resource at the same time
		// would have D3D11 quietly drop one of the two.
		context->OMSetRenderTargets(0, nullptr, nullptr);

		RaymarchGeometry geometry{};
		if (!BuildGeometry(*sky, geometry)) {
			return;
		}

		{
			const Render::PassScope scope{ "ScreenSpaceShadows/RayMarch" };
			RayMarch(*context, depth, geometry);
		}

		{
			const Render::PassScope scope{ "ScreenSpaceShadows/Modulate" };
			Modulate(*context, diffuse, specular);
		}
	}
```

**Die Sonnenrichtung.** `RE::NiDirectionalLight` ist in commonlibf4 **nur vorwärtsdeklariert** —
Objekt unvollständig, kein Methodenaufruf möglich. Die Basis `RE::NiLight` ist dagegen vollständig
deklariert und erbt einfach von `RE::NiAVObject`, das `GetWorldRotate()` führt. Das
`NiLight`-Subobjekt liegt bei Offset null, weil die ganze Kette einfach erbt, also trägt ein
`reinterpret_cast`. `static_cast` übersetzt nicht, der Typ ist unvollständig.

Welche Spalte der Rotationsmatrix die Richtung ist, ist **unbelegt**. Deshalb wird sie nicht geraten:
`BuildGeometry` protokolliert beim ersten Aufruf alle drei Spalten **und** die Weltposition von
`sun->sunBaseNode`, und ein Blick in das Log nach dem Abnahmelauf entscheidet die Frage in einer
Messung statt in drei Spielstarts.

```cpp
	struct RaymarchGeometry
	{
		float lightProjection[4]{};
		float lightCoordinate[4]{};
		Features::Bend::DispatchPlan plan;
	};

	bool ScreenSpaceShadows::BuildGeometry(const RE::Sky& a_sky, RaymarchGeometry& a_out)
	{
		// NiDirectionalLight is forward declared only; NiLight is its base and
		// sits at offset zero, the whole chain being single inheritance.
		const auto* const light = reinterpret_cast<const RE::NiLight*>(a_sky.sun->light.get());
		const auto& rotate = light->GetWorldRotate();

		if (!_loggedDirection) {
			_loggedDirection = true;
			REX::INFO(
				"sun light rows: [{:.3f} {:.3f} {:.3f}] [{:.3f} {:.3f} {:.3f}] "
				"[{:.3f} {:.3f} {:.3f}]",
				rotate.entry[0][0], rotate.entry[0][1], rotate.entry[0][2],
				rotate.entry[1][0], rotate.entry[1][1], rotate.entry[1][2],
				rotate.entry[2][0], rotate.entry[2][1], rotate.entry[2][2]);

			if (a_sky.sun->sunBaseNode != nullptr) {
				const auto& position = a_sky.sun->sunBaseNode->GetWorldTranslate();
				REX::INFO(
					"sun node at [{:.1f} {:.1f} {:.1f}]", position.x, position.y, position.z);
			}
		}

		// Gamebryo points a light down its local Y. If the first run shows the
		// shadows falling the wrong way, this column and this sign are the two
		// things to try, in that order - both one-liners.
		float direction[3] = { rotate.entry[0][1], rotate.entry[1][1], rotate.entry[2][1] };

		const auto length = std::sqrt(
			direction[0] * direction[0] +
			direction[1] * direction[1] +
			direction[2] * direction[2]);

		if (length < 1.0e-4f) {
			return false;
		}

		// Bend wants the direction *towards* the light, and w zero because a
		// directional light sits at infinity.
		const float light4[4] = {
			-direction[0] / length,
			-direction[1] / length,
			-direction[2] / length,
			0.0f
		};

		const auto* const state = RE::BSGraphics::RendererShadowState::GetSingleton();
		if (state == nullptr) {
			return false;
		}

		// viewProjMat is four __m128 rows. Read as floats rather than through
		// an SSE intrinsic: this runs once a frame, and four loads of four
		// floats are easier to be sure about than a shuffle.
		const auto* const matrix = reinterpret_cast<const float*>(state->cameraData.viewProjMat);

		for (int column = 0; column < 4; ++column) {
			a_out.lightProjection[column] =
				light4[0] * matrix[0 * 4 + column] +
				light4[1] * matrix[1 * 4 + column] +
				light4[2] * matrix[2 * 4 + column] +
				light4[3] * matrix[3 * 4 + column];
		}

		const int viewport[2] = {
			static_cast<int>(_mask.Width()),
			static_cast<int>(_mask.Height())
		};

		a_out.plan = Features::Bend::BuildPlan(a_out.lightProjection, viewport);
		return a_out.plan.count > 0;
	}
```

**Die Zeile, die die Matrixfrage entscheidet.** Ob `viewProjMat` so herum gelesen werden muß oder
transponiert, ist ohne Messung nicht zu sagen — die Vorlage transponiert, aber sie liest eine andere
Struktur. `BuildPlan` liefert die Sonne als **Pixelposition**, und die ist prüfbar: steht die Sonne
im Bild, muß `lightCoordinate` dort liegen, wo sie zu sehen ist. Also einmal je Sekunde eine Zeile,
solange das Feature jung ist:

```cpp
		if (_frames % 180 == 0) {
			REX::INFO(
				"sun at pixel [{:.0f} {:.0f}], w {:.3f}, {} dispatch(es)",
				a_out.plan.lightCoordinate[0],
				a_out.plan.lightCoordinate[1],
				a_out.lightProjection[3],
				a_out.plan.count);
		}
```

Ist die Ausgabe Unsinn — Pixelwerte in Millionen, oder unbewegt über den Tag —, wird die Schleife
oben auf `matrix[column * 4 + row]` umgestellt. Das ist die zweite der drei Gabelungen aus Abschnitt
9 der Spec, und sie kostet eine Zeile.

**Der Raymarch.**

```cpp
	void ScreenSpaceShadows::RayMarch(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11ShaderResourceView* a_depth,
		const RaymarchGeometry& a_geometry)
	{
		auto* const uav = _mask.UAV();
		auto* const buffer = _constants.Buffer();
		auto* const sampler = _pointBorder;

		a_context.CSSetShader(_raymarch, nullptr, 0);
		a_context.CSSetShaderResources(0, 1, std::addressof(a_depth));
		a_context.CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		a_context.CSSetSamplers(0, 1, std::addressof(sampler));
		a_context.CSSetConstantBuffers(1, 1, std::addressof(buffer));

		// Read fresh every frame: there is no change notification, and whoever
		// caches a setting has to refresh it himself.
		const auto thickness =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/surfaceThickness"));
		const auto threshold =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/bilinearThreshold"));
		const auto contrast =
			static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/shadowContrast"));

		for (int i = 0; i < a_geometry.plan.count; ++i) {
			const auto& dispatch = a_geometry.plan.dispatches[i];

			RaymarchConstants data{};
			data.lightCoordinate[0] = a_geometry.plan.lightCoordinate[0];
			data.lightCoordinate[1] = a_geometry.plan.lightCoordinate[1];
			data.lightCoordinate[2] = a_geometry.plan.lightCoordinate[2];
			data.lightCoordinate[3] = a_geometry.plan.lightCoordinate[3];
			data.waveOffset[0] = dispatch.waveOffset[0];
			data.waveOffset[1] = dispatch.waveOffset[1];

			// Which end of the range is near is the third fork in section 9 of
			// the spec: an empty mask means swapping these two.
			data.farDepthValue = 1.0f;
			data.nearDepthValue = 0.0f;

			data.invDepthTextureSize[0] = 1.0f / static_cast<float>(_mask.Width());
			data.invDepthTextureSize[1] = 1.0f / static_cast<float>(_mask.Height());
			data.dynamicRes[0] = 1.0f;
			data.dynamicRes[1] = 1.0f;
			data.surfaceThickness = thickness;
			data.bilinearThreshold = threshold;
			data.shadowContrast = contrast;

			static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));

			a_context.Dispatch(
				static_cast<std::uint32_t>(dispatch.waveCount[0]),
				static_cast<std::uint32_t>(dispatch.waveCount[1]),
				static_cast<std::uint32_t>(dispatch.waveCount[2]));
		}
	}
```

Der Konstantenpuffer wird zwischen den Teilzeichnungen mit `MAP_WRITE_DISCARD` neu beschrieben, was
genau der Grund für `D3D11_USAGE_DYNAMIC` in Task 6 ist: der Treiber legt jedesmal frischen Speicher
unter, statt auf die vorige Zeichnung zu warten.

**Die Modulation.**

```cpp
	void ScreenSpaceShadows::Modulate(
		REX::W32::ID3D11DeviceContext& a_context,
		REX::W32::ID3D11RenderTargetView* a_diffuse,
		REX::W32::ID3D11RenderTargetView* a_specular)
	{
		// Let go of the mask as a UAV before taking it as an SRV: D3D11 would
		// otherwise unbind one of the two without saying so.
		REX::W32::ID3D11UnorderedAccessView* noUAV[1]{ nullptr };
		REX::W32::ID3D11ShaderResourceView* noSRV[1]{ nullptr };
		a_context.CSSetUnorderedAccessViews(0, 1, noUAV, nullptr);
		a_context.CSSetShaderResources(0, 1, noSRV);
		a_context.CSSetShader(nullptr, nullptr, 0);

		// No depth view: we are only multiplying, and DS_002 is about to be
		// bound for writing again by the composite.
		REX::W32::ID3D11RenderTargetView* targets[2]{ a_diffuse, a_specular };
		a_context.OMSetRenderTargets(2, targets, nullptr);

		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		a_context.OMSetBlendState(_multiply, blendFactor, 0xFFFFFFFFu);

		auto* const mask = _mask.SRV();
		a_context.PSSetShaderResources(0, 1, std::addressof(mask));
		a_context.PSSetShader(_modulate, nullptr, 0);

		Render::DrawFullscreen();
	}
```

Der Viewport wird nicht gesetzt: er steht bereits auf der vollen Bildgröße, weil die Engine im
Begriff ist, in dieselben Ziele zu zeichnen. Der Zustandswächter gibt ihn ohnehin zurück.

Die drei zusätzlichen Felder, die dieser Entwurf braucht, gehören in den Header aus Step 1:
`bool _loggedDirection{ false };`, `std::uint64_t _frames{ 0 };` und die Deklarationen von
`BuildGeometry`, `RayMarch` und `Modulate`. `RaymarchGeometry` und `RaymarchConstants` liegen im
anonymen Namensraum der `.cpp`, weil sie niemanden außerhalb angehen — `RaymarchGeometry` steht
allerdings in einer Signatur, also in den Header, direkt über die Klasse.

Statt dessen: **`RaymarchGeometry` wird ein privater verschachtelter Typ der Klasse.** Dann bleibt
er zusammen mit den Methoden, die ihn nennen, und der Header exportiert nichts Zusätzliches.

```cpp
			RaymarchConstants data{};
			data.lightCoordinate[0] = plan.lightCoordinate[0];
			data.lightCoordinate[1] = plan.lightCoordinate[1];
			data.lightCoordinate[2] = plan.lightCoordinate[2];
			data.lightCoordinate[3] = plan.lightCoordinate[3];
			data.waveOffset[0] = plan.dispatches[i].waveOffset[0];
			data.waveOffset[1] = plan.dispatches[i].waveOffset[1];
			data.farDepthValue = 1.0f;
			data.nearDepthValue = 0.0f;
			data.invDepthTextureSize[0] = 1.0f / static_cast<float>(_mask.Width());
			data.invDepthTextureSize[1] = 1.0f / static_cast<float>(_mask.Height());
			data.dynamicRes[0] = 1.0f;
			data.dynamicRes[1] = 1.0f;
			data.surfaceThickness =
				static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/surfaceThickness"));
			data.bilinearThreshold =
				static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/bilinearThreshold"));
			data.shadowContrast =
				static_cast<float>(Settings::GetDouble("ScreenSpaceShadows/shadowContrast"));

			static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));

			context->Dispatch(
				static_cast<std::uint32_t>(plan.dispatches[i].waveCount[0]),
				static_cast<std::uint32_t>(plan.dispatches[i].waveCount[1]),
				static_cast<std::uint32_t>(plan.dispatches[i].waveCount[2]));
```

Davor einmal binden: `CSSetShader(_raymarch)`, `CSSetShaderResources(0, 1, &depth)`,
`CSSetUnorderedAccessViews(0, 1, &uav, nullptr)`, `CSSetSamplers(0, 1, &_pointBorder)`,
`CSSetConstantBuffers(1, 1, &buffer)`. Die Einstellungen werden je Frame frisch gelesen, nicht
zwischengespeichert.

**Modulation.**

```cpp
			REX::W32::ID3D11ShaderResourceView* nothing[1]{ nullptr };
			REX::W32::ID3D11UnorderedAccessView* noUAV[1]{ nullptr };
			context->CSSetUnorderedAccessViews(0, 1, noUAV, nullptr);
			context->CSSetShaderResources(0, 1, nothing);
			context->CSSetShader(nullptr, nullptr, 0);

			REX::W32::ID3D11RenderTargetView* targets[2]{ diffuse, specular };
			context->OMSetRenderTargets(2, targets, nullptr);

			const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
			context->OMSetBlendState(_multiply, blendFactor, 0xFFFFFFFFu);

			auto* const mask = _mask.SRV();
			context->PSSetShaderResources(0, 1, std::addressof(mask));
			context->PSSetShader(_modulate, nullptr, 0);

			Render::DrawFullscreen();
```

Der Viewport wird nicht gesetzt: er steht bereits auf der vollen Bildgröße, weil die Engine gerade
dabei ist, ins selbe Ziel zu zeichnen. Der Zustandswächter gibt ihn ohnehin zurück.

-   [ ] **Step 5: Die Hilfsmethoden**

```cpp
	std::uint32_t ScreenSpaceShadows::ScaledSampleCount() const
	{
		const auto multiplier = Settings::GetDouble("ScreenSpaceShadows/sampleCount");

		const auto width = static_cast<double>(_mask.Width());
		const auto height = static_cast<double>(_mask.Height());
		if (width <= 0.0 || height <= 0.0) {
			return 64;
		}

		// Scaled against 1920x1080 so the rays reach as far across the picture
		// at any resolution, then quantised to eights: a dynamic resolution
		// that wobbles by a few pixels must not recompile the shader.
		const auto scale = std::sqrt((width * height) / (1920.0 * 1080.0));
		const auto raw = static_cast<std::uint32_t>(std::lround(multiplier * 60.0 * scale));

		return std::max<std::uint32_t>(((raw + 7u) / 8u) * 8u, 8u);
	}

	bool ScreenSpaceShadows::CompileRaymarch(std::uint32_t a_sampleCount)
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kRaymarchFile);

		// Reset the watch even when loading failed. ImagespaceTint does not,
		// which is the bug D2 found: a file missing on the first try leaves the
		// watch empty for the rest of the session, and hot reload never works
		// again.
		if (source) {
			_watch.Reset(source->files);
		} else {
			const std::filesystem::path only[] = { ShaderRoot() / kRaymarchFile };
			_watch.Reset(only);
			REX::ERROR("ScreenSpaceShadows: {}", source.error());
			return false;
		}

		const Shader::ShaderDefine defines[] = {
			{ "SAMPLE_COUNT", std::to_string(a_sampleCount) }
		};

		// Warnings are not errors here, and only here. bend_sss_gpu.hlsli is
		// third-party source kept verbatim; it computes in half throughout and
		// would not survive /WX's equivalent. Same rule as a foreign C++
		// header: suppress at the one call site, name the source, do not lower
		// the standard for everything we write ourselves.
		const auto compiled = Shader::CompileComputeShader(
			source->text, "RaymarchCS.hlsl", "main", defines, false);

		if (!compiled.Succeeded()) {
			// The previous shader stays in place. A bad edit dims nothing and
			// costs no frame; the compiler's own text, with file and line, goes
			// to the log for whoever made it.
			REX::ERROR("ScreenSpaceShadows: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11ComputeShader* shader = nullptr;
		if (device == nullptr ||
			device->CreateComputeShader(
				compiled.bytecode.data(),
				compiled.bytecode.size(),
				nullptr,
				std::addressof(shader)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the raymarch shader could not be created");
			return false;
		}

		// Swapped in only once the new one exists, so a failure never leaves
		// the feature without a shader.
		if (_raymarch != nullptr) {
			_raymarch->Release();
		}
		_raymarch = shader;
		_compiledSampleCount = a_sampleCount;

		static_cast<void>(Render::SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_raymarch),
			"FO4CS_CS_ScreenSpaceShadows"));

		REX::INFO("ScreenSpaceShadows: raymarch compiled for {} samples", a_sampleCount);
		return true;
	}
```

`CompileModulate` folgt demselben Muster mit `Shader::CompilePixelShader`, `kModulateFile`,
`CreatePixelShader` und dem Namen `FO4CS_PS_ScreenSpaceShadowsModulate` — **mit** Warnungen als
Fehler, denn diesen Shader haben wir geschrieben. Er steht nicht unter dem Dateiwächter: er ändert
sich nicht mit der Abtastzahl, und ein zweiter Wächter für eine Datei wäre Apparat ohne Ertrag.

```cpp
	bool ScreenSpaceShadows::EnsureMask()
	{
		auto* const reference =
			Render::Targets::RenderTargetTexture(Render::Targets::kLightDiffuse);
		if (reference == nullptr) {
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		reference->GetDesc(std::addressof(desc));

		if (_mask.Valid() && _mask.Width() == desc.width && _mask.Height() == desc.height) {
			return true;
		}

		// The size comes from the light target rather than from the swap chain:
		// the mask is multiplied onto that target, so those two are the pair
		// that has to agree.
		REX::INFO("ScreenSpaceShadows: mask sized {}x{}", desc.width, desc.height);
		return _mask.Create(desc.width, desc.height, kMaskFormat, "FO4CS_TEX_ScreenSpaceShadows");
	}

	bool ScreenSpaceShadows::EnsureSampler()
	{
		if (_pointBorder != nullptr) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		REX::W32::D3D11_SAMPLER_DESC desc{};
		desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_POINT;

		// Border, not clamp: a ray that leaves the screen has to read "nothing
		// in the way" rather than whatever the edge pixel happens to be, or
		// every screen edge grows a shadow of its own.
		desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_BORDER;
		desc.maxAnisotropy = 1;
		desc.minLOD = 0.0f;
		desc.maxLOD = 3.402823466e+38f;
		desc.borderColor[0] = 1.0f;
		desc.borderColor[1] = 1.0f;
		desc.borderColor[2] = 1.0f;
		desc.borderColor[3] = 1.0f;

		if (device->CreateSamplerState(std::addressof(desc), std::addressof(_pointBorder)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the point border sampler could not be created");
			return false;
		}

		return true;
	}

	bool ScreenSpaceShadows::EnsureBlendState()
	{
		if (_multiply != nullptr) {
			return true;
		}

		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		// dest = dest * src. The card does the multiplication, which is why the
		// pixel shader only has to hand over the mask - the light targets carry
		// no unordered access view to read the destination through.
		REX::W32::D3D11_BLEND_DESC desc{};
		desc.alphaToCoverageEnable = false;

		// Left off, so the description of target zero governs both targets.
		desc.independentBlendEnable = false;

		auto& target = desc.renderTarget[0];
		target.blendEnable = true;
		target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
		target.destBlend = REX::W32::D3D11_BLEND_SRC_COLOR;
		target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
		target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
		target.destBlendAlpha = REX::W32::D3D11_BLEND_SRC_ALPHA;
		target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
		target.renderTargetWriteMask =
			static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);

		if (device->CreateBlendState(std::addressof(desc), std::addressof(_multiply)) < 0) {
			REX::ERROR("ScreenSpaceShadows: the multiply blend state could not be created");
			return false;
		}

		return true;
	}
```

**Zwei Werte, die nachzuschlagen und nicht zu raten sind:** `kMaskFormat` ist
`DXGI_FORMAT_R8_UNORM`; die Zahl steht in `REX/W32/DXGI.h`, und weil `BSGraphics::Format` das
DXGI-Enum **ist**, stehen die Werte auch in `render-targets.md`. Und sämtliche Feldnamen der
`REX::W32`-Beschreibungsblöcke oben — `filter`, `addressU`, `renderTarget`, `srcBlend` — sind im
Header zu prüfen; REX schreibt klein, aber ob ein Feld `renderTarget` oder `renderTargets` heißt,
entscheidet der Header.

-   [ ] **Step 6: Bauen und den Katalog nachziehen**

```pwsh
cmake --build --preset FO4
python tools/extract-i18n.py --write
python tools/extract-i18n.py
```

Erwartet: sauberer Bau; der zweite Aufruf ohne `--write` meldet keine Abweichung.

-   [ ] **Step 7: Commit**

```bash
git add src/Features/ScreenSpaceShadows.h src/Features/ScreenSpaceShadows.cpp src/Feature/FeatureSystem.cpp CMakeLists.txt package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: cast contact shadows from the sun"
```

---

## Task 11: Auslieferung, Abnahme und Roadmap

**Files:**

-   Modify: `tools/verify-package.ps1`
-   Modify: `docs/fallout4-port/ROADMAP.md`

-   [ ] **Step 1: Die Paketprüfung erweitern**

`tools/verify-package.ps1` erwartet bisher nichts unter `Shaders/FO4` in der Basis. Ergänzen:
`Shaders/FO4/Fullscreen.hlsl` muß im Basis-Baum und im Basis-Archiv liegen, und
`Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl` im Addon-Archiv `ScreenSpaceShadows`, **nicht** in
der Basis. Die vorhandenen Prüfungen als Muster nehmen.

-   [ ] **Step 2: Paketieren und prüfen**

```pwsh
cmake --build --preset FO4 --target package
pwsh tools/verify-package.ps1
pwsh tools/verify-plugin.ps1
```

Erwartet: vier Archive statt drei — Basis, `ImagespaceTint`, `ScreenSpaceShadows`, AIO. Die
Archivprüfung schlägt zu Recht an, wenn `package` nach dem letzten Codewechsel nicht lief.

-   [ ] **Step 3: Der Abnahmelauf**

Ein Spielstart über `f4se_loader.exe`, Sanctuary bei tiefer Sonne, dazu der Root Cellar. Die neun
Schritte aus Abschnitt 10 der Spec, in dieser Reihenfolge, mit dem, was ein Fehlschlag jeweils
bedeutet. Vor dem Lauf `CommunityShadersFO4.log` beiseite legen, danach ganz lesen.

Besonders zu beachten:

-   In Schritt 2 zuerst auf `frame phase installed` und dann auf die Zeilen aus
    `Render::Targets::LogSlots()` sehen. Steht dort `light diffuse is slot 58, named FO4_RT_058`,
    zeigen die Konstanten hin, wo wir denken — die entscheidende Prüfung aus Task 5.
-   In Schritt 3 entscheidet sich die tragende Annahme aus Abschnitt 9 der Spec. Werden Himmel und
    Leuchtreklamen mit dunkler, ist der Rückweg zu wechseln, und das ist ein Befund, kein
    Fehlschlag des Teilprojekts.
-   Bleibt die Maske leer, ist `farDepthValue` / `nearDepthValue` zu tauschen — eine Zeile in
    `Draw`. Fällt der Schatten zur falschen Seite, ist das Vorzeichen der Sonnenrichtung zu drehen.

-   [ ] **Step 4: Die Zahlen festhalten**

Die Momentaufnahme über die Logtaste aus F1 auslösen und die Tafel aus dem Log übernehmen. Sie muß
`ScreenSpaceShadows`, `ScreenSpaceShadows/RayMarch` und `ScreenSpaceShadows/Modulate` führen, und
sie wird gegen die F1-Grundlinie gestellt:

```
  Frame                      6.598     6.503     7.163     7.678
```

-   [ ] **Step 5: Roadmap fortschreiben**

In `docs/fallout4-port/ROADMAP.md`:

-   Kopfzeile und die Zerlegungstabelle: F2 auf **abgeschlossen**, als Nächstes F3.
-   Einen Abschnitt „Aus Teilprojekt F2 bestätigt" nach dem Muster der vorhandenen: eine Tabelle mit
    dem Bestätigten, dann die Korrekturen an Annahmen von Spec und Plan, dann die Fallstricke für
    spätere Teilprojekte. Die Antwort auf die tragende Annahme gehört an den Anfang.
-   Was ungeprüft blieb, **ausdrücklich als ungeprüft benennen** statt stillschweigend abzuhaken.

-   [ ] **Step 6: Commit**

```bash
git add tools/verify-package.ps1 docs/fallout4-port/ROADMAP.md
git commit -m "docs: record subproject f2 acceptance"
```

-   [ ] **Step 7: Abschluß**

Erst nach der Abnahme, und erst auf Ansage: `superpowers:finishing-a-development-branch`,
Fast-Forward-Merge nach `dev`, Feature-Branch löschen. Push nur, wenn ausdrücklich verlangt.

---

## Reihenfolge und Abhängigkeiten

```
Task 1 (Übersetzer) ──────┬──► Task 8 (Vollbild) ──┐
                          └──► Task 9 (Shader) ────┤
Task 2 (Dispatcher) ──► Task 3 (Patch) ────────────┤
Task 4 (Bend) ─────────────────────────────────────┼──► Task 10 (Feature) ──► Task 11 (Abnahme)
Task 5 (Targets) ──────────────────────────────────┤
Task 6 (Ressourcen) ───────────────────────────────┤
Task 7 (Zustandswächter) ──────────────────────────┘
```

Task 1 bis 9 sind untereinander unabhängig, abgesehen von 2 vor 3 und 1 vor 8 und 9. Nach jedem
Task steht ein bauender Zustand mit grünen Tests.
