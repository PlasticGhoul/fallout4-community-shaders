# F5.5a — DLSS Super Resolution: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DLSS ersetzt das TAA der Engine, als DLAA oder als Hochskalierung aus einer
niedrigeren Renderauflösung, sichtbar im Spiel und meßbar in der Tafel.

**Architecture:** Neun Call-Hooks auf 1.11.240 (offline geprüft), ein vtable-Eintrag und ein
Detours-Prolog reichen jeden Frame an einen `Upscale::Hooks::Handler` weiter; ohne Handler gehen
sie durch. Das Feature `DLSS` ist dieser Handler: es setzt Jitter und Auflösungsfaktoren am
Frame-Anfang, wertet DLSS über NGX auf D3D11 nach `RenderEffectRange` aus und schiebt in den
Hochskalier-Modi kleinere Ersatzziele, Sampler mit Mip-Bias und eine volle Tiefe unter die
Pässe, die das brauchen. F2 bis F5 lesen Render- und Anzeigegröße aus `Render::Resolution`.

**Tech Stack:** C++23, MSVC `/W4 /WX`, D3D11 über REX, NVIDIA DLSS-SDK 310.9.1 (NGX, statische
Bibliothek `nvsdk_ngx_d.lib`), Microsoft Detours (vcpkg), HLSL `cs/ps_5_0`; Host-Tests als `main`
mit `Check`.

**Spec:** `docs/superpowers/specs/2026-10-04-fallout4-f5-5a-dlss-sr-design.md`

**Quellen, die portiert werden:** doodlums Fallout-4-Upscaler,
`https://github.com/doodlum/fo4test`, Zweig `upscaler`, Commit `ce587f5` (GPL-3.0):
`src/Upscaling.cpp` (Hooks, Ersatzziele, Sampler, Tiefe), `package/F4SE/Plugins/Upscaling/`
(`DilateMotionVectorCS.hlsl`, `OverrideDepthCS.hlsl`, `RCAS/RCAS.hlsl`,
`BSImagespaceShaderSSLRRaytracing.hlsl`). Vor Task 5 einmal flach klonen:
`git clone --depth 1 --branch upscaler https://github.com/doodlum/fo4test.git <scratchpad>/fo4test-upscaler`.

## Global Constraints

-   Nur **AE 1.11.240**. Hooks werden **nach** `Runtime::IsSupported` angelegt, nie als statische
    Objekte: `REL::THook` liest die Bytes schon im Konstruktor, und F4SE aktiviert registrierte
    Hooks in `F4SE::Init`, vor der Versionsprüfung.
-   Vor jedem Hook: `E8` an der Stelle **und** das Aufrufziel gleich der erwarteten Funktions-ID.
    Paßt eine Stelle nicht, wird **keiner** installiert, und ein `[E]` nennt sie.
-   `/W4 /WX` auf unserem Ziel; nie lockern. `<Windows.h>` nur in `src/Upscale/LensFlareDetour.cpp`,
    das ohne PCH übersetzt wird; sonst nirgends.
-   Jede sichtbare Zeichenkette über `T("key", "English")`, `.Label` und `.Help`; danach
    `python tools/extract-i18n.py --write`.
-   Kein ImGui in `src/Features/`.
-   Bauen: `<cmake>\cmake.exe --build --preset FO4` (deployt ins Spiel); Tests:
    `<cmake>\ctest.exe --test-dir build/FO4 -C Release --output-on-failure`. In jeder PowerShell
    vorher `$env:VCPKG_ROOT = "C:\vcpkg"`; `<cmake>` ist
    `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin`.
-   Jeder Host-Test wird nach dem Grünwerden gebrochen; vorher benennen, welche Prüfung fällt;
    **erst prüfen, daß der Bau geglückt ist** (eine Mutation, die an `/WX` scheitert, läßt das alte
    Programm stehen). Mutationen per Edit setzen und zurücknehmen, nie `git checkout <datei>`.
