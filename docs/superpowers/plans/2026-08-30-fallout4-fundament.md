# Fallout 4 Port — Teilprojekt A (Fundament) Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein F4SE-Plugin `CommunityShadersFO4` 0.1.0, das unter Fallout 4 AE 1.11.240 geladen wird, sich meldet, fremde Spielversionen sauber ablehnt — und sonst nichts tut.

**Architecture:** Der geerbte Skyrim-Code verlässt den Arbeitsbaum (bleibt über Tag `skyrim-base` erreichbar), an seine Stelle tritt ein neues, minimales CMake-Gerüst. Die Engine-Bibliothek `commonlibf4` kommt als Submodul und wird über einen im Build-Verzeichnis erzeugten CMake-Shim eingebunden, weil sie nur `xmake.lua` ausliefert. Der Plugin-Code besteht aus einer Entrypoint-Datei und einer testbaren Versionsprüfung.

**Tech Stack:** C++23, MSVC (Visual Studio 18 2026), CMake ≥ 4.2, vcpkg (Manifest-Modus), commonlibf4 + commonlib-shared, spdlog (transitiv), Ninja für die Iteration.

**Spec:** `docs/superpowers/specs/2026-08-30-fallout4-fundament-design.md`

## Global Constraints

Diese gelten für jede Task, ohne dass sie dort wiederholt werden.

