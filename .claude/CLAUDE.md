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
python tools/extract-i18n.py               # fails if en.json is out of date
```

`verify-plugin.ps1` checks the built DLL: PE header, x64, DLL bit, the three F4SE exports, version
resource. It parses the PE image directly, so it needs no Developer prompt.

Twelve host test binaries, one per subject, each a `main` with a hand written `Check`. There is no
framework and none is wanted. **Break every test on purpose once it is green** — and check the
build actually succeeded first: a mutation that fails to compile leaves the previous executable
behind, and running it looks like a passing test.

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

## Features

Subproject D1 landed the runtime framework. Everything a feature needs is in `src/Feature/`, the
features themselves in `src/Features/`.

A feature derives from `Features::Feature` and implements five methods: `Name`, `Declare`,
`Setup`, `Frame` and `Shutdown`. `Features::Registry` owns them, holds one of three states per
feature — off, running, refused — and is driven once per `Present` from `Features::TickSystem`.
Register in `RegisterAll` (`src/Feature/FeatureSystem.cpp`); registration order is startup order,
and teardown runs in reverse. Assets a feature needs on disk go in `package/Features/<Name>/`, see
Packaging below.

**A feature declares its own settings, in `Declare`.** The registry calls it from `Register`,
which is what puts it before `Settings::Init` — a REX setting registers with its store at
construction, and `Init` walks that registration, so a declaration arriving later is never read
from the file. `Declare` must touch nothing but `Settings`; it runs whether or not the feature is
ever enabled, and it is emphatically not a place to acquire anything.

Three rules that are not obvious from the headers:

-   **`Setup` fails only on things that would fail again.** A refused feature is not retried until
    the settings file changes, so waiting for the engine to be ready belongs in `Frame`, never in
    `Setup`. `ImagespaceTint` is the worked example: its catalog runs from `Frame`.
-   **Read settings straight through `Settings::Get*`, every time.** There is no change
    notification. Whoever caches a value has to refresh it themselves.
-   **A feature that owns engine state must give it back in `Shutdown`.** It is called in the
    middle of a running game now, not only at exit: the overlay can switch a feature off. E2
    proved it on `ImagespaceTint`, which hands a pointer back into engine memory and joins a
    thread.

## Settings

`src/Settings/` is a system of its own, used by features, the menu and i18n alike. It is split in
two halves for a reason: `Schema.cpp` describes what settings exist and reaches neither REX nor a
file, which is what makes the description testable and is what the menu draws from; `Store.cpp`
owns the value and its way to disk.

Settings live in `<Documents>/My Games/Fallout4/F4SE/CommunityShadersFO4.json`, next to the log,
one block per feature. `Init` writes the file when it is absent and **extends it when declared
keys are missing**, keeping the values and the unknown keys it already had.

A declaration carries a kind, not merely a type — a virtual key code is a `double` like any
other, and neither a slider nor a number box is the right thing to put in front of one:

| Kind     | Declared with                            | Stored as     | Drawn as           |
| -------- | ---------------------------------------- | ------------- | ------------------ |
| `Bool`   | `DeclareBool(path, default)`             | `bool`        | checkbox           |
| `Slider` | `DeclareSlider(path, default, min, max)` | `double`      | slider             |
| `Choice` | `DeclareChoice(path, default, list)`     | `std::string` | combo              |
| `Key`    | `DeclareKey(path, default)`              | `double`      | key capture button |

Each returns a `Handle` carrying `.Label(key, english)` and `.Help(key, english)`.
`DeclareFeature` is shorthand for `DeclareBool("<Name>/enabled", …)` and additionally marks the
entry as the block's switch, which is why the panel draws it in the heading rather than in the
list. A path is `"Block/Key"` — one slash, neither half empty — because the store addresses
values as JSON pointers.

Four rules that cost measurements to find:

-   **Whole numbers are stored as `double`.** `glz::get<T>` matches the variant alternative with
    `std::same_as`, and `glz::generic` holds a JSON number only ever as a `double`, so
    `TJsonSetting<std::uint32_t>` links and runs but always returns its declared default. Only
    `bool`, `double` and `std::string` can read a file at all.
-   **We write the file ourselves, and REX's `Save()` is never called.** It writes through
    `glz::set`, which bails out at `value.find(key) == end()`, so it can neither create a file nor
    extend one. `glz::generic::operator[]` inserts, which is what our writer is built on.
-   **Our file goes in as `fileUser`, not `fileBase`.** `FJsonSettingStore::Load` hands `fileBase`
    to `TJsonSetting::Load(…, a_isBase = true)`, which reads the file's value into
    **`m_valueDefault`** — the declared default is then whatever the first file happened to say.
-   **`Save()` rebases the file watch.** Otherwise the next poll reads our own write as somebody
    else's change and sets every feature up again. `ConsumeChanged()` answers for both halves: the
    file changed, or something was `Set`.

## Menu

`src/Menu/` is a system of its own next to `Features`, and deliberately **not** a feature: it is
the thing that drives features, so switching it off through the file it exists to edit would be
backwards, and every toggle would tear down ImGui's D3D resources. `Menu::StartSystem` declares
its settings from `kGameDataReady`; `Menu::TickSystem` runs once per `Present`, after
`Features::TickSystem`, because the overlay belongs on top of whatever they drew.

`Menu::SettingsPanel` draws the whole overlay from the settings schema and the feature registry.
It names no feature: a schema block with no feature of that name is a general setting, one with a
feature is that feature. A feature therefore gets a surface by declaring settings, and nothing
else — no ImGui in `src/Features/`, ever.

Six things to know before touching it:

-   **The overlay owns the mouse position, and the ImGui backend must not contribute one.**
    `WM_MOUSEMOVE` and `WM_NCMOUSEMOVE` are deliberately kept from
    `ImGui_ImplWin32_WndProcHandler` while the overlay is open. ImGui trickles its event queue and
    stops at the first position event after a button event (`imgui.cpp`, "Trickling Rule"), so a
    frame carrying both a move and a click would hit-test the click at the _system_ cursor — which
    `MousePointer` parks in the middle of the window. This cost half of subproject E2's debugging;
    the full account is in the E2 section of the roadmap.
-   **The window procedure only sets a flag.** It runs on the window thread; the input layer and
    ImGui belong to the render thread. `Menu::Gate` turns that flag into exactly one transition
    per tick, however many key repeats arrived, and knows neither ImGui nor the engine — which is
    what makes it testable without a game (`tests/MenuGateTests.cpp`). Key capture takes the same
    route, and is asked **before** the toggle key: the other way round the toggle key could never
    be rebound onto itself.
-   **`src/Menu/Win32.h` carries what `REX::W32` lacks.** `SetWindowLongPtrW`, `CallWindowProcW`,
    the cursor functions, and every input `WM_` constant — the `WM` enumeration stops at
    `WM_CHILDACTIVATE` (`0x0022`). Add missing USER32 there, never `<Windows.h>`.
-   **`ImGui_ImplWin32_WndProcHandler` is declared against `::HWND__`**, not `REX::W32::HWND`. Its
    C++ mangled name carries the parameter types, and REX's `HWND` is a different tag type; the
    mismatch fails in the linker with a message that does not name the cause.
-   **The overlay drives its own pointer.** While the game runs, something holds the system cursor
    inside the middle 1280×720 of the screen and corrects a single pixel past it — not through
    `ClipCursor`. `Menu::MousePointer` parks the cursor in the window's middle every frame and
    uses only the distance travelled, so those edges are never approached. Details and the
    measurements are in the E1 section of the roadmap.
-   **ImGui 1.92 sizes a font where it is pushed.** `PushFont(font, size)` — the one argument form
    was removed. Glyphs load on demand because the dx11 backend reports
    `ImGuiBackendFlags_RendererHasTextures`, so no glyph ranges are declared and a new language
    needs no change in source. `Menu::ApplyTheme` resets the style before calling `ScaleAllSizes`,
    which multiplies what is already there.
-   **Every user-visible string goes through `T("key", "English default")`**, and so do
    `.Label(…)` and `.Help(…)`. `tools/extract-i18n.py --write` reads all three with one regex and
    regenerates `en.json`; running it without `--write` reports the difference and fails. The keys
    are written out rather than derived from a setting's path, so there is no rule to keep
    identical in two languages.

## Packaging

```pwsh
cmake --build --preset FO4 --target package   # writes dist/, three archives
pwsh tools/verify-package.ps1                 # checks the tree and the archives
```

`tools/package.ps1` holds the one rule that turns this repo into a mod tree, and it has two
callers. With `-Stage <dir>` it assembles the tree and stops; the build's deploy step calls it that
way, so the installation you play is the tree that ships. Without `-Stage` it writes three archives
to `dist/`: the base, one per feature without a `CORE` marker, and an all-in-one. The target hangs
outside `ALL` - the iteration build writes no archives.

Feature assets live in `package/Features/<Name>/`, laid out `Data`-relative. A file named `CORE` in
that directory means the feature ships inside the base archive rather than as its own addon; the
marker itself never reaches an archive. `features/` is **not** the place for them: it holds the 40
inherited Skyrim directories, 27 of which carry a `CORE` marker of their own.

`package/` is never copied wholesale. Only `package/Shaders/FO4`, `package/F4SE` and
`package/Features` travel.

`package/F4SE/Plugins/CommunityShadersFO4/` holds what the plugin reads at runtime — the overlay
font and the translations. It is in the **base**, not an addon: the overlay is what a feature is
switched from, so what the overlay reads cannot be optional. `Util::PluginDataDirectory()` is the
one place that path is formed.

**`package/Interface` and `package/SKSE` ship nothing, and are not deleted.** Subproject E2
settled this. Fallout 4 loads from `Data/F4SE/Plugins`, so the inherited `SKSE` tree could not be
pointed at even if we wanted it; the six themes, forty icons and fifty font faces stay as raw
material for F+, exactly like `features/`. `verify-package.ps1` fails if anything from either
turns up in a tree or an archive.

Two things worth knowing before touching any of it:

-   **The deploy step writes straight into the game, past whatever mod manager is installed.**
    Changing the staging rule changes the live installation on the next build.
-   **Archive paperwork stays out of the staged tree.** A mod manager deploys `COPYING` and the
    readme as hard links, and writing over those follows the link back into its staging folder.

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

| Area                                                               | Returns with          |
| ------------------------------------------------------------------ | --------------------- |
| Feature `.ini` versions and their audit                            | not before F          |
| Release branch model, semantic-release, hotfix lines, Nexus upload | after the port ships  |
| The inherited translations, themes and icons under `package/`      | not before F          |
| Release stages, feature categories and constraints                 | not before F          |
| Shader validation (`hlslkit`), shader defines, buffer scanning     | Subproject C          |
| CI workflows, PR checks, shader validation in CI                   | when the above return |

i18n itself is **no longer moot**: subproject E2 brought `T()` and `tools/extract-i18n.py` back,
on glaze rather than `nlohmann/json`, with a catalogue that starts empty. `TKEY` and
`sort-i18n.py` did not come back and are not planned. The inherited nine translation files are
raw material, not a catalogue — they describe a Skyrim interface this port does not have.

`features/` still holds all 40 inherited feature directories with their `.ini` files and assets.
They are raw material for subproject F, not an active feature set, and **nothing packages them** —
our own feature assets live under `package/Features/`, see below.
