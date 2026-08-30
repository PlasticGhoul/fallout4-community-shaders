# Community Shaders for Fallout 4

A port of [Skyrim Community Shaders](https://github.com/community-shaders/skyrim-community-shaders)
to Fallout 4, as an F4SE plugin.

## Status

Early. Subproject A of the port: the plugin loads, reports itself and refuses unsupported game
versions. It does not render anything yet. See [the roadmap](docs/fallout4-port/ROADMAP.md) for the
plan and the current state.

The inherited Skyrim plugin sources are not in the working tree. They remain reachable through the
`skyrim-base` tag, for instance `git show skyrim-base:src/Features/Skylighting.cpp`. The HLSL
shaders were kept — they are the actual thing being ported.

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

## License

### Default

[GPL-3.0-or-later](COPYING) WITH [Modding Exception AND GPL-3.0 Linking Exception (with Corresponding Source)](EXCEPTIONS.md).  
Specifically, the Modded Code includes:

-   Fallout 4 (and its variants)
-   Skyrim (and its variants)
-   Hardware drivers to enable additional functionality provided via proprietary SDKs, such as [Nvidia DLSS](https://developer.nvidia.com/rtx/dlss/get-started) and [AMD FidelityFX FSR3](https://gpuopen.com/fidelityfx-super-resolution-3/)

The Modding Libraries include:

-   [F4SE](https://f4se.silverlock.org/)
-   [SKSE](https://skse.silverlock.org/)
-   Commonlib (and variants).

### Shaders

See LICENSE within each directory; if none, it's [Default](#default)

-   [Features Shaders](features)
-   [Package Shaders](package/Shaders/)

### Icons

-   [Community Shaders Logo](package/Interface/CommunityShaders/Icons/Community%20Shaders%20Logo/) is not covered by the GPL-3.0 license. It is provided solely for personal use (e.g., building from source) and may only be used in unmodified form. There is no license for any other purpose or to distribute the logo. No trademark license is granted for the logo. Any use not expressly permitted is prohibited without the express written consent of the Community Shaders team.
