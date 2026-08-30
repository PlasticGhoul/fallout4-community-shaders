# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this repository is

A fork of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
(`1.9.0-rc.1`) being ported to **Fallout 4** as an F4SE plugin. It is no longer a Skyrim project.

The inherited Skyrim plugin sources were removed from the working tree in the first port commit.
They remain reachable through the `skyrim-base` tag and consulting them while porting is expected:

```bash
git show skyrim-base:src/Features/Skylighting.cpp
git show skyrim-base:src/Hooks.cpp
```

The HLSL under `package/Shaders/` and `features/*/Shaders/` was **kept untouched** — those 153
files are the actual porting target.

## Read first

1. `docs/fallout4-port/ROADMAP.md` — the port cut into subprojects A through F, with decisions and
   status. Decisions recorded there have been made deliberately; do not relitigate them.
2. `docs/superpowers/specs/` — the design for the subproject in flight.
3. `docs/superpowers/plans/` — its implementation plan.

## Build

```pwsh
# first time, or after switching branches
git submodule update --init --recursive

cmake -S . --preset FO4        # Visual Studio 18 2026, Release - canonical
cmake --build --preset FO4

cmake -S . --preset FO4-Fast   # Ninja, /Od, incremental - iteration
cmake --build --preset FO4-Fast
```

Requires Visual Studio 2026 with Desktop C++ and the Windows 11 SDK, CMake 4.2+, and vcpkg with
`VCPKG_ROOT` set. Set `FO4CS_DEPLOY_DIR` to copy the built plugin to `<dir>/F4SE/Plugins` after
linking.

**Keep the checkout path short.** The Visual Studio generator writes `.tlog` files deep under
`build/FO4/CMakeFiles/`, and MSBuild fails with `MSB6003 ... DirectoryNotFoundException` once those
exceed `MAX_PATH`. A checkout roughly 100 characters deep is already enough to break a cold
configure, and the error names `link.exe` rather than the real cause.

## Test

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

`verify-plugin.ps1` checks the built DLL: PE header, x64, DLL bit, the three F4SE exports, version
resource. It parses the PE image directly, so it needs no Developer prompt.

## Engine library

`extern/CommonLibF4` is a submodule of `PlasticGhoul/commonlibf4`, our fork of
`Dear-Modding-FO4/commonlibf4`. It carries `lib/commonlib-shared` as a nested submodule, which is
why `--recursive` matters.

Upstream ships **only `xmake.lua`**. CMake integration therefore works like this:

-   `commonlib-shared` has a full `CMakeLists.txt` of its own and is added with `add_subdirectory`.
-   `commonlibf4` gets a shim: `cmake/CommonLibF4.cmake.in` is written into the build tree by
    `configure_file` and added from there. It exports `CommonLibF4::CommonLibF4`.

The shim assumes the upstream layout `include/`, `src/`, `lib/commonlib-shared/`. If configuration
breaks, check whether upstream moved directories before changing anything else.

We fork rather than consume upstream directly because these are missing from **every** commonlibf4
fork and will have to be added by us: `BSRenderPass` (forward-declared only), a named
`RENDER_TARGET` enum (Fallout 4 keeps its targets as an anonymous `renderTargets[101]` in
`BSGraphics::RendererData`), `ShadowSceneNode`, and `BSLight`.

## Runtime targeting

Only **Fallout 4 AE 1.11.240** is supported. `Runtime::IsSupported` checks the version reported by
`F4SE::LoadInterface::RuntimeVersion()` for an **exact** match and `F4SEPlugin_Load` returns
`false` otherwise.

This is deliberate and must not be loosened into a range or a lower bound:
`REX::FModule::GetRuntimeIndex()` maps any unknown newer runtime onto the `kAE` bucket, so a
tolerant check would let a future game patch load the plugin against relocated addresses.

NG (1.10.980 / 1.10.984) and OG (1.10.163) are carried by the scaffolding — `REL::ID` takes an
`{ og, ng, ae }` triple and `COMMONLIB_RUNTIMECOUNT` defaults to 3 — but neither is validated.

## Logging

`F4SE::Init` opens the log channel itself when `InitInfo::log` is set, which is the default. It
resolves `<Documents>/My Games/{GetSaveFolderName()}/F4SE/{PluginName}.log`, installs an MSVC and a
file sink, applies `logLevel` and `logPattern`, and writes the first line with name and version.
Write through `REX::INFO` / `REX::ERROR`; do not add a logging module.

Consequence: `F4SE::Init` must run **before** the runtime check, otherwise a refusal cannot be
logged.

## Conventions

-   C++23, MSVC only. Our target builds with `/W4 /WX`; the two third-party targets keep their own
    options and are not to be touched. If a foreign header emits a new warning, suppress it
    narrowly on our target with a comment naming the header — never by relaxing `/WX`.
-   Conventional commits. Title at most 50 characters, body wrapped at 72.
-   pre-commit runs clang-format, prettier and gersemi. Expect it to reformat `CMakeLists.txt` and
    Markdown on first touch of a file.
-   Descriptive, domain-specific names. Comments explain why, not what.
-   Complete implementations only. No TODO or placeholder code.

## Temporarily moot

The following were inherited from the Skyrim project and are **not in effect**. Do not follow,
restore or reference them until the subproject that owns them lands. Their originals are in git
history and under `.github/workflows-disabled/`.

| Area                                                                    | Returns with          |
| ----------------------------------------------------------------------- | --------------------- |
| Packaging, AIO archives, `dist/`, feature `.ini` version audit          | Subproject D          |
| Release branch model, semantic-release, hotfix lines, Nexus upload      | after the port ships  |
| i18n (`T()`/`TKEY`, `extract-i18n.py`, `sort-i18n.py`), themes, fonts   | Subproject E          |
| Feature framework, `Feature` base class, release stages, `CORE` markers | Subproject D          |
| Shader validation (`hlslkit`), shader defines, buffer scanning          | Subproject C          |
| CI workflows, PR checks, shader validation in CI                        | when the above return |

`features/` still holds all 40 inherited feature directories with their `.ini` files and assets.
They are raw material for subprojects D and F, not an active feature set.