-   Plugin-Name exakt `CommunityShadersFO4`, Version exakt `0.1.0`.
-   Einzige unterstützte Spielversion: Fallout 4 AE `1.11.240`, geprüft gegen `F4SE::RUNTIME_1_11_240`.
-   Sprache C++23, Ziel x64, ausschließlich MSVC.
-   `cmake_minimum_required(VERSION 4.2)`.
-   vcpkg-Triplet `x64-windows-static-md-release`, `builtin-baseline` `dddca6fa87f177e0678e2545c4b4636a44aa05bd`.
-   Einzige direkte vcpkg-Abhängigkeit: `spdlog` mit Feature `wchar`. Kein Test-Framework, kein `catch2`.
-   Unser Plugin-Target führt `/W4 /WX`. Die Fremd-Targets (`CommonLibF4`, `commonlib-shared`) behalten ihre eigenen Optionen und werden nicht angefasst.
-   Kein Hook, kein D3D11-Zugriff, kein Trampolin: `InitInfo::trampoline = false` und `InitInfo::hook = false`.
-   Gearbeitet wird auf Branch `port/a-fundament`. `dev` wird nicht angefasst.
-   Commit-Nachrichten nach Conventional Commits, Titel ≤ 50 Zeichen, Fließtext auf 72 Zeichen umgebrochen, abschließend `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
-   Nach jeder Task laufen die pre-commit-Hooks mit; sie müssen grün sein.

## Vorbedingung (muss ein Mensch erledigen)

Vor Task 2: auf GitHub **`Dear-Modding-FO4/commonlibf4` nach `PlasticGhoul/commonlibf4` forken**. Der Fork ist notwendig, weil `BSRenderPass`, ein benanntes `RENDER_TARGET`-Enum, `ShadowSceneNode` und `BSLight` in allen Forks fehlen und spätestens in Teilprojekt B von uns ergänzt werden müssen. Task 1 ist ohne diesen Fork durchführbar.

## Dateistruktur

| Datei                               | Zuständigkeit                                                                                                                |
| ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `cmake/CommonLibF4.cmake.in`        | Shim-Vorlage: macht die xmake-Bibliothek zu einem CMake-Target. Wird per `configure_file` ins Build-Verzeichnis geschrieben. |
| `CMakeLists.txt`                    | Wurzelbuild: Version, Shim-Einbindung, Plugin-Target, Deploy-Option, Test-Target.                                            |
| `CMakePresets.json`                 | Zwei Presets: `FO4` (Visual Studio, kanonisch) und `FO4-Fast` (Ninja, Iteration).                                            |
| `vcpkg.json`                        | Genau eine Abhängigkeit.                                                                                                     |
| `include/PCH.h`                     | Vorkompilierter Header, ausschließlich `<F4SE/F4SE.h>`.                                                                      |
| `src/Runtime.h` / `src/Runtime.cpp` | Versionsakzeptanz und Runtime-Bezeichnung. Einzige testbare Einheit in A.                                                    |
| `src/XSEPlugin.cpp`                 | F4SE-Entrypoints, Initialisierung, Nachrichten-Listener.                                                                     |
| `tests/RuntimeTests.cpp`            | Host-Test ohne Framework; Rückgabewert ungleich null bei Fehlschlag.                                                         |
| `tools/verify-plugin.ps1`           | Prüft das erzeugte Artefakt: PE-Kopf, Architektur, Exporte, Versionsressource.                                               |

`cmake/Plugin.h.in`, `cmake/Version.rc.in` und `cmake/triplets/x64-windows-static-md-release.cmake` werden aus dem geerbten Baum unverändert weiterverwendet.

---

### Task 1: Sicherung und Repository-Schnitt

**Files:**

-   Create: `.github/workflows-disabled/` (13 verschobene Dateien)
-   Delete: `src/`, `include/PCH.h`, `include/FrameAnnotations.h`, `include/FidelityFX/`, `extern/*`, diverse `cmake/`- und Wurzeldateien
-   Modify: `.gitmodules` (wird geleert)

**Interfaces:**

-   Consumes: nichts
-   Produces: einen Arbeitsbaum, der die HLSL-Shader, `features/`, `package/`, `tools/`, `docs/` und die Formatierungskonfiguration enthält und sonst nichts Baubares. Der Tag `skyrim-base` als Zugang zum Altcode.

-   [ ] **Step 1: Tag setzen und pushen**

```bash
git tag -a skyrim-base 3d472fde -m "Skyrim Community Shaders 1.9.0-rc.1, base of the Fallout 4 port"
git push origin skyrim-base
```

-   [ ] **Step 2: Verifizieren, dass der Altcode über den Tag erreichbar ist**

```bash
git show skyrim-base:src/State.cpp | head -5
git show skyrim-base:cmake/XSEPlugin.cmake | head -3
```

Erwartet: beide Befehle geben Inhalt aus. Schlägt einer fehl, **hier abbrechen** — ohne diesen Zugang darf nichts gelöscht werden.

-   [ ] **Step 3: Submodule sauber entfernen**

```bash
for m in extern/CommonLibSSE-NG extern/Streamline-DX12 extern/FidelityFX-SDK; do
  git submodule deinit -f "$m"
  git rm -f "$m"
  rm -rf ".git/modules/$m"
done
git rm -r -f extern/sk_hdr_png
rm -f .gitmodules
```

-   [ ] **Step 4: Quellcode und Build-Gerüst entfernen**

```bash
git rm -r -f src include
git rm -f cmake/XSEPlugin.cmake cmake/FidelityFX-SDK.cmake \
           cmake/AddCXXFiles.cmake cmake/CleanupStaleEntries.cmake \
           cmake/FeatureVersions.h.in cmake/ThemePresets.h.in \
           cmake/shadertoolsconfig.json.in
git rm -r -f cmake/Streamline cmake/ports
git rm -f CMakeLists.txt CMakePresets.json CMakeUserPresets.json.template
git rm -f BuildRelease.bat BuildDev.bat BuildDevFast.bat BuildPR.bat BuildDebug.bat
git rm -f Dockerfile containerbuild.ps1 .releaserc.js .coderabbit.yaml
```

-   [ ] **Step 5: Workflows stilllegen**

```bash
git mv .github/workflows .github/workflows-disabled
```

`.github/actions/` und `.github/configs/` bleiben, wo sie sind — sie werden nur von den Workflows referenziert und sind ohne diese wirkungslos.

-   [ ] **Step 6: Prüfen, dass nur Gewolltes übrig ist**

```bash
git status --short
ls
git ls-files | awk -F/ '{print $1}' | sort | uniq -c | sort -rn
```

Erwartet: `features`, `package`, `docs`, `tools`, `.github` und die Konfigurationsdateien im Wurzelverzeichnis sind vorhanden; `src`, `include`, `extern` sind verschwunden. Die Shader-Zählung muss unverändert sein:

```bash
find package/Shaders features -name '*.hlsl' -o -name '*.hlsli' | wc -l
```

Erwartet: `153`.

-   [ ] **Step 7: Commit**

```bash
git add -A
git commit -F - <<'MSG'
chore: strip skyrim sources for the fallout 4 port

Remove the inherited plugin sources, submodules and build scaffolding.
Everything deleted here stays reachable through the skyrim-base tag and
is expected to be consulted when porting individual features later.

The HLSL shaders, feature directories, packaged assets, tooling and
formatting configuration are kept untouched: they are the actual port
target, not the thing being replaced.

CI workflows are moved aside rather than deleted so they can serve as
templates once shader validation and release automation return.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 2: Build-Gerüst mit commonlibf4

**Files:**

-   Create: `cmake/CommonLibF4.cmake.in`, `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `include/PCH.h`, `src/Placeholder.cpp`
-   Modify: `.gitmodules` (neu angelegt durch `git submodule add`)

**Interfaces:**

-   Consumes: den Baum aus Task 1; den Fork `PlasticGhoul/commonlibf4` (siehe Vorbedingung)
-   Produces: das CMake-Target `CommunityShadersFO4` (SHARED), das Alias-Target `CommonLibF4::CommonLibF4`, den vorkompilierten Header `include/PCH.h`, sowie die Cache-Variablen `FO4CS_DEPLOY_DIR` und `FO4CS_BUILD_TESTS`

-   [ ] **Step 1: Submodul hinzufügen**

```bash
git submodule add https://github.com/PlasticGhoul/commonlibf4.git extern/CommonLibF4
git submodule update --init --recursive
ls extern/CommonLibF4/lib/commonlib-shared/CMakeLists.txt
```

Erwartet: die Datei existiert. Fehlt sie, wurde das verschachtelte Submodul nicht initialisiert — `git submodule update --init --recursive` wiederholen.

-   [ ] **Step 2: Shim-Vorlage schreiben**

Datei `cmake/CommonLibF4.cmake.in`:

```cmake
# CMake shim for commonlibf4, which upstream ships with xmake.lua only.
#
# Written against the upstream layout as of 2026-08-30: include/, src/ and
# lib/commonlib-shared/. commonlib-shared carries a full CMakeLists of its own
# and is used directly; only commonlibf4 itself needs rebuilding here.
# If this file stops configuring, check whether upstream moved directories
# before changing anything else.
cmake_minimum_required(VERSION 4.2)

project(CommonLibF4Shim LANGUAGES CXX)

set(COMMONLIBF4_SOURCE_DIR "@COMMONLIBF4_SOURCE_DIR@")

add_subdirectory(
    "${COMMONLIBF4_SOURCE_DIR}/lib/commonlib-shared"
    "${CMAKE_CURRENT_BINARY_DIR}/commonlib-shared"
)

file(
    GLOB_RECURSE COMMONLIBF4_SOURCES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${COMMONLIBF4_SOURCE_DIR}/src/*.cpp"
)

file(
    GLOB_RECURSE COMMONLIBF4_HEADERS
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${COMMONLIBF4_SOURCE_DIR}/include/*.h"
)

add_library(CommonLibF4 STATIC ${COMMONLIBF4_SOURCES} ${COMMONLIBF4_HEADERS})
add_library(CommonLibF4::CommonLibF4 ALIAS CommonLibF4)

target_include_directories(
    CommonLibF4
    PUBLIC "${COMMONLIBF4_SOURCE_DIR}/include"
)

target_compile_features(CommonLibF4 PUBLIC cxx_std_23)

target_link_libraries(CommonLibF4 PUBLIC commonlib-shared::commonlib-shared)

target_compile_definitions(
    CommonLibF4
    PUBLIC
        _UNICODE
        UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_precompile_headers(
    CommonLibF4
    PRIVATE "${COMMONLIBF4_SOURCE_DIR}/include/F4SE/Impl/PCH.h"
)

if(MSVC)
    target_compile_options(CommonLibF4 PRIVATE /bigobj /utf-8)
endif()
```

-   [ ] **Step 3: vcpkg-Manifest schreiben**

Datei `vcpkg.json`:

```json
{
    "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
    "name": "communityshaders-fo4",
    "version-string": "0.1.0",
    "description": "Community Shaders for Fallout 4",
    "license": "GPL-3.0-or-later",
    "supports": "windows & x64",
    "builtin-baseline": "dddca6fa87f177e0678e2545c4b4636a44aa05bd",
    "dependencies": [
        {
            "name": "spdlog",
            "default-features": false,
            "features": ["wchar"]
        }
    ]
}
```

-   [ ] **Step 4: Vorkompilierten Header schreiben**

Datei `include/PCH.h`:

```cpp
#pragma once

// Pulls in F4SE/Impl/PCH.h, which in turn brings REL, REX (including REX::INFO
// and REX::FModule) and the RE ID tables. One include covers everything
// subproject A needs.
#include <F4SE/F4SE.h>

using namespace std::literals;
```

`<RE/Fallout.h>` fehlt hier bewusst: Teilprojekt A fasst keinen Engine-Typen an, und die rund 1450 RE-Header würden jeden Übersetzungslauf verlangsamen. Schritt 9 prüft trotzdem, dass sich der Header übersetzen lässt, damit ein Problem nicht erst in Teilprojekt B auffällt.

-   [ ] **Step 5: Platzhalter-Quelle anlegen**

Ein `SHARED`-Target braucht mindestens eine Übersetzungseinheit. Datei `src/Placeholder.cpp`:

```cpp
#include "PCH.h"

// Temporary translation unit so the shared library has something to compile
// before the F4SE entry points land in Task 4. Deleted there.
namespace
{
	[[maybe_unused]] constexpr auto kBuildProbe = 1;
}
```

-   [ ] **Step 6: Wurzel-CMakeLists schreiben**

Datei `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 4.2)

project(
    CommunityShadersFO4
    VERSION 0.1.0
    LANGUAGES CXX
)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "in-source builds are not allowed")
endif()

option(FO4CS_BUILD_TESTS "Build the host tests" ON)
set(FO4CS_DEPLOY_DIR
    ""
    CACHE PATH
    "If set, the built plugin is copied to <dir>/F4SE/Plugins after linking"
)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ---------------------------------------------------------------- git identity
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()

# ------------------------------------------------------------------ commonlibf4
# Upstream ships xmake.lua only, so a CMake shim is generated in the build tree.
set(COMMONLIBF4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/CommonLibF4")
if(NOT EXISTS "${COMMONLIBF4_SOURCE_DIR}/xmake.lua")
    message(
        FATAL_ERROR
        "extern/CommonLibF4 is empty. Run: git submodule update --init --recursive"
    )
endif()

set(COMMONLIBF4_SHIM_DIR "${CMAKE_BINARY_DIR}/CommonLibF4-shim")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/CommonLibF4.cmake.in"
    "${COMMONLIBF4_SHIM_DIR}/CMakeLists.txt"
    @ONLY
)
add_subdirectory("${COMMONLIBF4_SHIM_DIR}" CommonLibF4 EXCLUDE_FROM_ALL)

# --------------------------------------------------------------- plugin target
add_library(${PROJECT_NAME} SHARED)

file(
    GLOB_RECURSE PLUGIN_SOURCES
    LIST_DIRECTORIES false
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/*.h"
)
target_sources(${PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/Plugin.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
    @ONLY
)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/Version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
    @ONLY
)
target_sources(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/Plugin.h"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc"
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
        "${CMAKE_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake"
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(
    ${PROJECT_NAME}
    PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
)
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibF4::CommonLibF4)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}
        PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
    )
    # /MP is an MSBuild-only knob; Ninja parallelises on its own and warns.
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        target_compile_options(${PROJECT_NAME} PRIVATE /MP)
    endif()
endif()

if(FO4CS_DEPLOY_DIR)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            "${CMAKE_COMMAND}" -E make_directory
            "${FO4CS_DEPLOY_DIR}/F4SE/Plugins"
        COMMAND
            "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:${PROJECT_NAME}>" "${FO4CS_DEPLOY_DIR}/F4SE/Plugins/"
        COMMENT "Deploying to ${FO4CS_DEPLOY_DIR}/F4SE/Plugins"
        VERBATIM
    )
endif()
```

-   [ ] **Step 7: Presets schreiben**

Datei `CMakePresets.json`:

```json
{
    "version": 3,
    "cmakeMinimumRequired": {
        "major": 4,
        "minor": 2,
        "patch": 0
    },
    "configurePresets": [
        {
            "name": "common",
            "hidden": true,
            "binaryDir": "${sourceDir}/build/${presetName}",
            "cacheVariables": {
                "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
                "VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/cmake/triplets/",
                "VCPKG_TARGET_TRIPLET": "x64-windows-static-md-release",
                "VCPKG_BUILD_TYPE": "release",
                "CMAKE_MSVC_RUNTIME_LIBRARY": "MultiThreadedDLL"
            },
            "warnings": {
                "dev": false,
                "deprecated": false
            }
        },
        {
            "name": "FO4",
            "displayName": "Fallout 4 (Visual Studio, Release)",
            "inherits": "common",
            "generator": "Visual Studio 18 2026",
            "architecture": "x64",
            "cacheVariables": {
                "CMAKE_CXX_FLAGS": "/EHsc $penv{CXXFLAGS}",
                "CMAKE_CXX_FLAGS_RELEASE": "/O2 /Ob3 /DNDEBUG"
            }
        },
        {
            "name": "FO4-Fast",
            "displayName": "Fallout 4 (Ninja, fast iteration)",
            "inherits": "common",
            "generator": "Ninja",
            "architecture": {
                "value": "x64",
                "strategy": "external"
            },
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_CXX_FLAGS": "/EHsc $penv{CXXFLAGS}",
                "CMAKE_CXX_FLAGS_RELEASE": "/Od /Ob1 /DNDEBUG",
                "CMAKE_EXE_LINKER_FLAGS": "/machine:x64"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "FO4",
            "configurePreset": "FO4",
            "configuration": "Release"
        },
        {
            "name": "FO4-Fast",
            "configurePreset": "FO4-Fast"
        }
    ]
}
```

-   [ ] **Step 8: Konfigurieren und bauen**

Run:

```bash
cmake -S . --preset FO4
cmake --build --preset FO4
```

Erwartet: Konfiguration und Build laufen durch. `build/FO4/Release/CommunityShadersFO4.dll` existiert.

Bekanntes Risiko: die Include-Verzeichnisse von `CommonLibF4` sind `PUBLIC`, nicht `SYSTEM`. Warnungen aus Fremd-Headern schlagen daher gegen unser `/WX` durch. `commonlib-shared` unterdrückt `C4200`, `C4201` und `C4324` bereits `PUBLIC`. Tritt eine weitere Warnung auf, wird sie gezielt am **Plugin-Target** unterdrückt (`/wd<nummer>`) und mit einem Kommentar versehen, der den Ursprungs-Header nennt — nicht durch Abschalten von `/WX`.

-   [ ] **Step 9: Prüfen, dass sich auch `<RE/Fallout.h>` übersetzen lässt**

Dieser Schritt verifiziert die Bibliothek für Teilprojekt B, ohne den Header in den PCH zu ziehen.

Lege die Probe an:

```bash
cat > /tmp/re_probe.cpp <<'EOF'
#include <F4SE/F4SE.h>
#include <RE/Fallout.h>
int main() { return 0; }
EOF
```

Übersetze sie aus einer Developer-Shell heraus, getrennt vom Build:

```bash
cl /nologo /std:c++latest /EHsc /c /Fo:/tmp/re_probe.obj /tmp/re_probe.cpp \
   /I extern/CommonLibF4/include \
   /I extern/CommonLibF4/lib/commonlib-shared/include \
   /I build/FO4/vcpkg_installed/x64-windows-static-md-release/include
```

Erwartet: Exit-Code 0. Schlägt es fehl, wird das Ergebnis in `docs/fallout4-port/ROADMAP.md` unter Teilprojekt B als bekannter Blocker notiert — Task 2 gilt trotzdem als erfüllt, weil A den Header nicht braucht.

-   [ ] **Step 10: Warmbau prüfen**

Run: `cmake --build --preset FO4`
Erwartet: keine Übersetzungseinheit wird neu gebaut.

-   [ ] **Step 11: Commit**

```bash
git add -A
git commit -F - <<'MSG'
build: add commonlibf4 and a minimal cmake tree

Bring in commonlibf4 as a submodule and wire it into CMake through a
shim generated in the build tree, since upstream ships xmake.lua only.
commonlib-shared carries a usable CMakeLists of its own and is added
directly rather than rebuilt.

The plugin target compiles a placeholder translation unit for now; the
F4SE entry points follow. Dependencies are down to spdlog, which
commonlib-shared requires.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 3: Runtime-Erkennung

Die einzige Einheit in A, die still und folgenschwer falsch sein kann: ein Plugin, das sich auf einer fremden Spielversion lädt, schreibt an Adressen, die dort etwas anderes bedeuten. Deshalb testgetrieben.

**Files:**

-   Create: `src/Runtime.h`, `src/Runtime.cpp`, `tests/RuntimeTests.cpp`
-   Modify: `CMakeLists.txt` (Test-Target anhängen)

**Interfaces:**

-   Consumes: `CommonLibF4::CommonLibF4`, `include/PCH.h` aus Task 2
-   Produces:

    -   `constexpr REL::Version Runtime::kSupported` — die einzige validierte Spielversion
    -   `bool Runtime::IsSupported(REL::Version) noexcept` — exakte Übereinstimmung, ohne Spielzustand, daher host-testbar
    -   `std::string_view Runtime::BucketName() noexcept` — Name des von commonlibf4 aufgelösten Runtime-Eimers; **fasst den geladenen Prozess an und darf im Test nicht aufgerufen werden**

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

Datei `tests/RuntimeTests.cpp`:

```cpp
#include "Runtime.h"

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
	Check(
		Runtime::IsSupported(F4SE::RUNTIME_1_11_240),
		"accepts the validated runtime 1.11.240");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_163),
		"rejects OG 1.10.163");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_980),
		"rejects NG 1.10.980");
	Check(
		!Runtime::IsSupported(F4SE::RUNTIME_1_10_984),
		"rejects NG 1.10.984");
	Check(
		!Runtime::IsSupported(REL::Version{ 1, 11, 241, 0 }),
		"rejects a future version that would fall through to the AE bucket");
	Check(
		!Runtime::IsSupported(REL::Version{ 1, 11, 240, 1 }),
		"rejects a differing build field");

	if (g_failures != 0) {
		std::printf("\n%d check(s) failed\n", g_failures);
		return 1;
	}

	std::printf("\nall checks passed\n");
	return 0;
}
```

-   [ ] **Step 2: Test-Target anhängen**

An das Ende von `CMakeLists.txt`:

```cmake
if(FO4CS_BUILD_TESTS)
    enable_testing()

    add_executable(
        RuntimeTests
        "${CMAKE_SOURCE_DIR}/tests/RuntimeTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Runtime.cpp"
    )

    target_include_directories(
        RuntimeTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(RuntimeTests PRIVATE cxx_std_23)
    target_precompile_headers(
        RuntimeTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(RuntimeTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            RuntimeTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME Runtime COMMAND RuntimeTests)
endif()
```

-   [ ] **Step 3: Lauf zur Bestätigung, dass es fehlschlägt**

Run: `cmake -S . --preset FO4 && cmake --build --preset FO4`
Erwartet: Übersetzungsfehler — `Runtime.h` existiert nicht, `Runtime::IsSupported` ist unbekannt.

-   [ ] **Step 4: Minimale Implementierung schreiben**

Datei `src/Runtime.h`:

```cpp
#pragma once

namespace Runtime
{
	/// The one game version this build has been validated against.
	inline constexpr REL::Version kSupported = F4SE::RUNTIME_1_11_240;

	/// Exact match, deliberately not a range or a lower bound.
	///
	/// REX::FModule maps any unknown newer runtime onto the AE bucket, so a
	/// tolerant check would let a future game patch load this plugin against
	/// addresses that have moved. Free of game state so it can be tested on
	/// the host.
	[[nodiscard]] bool IsSupported(REL::Version a_version) noexcept;

	/// Name of the runtime bucket commonlibf4 resolved for the loaded process.
	/// Inspects the running module - never call this from a host test.
	[[nodiscard]] std::string_view BucketName() noexcept;
}
```

Datei `src/Runtime.cpp`:

```cpp
#include "Runtime.h"

namespace Runtime
{
	bool IsSupported(REL::Version a_version) noexcept
	{
		return a_version == kSupported;
	}

	std::string_view BucketName() noexcept
	{
		switch (REX::FModule::GetRuntimeIndex()) {
		case REX::FModule::Runtime::kOG:
			return "OG"sv;
		case REX::FModule::Runtime::kNG:
			return "NG"sv;
		case REX::FModule::Runtime::kAE:
			return "AE"sv;
		default:
			return "unknown"sv;
		}
	}
}
```

-   [ ] **Step 5: Lauf zur Bestätigung, dass es besteht**

Run:

```bash
cmake -S . --preset FO4
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: `1/1 Test #1: Runtime .... Passed`, alle sechs Prüfungen mit `ok`.

-   [ ] **Step 6: Den Test kurz brechen, um zu belegen, dass er greift**

Ändere in `src/Runtime.cpp` vorübergehend `return a_version == kSupported;` zu `return true;`, baue und führe `ctest` aus.
Erwartet: fünf `FAIL`-Zeilen, Rückgabewert 1. Danach zurückändern und erneut prüfen, dass alles besteht.

-   [ ] **Step 7: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add exact runtime version acceptance

Add Runtime::IsSupported, which accepts only the game version this build
was validated against, plus a host test covering the versions we expect
to see in the wild.

The check is an exact match on purpose. REX::FModule maps unknown newer
runtimes onto the AE bucket, so a lower-bound check would silently let a
future game patch load the plugin against relocated addresses.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 4: F4SE-Entrypoints

**Files:**

-   Create: `src/XSEPlugin.cpp`, `tools/verify-plugin.ps1`
-   Delete: `src/Placeholder.cpp`

**Interfaces:**

-   Consumes: `Runtime::IsSupported`, `Runtime::kSupported`, `Runtime::BucketName` aus Task 3; `Plugin::NAME`, `Plugin::VERSION`, `Plugin::BUILD_DESCRIBE` aus dem erzeugten `Plugin.h`
-   Produces: die exportierten Symbole `F4SEPlugin_Version`, `F4SEPlugin_Query`, `F4SEPlugin_Load`

-   [ ] **Step 1: Entrypoints schreiben**

Datei `src/XSEPlugin.cpp`:

```cpp
#include "Plugin.h"
#include "Runtime.h"

namespace
{
	void MessageHandler(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message == nullptr) {
			return;
		}

		switch (a_message->type) {
		case F4SE::MessagingInterface::kPostPostLoad:
			REX::INFO("kPostPostLoad received");
			break;
		case F4SE::MessagingInterface::kGameDataReady:
			REX::INFO("kGameDataReady received");
			break;
		default:
			break;
		}
	}
}

extern "C" [[maybe_unused]] __declspec(dllexport) constinit auto F4SEPlugin_Version = []() noexcept {
	F4SE::PluginVersionData data{};

	data.PluginName(Plugin::NAME.data());
	data.PluginVersion(Plugin::VERSION);
	data.AuthorName("PlasticGhoul");
	data.UsesAddressLibrary(true);
	data.UsesSigScanning(false);
	data.IsLayoutDependent(true);
	data.HasNoStructUse(false);
	// Pinned rather than RUNTIME_LATEST: a moving target would silently admit
	// an untested game version after the next Bethesda patch.
	data.CompatibleVersions({ F4SE::RUNTIME_1_11_240 });

	return data;
}();

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Query(
	const F4SE::QueryInterface*,
	F4SE::PluginInfo*           a_info)
{
	a_info->infoVersion = F4SE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];
	return true;
}

extern "C" __declspec(dllexport) bool F4SEAPI F4SEPlugin_Load(const F4SE::LoadInterface* a_f4se)
{
	F4SE::InitInfo initInfo{};
	initInfo.trampoline = false;  // subproject A installs no hooks
	initInfo.hook = false;        // no REL::FHook objects are registered either

	// Must run before the version check: Init is what opens the log channel,
	// and a refusal that cannot be logged is a refusal nobody can diagnose.
	F4SE::Init(a_f4se, initInfo);

	const auto runtime = a_f4se->RuntimeVersion();
	REX::INFO("build {}", Plugin::BUILD_DESCRIBE);
	REX::INFO("game runtime {}, resolved bucket {}", runtime, Runtime::BucketName());

	if (!Runtime::IsSupported(runtime)) {
		REX::ERROR(
			"unsupported game version {}; this build is validated against {} only, refusing to load",
			runtime,
			Runtime::kSupported);
		return false;
	}

	const auto* messaging = F4SE::GetMessagingInterface();
	if (messaging == nullptr || !messaging->RegisterListener(MessageHandler)) {
		REX::ERROR("failed to register the F4SE message listener");
		return false;
	}

	REX::INFO("loaded");
	return true;
}
```

-   [ ] **Step 2: Platzhalter entfernen**

```bash
git rm -f src/Placeholder.cpp
```

-   [ ] **Step 3: Bauen**

Run: `cmake -S . --preset FO4 && cmake --build --preset FO4`
Erwartet: Build grün.

Zwei mögliche Stolpersteine:

-   Ist `REX::INFO` unbekannt, ergänze in `include/PCH.h` nach dem F4SE-Include die Zeile
    `#include <REX/LOG.h>`. `REX/REX.h` sollte sie mitbringen, aber verlass dich nicht darauf.
-   `REX::INFO` ist ein Klassentemplate mit Argumentherleitung und einer eigenen Spezialisierung
    für den argumentlosen Fall. Scheitert der argumentlose Aufruf `REX::INFO("loaded")` an der
    Herleitung, schreibe ihn als `REX::INFO<void>("loaded")`. Betrifft in dieser Datei nur die
    drei Aufrufe ohne Formatargumente.

-   [ ] **Step 4: Prüfskript schreiben**

Datei `tools/verify-plugin.ps1`:

```powershell
<#
.SYNOPSIS
    Verify the built plugin is a loadable x64 F4SE plugin.

.DESCRIPTION
    Checks the PE header, the machine type, the DLL characteristic bit, the
    exported entry points and the version resource. Exits non-zero on the first
    failed check so it can gate a build.
#>
param(
    [string]$Dll = "build/FO4/Release/CommunityShadersFO4.dll",
    [string]$ExpectedName = "CommunityShadersFO4",
    [string]$ExpectedVersion = "0.1.0.0"
)

$ErrorActionPreference = "Stop"
$failures = 0

function Check([bool]$Passed, [string]$What) {
    if ($Passed) { Write-Host "ok    $What" }
    else { Write-Host "FAIL  $What"; $script:failures++ }
}

if (-not (Test-Path $Dll)) {
    Write-Host "FAIL  artefact not found: $Dll"
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($Dll)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)

$signature = [System.Text.Encoding]::ASCII.GetString($bytes, $peOffset, 2)
Check ($signature -eq "PE") "PE signature present"

$machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
Check ($machine -eq 0x8664) ("machine type is x64 (found 0x{0:X4})" -f $machine)

$characteristics = [BitConverter]::ToUInt16($bytes, $peOffset + 22)
Check (($characteristics -band 0x2000) -ne 0) "DLL characteristic bit set"

$exports = (dumpbin /exports $Dll) -join "`n"
foreach ($symbol in @("F4SEPlugin_Load", "F4SEPlugin_Query", "F4SEPlugin_Version")) {
    Check ($exports -match [regex]::Escape($symbol)) "exports $symbol"
}

$info = (Get-Item $Dll).VersionInfo
Check ($info.ProductName -eq $ExpectedName) "version resource names $ExpectedName (found '$($info.ProductName)')"
Check ($info.FileVersion -eq $ExpectedVersion) "file version is $ExpectedVersion (found '$($info.FileVersion)')"

if ($failures -gt 0) {
    Write-Host ""
    Write-Host "$failures check(s) failed"
    exit 1
}

Write-Host ""
Write-Host "all checks passed"
exit 0
```

-   [ ] **Step 5: Prüfskript ausführen**

Run (aus einer Developer-Shell, damit `dumpbin` verfügbar ist):

```powershell
pwsh tools/verify-plugin.ps1
```

Erwartet: acht `ok`-Zeilen, `all checks passed`, Rückgabewert 0.

Steht `dumpbin` nicht zur Verfügung, ersetze den Export-Block durch die Python-Variante, die die Exporttabelle direkt aus dem PE-Abbild liest — sie ist zuverlässiger als ein Werkzeug aus dem `PATH`, aber `dumpbin` ist in einer Developer-Shell immer da.

-   [ ] **Step 6: Test-Suite erneut laufen lassen**

Run: `ctest --test-dir build/FO4 -C Release --output-on-failure`
Erwartet: weiterhin grün.

-   [ ] **Step 7: Commit**

```bash
git add -A
git commit -F - <<'MSG'
feat: add f4se entry points and artefact check

Export F4SEPlugin_Version, _Query and _Load. The plugin initialises
F4SE without a trampoline and without hooks, logs its identity and the
runtime it found, and refuses to load on any version other than the one
it was validated against.

F4SE::Init runs before the version check on purpose: it is what opens
the log channel, so refusing earlier would produce a silent failure.

tools/verify-plugin.ps1 checks the built DLL for architecture, the DLL
bit, the three exports and the version resource.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 5: Dokumentation

Die geerbte Dokumentation beschreibt durchgehend Skyrim und würde jeden künftigen Beitragenden — Menschen wie Agenten — in die Irre führen.

**Files:**

-   Modify: `.clangd`, `.claude/CLAUDE.md`, `AI-INSTRUCTIONS.md`, `.github/copilot-instructions.md`, `README.md`, `TRANSLATING.md`

**Interfaces:**

-   Consumes: den fertigen Build aus Task 4
-   Produces: keine Code-Schnittstelle

-   [ ] **Step 1: `.clangd` umstellen**

```yaml
CompileFlags:
    Add:
        - -std=c++23
        - -Iextern/CommonLibF4/include
        - -Iextern/CommonLibF4/lib/commonlib-shared/include
        - -Isrc
        - -Iinclude
        - -Ibuild/FO4/vcpkg_installed/x64-windows-static-md-release/include
    Compiler: clang-cl
```

-   [ ] **Step 2: `README.md` ersetzen**

Ersetze alles oberhalb des Abschnitts `## License` durch den folgenden Text. Der Lizenzabschnitt
des geerbten README bleibt **unverändert** erhalten — er regelt auch die Shader, die wir behalten.

````markdown
# Community Shaders for Fallout 4

A port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
to Fallout 4, as an F4SE plugin.

## Status

Early. Subproject A of the port: the plugin loads, reports itself and refuses unsupported game
versions. It does not render anything yet. See
[the roadmap](docs/fallout4-port/ROADMAP.md) for the plan and current state.

## Requirements

### Building

-   [Visual Studio 2026](https://visualstudio.microsoft.com/) with **Desktop development with C++**
    and the **Windows 11 SDK**
-   CMake 4.2 or newer (the Visual Studio component is sufficient)
-   [vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` pointing at it
-   [Git](https://git-scm.com/downloads)

### Running

-   Fallout 4 **AE 1.11.240**. Older runtimes are not supported yet; the plugin refuses to load on
    them rather than misbehave.
-   [F4SE](https://f4se.silverlock.org/)
-   [Address Library for F4SE Plugins](https://www.nexusmods.com/fallout4/mods/47327)

## Building

```pwsh
git clone --recursive https://github.com/PlasticGhoul/fallout4-community-shaders.git
cd fallout4-community-shaders
cmake -S . --preset FO4
cmake --build --preset FO4
```

The `--recursive` matters: commonlibf4 carries a submodule of its own. If you already cloned
without it, run `git submodule update --init --recursive`.

Use the `FO4-Fast` preset (Ninja, unoptimised) while iterating. Set `FO4CS_DEPLOY_DIR` to have the
built plugin copied to `<dir>/F4SE/Plugins` after linking:

```pwsh
cmake -S . --preset FO4 -DFO4CS_DEPLOY_DIR="C:/path/to/your/mod/folder"
```

## Testing

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```
````

-   [ ] **Step 3: `.claude/CLAUDE.md` auf den Fallout-Stand bringen**

Ersetze die Skyrim-Abschnitte durch: Build-Befehle der neuen Presets, commonlibf4 statt CommonLibSSE-NG samt Shim-Erklärung, Verweis auf Roadmap und Specs als verbindliche Quelle, sowie einen ausdrücklichen Abschnitt „Derzeit gegenstandslos", der Packaging, Release-Zweigmodell, i18n, Feature-Versionierung und Shader-Validierung als bis zur Reaktivierung der jeweiligen Teilprojekte ungültig markiert. Die allgemeinen Abschnitte zu Codequalität, Namensgebung und Fehlerbehandlung bleiben.

-   [ ] **Step 4: `AI-INSTRUCTIONS.md` kürzen**

Auf einen Verweis auf `.claude/CLAUDE.md` und `docs/fallout4-port/ROADMAP.md` reduzieren, Skyrim-spezifische Kommandos entfernen.

-   [ ] **Step 5: `.github/copilot-instructions.md` nachziehen**

Die Datei verweist auf `.claude/CLAUDE.md` und `AI-INSTRUCTIONS.md` und ist nach den Schritten 3
und 4 damit zur Hälfte richtig. Zu korrigieren sind die Copilot-spezifischen Abschnitte darunter,
die weiterhin Skyrim-Build-Befehle und -Pfade nennen. Die Datei ist erst bei der Ausführung von
Task 1 aufgefallen; Spec Abschnitt 4.5 führt sie nicht.

-   [ ] **Step 6: `TRANSLATING.md` mit Hinweis versehen**

Einen Absatz am Anfang: das i18n-System ist bis Teilprojekt E inaktiv, die Anleitung bleibt gültig für den Zeitpunkt, an dem es zurückkehrt.

-   [ ] **Step 7: pre-commit über alles laufen lassen**

Run: `pre-commit run --all-files`
Erwartet: grün, oder automatisch korrigierte Formatierung — dann erneut laufen lassen.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
docs: point the project docs at fallout 4

Rewrite the inherited README, CLAUDE.md and AI-INSTRUCTIONS.md, which
described the Skyrim build throughout and would misdirect anyone -
human or agent - working on the port.

CLAUDE.md gains an explicit section listing what is currently moot:
packaging, the release branch model, i18n, feature versioning and
shader validation all return with their respective subprojects.

.clangd now points at commonlibf4 so editor diagnostics work.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

### Task 6: Abnahme im Spiel

Diese Task braucht einen Menschen an einer Fallout-4-Installation. Sie schließt A ab.

**Files:**

-   Modify: `docs/fallout4-port/ROADMAP.md`

**Interfaces:**

-   Consumes: alles Vorherige
-   Produces: den bestätigten Logpfad und die getestete F4SE-Version als Tatsachen für Teilprojekt B

-   [ ] **Step 1: Kaltbau aus frischem Klon**

```bash
cd "$(mktemp -d)"
git clone --recursive -b port/a-fundament https://github.com/PlasticGhoul/fallout4-community-shaders.git cold
cd cold
cmake -S . --preset FO4
cmake --build --preset FO4
```

Erwartet: läuft ohne manuelle Zwischenschritte durch. Das ist der eigentliche Test des Fundaments.

Anschließend denselben Build-Befehl ein zweites Mal ausführen. Erwartet: keine Übersetzungseinheit
wird neu gebaut. Der Warmbau-Test aus Task 2 lief gegen einen Baum ohne Plugin-Quellen; hier wird
er gegen den Endstand wiederholt.

-   [ ] **Step 2: Deployen**

```bash
cmake -S . --preset FO4 -DFO4CS_DEPLOY_DIR="<Pfad zum Data-Ordner oder Mod-Ordner>"
cmake --build --preset FO4
```

Erwartet: `<Ziel>/F4SE/Plugins/CommunityShadersFO4.dll` existiert.

-   [ ] **Step 3: Spiel starten**

Fallout 4 AE 1.11.240 über F4SE starten, bis ins Hauptmenü.
Erwartet: Das Spiel startet normal.

-   [ ] **Step 4: Unser Log prüfen**

Datei: `<Dokumente>/My Games/Fallout4/F4SE/CommunityShadersFO4.log`
Erwartet: eine Zeile mit Name und Version (von der Bibliothek geschrieben), eine mit `build …`, eine mit `game runtime 1.11.240.0, resolved bucket AE`, eine mit `loaded`, sowie je eine für `kPostPostLoad` und `kGameDataReady`.

Weicht der Pfad ab, ist das kein Fehler unsererseits — die Bibliothek bildet ihn aus `GetSaveFolderName()`. Den tatsächlichen Pfad notieren, er geht in Schritt 7.

-   [ ] **Step 5: F4SE-Log prüfen**

Datei: `<Dokumente>/My Games/Fallout4/F4SE/f4se.log`
Erwartet: das Plugin ist als geladen aufgeführt, nicht als abgelehnt oder inkompatibel.

-   [ ] **Step 6: Negativtest**

Ändere in `src/Runtime.h` vorübergehend `kSupported` auf `F4SE::RUNTIME_1_10_163`, baue, deploye, starte das Spiel.

Erwartet: Das Spiel erreicht das Hauptmenü. Unser Log enthält die `unsupported game version`-Zeile mit beiden Versionen. Das F4SE-Log führt das Plugin nicht als geladen.

Danach zurückändern, neu bauen, deployen und Schritt 4 wiederholen.

Ohne diesen Schritt ist die Ablehnungslogik im laufenden Spiel unbelegt — der Host-Test aus Task 3 deckt nur die Entscheidungsfunktion ab, nicht ihre Wirkung.

-   [ ] **Step 7: Roadmap fortschreiben**

In `docs/fallout4-port/ROADMAP.md` den Status von Teilprojekt A von „in Spezifikation" auf „abgeschlossen" setzen und einen kurzen Abschnitt „Für B bestätigt" ergänzen mit: dem tatsächlichen Logpfad, dem von `GetSaveFolderName()` gelieferten Wert, der getesteten F4SE-Version, dem von `BucketName()` gemeldeten Eimer, und dem Ergebnis der `<RE/Fallout.h>`-Probe aus Task 2 Schritt 9.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -F - <<'MSG'
docs: record subproject a acceptance

Mark the fundament as complete and write down what the in-game run
actually confirmed: the log path the library produced, the F4SE version
tested against and the runtime bucket it resolved.

Subproject B builds on these as given rather than re-deriving them.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Offene Risiken

-   **Warnungen aus Fremd-Headern.** `CommonLibF4` exportiert seine Includes als `PUBLIC`, nicht `SYSTEM`; unter `/W4 /WX` schlagen deren Warnungen bei uns durch. `commonlib-shared` unterdrückt `C4200`, `C4201` und `C4324` bereits. Weitere Fälle werden gezielt am Plugin-Target unterdrückt, nie durch Abschalten von `/WX`.
-   **Upstream-Umbau von commonlibf4.** Der Shim setzt das Layout `include/`, `src/`, `lib/commonlib-shared/` voraus. Upstream pusht täglich. Der Kommentar im Shim nennt den Stand, gegen den er geschrieben wurde.
-   **`commonlib-shared` per `add_subdirectory`.** Belegt durch das vorhandene, vollständige `CMakeLists.txt` der Bibliothek, aber nicht in dieser Kombination erprobt. Fällt es durch, weicht Task 2 auf den Nachbau per Glob aus, den die Referenzimplementierung verwendet — rund achtzig zusätzliche Zeilen im Shim.
-   **`GetSaveFolderName()`** ist erst zur Laufzeit bekannt. Task 6 Schritt 4 stellt das fest, statt es anzunehmen.