-   Conventional Commits, Titel ≤ 50, Body bei 72, Abschluß
    `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; nach „files were modified by this
    hook" erneut `git add` und committen.
-   Quelltext und HLSL mit Write/Edit schreiben, nie über die Shell.
-   Diagnose je Sekunde (`kLogInterval = 180` Frames), nie einmalig; jede Ablehnung nennt ihren
    Grund.
-   Ein Feature, das Engine-Zustand besitzt, gibt ihn in `Shutdown` zurück: Faktor 1, Jitter 0,
    TAA-Schalter, FXAA, Ersatzziele, Sampler, Tiefe, Handler `nullptr`.
-   Branch `port/f5-5a-dlss-sr`, Basis `dev`.

---

## Dateien

| Datei                                                                                 | Aufgabe                                              |
| ------------------------------------------------------------------------------------- | ---------------------------------------------------- |
| `.gitmodules`, `extern/DLSS` (Tag `v310.9.1`), `vcpkg.json`                           | DLSS-SDK, Detours                                    |
| `CMakeLists.txt`                                                                      | NGX-Header und -Bibliothek, Detours, Tests           |
| `tools/package.ps1`, `tools/verify-package.ps1`                                       | `EXTERNAL`-Datei je Feature, DLL im DLSS-Archiv      |
| `package/Features/DLSS/EXTERNAL`, `…/Shaders/FO4/DLSS/*.hlsl`                         | DLL-Abbildung, vier Shader                           |
| `src/Render/Extent.h` (neu)                                                           | `Render::Extent`                                     |
| `src/Upscale/UpscaleMath.h/.cpp` (neu), `tests/UpscaleMathTests.cpp`                  | Modi, Renderauflösung, Jitter, Mip-Bias              |
| `src/Upscale/CameraCut.h/.cpp` (neu), `tests/CameraCutTests.cpp`                      | Reset bei Kameraschnitt                              |
| `src/Render/Resolution.h/.cpp` (neu), `tests/ResolutionTests.cpp`                     | Render- und Anzeigegröße, volle Tiefe                |
| `src/Upscale/CallSite.h/.cpp` (neu), `tests/CallSiteTests.cpp`                        | `E8`-Ziel einer Aufrufstelle                         |
| `src/Upscale/EngineHooks.h/.cpp` (neu)                                                | die Hooks, `Handler`                                 |
| `src/Upscale/LensFlareDetour.cpp` (neu, ohne PCH)                                     | Detours-Prolog                                       |
| `src/XSEPlugin.cpp`                                                                   | Trampolin, `EngineHooks::Install` nach der Prüfung   |
| `src/Upscale/Ngx.h/.cpp` (neu)                                                        | NGX-Hülle                                            |
| `src/Upscale/Passes.h/.cpp` (neu)                                                     | Dilatation, Farbkopie, RCAS, volle Tiefe, SSR-Shader |
| `src/Upscale/ProxyTargets.h/.cpp` (neu)                                               | Ersatzziele, Sampler mit Mip-Bias                    |
| `src/Features/DLSS.h/.cpp` (neu), `src/Feature/FeatureSystem.cpp`                     | das Feature                                          |
| `src/Features/{ScreenSpaceShadows,ExponentialHeightFog,CloudShadows,Skylighting}.cpp` | auf `Render::Resolution`                             |

---

## Task 1: SDK, Detours, Paketregel

**Files:**

-   Modify: `.gitmodules`, `vcpkg.json`, `CMakeLists.txt`, `tools/package.ps1`,
    `tools/verify-package.ps1`, `docs/superpowers/specs/2026-10-04-fallout4-f5-5a-dlss-sr-design.md`
-   Create: `extern/DLSS` (Submodul), `package/Features/DLSS/EXTERNAL`

**Interfaces:**

-   Produces: Include-Pfad `extern/DLSS/include`, Bibliothek `nvsdk_ngx_d.lib`, `DETOURS_LIBRARY`;
    die Regel „eine Zeile `<quelle> => <ziel>` in `package/Features/<Name>/EXTERNAL`".

-   [ ] **Step 1: Submodul auf den Tag**

```bash
git submodule add https://github.com/NVIDIA/DLSS.git extern/DLSS
git -C extern/DLSS checkout v310.9.1
git add .gitmodules extern/DLSS
```

Erwartet: `git -C extern/DLSS describe --tags` gibt `v310.9.1`.

-   [ ] **Step 2: Detours in `vcpkg.json`**

In `dependencies` nach `"glaze",` die Zeile `"detours",` einfügen.

-   [ ] **Step 3: CMake** — nach `find_package(imgui CONFIG REQUIRED)`:

```cmake
# NVIDIA DLSS SDK: headers and the static NGX library for the dynamic CRT
# (/MD, CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL). The runtime DLL is not
# linked; it ships in the DLSS archive and NGX loads it at runtime.
set(DLSS_SDK_DIR "${CMAKE_SOURCE_DIR}/extern/DLSS")
if(NOT EXISTS "${DLSS_SDK_DIR}/include/nvsdk_ngx.h")
    message(FATAL_ERROR "extern/DLSS missing - git submodule update --init --recursive")
endif()

# One prolog hook (BSImagespaceShaderLensFlare) has no call site to bend:
# REL::THook handles calls and jumps only.
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" REQUIRED)
find_library(DETOURS_LIBRARY detours REQUIRED)
```

Im `target_include_directories(${PROJECT_NAME} PRIVATE …)` ergänzen:
`"${DLSS_SDK_DIR}/include"` und `"${DETOURS_INCLUDE_DIRS}"`. Beim `target_link_libraries` des
Plugins ergänzen: `"${DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib"` und
`"${DETOURS_LIBRARY}"`. Nach `target_sources(${PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})`:

```cmake
# Detours needs <Windows.h>, which the rest of the plugin never sees; this one
# file is compiled without the precompiled header that brings REX in.
set_source_files_properties(
    "${CMAKE_SOURCE_DIR}/src/Upscale/LensFlareDetour.cpp"
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
```

-   [ ] **Step 4: `EXTERNAL` — die Datei `package/Features/DLSS/EXTERNAL`:**

```
extern/DLSS/lib/Windows_x86_64/rel/nvngx_dlss.dll => F4SE/Plugins/CommunityShadersFO4/nvngx_dlss.dll
```

-   [ ] **Step 5: `tools/package.ps1`** — `Copy-FeatureTree` ersetzen durch:

```powershell
# The markers are decisions, not content: CORE says where the feature ships,
# EXTERNAL says which files come from outside package/ - a 59 MB vendor DLL
# has no business in this repository's history.
function Copy-FeatureTree([string]$From, [string]$To) {
    $from = (Resolve-Path $From).Path
    New-Item -ItemType Directory -Force -Path $To | Out-Null

    foreach ($file in Get-ChildItem $from -Recurse -File) {
        $relative = $file.FullName.Substring($from.Length).TrimStart('\', '/')
        if ($relative -eq "CORE" -or $relative -eq "EXTERNAL") {
            continue
        }
        $target = Join-Path $To $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item $file.FullName $target -Force
    }

    $external = Join-Path $from "EXTERNAL"
    if (Test-Path $external) {
        foreach ($line in Get-Content $external) {
            if ($line.Trim() -eq "") { continue }
            $parts = $line -split "=>"
            if ($parts.Count -ne 2) { throw "EXTERNAL: cannot read '$line' in $external" }
            $source = Join-Path $root $parts[0].Trim()
            if (-not (Test-Path $source)) { throw "EXTERNAL: missing $source" }
            $target = Join-Path $To $parts[1].Trim()
            New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
            Copy-Item $source $target -Force
        }
    }
}
```

-   [ ] **Step 6: `tools/verify-package.ps1`** — die Stelle suchen, die je Archiv die Dateiliste
        prüft (`Select-String -Path tools/verify-package.ps1 -Pattern "CORE"`), und daneben prüfen:
        `nvngx_dlss.dll` liegt im DLSS-Archiv und im All-in-One unter
        `F4SE/Plugins/CommunityShadersFO4/`, in keinem anderen Archiv, und weder `CORE` noch
        `EXTERNAL` liegt in irgendeinem Archiv. Jede Verletzung ein eigener Fehlertext.

-   [ ] **Step 7: Spec-Korrektur** — in Abschnitt 6.3 der Spec „(310.9.1)" ergänzen um „, 59 MB".

-   [ ] **Step 8: Bauen und packen**

```pwsh
& "<cmake>\cmake.exe" -S . --preset FO4
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\cmake.exe" --build --preset FO4 --target package
pwsh tools/verify-package.ps1
```

Erwartet: Bau grün (vcpkg holt Detours), vier Archive in `dist/` inklusive DLSS, `verify-package`
„all checks passed".

-   [ ] **Step 9: Commit** — `build: DLSS SDK, Detours, external package files`

---

## Task 2: `Render::Extent` und `Upscale::Math`

**Files:**

-   Create: `src/Render/Extent.h`, `src/Upscale/UpscaleMath.h`, `src/Upscale/UpscaleMath.cpp`,
    `tests/UpscaleMathTests.cpp`
-   Modify: `CMakeLists.txt` (Test)

**Interfaces:**

-   Produces:
    `struct Render::Extent { std::uint32_t width; std::uint32_t height; bool operator==(const Extent&) const = default; };`
    `enum class Upscale::Mode : std::uint8_t { kDLAA, kQuality, kBalanced, kPerformance, kUltraPerformance };`
    `std::optional<Mode> ModeFromName(std::string_view)`, `double ModeScale(Mode)`,
    `Render::Extent RenderSize(Render::Extent display, Mode)`,
    `std::uint32_t JitterPhaseCount(Render::Extent render, Render::Extent display)`,
    `double Halton(std::uint32_t index, std::uint32_t base)`,
    `struct Jitter { float x; float y; }; Jitter JitterOffset(std::uint32_t frame, std::uint32_t phases)`,
    `struct EngineOffset { float x; float y; }; EngineOffset ToEngineOffset(Jitter, Render::Extent reference)`,
    `float MipBias(Render::Extent render, Render::Extent display)`.

-   [ ] **Step 1: `src/Render/Extent.h`**

```cpp
#pragma once

#include <cstdint>

namespace Render
{
	/// A size in pixels. Its own type rather than two loose integers because
	/// F5.5a carries two of them through every pass - the size the world is
	/// rendered at and the size it is shown at - and mixing them up is the
	/// error this subproject is most exposed to.
	struct Extent
	{
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };

		bool operator==(const Extent&) const = default;
	};
}
```

-   [ ] **Step 2: Der Test `tests/UpscaleMathTests.cpp`**

```cpp
#include "Upscale/UpscaleMath.h"

#include <cmath>
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

	bool Near(double a_value, double a_expected, double a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}
}

int main()
{
	using namespace Upscale;
	const Render::Extent qhd{ 2560, 1440 };
	const Render::Extent fhd{ 1920, 1080 };

	Check(ModeFromName("dlaa") == Mode::kDLAA && ModeFromName("ultra_performance") == Mode::kUltraPerformance,
		"mode names as the settings write them");
	Check(!ModeFromName("ultra").has_value(), "an unknown name is no mode");

	Check(RenderSize(qhd, Mode::kDLAA) == qhd, "DLAA renders at the display size");
	Check(RenderSize(qhd, Mode::kQuality) == Render::Extent{ 1707, 960 }, "Quality at 1440p: 1707x960");
	Check(RenderSize(qhd, Mode::kBalanced) == Render::Extent{ 1485, 835 }, "Balanced at 1440p: 1485x835");
	Check(RenderSize(qhd, Mode::kPerformance) == Render::Extent{ 1280, 720 }, "Performance at 1440p: 1280x720");
	Check(RenderSize(qhd, Mode::kUltraPerformance) == Render::Extent{ 853, 480 }, "Ultra Performance at 1440p: 853x480");
	Check(RenderSize(fhd, Mode::kQuality) == Render::Extent{ 1280, 720 }, "Quality at 1080p: 1280x720");

	Check(JitterPhaseCount(qhd, qhd) == 8, "eight phases at native resolution");
	Check(JitterPhaseCount(Render::Extent{ 1280, 720 }, qhd) == 32, "thirty-two at half resolution");

	Check(Near(Halton(1, 2), 0.5, 1e-12) && Near(Halton(2, 2), 0.25, 1e-12) && Near(Halton(3, 2), 0.75, 1e-12),
		"Halton base 2: 1/2, 1/4, 3/4");
	Check(Near(Halton(1, 3), 1.0 / 3.0, 1e-12) && Near(Halton(2, 3), 2.0 / 3.0, 1e-12) && Near(Halton(3, 3), 1.0 / 9.0, 1e-12),
		"Halton base 3: 1/3, 2/3, 1/9");

	bool inRange = true;
	bool repeats = true;
	for (std::uint32_t frame = 0; frame < 64; ++frame) {
		const auto j = JitterOffset(frame, 8);
		inRange = inRange && j.x >= -0.5f && j.x < 0.5f && j.y >= -0.5f && j.y < 0.5f;
		const auto again = JitterOffset(frame + 8, 8);
		repeats = repeats && j.x == again.x && j.y == again.y;
	}
	Check(inRange, "jitter stays within half a pixel");
	Check(repeats, "the sequence repeats after its phase count");
	const auto first = JitterOffset(0, 8);
	Check(Near(first.x, 0.0, 1e-6) && Near(first.y, 1.0 / 3.0 - 0.5, 1e-6), "frame 0 is Halton index 1, minus one half");

	const auto offset = ToEngineOffset(Jitter{ 0.25f, -0.5f }, qhd);
	Check(Near(offset.x, -0.5 / 2560.0, 1e-9), "engine x offset is negated, in NDC");
	Check(Near(offset.y, -1.0 / 1440.0, 1e-9), "engine y offset keeps its sign, in NDC");

	Check(Near(MipBias(qhd, qhd), -1.0, 1e-6), "native: bias minus one");
	Check(Near(MipBias(Render::Extent{ 1280, 720 }, qhd), -2.0, 1e-6), "half resolution: bias minus two");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

-   [ ] **Step 3: Test in `CMakeLists.txt`** — nach dem Block `SkylightingProjectionTests`
        (`add_test(NAME SkylightingProjection …)`) einen gleich gebauten Block für
        `UpscaleMathTests` mit den Quellen `tests/UpscaleMathTests.cpp` und
        `src/Upscale/UpscaleMath.cpp`, `add_test(NAME UpscaleMath COMMAND UpscaleMathTests)`.

-   [ ] **Step 4: Bauen, Fehlschlag ansehen** — erwartet: Übersetzungsfehler, `UpscaleMath.h`
        fehlt.

-   [ ] **Step 5: `src/Upscale/UpscaleMath.h`**

```cpp
#pragma once

#include "Render/Extent.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace Upscale
{
	enum class Mode : std::uint8_t
	{
		kDLAA,
		kQuality,
		kBalanced,
		kPerformance,
		kUltraPerformance,
	};

	/// The names DLSS/mode stores.
	[[nodiscard]] std::optional<Mode> ModeFromName(std::string_view a_name) noexcept;

	/// Render size over display size, per axis - NVIDIA's DLSS ratios. In the
	/// game the optimal settings NGX reports win; this is what is used when
	/// that query fails, and what the tests pin.
	[[nodiscard]] double ModeScale(Mode a_mode) noexcept;
	[[nodiscard]] Render::Extent RenderSize(Render::Extent a_display, Mode a_mode) noexcept;

	/// FSR's rule, which doodlum's upscaler uses for DLSS as well: eight
	/// phases times the square of the upscale ratio, truncated.
	[[nodiscard]] std::uint32_t JitterPhaseCount(Render::Extent a_render, Render::Extent a_display) noexcept;

	/// Radical inverse of a_index (from 1) in a_base.
	[[nodiscard]] double Halton(std::uint32_t a_index, std::uint32_t a_base) noexcept;

	/// In render pixels, in [-0.5, 0.5): Halton 2 and 3 at index
	/// (frame mod phases) + 1, minus one half.
	struct Jitter
	{
		float x;
		float y;
	};
	[[nodiscard]] Jitter JitterOffset(std::uint32_t a_frame, std::uint32_t a_phases) noexcept;

	/// What BSGraphics::State::offsetX/Y take: NDC, x negated (doodlum,
	/// fo4test upscaler). a_reference is the size the engine relates the
	/// offset to; doodlum passes the display size even when upscaling, and the
	/// first probe run decides whether that holds on 1.11.240.
	struct EngineOffset
	{
		float x;
		float y;
	};
	[[nodiscard]] EngineOffset ToEngineOffset(Jitter a_jitter, Render::Extent a_reference) noexcept;

	/// NVIDIA's recommendation for DLSS: log2(render / display) - 1.
	[[nodiscard]] float MipBias(Render::Extent a_render, Render::Extent a_display) noexcept;
}
```

-   [ ] **Step 6: `src/Upscale/UpscaleMath.cpp`**

```cpp
#include "Upscale/UpscaleMath.h"

#include <cmath>

namespace Upscale
{
	std::optional<Mode> ModeFromName(std::string_view a_name) noexcept
	{
		if (a_name == "dlaa") {
			return Mode::kDLAA;
		}
		if (a_name == "quality") {
			return Mode::kQuality;
		}
		if (a_name == "balanced") {
			return Mode::kBalanced;
		}
		if (a_name == "performance") {
			return Mode::kPerformance;
		}
		if (a_name == "ultra_performance") {
			return Mode::kUltraPerformance;
		}
		return std::nullopt;
	}

	double ModeScale(Mode a_mode) noexcept
	{
		switch (a_mode) {
			case Mode::kQuality:
				return 2.0 / 3.0;
			case Mode::kBalanced:
				return 0.58;
			case Mode::kPerformance:
				return 0.5;
			case Mode::kUltraPerformance:
				return 1.0 / 3.0;
			case Mode::kDLAA:
			default:
				return 1.0;
		}
	}

	Render::Extent RenderSize(Render::Extent a_display, Mode a_mode) noexcept
	{
		const auto scale = ModeScale(a_mode);
		return Render::Extent{
			static_cast<std::uint32_t>(std::lround(a_display.width * scale)),
			static_cast<std::uint32_t>(std::lround(a_display.height * scale)),
		};
	}

	std::uint32_t JitterPhaseCount(Render::Extent a_render, Render::Extent a_display) noexcept
	{
		if (a_render.width == 0) {
			return 8;
		}
		const auto ratio = static_cast<double>(a_display.width) / static_cast<double>(a_render.width);
		return static_cast<std::uint32_t>(8.0 * ratio * ratio);
	}

	double Halton(std::uint32_t a_index, std::uint32_t a_base) noexcept
	{
		double fraction = 1.0;
		double result = 0.0;
		while (a_index > 0) {
			fraction /= a_base;
			result += fraction * (a_index % a_base);
			a_index /= a_base;
		}
		return result;
	}

	Jitter JitterOffset(std::uint32_t a_frame, std::uint32_t a_phases) noexcept
	{
		const auto index = (a_phases == 0 ? 0 : a_frame % a_phases) + 1;
		return Jitter{
			static_cast<float>(Halton(index, 2) - 0.5),
			static_cast<float>(Halton(index, 3) - 0.5),
		};
	}

	EngineOffset ToEngineOffset(Jitter a_jitter, Render::Extent a_reference) noexcept
	{
		return EngineOffset{
			2.0f * -a_jitter.x / static_cast<float>(a_reference.width),
			2.0f * a_jitter.y / static_cast<float>(a_reference.height),
		};
	}

	float MipBias(Render::Extent a_render, Render::Extent a_display) noexcept
	{
		const auto ratio = static_cast<double>(a_render.width) / static_cast<double>(a_display.width);
		return static_cast<float>(std::log2(ratio) - 1.0);
	}
}
```

-   [ ] **Step 7: Bauen und testen** — erwartet `all checks passed` in `UpscaleMathTests`.

-   [ ] **Step 8: Mutationen** (je: Edit, Bau grün prüfen, Test, zurück):

    1. `std::lround` → `static_cast<std::uint32_t>(a_display.width * scale)` (abschneiden) — fällt
       „Quality at 1440p" (1706), „Balanced" (1484).
    2. In `ToEngineOffset` das Minus bei x entfernen — fällt „engine x offset is negated".
    3. In `JitterOffset` `+ 1` entfernen — fällt „frame 0 is Halton index 1".

-   [ ] **Step 9: Commit** — `feat: upscale modes, jitter and mip bias`

---

## Task 3: `Upscale::CameraCut`

**Files:**

-   Create: `src/Upscale/CameraCut.h`, `src/Upscale/CameraCut.cpp`, `tests/CameraCutTests.cpp`
-   Modify: `CMakeLists.txt` (Test `CameraCut`)

**Interfaces:**

-   Produces: `struct Upscale::CameraState { double position[3]; double forward[3]; double fovDegrees; };`
    `struct Upscale::CutThresholds { double distance{ 500.0 }; double degrees{ 30.0 }; double fovDegrees{ 5.0 }; };`
    `bool Upscale::IsCameraCut(const CameraState& a_previous, const CameraState& a_current, const CutThresholds& a_limits = {}) noexcept`.

-   [ ] **Step 1: Test `tests/CameraCutTests.cpp`** (Gerüst mit `Check` wie Task 2)

```cpp
#include "Upscale/CameraCut.h"

#include <cmath>
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

	Upscale::CameraState At(double a_x, double a_yawDegrees, double a_fov)
	{
		const double radians = a_yawDegrees * 3.14159265358979323846 / 180.0;
		return Upscale::CameraState{ { a_x, 0.0, 0.0 }, { std::sin(radians), std::cos(radians), 0.0 }, a_fov };
	}
}

int main()
{
	using Upscale::IsCameraCut;
	const auto start = At(-69481.0, 0.0, 90.0);

	Check(!IsCameraCut(start, start), "a camera standing still is no cut");
	Check(!IsCameraCut(start, At(-69481.0 + 20.0, 0.0, 90.0)), "a vertibird's twenty units a frame is no cut");
	Check(IsCameraCut(start, At(-69481.0 + 3000.0, 0.0, 90.0)), "three thousand units in a frame is a cut");
	Check(!IsCameraCut(start, At(-69481.0, 10.0, 90.0)), "a flick of ten degrees is no cut");
	Check(IsCameraCut(start, At(-69481.0, 120.0, 90.0)), "a hundred and twenty degrees is a cut");
	Check(!IsCameraCut(start, At(-69481.0, 0.0, 88.0)), "sights easing in by two degrees is no cut");
	Check(IsCameraCut(start, At(-69481.0, 0.0, 55.0)), "a jump of thirty-five degrees in field of view is a cut");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

-   [ ] **Step 2: Bauen, Fehlschlag ansehen** (Header fehlt).

-   [ ] **Step 3: `src/Upscale/CameraCut.h`**

```cpp
#pragma once

namespace Upscale
{
	/// What a frame's camera is, for telling a cut from movement.
	struct CameraState
	{
		double position[3];
		double forward[3];
		double fovDegrees;
	};

	/// A frame at 180 fps moves a sprinting player three units and a
	/// vertibird twenty; five hundred is a teleport, a load or a cut to a
	/// dialogue camera. Thirty degrees in one frame is beyond a mouse flick,
	/// five degrees of field of view beyond the ease into iron sights.
	struct CutThresholds
	{
		double distance{ 500.0 };
		double degrees{ 30.0 };
		double fovDegrees{ 5.0 };
	};

	/// True when the history DLSS keeps no longer describes this frame.
	[[nodiscard]] bool IsCameraCut(
		const CameraState& a_previous,
		const CameraState& a_current,
		const CutThresholds& a_limits = {}) noexcept;
}
```

-   [ ] **Step 4: `src/Upscale/CameraCut.cpp`**

```cpp
#include "Upscale/CameraCut.h"

#include <algorithm>
#include <cmath>

namespace Upscale
{
	bool IsCameraCut(const CameraState& a_previous, const CameraState& a_current, const CutThresholds& a_limits) noexcept
	{
		double squared = 0.0;
		double dot = 0.0;
		double lengthPrevious = 0.0;
		double lengthCurrent = 0.0;
		for (int i = 0; i < 3; ++i) {
			const auto delta = a_current.position[i] - a_previous.position[i];
			squared += delta * delta;
			dot += a_previous.forward[i] * a_current.forward[i];
			lengthPrevious += a_previous.forward[i] * a_previous.forward[i];
			lengthCurrent += a_current.forward[i] * a_current.forward[i];
		}
		if (std::sqrt(squared) > a_limits.distance) {
			return true;
		}
		const auto lengths = std::sqrt(lengthPrevious * lengthCurrent);
		if (lengths > 0.0) {
			const auto cosine = std::clamp(dot / lengths, -1.0, 1.0);
			const auto degrees = std::acos(cosine) * 180.0 / 3.14159265358979323846;
			if (degrees > a_limits.degrees) {
				return true;
			}
		}
		return std::abs(a_current.fovDegrees - a_previous.fovDegrees) > a_limits.fovDegrees;
	}
}
```

-   [ ] **Step 5: Bauen und testen** — `all checks passed`.

-   [ ] **Step 6: Mutationen:** (1) `> a_limits.distance` → `> a_limits.distance * 10.0` — fällt
        „three thousand units"; (2) die FOV-Zeile durch `return false;` — fällt „a jump of
        thirty-five degrees".

-   [ ] **Step 7: Commit** — `feat: tell a camera cut from movement`

---

## Task 4: `Render::Resolution`

**Files:**

-   Create: `src/Render/Resolution.h`, `src/Render/Resolution.cpp`, `tests/ResolutionTests.cpp`
-   Modify: `CMakeLists.txt` (Test `Resolution`, Quellen `tests/ResolutionTests.cpp` und
    `src/Render/Resolution.cpp`)

**Interfaces:**

-   Produces:
    `struct Render::Resolution::Frame { Extent display; Extent render; bool depthFull; };`
    `Extent DepthExtent(const Frame&) noexcept` (rein),
    `std::array<float, 2> RenderToTexture(const Frame&, Extent texture) noexcept` (rein: Faktor,
    mit dem eine UV über dem Renderbereich auf eine Textur der Größe `texture` abgebildet wird),
    `Frame Current() noexcept`, `void Publish(Extent display, Extent render) noexcept`,
    `void SetDepthFull(bool) noexcept`, `void Withdraw() noexcept`.

-   [ ] **Step 1: Test `tests/ResolutionTests.cpp`**

```cpp
#include "Render/Resolution.h"

#include <cmath>
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
}

int main()
{
	using namespace Render;
	const Resolution::Frame quality{ Extent{ 2560, 1440 }, Extent{ 1707, 960 }, false };

	Check(Resolution::DepthExtent(quality) == Extent{ 1707, 960 }, "before the swap, depth covers the render rectangle");
	auto swapped = quality;
	swapped.depthFull = true;
	Check(Resolution::DepthExtent(swapped) == Extent{ 2560, 1440 }, "while the full depth is bound, it covers the display");

	const auto scale = Resolution::RenderToTexture(quality, Extent{ 2560, 1440 });
	Check(std::abs(scale[0] - 1707.0f / 2560.0f) < 1e-6f && std::abs(scale[1] - 960.0f / 1440.0f) < 1e-6f,
		"a uv over the render rectangle maps into the full texture's corner");
	const auto proxy = Resolution::RenderToTexture(quality, Extent{ 1707, 960 });
	Check(proxy[0] == 1.0f && proxy[1] == 1.0f, "a texture of the render size is used whole");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

-   [ ] **Step 2: Bauen, Fehlschlag ansehen.**

-   [ ] **Step 3: `src/Render/Resolution.h`**

```cpp
#pragma once

#include "Render/Extent.h"

#include <array>

namespace Render::Resolution
{
	/// The two sizes of a frame, and which depth is bound. With DLSS
	/// upscaling the world is drawn into the top left render rectangle of the
	/// full size targets; during the composite and the lens flare the DLSS
	/// feature binds a depth at display size instead. Both depths are the same
	/// texture size, so the description cannot tell them apart - this can.
	struct Frame
	{
		Extent display;
		Extent render;
		bool depthFull{ false };
	};

	/// The pixels of the depth texture that hold the picture.
	[[nodiscard]] Extent DepthExtent(const Frame& a_frame) noexcept;

	/// The factor that takes a uv over the render rectangle into a texture of
	/// size a_texture: render / texture per axis, one for a texture that is
	/// the render rectangle itself.
	[[nodiscard]] std::array<float, 2> RenderToTexture(const Frame& a_frame, Extent a_texture) noexcept;

	/// This frame. Without DLSS both sizes are those of RT_004 and nothing
	/// is swapped.
	[[nodiscard]] Frame Current() noexcept;

	/// Called by the DLSS feature at the start of every frame it runs.
	void Publish(Extent a_display, Extent a_render) noexcept;
	void SetDepthFull(bool a_full) noexcept;
	/// Called by the DLSS feature when it stops.
	void Withdraw() noexcept;
}
```

-   [ ] **Step 4: `src/Render/Resolution.cpp`** — die reinen Funktionen oben, die Laufzeit unten.
        `Current()` fragt `Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR)` nur, wenn
        nichts veröffentlicht ist; im Host-Test wird `Current()` nicht gerufen, die Engine-Aufrufe
        lösen erst bei Gebrauch auf (wie `Camera.cpp` in `CameraTests`).

```cpp
#include "Render/Resolution.h"

#include "Render/Targets.h"

#include <atomic>

namespace Render::Resolution
{
	namespace
	{
		std::atomic<bool> g_published{ false };
		std::atomic<bool> g_depthFull{ false };
		Extent g_display{};
		Extent g_render{};
	}

	Extent DepthExtent(const Frame& a_frame) noexcept
	{
		return a_frame.depthFull ? a_frame.display : a_frame.render;
	}

	std::array<float, 2> RenderToTexture(const Frame& a_frame, Extent a_texture) noexcept
	{
		if (a_texture.width == 0 || a_texture.height == 0) {
			return { 1.0f, 1.0f };
		}
		return {
			static_cast<float>(a_frame.render.width) / static_cast<float>(a_texture.width),
			static_cast<float>(a_frame.render.height) / static_cast<float>(a_texture.height),
		};
	}

	Frame Current() noexcept
	{
		if (g_published.load(std::memory_order_acquire)) {
			return Frame{ g_display, g_render, g_depthFull.load(std::memory_order_acquire) };
		}
		Extent size{};
		if (auto* const texture = Targets::RenderTargetTexture(Targets::kSceneHDR); texture != nullptr) {
			REX::W32::D3D11_TEXTURE2D_DESC desc{};
			texture->GetDesc(std::addressof(desc));
			size = Extent{ desc.width, desc.height };
		}
		return Frame{ size, size, false };
	}

	void Publish(Extent a_display, Extent a_render) noexcept
	{
		g_display = a_display;
		g_render = a_render;
		g_published.store(true, std::memory_order_release);
	}

	void SetDepthFull(bool a_full) noexcept
	{
		g_depthFull.store(a_full, std::memory_order_release);
	}

	void Withdraw() noexcept
	{
		g_published.store(false, std::memory_order_release);
		g_depthFull.store(false, std::memory_order_release);
	}
}
```

Der Test linkt `src/Render/Targets.cpp` mit, falls der Linker `RenderTargetTexture` verlangt
(dieselbe Lösung wie `CameraTests`).

-   [ ] **Step 5: Bauen und testen.**

-   [ ] **Step 6: Mutationen:** (1) `DepthExtent` gibt immer `a_frame.render` — fällt „while the full
        depth is bound"; (2) `RenderToTexture` teilt `texture / render` — fällt „maps into the full
        texture's corner".

-   [ ] **Step 7: Commit** — `feat: render and display size in one place`

---

## Task 5: Aufrufstellen prüfen, Hooks installieren

**Files:**

-   Create: `src/Upscale/CallSite.h/.cpp`, `tests/CallSiteTests.cpp`, `src/Upscale/EngineHooks.h`,
    `src/Upscale/EngineHooks.cpp`, `src/Upscale/LensFlareDetour.cpp`
-   Modify: `src/XSEPlugin.cpp`, `CMakeLists.txt` (Test `CallSite`)

**Interfaces:**

-   Produces:
    `std::optional<std::uintptr_t> Upscale::CallTarget(std::span<const std::uint8_t, 5> a_bytes, std::uintptr_t a_site) noexcept`;
    `class Upscale::Hooks::Handler` (siehe Step 4);
    `bool Upscale::Hooks::Install() noexcept`, `bool Installed() noexcept`,
    `std::string_view FailedSite() noexcept`, `void SetHandler(Handler*) noexcept`,
    `void SetDynamicResolutionViewport(bool) noexcept`,
    `void SkipDynamicResolutionUpdate(bool) noexcept`;
    `bool Upscale::AttachLensFlare(void** a_original, void* a_replacement) noexcept`.

-   [ ] **Step 1: Test `tests/CallSiteTests.cpp`**

```cpp
#include "Upscale/CallSite.h"

#include <array>
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
}

int main()
{
	// The UpdateTemporalData site on 1.11.240: rva 0x21f0e4c, e8 2f c7 63 ff,
	// target rva 0x182d580 (id 2277095).
	const std::array<std::uint8_t, 5> call{ 0xE8, 0x2F, 0xC7, 0x63, 0xFF };
	const auto target = Upscale::CallTarget(call, 0x21F0E4C);
	Check(target.has_value() && *target == 0x182D580, "a backward call resolves to its target");

	const std::array<std::uint8_t, 5> forward{ 0xE8, 0xA6, 0x6F, 0x02, 0x00 };
	const auto ahead = Upscale::CallTarget(forward, 0x21F8BB5);
	Check(ahead.has_value() && *ahead == 0x221FB60, "a forward call resolves to its target");

	const std::array<std::uint8_t, 5> jump{ 0xE9, 0x00, 0x00, 0x00, 0x00 };
	Check(!Upscale::CallTarget(jump, 0x1000).has_value(), "a jump is not a call site");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

(`0x21f0e4c` = `0x21f0ac0 + 0x38c`; `0x21f8bb5` = `0x21f82a0 + 0x915`; beide aus `addrcheck.py`.)

-   [ ] **Step 2: Bauen, Fehlschlag ansehen.**

-   [ ] **Step 3: `src/Upscale/CallSite.h/.cpp`**

```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace Upscale
{
	/// The target of a five byte relative call (E8 rel32) at a_site, or
	/// nothing when the bytes are not one. Pure, so that the check every hook
	/// makes before it writes can be tested without the game.
	[[nodiscard]] std::optional<std::uintptr_t> CallTarget(std::span<const std::uint8_t, 5> a_bytes, std::uintptr_t a_site) noexcept;
}
```

```cpp
#include "Upscale/CallSite.h"

#include <cstring>

namespace Upscale
{
	std::optional<std::uintptr_t> CallTarget(std::span<const std::uint8_t, 5> a_bytes, std::uintptr_t a_site) noexcept
	{
		if (a_bytes[0] != 0xE8) {
			return std::nullopt;
		}
		std::int32_t relative = 0;
		std::memcpy(std::addressof(relative), a_bytes.data() + 1, sizeof(relative));
		return a_site + 5 + static_cast<std::intptr_t>(relative);
	}
}
```

Test bauen und laufen lassen; Mutation: `a_site + 5` → `a_site` — fallen beide
„resolves"-Prüfungen.

-   [ ] **Step 4: `src/Upscale/EngineHooks.h`**

```cpp
#pragma once

#include <string_view>

namespace Upscale::Hooks
{
	/// What the DLSS feature does at each hooked point. Every call arrives on
	/// the render thread, inside the engine's frame. The defaults are what
	/// the engine does without us.
	class Handler
	{
	public:
		virtual ~Handler() = default;

		/// After BSGraphics::State::UpdateTemporalData: jitter and the
		/// resolution ratios for this frame.
		virtual void AfterTemporalData() noexcept {}
		/// ImageSpaceEffectTemporalAA::IsActive; false keeps the engine's TAA
		/// out of the chain.
		[[nodiscard]] virtual bool AllowTemporalAA() noexcept { return true; }
		/// After DrawWorld_Imagespace's RenderEffectRange - where DLSS runs.
		virtual void AfterEffectRange() noexcept {}
		/// Before SetUseDynamicResolutionViewportAsDefaultViewport.
		virtual void BeforeDynamicViewportDefault() noexcept {}
		/// Around the deferred prepass and the forward pass (sampler bias).
		virtual void AroundWorldPass(bool a_before) noexcept { static_cast<void>(a_before); }
		/// Around the deferred composite (proxy targets, full depth).
		virtual void AroundComposite(bool a_before) noexcept { static_cast<void>(a_before); }
		/// Around the lens flare (full depth).
		virtual void AroundLensFlare(bool a_before) noexcept { static_cast<void>(a_before); }
		/// After BSImagespaceShaderSSLRRaytracing has set its technique up.
		virtual void AfterReflectionsSetup() noexcept {}
	};

	/// Verifies and installs every hook; called once from F4SEPlugin_Load,
	/// after the runtime check. All or nothing.
	[[nodiscard]] bool Install() noexcept;
	[[nodiscard]] bool Installed() noexcept;
	/// The site that refused, for the feature's refusal line.
	[[nodiscard]] std::string_view FailedSite() noexcept;

	/// nullptr passes everything through.
	void SetHandler(Handler* a_handler) noexcept;

	/// The engine's own SetUseDynamicResolutionViewportAsDefaultViewport, for
	/// the handler to call.
	void SetDynamicResolutionViewport(bool a_enabled) noexcept;

	/// The engine adjusts its dynamic resolution ratios each frame from its
	/// own heuristics; while we set them, that call goes to an empty function
	/// instead (doodlum nops it). Toggled from the render thread only.
	void SkipDynamicResolutionUpdate(bool a_skip) noexcept;
}
```

-   [ ] **Step 5: `src/Upscale/LensFlareDetour.cpp`** (ohne PCH, Task 1 Step 3)

```cpp
// Compiled without the precompiled header: Detours needs <Windows.h>, and
// nothing else in the plugin may see it.
#include <Windows.h>

#include <detours/detours.h>

namespace Upscale
{
	bool AttachLensFlare(void** a_original, void* a_replacement) noexcept
	{
		if (DetourTransactionBegin() != NO_ERROR) {
			return false;
		}
		DetourUpdateThread(GetCurrentThread());
		if (DetourAttach(a_original, a_replacement) != NO_ERROR) {
			DetourTransactionAbort();
			return false;
		}
		return DetourTransactionCommit() == NO_ERROR;
	}
}
```

-   [ ] **Step 6: `src/Upscale/EngineHooks.cpp`**

```cpp
#include "Upscale/EngineHooks.h"

#include "Render/VTablePatch.h"
#include "Upscale/CallSite.h"

#include <RE/IDs_VTABLE.h>

#include <REL/THook.h>

#include <array>
#include <atomic>
#include <cstring>
#include <memory>

namespace Upscale
{
	bool AttachLensFlare(void** a_original, void* a_replacement) noexcept;
}

namespace Upscale::Hooks
{
	namespace
	{
		/// One call site: the function that holds it, the offset of the E8,
		/// and the function the call must reach. Verified offline on 1.11.240
		/// (spec, finding 1); a mismatch means another plugin got there first
		/// or the game changed, and either way nothing is written.
		struct Site
		{
			const char* name;
			std::uint64_t function;
			std::ptrdiff_t offset;
			std::uint64_t callee;
		};

		constexpr Site kTemporalData{ "State::UpdateTemporalData", 2318286, 0x38C, 2277095 };
		constexpr Site kEffectRange{ "Imagespace RenderEffectRange", 2318322, 0x83, 2316593 };
		constexpr Site kViewportDefault{ "SetUseDynamicResolutionViewportAsDefaultViewport", 2318322, 0xC5, 2277194 };
		constexpr Site kPrePass{ "PreUI DeferredPrePass", 2318321, 0x2E3, 2318301 };
		constexpr Site kForward{ "PreUI Forward", 2318321, 0x3A6, 2318315 };
		constexpr Site kDynamicUpdate{ "UpdateDynamicResolution", 2318321, 0x29F, 2277195 };
		constexpr Site kComposite{ "DeferredComposite RenderPassImmediately", 2318313, 0x915, 2318699 };
		constexpr Site kReflections{ "SSLRRaytracing BeginTechnique", 2317302, 0x1C, 2318876 };
		constexpr std::uint64_t kLensFlare = 2317547;
		constexpr std::size_t kTemporalAAIsActive = 8;

		std::atomic<Handler*> g_handler{ nullptr };
		bool g_installed{ false };
		std::string_view g_failed{};

		std::unique_ptr<REL::THook<void(void*)>> g_temporalData;
		std::unique_ptr<REL::THook<void(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>> g_effectRange;
		std::unique_ptr<REL::THook<void(void*, bool)>> g_viewportDefault;
		std::unique_ptr<REL::THook<void(void*)>> g_prePass;
		std::unique_ptr<REL::THook<void(void*)>> g_forward;
		std::unique_ptr<REL::THook<void(void*)>> g_dynamicUpdate;
		std::unique_ptr<REL::THook<void(void*, std::uint32_t, bool)>> g_composite;
		std::unique_ptr<REL::THook<void(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>> g_reflections;
		Render::VTablePatch g_temporalAA;
		void* g_lensFlare{ nullptr };
		void* g_viewportObject{ nullptr };

		Handler* Current() noexcept { return g_handler.load(std::memory_order_acquire); }

		bool Verify(const Site& a_site) noexcept
		{
			const auto address = REL::ID(a_site.function).address() + a_site.offset;
			std::array<std::uint8_t, 5> bytes{};
			std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), bytes.size());
			const auto target = CallTarget(bytes, address);
			if (!target || *target != REL::ID(a_site.callee).address()) {
				REX::ERROR("upscale hooks: {} does not call what 1.11.240 calls there; nothing installed", a_site.name);
				g_failed = a_site.name;
				return false;
			}
			return true;
		}

		void TemporalData(void* a_state)
		{
			(*g_temporalData)(a_state);
			if (auto* const handler = Current()) {
				handler->AfterTemporalData();
			}
		}

		void EffectRange(void* a_self, std::uint32_t a_1, std::uint32_t a_2, std::uint32_t a_3, std::uint32_t a_4)
		{
			(*g_effectRange)(a_self, a_1, a_2, a_3, a_4);
			g_viewportObject = a_self;
			if (auto* const handler = Current()) {
				handler->AfterEffectRange();
			}
		}

		void ViewportDefault(void* a_self, bool a_enabled)
		{
			g_viewportObject = a_self;
			if (auto* const handler = Current()) {
				handler->BeforeDynamicViewportDefault();
			}
			(*g_viewportDefault)(a_self, a_enabled);
		}

		void PrePass(void* a_drawWorld)
		{
			auto* const handler = Current();
			if (handler) {
				handler->AroundWorldPass(true);
			}
			(*g_prePass)(a_drawWorld);
			if (handler) {
				handler->AroundWorldPass(false);
			}
		}

		void Forward(void* a_drawWorld)
		{
			auto* const handler = Current();
			if (handler) {
				handler->AroundWorldPass(true);
			}
			(*g_forward)(a_drawWorld);
			if (handler) {
				handler->AroundWorldPass(false);
			}
		}

		void SkipUpdate(void*) {}

		void Composite(void* a_pass, std::uint32_t a_technique, bool a_flag)
		{
			auto* const handler = Current();
			if (handler) {
				handler->AroundComposite(true);
			}
			(*g_composite)(a_pass, a_technique, a_flag);
			if (handler) {
				handler->AroundComposite(false);
			}
		}

		void Reflections(void* a_shader, std::uint32_t a_1, std::uint32_t a_2, std::uint32_t a_3, std::uint32_t a_4)
		{
			(*g_reflections)(a_shader, a_1, a_2, a_3, a_4);
			if (auto* const handler = Current()) {
				handler->AfterReflectionsSetup();
			}
		}

		void LensFlare(void* a_camera)
		{
			auto* const handler = Current();
			if (handler) {
				handler->AroundLensFlare(true);
			}
			reinterpret_cast<void (*)(void*)>(g_lensFlare)(a_camera);
			if (handler) {
				handler->AroundLensFlare(false);
			}
		}

		bool TemporalAAIsActive(void* a_effect)
		{
			const auto original = reinterpret_cast<bool (*)(void*)>(g_temporalAA.Original());
			auto* const handler = Current();
			return (handler == nullptr || handler->AllowTemporalAA()) && original(a_effect);
		}

		template <class T, class F>
		bool Make(std::unique_ptr<T>& a_hook, const Site& a_site, F a_function) noexcept
		{
			a_hook = std::make_unique<T>(a_site.name, REL::ID(a_site.function), a_site.offset, a_function);
			return a_hook->Init() && a_hook->Enable();
		}
	}

	bool Install() noexcept
	{
		for (const auto* site : { &kTemporalData, &kEffectRange, &kViewportDefault, &kPrePass, &kForward,
				 &kDynamicUpdate, &kComposite, &kReflections }) {
			if (!Verify(*site)) {
				return false;
			}
		}

		if (!Make(g_temporalData, kTemporalData, &TemporalData) ||
			!Make(g_effectRange, kEffectRange, &EffectRange) ||
			!Make(g_viewportDefault, kViewportDefault, &ViewportDefault) ||
			!Make(g_prePass, kPrePass, &PrePass) ||
			!Make(g_forward, kForward, &Forward) ||
			!Make(g_composite, kComposite, &Composite) ||
			!Make(g_reflections, kReflections, &Reflections)) {
			REX::ERROR("upscale hooks: a verified site refused its hook");
			g_failed = "trampoline";
			return false;
		}
		// Built and initialised, but enabled only while the DLSS feature sets
		// the ratios itself.
		g_dynamicUpdate = std::make_unique<REL::THook<void(void*)>>(kDynamicUpdate.name, REL::ID(kDynamicUpdate.function), kDynamicUpdate.offset, &SkipUpdate);
		if (!g_dynamicUpdate->Init()) {
			g_failed = kDynamicUpdate.name;
			return false;
		}

		auto** const table = reinterpret_cast<void**>(RE::VTABLE::ImageSpaceEffectTemporalAA[0].address());
		if (!g_temporalAA.InstallAtTable(table, kTemporalAAIsActive, reinterpret_cast<void*>(&TemporalAAIsActive))) {
			g_failed = "ImageSpaceEffectTemporalAA::IsActive";
			return false;
		}

		g_lensFlare = reinterpret_cast<void*>(REL::ID(kLensFlare).address());
		if (!AttachLensFlare(std::addressof(g_lensFlare), reinterpret_cast<void*>(&LensFlare))) {
			g_failed = "BSImagespaceShaderLensFlare (Detours)";
			return false;
		}

		g_installed = true;
		REX::INFO("upscale hooks: eight call sites, one vtable entry and one prolog in place");
		return true;
	}

	bool Installed() noexcept { return g_installed; }
	std::string_view FailedSite() noexcept { return g_failed; }

	void SetHandler(Handler* a_handler) noexcept
	{
		g_handler.store(a_handler, std::memory_order_release);
	}

	void SetDynamicResolutionViewport(bool a_enabled) noexcept
	{
		if (g_viewportDefault && g_viewportObject != nullptr) {
			(*g_viewportDefault)(g_viewportObject, a_enabled);
		}
	}

	void SkipDynamicResolutionUpdate(bool a_skip) noexcept
	{
		if (!g_dynamicUpdate) {
			return;
		}
		if (a_skip) {
			static_cast<void>(g_dynamicUpdate->Enable());
		} else {
			static_cast<void>(g_dynamicUpdate->Disable());
		}
	}
}
```

Anmerkungen für den Bau: Paßt die `THook`-Konstruktorüberladung `(const char*, ID, ptrdiff_t,
fn)` nicht, die ohne Namen nehmen. `g_viewportObject` ist der `RenderTargetManager`; er läßt sich
statt aus dem Aufruf auch aus `RE::BSGraphics::RenderTargetManager::GetSingleton()` nehmen — wenn
beide abweichen, gilt das Singleton (eine Logzeile im ersten Lauf vergleicht beide).

-   [ ] **Step 7: `src/XSEPlugin.cpp`** — in `F4SEPlugin_Load`:

```cpp
	F4SE::InitInfo initInfo{};
	// Eight call hooks of F5.5a, fourteen bytes of branch each; the rest is
	// headroom. hook stays false: our hooks are made after the runtime check,
	// not registered statically, because a REL::THook reads its site the
	// moment it is constructed.
	initInfo.trampoline = true;
	initInfo.trampolineSize = 256;
	initInfo.hook = false;
```

und nach dem erfolgreichen `RegisterListener`, vor `REX::INFO("loaded")`:

```cpp
	// Installed at load, not when the DLSS feature starts: a call hook written
	// while the render thread runs the code under it is a race. Without a
	// handler every hook passes straight through. A failure costs the DLSS
	// feature, not the plugin.
	if (!Upscale::Hooks::Install()) {
		REX::ERROR("upscale hooks not installed ({}); DLSS will refuse", Upscale::Hooks::FailedSite());
	}
```

mit `#include "Upscale/EngineHooks.h"` oben.

-   [ ] **Step 8: Bauen; `verify-plugin.ps1`; Commit** — `feat: the upscaling hooks, verified per site`

---

## Task 6: NGX-Hülle

**Files:**

-   Create: `src/Upscale/Ngx.h`, `src/Upscale/Ngx.cpp`

**Interfaces:**

-   Consumes: `Render::Extent`, `Upscale::Mode`.
-   Produces:

```cpp
namespace Upscale
{
	enum class Preset : std::uint8_t { kDefault, kK, kL, kM };
	std::optional<Preset> PresetFromName(std::string_view) noexcept;

	struct EvaluateInputs
	{
		REX::W32::ID3D11Resource* color;          // render size, ours
		REX::W32::ID3D11Resource* output;         // display size, RT_004
		REX::W32::ID3D11Resource* depth;          // DS_002 texture
		REX::W32::ID3D11Resource* motionVectors;  // render size, dilated
		Render::Extent render;
		float jitterX;                            // render pixels, as passed to NGX
		float jitterY;
		float motionScaleX;
		float motionScaleY;
		bool reset;
	};

	class Ngx
	{
	public:
		[[nodiscard]] bool Init(REX::W32::ID3D11Device* a_device, const std::filesystem::path& a_dllDirectory) noexcept;
		[[nodiscard]] bool Available() const noexcept;
		[[nodiscard]] std::string_view Refusal() const noexcept;
		[[nodiscard]] std::optional<Render::Extent> OptimalRenderSize(Render::Extent a_display, Mode a_mode) noexcept;
		[[nodiscard]] bool Ensure(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, Mode a_mode, Preset a_preset) noexcept;
		[[nodiscard]] bool Evaluate(REX::W32::ID3D11DeviceContext& a_context, const EvaluateInputs& a_inputs) noexcept;
		void Shutdown() noexcept;
	};
}
```

-   [ ] **Step 1: `src/Upscale/Ngx.h`** — wie oben, dazu private Felder:
        `REX::W32::ID3D11Device* _device{}; NVSDK_NGX_Parameter* _parameters{}; NVSDK_NGX_Handle* _feature{};`
        `Render::Extent _render{}, _display{}; Mode _mode{}; Preset _preset{}; bool _available{}; std::string _refusal;`
        `std::wstring _dllDirectory;` und die Vorwärtsdeklarationen `struct NVSDK_NGX_Parameter;`
        `struct NVSDK_NGX_Handle;` außerhalb des Namensraums. Klassenkommentar: dünne Hülle um das
        DLSS-SDK 310.9.1 auf D3D11, ein DLSS-Objekt je Kombination aus Auflösung, Modus und Preset;
        jeder Fehler kommt mit dem `NVSDK_NGX_Result` ins Log.

-   [ ] **Step 2: `src/Upscale/Ngx.cpp`**

```cpp
#include "Upscale/Ngx.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#include <nvsdk_ngx_helpers_d3d.h>

#include <format>

namespace Upscale
{
	namespace
	{
		/// NVIDIA asks engines without an NVIDIA-issued id to describe
		/// themselves with a project id of their own and ENGINE_TYPE_CUSTOM.
		constexpr const char* kProjectId = "99023707-327d-45dc-ba45-0c47e8445542";
		constexpr const char* kEngineVersion = "Fallout 4 1.11.240";

		NVSDK_NGX_PerfQuality_Value ToQuality(Mode a_mode) noexcept
		{
			switch (a_mode) {
				case Mode::kQuality: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
				case Mode::kBalanced: return NVSDK_NGX_PerfQuality_Value_Balanced;
				case Mode::kPerformance: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
				case Mode::kUltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
				case Mode::kDLAA:
				default: return NVSDK_NGX_PerfQuality_Value_DLAA;
			}
		}

		unsigned int ToPreset(Preset a_preset) noexcept
		{
			switch (a_preset) {
				case Preset::kK: return NVSDK_NGX_DLSS_Hint_Render_Preset_K;
				case Preset::kL: return NVSDK_NGX_DLSS_Hint_Render_Preset_L;
				case Preset::kM: return NVSDK_NGX_DLSS_Hint_Render_Preset_M;
				case Preset::kDefault:
				default: return NVSDK_NGX_DLSS_Hint_Render_Preset_Default;
			}
		}

		std::string Describe(NVSDK_NGX_Result a_result)
		{
			return std::format("0x{:08X}", static_cast<std::uint32_t>(a_result));
		}
	}

	std::optional<Preset> PresetFromName(std::string_view a_name) noexcept
	{
		if (a_name == "default") return Preset::kDefault;
		if (a_name == "k") return Preset::kK;
		if (a_name == "l") return Preset::kL;
		if (a_name == "m") return Preset::kM;
		return std::nullopt;
	}

	bool Ngx::Init(REX::W32::ID3D11Device* a_device, const std::filesystem::path& a_dllDirectory) noexcept
	{
		_device = a_device;
		_dllDirectory = a_dllDirectory.wstring();
		const wchar_t* paths[]{ _dllDirectory.c_str() };
		NVSDK_NGX_FeatureCommonInfo info{};
		info.PathListInfo.Path = paths;
		info.PathListInfo.Length = 1;

		const auto init = NVSDK_NGX_D3D11_Init_with_ProjectID(
			kProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, kEngineVersion, _dllDirectory.c_str(),
			reinterpret_cast<::ID3D11Device*>(a_device), std::addressof(info), NVSDK_NGX_Version_API);
		if (NVSDK_NGX_FAILED(init)) {
			_refusal = std::format("NGX did not start ({}): no NVIDIA GPU, or a driver older than 616.64", Describe(init));
			return false;
		}
		const auto capability = NVSDK_NGX_D3D11_GetCapabilityParameters(std::addressof(_parameters));
		if (NVSDK_NGX_FAILED(capability) || _parameters == nullptr) {
			_refusal = std::format("NGX capability parameters not readable ({})", Describe(capability));
			return false;
		}
		int available = 0;
		int needsDriver = 0;
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_SuperSampling_Available, std::addressof(available));
		NVSDK_NGX_Parameter_GetI(_parameters, NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, std::addressof(needsDriver));
		if (needsDriver != 0) {
			_refusal = "DLSS needs a newer driver";
			return false;
		}
		if (available == 0) {
			_refusal = std::format("DLSS not available - is nvngx_dlss.dll in {}?", a_dllDirectory.string());
			return false;
		}
		_available = true;
		return true;
	}

	bool Ngx::Available() const noexcept { return _available; }
	std::string_view Ngx::Refusal() const noexcept { return _refusal; }

	std::optional<Render::Extent> Ngx::OptimalRenderSize(Render::Extent a_display, Mode a_mode) noexcept
	{
		if (!_available) {
			return std::nullopt;
		}
		unsigned int width = 0, height = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
		float sharpness = 0.0f;
		const auto result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
			_parameters, a_display.width, a_display.height, ToQuality(a_mode),
			std::addressof(width), std::addressof(height), std::addressof(maxW), std::addressof(maxH),
			std::addressof(minW), std::addressof(minH), std::addressof(sharpness));
		if (NVSDK_NGX_FAILED(result) || width == 0 || height == 0) {
			return std::nullopt;
		}
		return Render::Extent{ width, height };
	}

	bool Ngx::Ensure(REX::W32::ID3D11DeviceContext& a_context, Render::Extent a_render, Render::Extent a_display, Mode a_mode, Preset a_preset) noexcept
	{
		if (!_available) {
			return false;
		}
		if (_feature != nullptr && _render == a_render && _display == a_display && _mode == a_mode && _preset == a_preset) {
			return true;
		}
		if (_feature != nullptr) {
			NVSDK_NGX_D3D11_ReleaseFeature(_feature);
			_feature = nullptr;
		}
		const auto preset = ToPreset(a_preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, preset);
		NVSDK_NGX_Parameter_SetUI(_parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, preset);

		NVSDK_NGX_DLSS_Create_Params create{};
		create.Feature.InWidth = a_render.width;
		create.Feature.InHeight = a_render.height;
		create.Feature.InTargetWidth = a_display.width;
		create.Feature.InTargetHeight = a_display.height;
		create.Feature.InPerfQualityValue = ToQuality(a_mode);
		// HDR input (R11G11B10 before tonemapping), motion vectors at render
		// size, exposure left to DLSS (spec, section 2).
		create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
		                              NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
		const auto result = NGX_D3D11_CREATE_DLSS_EXT(
			reinterpret_cast<::ID3D11DeviceContext*>(std::addressof(a_context)), std::addressof(_feature), _parameters, std::addressof(create));
		if (NVSDK_NGX_FAILED(result) || _feature == nullptr) {
			REX::ERROR("DLSS: the DLSS object for {}x{} -> {}x{} could not be created ({})",
				a_render.width, a_render.height, a_display.width, a_display.height, Describe(result));
			return false;
		}
		_render = a_render;
		_display = a_display;
		_mode = a_mode;
		_preset = a_preset;
		REX::INFO("DLSS: object created, {}x{} -> {}x{}", a_render.width, a_render.height, a_display.width, a_display.height);
		return true;
	}

	bool Ngx::Evaluate(REX::W32::ID3D11DeviceContext& a_context, const EvaluateInputs& a_inputs) noexcept
	{
		if (_feature == nullptr) {
			return false;
		}
		NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
		eval.Feature.pInColor = reinterpret_cast<::ID3D11Resource*>(a_inputs.color);
		eval.Feature.pInOutput = reinterpret_cast<::ID3D11Resource*>(a_inputs.output);
		eval.pInDepth = reinterpret_cast<::ID3D11Resource*>(a_inputs.depth);
		eval.pInMotionVectors = reinterpret_cast<::ID3D11Resource*>(a_inputs.motionVectors);
		eval.InJitterOffsetX = a_inputs.jitterX;
		eval.InJitterOffsetY = a_inputs.jitterY;
		eval.InRenderSubrectDimensions.Width = a_inputs.render.width;
		eval.InRenderSubrectDimensions.Height = a_inputs.render.height;
		eval.InReset = a_inputs.reset ? 1 : 0;
		eval.InMVScaleX = a_inputs.motionScaleX;
		eval.InMVScaleY = a_inputs.motionScaleY;
		const auto result = NGX_D3D11_EVALUATE_DLSS_EXT(
			reinterpret_cast<::ID3D11DeviceContext*>(std::addressof(a_context)), _feature, _parameters, std::addressof(eval));
		return NVSDK_NGX_SUCCEED(result);
	}

	void Ngx::Shutdown() noexcept
	{
		if (_feature != nullptr) {
			NVSDK_NGX_D3D11_ReleaseFeature(_feature);
			_feature = nullptr;
		}
		if (_parameters != nullptr) {
			NVSDK_NGX_D3D11_DestroyParameters(_parameters);
			_parameters = nullptr;
		}
		if (_device != nullptr) {
			NVSDK_NGX_D3D11_Shutdown1(reinterpret_cast<::ID3D11Device*>(_device));
			_device = nullptr;
		}
		_available = false;
	}
}
```

`kProjectId` ist eine einmal erzeugte GUID und bleibt fest; NGX ordnet damit seine eigenen
Einstellungen und Protokolle dem Projekt zu. Die Namen
`NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_*` und `NVSDK_NGX_DLSS_Hint_Render_Preset_K/L/M` sind
gegen `extern/DLSS/include/nvsdk_ngx_defs.h` geprüft (Spec, Befund aus der Recherche).

-   [ ] **Step 3: Bauen** — erwartet grün. Unaufgelöste Symbole aus `nvsdk_ngx_d.lib` (etwa
        `GetFileVersionInfo*`, `Reg*`) mit genau den gemeldeten Systembibliotheken (`version.lib`,
        `advapi32.lib`) in `target_link_libraries` ergänzen, mit einem Kommentar, der die Bibliothek
        nennt.

-   [ ] **Step 4: Commit** — `feat: a thin NGX layer for DLSS on D3D11`

---

## Task 7: Shader und Pässe

**Files:**

-   Create: `package/Features/DLSS/Shaders/FO4/DLSS/{DilateMotionVectors,FullDepth,RCAS,SSLRRaytracing}.hlsl`,
    `src/Upscale/Passes.h`, `src/Upscale/Passes.cpp`

**Interfaces:**

-   Consumes: `Render::Texture`, `Render::ConstantBuffer`, `Shader::LoadSource`,
    `Shader::CompileComputeShader`, `Shader::CompilePixelShader`, `Util::FileWatch`.
-   Produces:

```cpp
namespace Upscale
{
	class Passes
	{
	public:
		[[nodiscard]] bool Compile() noexcept;            // all four, swaps only when all succeed
		[[nodiscard]] bool PollReload() noexcept;         // true when recompiled
		[[nodiscard]] bool EnsureTextures(Render::Extent a_render, Render::Extent a_display) noexcept;
		void DilateMotionVectors(REX::W32::ID3D11DeviceContext&, REX::W32::ID3D11ShaderResourceView* a_motion, REX::W32::ID3D11ShaderResourceView* a_depth, Render::Extent a_render, Render::Extent a_display) noexcept;
		void CopyColor(REX::W32::ID3D11DeviceContext&, REX::W32::ID3D11Resource* a_sceneHDR, Render::Extent a_render) noexcept;
		void Sharpen(REX::W32::ID3D11DeviceContext&, REX::W32::ID3D11Resource* a_sceneHDR, REX::W32::ID3D11UnorderedAccessView* a_sceneUAV, float a_sharpness, Render::Extent a_display) noexcept;
		void FullDepth(REX::W32::ID3D11DeviceContext&, REX::W32::ID3D11ShaderResourceView* a_depth, REX::W32::ID3D11UnorderedAccessView* a_linearMip0, Render::Extent a_render, Render::Extent a_display) noexcept;
		[[nodiscard]] REX::W32::ID3D11PixelShader* Reflections() const noexcept;
		[[nodiscard]] REX::W32::ID3D11Resource* ColorInput() const noexcept;      // render size, R11G11B10
		[[nodiscard]] REX::W32::ID3D11Resource* MotionVectors() const noexcept;   // render size, R16G16_FLOAT
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* FullDepthSRV() const noexcept;
		void Release() noexcept;
	};
}
```

-   [ ] **Step 1: `DilateMotionVectors.hlsl`** — doodlums `DilateMotionVectorCS.hlsl` (Spec, Befund 4) mit unserem Kopf: Quelle und Lizenz im Kommentar („after doodlum/fo4test, GPL-3.0"),
        `cbuffer Upscaling : register(b0) { uint2 ScreenSize; uint2 RenderSize; float4 CameraData; }`
        wie dort, Inhalt unverändert.

-   [ ] **Step 2: `FullDepth.hlsl`** — doodlums `OverrideDepthCS.hlsl` mit unserem Kopf, Inhalt
        unverändert (Tiefe nach `u0`, linear nach `u1` = Mip 0 von `RT_039`).

-   [ ] **Step 3: `RCAS.hlsl`** — aus doodlums `RCAS/RCAS.hlsl`, die Schärfe nicht als `#define`,
        sondern als `float Sharpness` im selben `cbuffer` an Stelle von `CameraData.x` (ein Shader für
        jede Schärfe; ein Define je Wert hieße Neuübersetzen beim Ziehen des Reglers).

-   [ ] **Step 4: `SSLRRaytracing.hlsl`** — doodlums
        `BSImagespaceShaderSSLRRaytracing.hlsl` unverändert, mit Kopf: „a decompiled and edited copy of
        the engine's SSLR shader by doodlum (fo4test, GPL-3.0); replaces the engine's only while DLSS
        upscales".

-   [ ] **Step 5: Offline übersetzen** — wie in F5: alle vier mit `fxc` aus dem Windows-Kit
        (`cs_5_0` beziehungsweise `ps_5_0`, Einstieg `main`), in Git-Bash mit
        `MSYS2_ARG_CONV_EXCL="*"`. Erwartet: viermal „compilation object save succeeded".

-   [ ] **Step 6: `src/Upscale/Passes.h/.cpp`** — nach dem Muster von `Skylighting::Compile`:
        `ShaderRoot()` = `Util::DataDirectory() / "Shaders" / "FO4"`, `LoadSource(root, "DLSS/<Datei>")`,
        alle Dateien in `_watch`. Texturen über `Render::Texture::Create`: `_color` (render,
        `DXGI_FORMAT_R11G11B10_FLOAT`), `_motion` (render, `DXGI_FORMAT_R16G16_FLOAT`), `_depth`
        (display, `DXGI_FORMAT_R32_FLOAT`), `_sharpenSource` (display, `R11G11B10_FLOAT`). Konstanten
        über `Render::ConstantBuffer` (32 Byte: `uint ScreenSize[2]; uint RenderSize[2]; float
CameraData[4];`, `CameraData` = `{ far, near, far − near, far · near }` aus
        `REL::ID(2712883)` und `REL::ID(2712882)`, für RCAS `CameraData[0]` = Schärfe). Jede Methode
        bindet, dispatcht `ceil(x / 8) × ceil(y / 8)` und **löst danach alles, was sie gebunden hat**
        (SRV, UAV, CB, Shader), wie `Skylighting::UpdateProbes`. `CopyColor` ist ein
        `CopySubresourceRegion` des Renderrechtecks von `RT_004` nach `_color` (Farbziel, geboxte Kopie
        erlaubt — anders als bei Tiefe, F5). `Sharpen` kopiert `RT_004` ganz nach `_sharpenSource` und
        schreibt über die UAV von `RT_004` zurück.

-   [ ] **Step 7: Bauen; Commit** — `feat: motion vector, depth and sharpening passes`

---

## Task 8: Das Feature, DLAA

**Files:**

-   Create: `src/Features/DLSS.h`, `src/Features/DLSS.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp`, `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json`
    (über `extract-i18n.py --write`)

**Interfaces:**

-   Consumes: alles aus Task 2–7.
-   Produces: `class Features::DLSS final : public Feature, public Upscale::Hooks::Handler`.

-   [ ] **Step 1: `Declare`**

```cpp
	void DLSS::Declare()
	{
		Settings::DeclareFeature("DLSS", false)
			.Label("feature.dlss.name", "DLSS")
			.Help(
				"feature.dlss.help",
				"NVIDIA DLSS replaces the game's temporal anti-aliasing: DLAA at full resolution, or "
				"upscaling from a lower one for more frames. Uses NVIDIA DLSS technology.");
		Settings::DeclareChoice(
			"DLSS/mode", "quality",
			std::vector<std::string>{ "dlaa", "quality", "balanced", "performance", "ultra_performance" })
			.Label("feature.dlss.mode", "Mode")
			.Help("feature.dlss.mode_help", "DLAA keeps the full resolution; the others render lower and upscale.");
		Settings::DeclareChoice("DLSS/preset", "default", std::vector<std::string>{ "default", "k", "l", "m" })
			.Label("feature.dlss.preset", "Preset")
			.Help("feature.dlss.preset_help", "The DLSS model. Default lets DLSS choose; K is DLSS 4, L and M are DLSS 4.5.");
		Settings::DeclareSlider("DLSS/sharpness", 0.0, 0.0, 1.0)
			.Label("feature.dlss.sharpness", "Sharpness")
			.Help("feature.dlss.sharpness_help", "Contrast adaptive sharpening after DLSS. 0 is off.");
	}
```

Die NVIDIA-Nennung (Ergänzung 7.1 der Lizenz) steckt in der `Help` des Schalters — sie ist die
Beschreibung oben auf der Seite.

-   [ ] **Step 2: `Setup`** — in dieser Reihenfolge, jede Ablehnung mit eigenem `REX::ERROR`:

    1. `Upscale::Hooks::Installed()`, sonst „hooks not installed: {FailedSite()}".
    2. Fremder Upscaler: `REX::W32::GetModuleHandleW` auf `L"Upscaling.dll"`, `L"OptiScaler.dll"`,
       `L"nvngx.dll"` im Spielordner (OptiScaler gibt sich so aus) — der erste Treffer lehnt ab
       und nennt das Modul.
    3. ENB: `GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "ENBGetSDKVersion") != nullptr` →
       `_enb = true` (keine Ablehnung; nur DLAA).
    4. `_ngx.Init(Render::GetDevice(), Util::PluginDataDirectory())`, sonst `_ngx.Refusal()`.
    5. `_passes.Compile()`.
    6. `TexelProbe` `_motionProbe.Create(DXGI_FORMAT_R16G16_FLOAT, 4, "FO4CS_PROBE_Motion")`.
    7. Engine-Schalter merken und setzen: `_taaWas = *TAAFlag()` mit
       `TAAFlag() = reinterpret_cast<bool*>(REL::ID(2704658).address())`, dann `true`;
       `_fxaaWas` = `ImageSpaceManager::GetSingleton()->effectList[EFFECT_SHADER_FXAA]->isActive`,
       dann `false` (die Engine erzeugt Jitter und Bewegungsvektoren nur mit TAA an).
    8. `Upscale::Hooks::SetHandler(this)`.

-   [ ] **Step 3: `AfterTemporalData`** (DLAA in diesem Task, Hochskalierung in Task 10)

```cpp
	void DLSS::AfterTemporalData() noexcept
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		const Render::Extent display{ state->screenWidth, state->screenHeight };
		_active = !MenuForcesNative();
		_mode = _active ? Upscale::ModeFromName(Settings::GetString("DLSS/mode")).value_or(Upscale::Mode::kQuality) : Upscale::Mode::kDLAA;
		_render = display;  // Task 10: RenderSize for the upscaling modes
		_display = display;
		Render::Resolution::Publish(_display, _render);
		if (!_active) {
			state->offsetX = 0.0f;
			state->offsetY = 0.0f;
			return;
		}
		const auto phases = Upscale::JitterPhaseCount(_render, _display);
		_jitter = Upscale::JitterOffset(state->frameCount, phases);
		const auto offset = Upscale::ToEngineOffset(_jitter, _display);
		state->offsetX = offset.x;
		state->offsetY = offset.y;
		_reset = _reset || TakeCameraCut();
	}
```

`MenuForcesNative()`: `RE::UI::GetSingleton()->GetMenuOpen("PipboyMenu") || …("ExamineMenu")`;
der Übergang von nativ zurück setzt `_reset = true`. `TakeCameraCut()` bildet `CameraState` aus
`RE::Main::WorldRootCamera()` (Position aus `GetWorldTransform().translate`, Blickrichtung und
Sichtfeld aus `Render::FogCameraFromMatrix(world->worldToCam, z)`: `forward`, und
`2 · atan(|rightOverScaleX|)` in Grad), vergleicht mit dem letzten und merkt den aktuellen.

-   [ ] **Step 4: `AllowTemporalAA`** — `return !_active;`

-   [ ] **Step 5: `AfterEffectRange`** — die Auswertung:

```cpp
	void DLSS::AfterEffectRange() noexcept
	{
		if (!_active) {
			return;
		}
		auto* const context = Render::GetContext();
		auto* const scene = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		auto* const sceneUAV = Render::Targets::RenderTargetUAV(Render::Targets::kSceneHDR);
		auto* const motion = Render::Targets::RenderTargetSRV(kMotionVectors);
		auto* const depthSRV = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const depth = Render::Targets::DepthTexture(Render::Targets::kSceneDepth);
		if (context == nullptr || scene == nullptr || sceneUAV == nullptr || motion == nullptr || depthSRV == nullptr || depth == nullptr) {
			return;
		}
		const Render::PassScope scope{ "DLSS/Evaluate" };
		context->OMSetRenderTargets(0, nullptr, nullptr);
		if (!_passes.EnsureTextures(_render, _display) ||
			!_ngx.Ensure(*context, _render, _display, _mode, _preset)) {
			return;
		}
		_passes.DilateMotionVectors(*context, motion, depthSRV, _render, _display);
		_passes.CopyColor(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(scene), _render);
		const Upscale::EvaluateInputs inputs{
			_passes.ColorInput(), reinterpret_cast<REX::W32::ID3D11Resource*>(scene),
			reinterpret_cast<REX::W32::ID3D11Resource*>(depth), _passes.MotionVectors(), _render,
			-_jitter.x, -_jitter.y, kMotionScaleX * static_cast<float>(_render.width),
			kMotionScaleY * static_cast<float>(_render.height), _reset,
		};
		if (_ngx.Evaluate(*context, inputs)) {
			++_evaluations;
		} else {
			++_failedEvaluations;
		}
		_reset = false;
		const auto sharpness = static_cast<float>(Settings::GetDouble("DLSS/sharpness"));
		if (sharpness > 0.0f) {
			_passes.Sharpen(*context, reinterpret_cast<REX::W32::ID3D11Resource*>(scene), sceneUAV, sharpness, _display);
		}
	}
```

`kMotionVectors = 29`; `kMotionScaleX = 1.0f`, `kMotionScaleY = 1.0f` als benannte Konstanten mit
dem Kommentar: „RT_029 holds uv units (doodlum: mvecScale 1 in Streamline, which wants [-1, 1]
uv); NGX wants render pixels, so the scale is the render size. Sign: the first probe run". Fehlt
in `Render::Targets` ein Zugriff (`RenderTargetUAV`, `DepthTexture`), wird er dort ergänzt, im
Stil der vorhandenen (`renderTargets[slot].uaView`, `depthStencilTargets[slot].texture`).

-   [ ] **Step 6: `Frame`** — Hot-Reload (`_passes.PollReload()` → `REX::INFO("DLSS: shaders
changed, recompiling")`), Preset aus der Einstellung (`_preset`), und je `kLogInterval`:

```cpp
			REX::INFO(
				"dlss: mode {} {}x{} -> {}x{}, jitter [{:.3f} {:.3f}], {} evaluation(s), {} failed, {} reset(s), native {}",
				Settings::GetString("DLSS/mode"), _render.width, _render.height, _display.width, _display.height,
				_jitter.x, _jitter.y, _evaluations, _failedEvaluations, _resets, !_active);
```

dazu die Stichprobe `_motionProbe` auf `RT_029` (Capture in `AfterEffectRange`, Read hier), mit
`R16G16_FLOAT` als zwei Halbfloats dekodiert (eine kleine `HalfToFloat`-Funktion im anonymen
Namensraum, IEEE 754 binary16), Zeile
`dlss probe: motion vectors x [min max mean] y [min max mean]`.

-   [ ] **Step 7: `Shutdown`** — `Upscale::Hooks::SetHandler(nullptr)` zuerst; `offsetX/Y = 0`;
        `*TAAFlag() = _taaWas`; FXAA zurück; `Render::Resolution::Withdraw()`; `_passes.Release()`,
        `_motionProbe.Release()`, `_ngx.Shutdown()`.

-   [ ] **Step 8: Registrieren** — in `RegisterAll` vor `ScreenSpaceShadows`, mit Kommentar: „Sets
        the resolution the features after it render at; registered first so that it starts first and
        stops last".

-   [ ] **Step 9: i18n, Bau, Tests, Paket**

```pwsh
python tools/extract-i18n.py --write
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

-   [ ] **Step 10: Commit** — `feat: DLSS as DLAA in place of the engine's TAA`

---

## Task 9: Lauf 1 — DLAA (Nutzer)

Ansage an den Nutzer, konkret:

1. Fallout 4 starten, Spielstand im Freien laden.
2. Overlay (Taste), Seite **DLSS**: Schalter an, **Mode `dlaa`**.
3. Zehn Sekunden stehen, dann zehn Sekunden seitwärts gehen (A/D), dann die Kamera drehen.
4. Overlay: DLSS aus, wieder an. Pip-Boy auf und zu.
5. Spiel beenden. Sagen, ob das Bild ruhiger oder schärfer war als mit TAA, und ob beim Gehen
   etwas schmierte oder zitterte.

Was schiefgehen kann: das Bild wird schwarz oder bunt (Formate, Ein- und Ausgabe), es schmiert
beim Gehen (Skala oder Vorzeichen der Bewegungsvektoren), es zittert im Stand (Jitter-Vorzeichen).

Auswertung im Log: `upscale hooks: … in place`, keine `[E]`, `dlss:`-Zeilen mit Auswertungen = 180
je Intervall, Resets beim Pip-Boy; `dlss probe: motion vectors` im Stand ≈ 0, beim Seitwärtsgehen
in x ein Vorzeichen — daraus `kMotionScaleX/Y` (Vorzeichen) festlegen, im Code mit dem Meßwert
kommentieren, Commit `fix: motion vector scale from the first run`.

---

## Task 10: Hochskalierung

**Files:**

-   Create: `src/Upscale/ProxyTargets.h`, `src/Upscale/ProxyTargets.cpp`
-   Modify: `src/Features/DLSS.h/.cpp`

**Interfaces:**

-   Produces:

```cpp
namespace Upscale
{
	/// The twelve smaller stand-ins for the targets the composite reads
	/// (doodlum: 20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60, 61), and the 320
	/// engine sampler states with a mip bias.
	class ProxyTargets
	{
	public:
		[[nodiscard]] bool Update(float a_ratioX, float a_ratioY) noexcept;  // recreates on change
		void Swap(REX::W32::ID3D11DeviceContext& a_context) noexcept;         // engine -> proxies, copies the render rectangle in
		void Restore(REX::W32::ID3D11DeviceContext& a_context) noexcept;      // proxies -> engine, copies back
		[[nodiscard]] bool UpdateSamplers(float a_mipBias) noexcept;
		void BiasSamplers() noexcept;
		void RestoreSamplers() noexcept;
		void Release() noexcept;  // restores first if swapped
	};
}
```

-   [ ] **Step 1: `ProxyTargets`** — doodlums `UpdateRenderTarget`, `OverrideRenderTarget`,
        `ResetRenderTarget`, `UpdateSamplerStates`, `OverrideSamplerStates`, `ResetSamplerStates`
        (Spec, Befund 2; Quelle im Kommentar) auf unsere Typen: `RE::BSGraphics::RendererData` für
        `renderTargets[i]`, `REX::W32` statt `ID3D11*`, Fehler über `REX::ERROR` statt
        `ThrowIfFailed`, die Metadaten `renderTargetData[i].width/height` beim `Swap` skaliert und beim
        `Restore` zurück. Sampler-Array: `reinterpret_cast<REX::W32::ID3D11SamplerState**>(REL::ID(2704455).address())`,
        320 Einträge; nur `D3D11_FILTER_ANISOTROPIC` bekommt den Bias (und `MaxAnisotropy` 8, wie
        doodlum). `Release` gibt jeden selbst angelegten View frei — die der Engine nie.

-   [ ] **Step 2: `AfterTemporalData` erweitern**

```cpp
		const bool upscaling = _active && !_enb && _mode != Upscale::Mode::kDLAA;
		_render = upscaling ? _ngx.OptimalRenderSize(display, _mode).value_or(Upscale::RenderSize(display, _mode)) : display;
		const auto ratioX = static_cast<float>(_render.width) / static_cast<float>(display.width);
		const auto ratioY = static_cast<float>(_render.height) / static_cast<float>(display.height);
		if (!_proxies.Update(ratioX, ratioY) || !_proxies.UpdateSamplers(upscaling ? Upscale::MipBias(_render, display) : 0.0f)) {
			_render = display;
		}
		_upscaling = upscaling && _render != display;
		Upscale::Hooks::SkipDynamicResolutionUpdate(_upscaling);
		RE::BSGraphics::RenderTargetManager::GetSingleton()->SetDynamicResolutionState(
			_upscaling ? ratioX : 1.0f, _upscaling ? ratioY : 1.0f, _upscaling);
```

vor dem Jitter; der Jitter nimmt `JitterPhaseCount(_render, _display)` und — bis Lauf 2a es
anders zeigt — `ToEngineOffset(_jitter, _display)` (doodlum).

-   [ ] **Step 3: Die übrigen Hooks** (alle nur bei `_upscaling`):

    -   `AroundWorldPass(before)`: `before` → `_proxies.BiasSamplers()`, sonst `RestoreSamplers()`.
    -   `AfterEffectRange`: zuerst `Upscale::Hooks::SetDynamicResolutionViewport(false)`, dann die
        Auswertung aus Task 8, danach `FullDepth` und `OverrideDepth()`.
    -   `BeforeDynamicViewportDefault`: `ResetDepth()`.
    -   `AroundComposite(before)`: `before` → `_proxies.Swap`, `OverrideDepth()`, Faktoren 1;
        sonst `ResetDepth()`, `_proxies.Restore`, Faktoren zurück — wie doodlums
        `DrawWorld_DeferredComposite_RenderPassImmediately`.
    -   `AroundLensFlare`: `OverrideDepth()` / `ResetDepth()`.
    -   `AfterReflectionsSetup`: `context->PSSetShader(_passes.Reflections(), nullptr, 0)`.

    `OverrideDepth()` setzt `depthStencilTargets[kSceneDepth].srViewDepth` auf
    `_passes.FullDepthSRV()` (alten Zeiger merken) und `Render::Resolution::SetDepthFull(true)`;
    `ResetDepth()` stellt zurück und `SetDepthFull(false)`. `FullDepth` läuft einmal je Frame
    (Frame-Nummer merken), wie doodlums `CopyDepth`.

-   [ ] **Step 4: Sonde der Phasen** — ein Diagnose-Commit für Lauf 2a: je `kLogInterval` eine Zeile
        je Phase (`kBeforeComposite`, `kComposite`, `kAfterOpaque`), abonniert vom DLSS-Feature mit
        `Render::SubscribeFramePhase`, die zeigt: Größe der Textur hinter `RT_004`, `RT_028`, `RT_058`,
        `DS_002` (über deren `GetDesc`/`GetResource`), Viewport 0 (`RSGetViewports`),
        `Render::Resolution::Current()` mit `depthFull`. Zeile:
        `dlss phase {name}: viewport {w}x{h}, RT_004 {w}x{h}, RT_028 {w}x{h}, RT_058 {w}x{h}, depth {w}x{h} full {b}`.
        Commit `diag: what each phase sees while DLSS upscales`.

-   [ ] **Step 5: `Shutdown` erweitern** — `SkipDynamicResolutionUpdate(false)`,
        `SetDynamicResolutionState(1, 1, false)`, `ResetDepth()` falls gesetzt, `_proxies.Release()`.

-   [ ] **Step 6: Bauen, Tests, Commit** — `feat: DLSS upscaling through the dynamic resolution`

---

## Task 11: Lauf 2a — Quality (Nutzer)

1. Spiel starten, Spielstand im Freien.
2. Overlay → DLSS: an, **Mode `quality`**.
3. Zehn Sekunden stehen, zehn Sekunden gehen, an eine Pfütze oder ans Wasser gehen.
4. Overlay → Seite **Exponential Height Fog**, **Screen-Space Shadows**, **Cloud Shadows**,
   **Skylighting**: je die Debug-Ansicht einmal an und wieder aus.
5. Spiel beenden; sagen: ist das Bild vollflächig und scharf, sitzen die vier Debug-Ansichten auf
   dem Bild oder sind sie verschoben/verkleinert, sieht die Spiegelung im Wasser richtig aus.

Auswertung: die `dlss phase`-Zeilen entscheiden für Task 12, welche Größe jede Phase sieht; die
Beobachtungen des Nutzers, ob `ToEngineOffset` den Bezug `_display` oder `_render` braucht (Zittern
in Quality), und ob der SSR-Ersatz paßt (sonst Gabelung: SSR in den Hochskalier-Modi abschalten,
über `ImageSpaceManager::effectList` den SSR-Effekt `isActive = false`, als Zugeständnis in der
Roadmap). Danach Sonde per `git revert` zurück.

---

## Task 12: F2–F5 auf `Render::Resolution`

**Files:**

-   Modify: `src/Features/ScreenSpaceShadows.cpp`, `src/Features/ExponentialHeightFog.cpp`,
    `src/Features/CloudShadows.cpp`, `src/Features/Skylighting.cpp`

**Regel** (aus Lauf 2a bestätigt oder korrigiert): Wer **schreibt**, setzt seinen Viewport auf das
Renderrechteck `Current().render` (in `kComposite`, wo die Ersatzziele gebunden sind, auf die Größe
des Ziels selbst); wer **Tiefe liest**, rechnet Pixel aus `DepthExtent(Current())`; wer eine
**volle Textur mit Bild im Renderrechteck** über eine UV liest, multipliziert die UV mit
`RenderToTexture(Current(), Größe der Textur)`.

-   [ ] **Step 1: ScreenSpaceShadows** — `EnsureMask` bleibt bei der Zielgröße (die Maske wird auf
        das Ziel multipliziert). In `BuildGeometry` (Zeile ~380) `viewport[2]` =
        `Current().render`; in der Dispatch-Schleife `data.dynamicRes[0/1]` =
        `RenderToTexture(Current(), Extent{ _mask.Width(), _mask.Height() })` statt `1.0f` (Bends
        eigener Eingang für genau diesen Fall; der Kommentar dort wird ersetzt). `ScaledSampleCount`
        rechnet mit `Current().render` statt der Maskengröße. Der Modulate-Pass setzt seinen Viewport
        auf `Current().render`.

-   [ ] **Step 2: ExponentialHeightFog** — `data.screen` aus `Current().render` statt aus der
        `RT_004`-Beschreibung; vor `DrawFullscreen` Viewport `{0, 0, render.width, render.height, 0, 1}`
        (bisher geerbt).

-   [ ] **Step 3: CloudShadows** — im Schattenpass Viewport auf `Current().render` setzen (bisher
        geerbt); der Shader liest Tiefe über `Load(position.xy)` und braucht nichts weiter.

-   [ ] **Step 4: Skylighting** — `FillConstants`: `data.params[2/3]` = `DepthExtent(Current())`
        statt `RT_004`-Größe. `DrawPass` behält den Viewport aus dem Ziel (`RT_028` ist in `kComposite`
        selbst das Ersatzziel). `DrawDebug` (in `kAfterOpaque`) setzt den Viewport auf
        `Current().render`.

-   [ ] **Step 5: Ohne DLSS unverändert** — ohne veröffentlichte Auflösung sind `render` und
        `display` gleich der `RT_004`-Größe und `depthFull` falsch: alle vier Rechnungen ergeben
        exakt die bisherigen Werte. Bauen, Tests, Commit `feat: F2 to F5 follow the render size`.

---

## Task 13: Lauf 2 — Abnahme (Nutzer)

Die zehn Schritte aus Spec 8.3, in einem Start, jeder mit „was schiefgehen kann". Hot-Reload:
**vorher fragen**, dann berühre ich die vier DLSS-Shader im Spielordner. F11-Paar: DLSS aus, DLAA,
Quality, Performance, je ein Schnappschuß an derselben Stelle.

---

## Task 14: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: Roadmap** — Zerlegungstabelle: neue Zeile F5.5a (abgeschlossen) und F5.5b (offen,
        Frame Generation, Andockpunkt DLSS 5) zwischen F5 und F6; der F13-Abschnitt „DLSS ja, DLSS 5
        nein" bekommt den Stand 2026-10-04 (Streamline 2.14.1, DLSS-NR ohne SDK, F5.5b); Abschnitt
        „Aus Teilprojekt F5.5a bestätigt": Zahlen (vier Schnappschüsse), was Lauf 1 und 2a gezeigt
        haben (MV-Skala, Jitter-Bezug, Phasengrößen, SSR), die Abnahme mit dem Ungeprüften,
        Zugeständnisse, was F5.5b erbt. `render-targets.md`: `RT_029` Einheit und Vorzeichen, `RT_039`
        als lineare Tiefe bestätigt.
-   [ ] **Step 2: Commit** — `docs: record what F5.5a confirmed`
-   [ ] **Step 3: Erinnerungen** — `fallout4-engine-facts.md` (Hook-Tabelle, Laufzeit-Offsets der
        dynamischen Auflösung, Phasengrößen), `fallout4-port-roadmap.md` (F5.5a fertig, F5.5b als
        Nächstes), `fallout4-dlss-research.md` ergänzen.
-   [ ] **Step 4: Branch abschließen** — finishing-a-development-branch: Tests, Paket, Prüfungen,
        Menü per AskUserQuestion, Fast-Forward nach `dev`, Push nur auf Ansage.

---

## Reihenfolge

```
1 SDK ─┬─► 2 Math ─► 3 CameraCut ─► 4 Resolution ─┐
       ├─► 5 Hooks ───────────────────────────────┼─► 8 DLAA ─► 9 Lauf 1 ─► 10 Hochskalierung ─► 11 Lauf 2a ─► 12 F2–F5 ─► 13 Lauf 2 ─► 14
       ├─► 6 NGX ─────────────────────────────────┤
       └─► 7 Pässe ───────────────────────────────┘
```

Drei Spielstarts (1, 2a, 2). Tasks 2–7 hängen an keinem Lauf.
