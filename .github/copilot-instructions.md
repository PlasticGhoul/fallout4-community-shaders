# GitHub Copilot Instructions

**ALWAYS follow these instructions first and only fall back to additional search and context
gathering if the information is incomplete or found to be in error.**

## What this repository is

A fork of Skyrim Community Shaders being ported to **Fallout 4** as an F4SE plugin. It is no
longer a Skyrim project. The inherited Skyrim plugin sources were removed from the working tree
and remain reachable through the `skyrim-base` tag; the HLSL shaders were kept and are the actual
porting target.

## Primary documentation

-   **`docs/fallout4-port/ROADMAP.md`** — how the port is cut into subprojects, what is decided,
    what is open. Decisions there are not to be relitigated.
-   **`.claude/CLAUDE.md`** — build, test, engine library, runtime targeting, conventions, and an
    explicit list of inherited areas that are temporarily moot.
-   **`docs/superpowers/specs/`** and **`docs/superpowers/plans/`** — the subproject in flight.

This file avoids duplicating those.

## Build essentials

Windows only. Visual Studio 2026 with Desktop C++ and the Windows 11 SDK, CMake 4.2+, vcpkg with
`VCPKG_ROOT`, Git.

```pwsh
git clone --recursive https://github.com/PlasticGhoul/fallout4-community-shaders.git
cd fallout4-community-shaders
cmake -S . --preset FO4
cmake --build --preset FO4
```

`--recursive` matters: commonlibf4 carries a nested submodule. A cold build takes minutes, not the
better part of an hour — the inherited Skyrim build times no longer apply.

Use `FO4-Fast` (Ninja) while iterating. Verify with `ctest --test-dir build/FO4 -C Release` and
`pwsh tools/verify-plugin.ps1`.

Linux and WSL cannot build this: it needs MSVC and, later, `fxc.exe`.

## Role

Act as an experienced graphics programming and Fallout 4 modding expert: DirectX 11 pipelines and
performance, F4SE plugin development, commonlibf4 runtime targeting, HLSL and GPU compute, ImGui.

Flag problems before they land — performance cost in the render path, crashes from unvalidated
input, runtime-compatibility breaks across OG/NG/AE, GPU register conflicts. Provide complete,
working code; no TODO or placeholder implementations. Explain reasoning for anything that touches
the render pipeline.
