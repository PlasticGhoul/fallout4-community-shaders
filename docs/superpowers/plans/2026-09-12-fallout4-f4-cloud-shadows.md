# Teilprojekt F4 — Cloud Shadows: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wolkenschatten aus den Wolken, die die Engine zeichnet, als Faktor auf das direkte Licht
— und davor eine Sonde, die mißt, wo die Engine ihre Wolken hinzeichnet.

**Architecture:** Zwei bleibende Bausteine: `Render::TechniqueTracker` (welche Technik gerade
eingerichtet ist, aus Slot 02 aller dreizehn Shader-Klassen) und `Render::DrawHook` (Slot 12
`DrawIndexed` des Gerätekontexts mit einem Beobachter). Darauf eine Sonde für Lauf 1, dann die
Erfassung der Wolkendeckung in eine eigene `A8_UNORM`-Cubemap — Weg A lenkt den Wolkenzug der
Engine an ihrer Cubemap um, Weg B zeichnet ihn fünfmal mit eigener Sicht nach — und ein
Schattenpass an `kBeforeComposite`, der `RT_058/059` mit `1 − Deckung · Opacity` multipliziert,
Kugelprojektion wie die Vorlage.

**Tech Stack:** C++23, MSVC, CommonLibF4 (`REX::W32`-D3D11, kein `<d3d11.h>`), `D3DCompile`,
ImGui-freies Feature, HLSL `ps_5_0`.

**Spec:** `docs/superpowers/specs/2026-09-12-fallout4-f4-cloud-shadows-design.md`

## Global Constraints

-   **C++23, MSVC, `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`.** Eine neue Warnung aus einem
    Fremdheader wird eng auf unserem Ziel unterdrückt, mit Kommentar, der den Header nennt — nie
    durch Lockern von `/WX`. Ein unbenutzter Parameter ist ein Fehler; Mutationen brauchen
    `static_cast<void>(x)`.
-   **`<d3d11.h>`, `<dxgi.h>` und `<Windows.h>` sind verboten.** D3D-Typen kommen aus `REX::W32`;
    der PCH bringt nur `REL` und `REX` mit, also `REX/W32/D3D11.h` selbst einbinden.
-   **Kein ImGui in `src/Features/`.** Ein Feature bekommt seine Oberfläche allein dadurch, daß es
    Einstellungen deklariert.
-   **Einstellungspfade sind `"Block/Key"`.** Ganze Zahlen werden als `double` gespeichert.
-   **Einstellungen werden bei jeder Verwendung frisch über `Settings::Get*` gelesen.**
-   **`Setup` schlägt nur an Dingen fehl, die wieder fehlschlügen.** Warten auf die Engine gehört in
    `Frame` oder `Draw`.
-   **`Shutdown` läuft im laufenden Spiel** und muß nach einem halb gescheiterten `Setup`
    aufrufbar sein. Der Draw-Beobachter wird ausgetragen, **bevor** Ressourcen freigegeben werden.
-   **Ein Profiler-Bereich heißt nie wie das Feature selbst.** Der Rückruf einer Phase heißt
    `<Feature>/Draw`.
-   **Werte, die sich ändern, werden periodisch protokolliert, nie einmalig.** Eine Ablehnung nennt
    die Prüfung, die riß.
-   **Kein Nicht-ASCII durch die Bash-Shell.** Weder Heredoc noch Python darin. Dateien mit
    Umlauten nur über Write/Edit; ein Python-Skript, das Dateien schreibt, nur mit
    `\uXXXX`-Escapes.
-   **Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen.** Vorher wird geprüft, daß der
    Bau geglückt ist (`(Get-Item build/FO4/Release/<Test>.exe).LastWriteTime`). Eine Mutation, die
    nicht fällt, ist ein Befund über den Test. Mutationen mit dem Edit-Werkzeug setzen **und
    zurücknehmen**, nie mit `git checkout`.
-   **Commits:** Conventional Commits, Titel höchstens 50 Zeichen, Rumpf auf 72 umbrochen, Englisch,
    Abschluß `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Dokumente unter `docs/`
    sind Deutsch. pre-commit formatiert; schlägt prettier an, dieselben Dateien erneut `add` und
    noch einmal committen.
-   **Branch:** `port/f4-cloud-shadows`, angelegt. Nicht nach `dev` mergen, bevor die Abnahme steht.
-   **Bauen:**
    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure
    ```
    Für ein einzelnes Ziel `--target <Name>`. Der volle Bau deployt nach
    `F:/SteamLibrary/steamapps/common/Fallout 4/Data`. Quellen laufen über `GLOB_RECURSE`; eine neue
    `.cpp` unter `src/` braucht keinen CMake-Eintrag.
-   **Spiel starten über `f4se_loader.exe`**, nie über Steam. Log unter
    `C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`, die letzten fünf
    Läufe als `.1.log` bis `.5.log`. Die Maschine hält rund 180 fps. Der Nutzer editiert keine
    Dateien im Spielordner; Hot-Reload stoße ich von hier aus an (`touch` auf die deployte Datei).
-   **Fragen an den Nutzer** über AskUserQuestion, empfohlene Option zuerst und markiert.

---

## Dateiübersicht

| Datei                                                                            | Verantwortung                                                                    |
| -------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| `src/Render/TechniqueTracker.{h,cpp}` (neu)                                      | Slot 02 aller dreizehn Klassen: welche Technik gerade eingerichtet ist           |
| `src/Render/DrawObserver.{h,cpp}` (neu)                                          | Reiner Teil des Draw-Hooks: Filter `(Klasse, Technik)`, Beobachter-Schnittstelle |
| `src/Render/DrawHook.{h,cpp}` (neu)                                              | COM-Patch auf `DrawIndexed`, ein Beobachter                                      |
| `src/Render/SwapChainHook.cpp`                                                   | Installiert den Draw-Hook nach dem Present-Hook                                  |
| `src/XSEPlugin.cpp`                                                              | Installiert den Tracker nach `FramePhase`                                        |
| `src/Features/SkyProbe.{h,cpp}` (neu, `diag`, wird entfernt)                     | Die Sonde für Lauf 1                                                             |
| `src/Features/CloudShadows/CloudProjection.{h,cpp}` (neu)                        | Kugelprojektion und Faktor, rein                                                 |
| `src/Render/CubeTarget.{h,cpp}` (neu)                                            | Eigene Cubemap mit sechs RTVs und einer Cube-SRV                                 |
| `src/Render/StateGuard.{h,cpp}`                                                  | Sichert zusätzlich PS-Sampler 0 und zwei PS-SRVs                                 |
| `src/Render/Camera.{h,cpp}`                                                      | Nur Weg B: Kameraposition aus `worldToCam`, Flächenmatrizen                      |
| `src/Features/CloudShadows.{h,cpp}` (neu)                                        | Das Feature: Erfassung, Schattenpass                                             |
| `src/Feature/FeatureSystem.cpp`                                                  | Registriert `CloudShadows` vor `ScreenSpaceShadows`                              |
| `package/Features/CloudShadows/Shaders/FO4/CloudShadows/CloudShadows.hlsl` (neu) | Der Schattenpass                                                                 |
| `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json`                  | Katalog, erzeugt                                                                 |
| `tests/DrawObserverTests.cpp`, `tests/CloudProjectionTests.cpp` (neu)            | Host-Tests                                                                       |
| `tests/CameraTests.cpp`, `tests/SettingsSchemaTests.cpp`                         | Erweitert                                                                        |
| `CMakeLists.txt`                                                                 | Zwei Testziele                                                                   |
| `tools/verify-package.ps1`                                                       | Sechstes Archiv                                                                  |
| `docs/fallout4-port/ROADMAP.md`, `frame-order.md`                                | Status und „Aus Teilprojekt F4 bestätigt"                                        |

---

## Task 1: `Render::TechniqueTracker`

Slot 02 (`SetupTechnique`) aller dreizehn Shader-Klassen, einmal für den Prozeß gepatcht wie
`FramePhase` seine drei, mit derselben Prüfung durch `Util::DescribeVTable`. Der Thunk schreibt
Klasse und Technik in **ein** atomares 64-Bit-Wort, damit ein Leser nie eine halbe Aktualisierung
sieht, und ruft das Original. Kein Host-Test: der Baustein ist engine-gebunden; seine Zahl steht
in der Sonde.

**Files:**

-   Create: `src/Render/TechniqueTracker.h`, `src/Render/TechniqueTracker.cpp`
-   Modify: `src/XSEPlugin.cpp:36` (nach `InstallFramePhase`)

**Interfaces:**

-   Consumes: `Shader::ShaderClasses()` (`std::span<const Shader::ShaderClass>`, Felder `className`,
    `vtable`), `Util::DescribeVTable`, `Render::VTablePatch::InstallAtTable`.
-   Produces: `Render::CurrentTechnique { std::size_t classIndex; std::uint32_t technique; bool valid; }`,
    `bool Render::InstallTechniqueTracker() noexcept`, `CurrentTechnique Render::CurrentTechniqueOf() noexcept`,
    `std::optional<std::size_t> Render::ClassIndexOf(std::string_view) noexcept`,
    `constexpr std::size_t Render::kTrackedClasses = 13`.

-   [ ] **Step 1: Header schreiben**

`src/Render/TechniqueTracker.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Render
{
	/// Which shader class most recently set up which technique, on the render
	/// thread. The engine calls SetupTechnique once per technique and then
	/// issues the draws of every geometry using it, so between two calls this
	/// names the technique the draws belong to.
	struct CurrentTechnique
	{
		std::size_t classIndex{ 0 };
		std::uint32_t technique{ 0 };
		bool valid{ false };
	};

	/// The classes Shader::ShaderClasses lists, in its order.
	inline constexpr std::size_t kTrackedClasses = 13;

	/// Patches slot 02 of every class once, for the life of the process, with
	/// the same checks FramePhase makes and for the same reason it never takes
	/// a patch back. Returns true when at least one class is watched.
	///
	/// Install after FramePhase and before the Present hook: ShaderCensus
	/// patches the same slots while it counts and restores what it found
	/// underneath, which has to be ours.
	[[nodiscard]] bool InstallTechniqueTracker() noexcept;

	/// From the render thread. Invalid until the first SetupTechnique.
	[[nodiscard]] CurrentTechnique CurrentTechniqueOf() noexcept;

	/// The index Shader::ShaderClasses gives a class, by its RTTI name.
	[[nodiscard]] std::optional<std::size_t> ClassIndexOf(std::string_view a_className) noexcept;
}
```

-   [ ] **Step 2: Implementierung schreiben**

`src/Render/TechniqueTracker.cpp`:

```cpp
#include "Render/TechniqueTracker.h"

#include "Render/VTablePatch.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <array>
#include <atomic>
#include <string>
#include <utility>

namespace Render
{
	namespace
	{
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;

		// One word: bit 63 valid, bits 32..47 the class, bits 0..31 the
		// technique. A reader on the same thread never sees half an update,
		// and a reader on another thread sees a whole one or the previous.
		constexpr std::uint64_t kValidBit = std::uint64_t{ 1 } << 63;

		std::array<VTablePatch, kTrackedClasses> g_patches{};
		std::array<void*, kTrackedClasses> g_original{};
		std::atomic<std::uint64_t> g_current{ 0 };
		bool g_installed = false;

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			g_current.store(
				kValidBit | (static_cast<std::uint64_t>(N) << 32) | a_pass,
				std::memory_order_relaxed);
			return reinterpret_cast<SetupTechniqueFn>(g_original[N])(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kTrackedClasses>{});
	}

	bool InstallTechniqueTracker() noexcept
	{
		if (g_installed) {
			return true;
		}

		const auto classes = Shader::ShaderClasses();
		std::size_t installed = 0;

		for (std::size_t i = 0; i < classes.size() && i < kTrackedClasses; ++i) {
			auto** const table = reinterpret_cast<void**>(classes[i].vtable);
			if (table == nullptr) {
				REX::WARN("technique tracker: no vtable address for {}", classes[i].className);
				continue;
			}

			// The table has to be the one the id promised, and a primary one,
			// before an entry is touched: the first call through a wrongly
			// patched entry ends the process.
			const auto identity = Util::DescribeVTable(table);
			if (!identity.has_value() ||
				identity->className != classes[i].className ||
				identity->subobjectOffset != 0) {
				REX::WARN(
					"technique tracker: the vtable id for {} names {} at +0x{:X}, leaving it alone",
					classes[i].className,
					identity.has_value() ? identity->className : std::string{ "nothing" },
					identity.has_value() ? identity->subobjectOffset : 0);
				continue;
			}

			if (!g_patches[i].InstallAtTable(
					table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[i]))) {
				REX::WARN("technique tracker: could not patch {}", classes[i].className);
				continue;
			}

			g_original[i] = g_patches[i].Original();
			++installed;
		}

		if (installed == 0) {
			REX::ERROR("technique tracker: no class could be patched");
			return false;
		}

		g_installed = true;
		REX::INFO(
			"technique tracker: watching {} of {} shader classes, patches stay for the process",
			installed,
			classes.size());
		return true;
	}

	CurrentTechnique CurrentTechniqueOf() noexcept
	{
		const auto word = g_current.load(std::memory_order_relaxed);
		CurrentTechnique current;
		current.valid = (word & kValidBit) != 0;
		current.classIndex = static_cast<std::size_t>((word >> 32) & 0xFFFF);
		current.technique = static_cast<std::uint32_t>(word & 0xFFFFFFFFu);
		return current;
	}

	std::optional<std::size_t> ClassIndexOf(std::string_view a_className) noexcept
	{
		const auto classes = Shader::ShaderClasses();
		for (std::size_t i = 0; i < classes.size(); ++i) {
			if (classes[i].className == a_className) {
				return i;
			}
		}
		return std::nullopt;
	}
}
```

-   [ ] **Step 3: Installieren**

In `src/XSEPlugin.cpp` `#include "Render/TechniqueTracker.h"` ergänzen und nach
`static_cast<void>(Render::InstallFramePhase());`:

```cpp
			// Same slots, same rule: before the Present hook, so that a census
			// restores our thunk and not the engine's entry over it.
			static_cast<void>(Render::InstallTechniqueTracker());
```

-   [ ] **Step 4: Bauen**

`cmake --build --preset FO4 --target CommunityShadersFO4`. Erwartet: grün.

-   [ ] **Step 5: Commit**

```bash
git add src/Render/TechniqueTracker.h src/Render/TechniqueTracker.cpp src/XSEPlugin.cpp
git commit -m "feat: track which technique is being set up"
```

---

## Task 2: `Render::DrawObserver` und `Render::DrawHook`

Der reine Teil zuerst, mit Test: ein Filter aus `(Klasse, Technik)`, der auch „jede Technik einer
Klasse" kann, und die Beobachter-Schnittstelle. Dann der COM-Patch auf Slot 12 des Gerätekontexts.

**Files:**

-   Create: `src/Render/DrawObserver.h`, `src/Render/DrawObserver.cpp`
-   Create: `src/Render/DrawHook.h`, `src/Render/DrawHook.cpp`
-   Create: `tests/DrawObserverTests.cpp`
-   Modify: `CMakeLists.txt` (neues Testziel nach `FrameTraceTests`, vor `CameraTests`)
-   Modify: `src/Render/SwapChainHook.cpp:92-100` (nach dem Present-Patch)

**Interfaces:**

-   Consumes: `Render::CurrentTechnique`, `Render::VTablePatch::Install`, `Render::GetContext`.
-   Produces: `struct Render::TechniqueFilter { std::size_t classIndex; std::uint32_t technique; bool Matches(const CurrentTechnique&) const noexcept; }`,
    `constexpr std::uint32_t Render::kAnyTechnique = 0xFFFFFFFFu`,
    `class Render::DrawObserver { virtual bool Wants(const CurrentTechnique&) const noexcept = 0; virtual void BeforeDraw(REX::W32::ID3D11DeviceContext&) noexcept = 0; virtual void AfterDraw(REX::W32::ID3D11DeviceContext&, std::uint32_t a_indexCount, std::uint32_t a_startIndex, std::int32_t a_baseVertex) noexcept = 0; }`,
    `bool Render::InstallDrawHook() noexcept`, `void Render::SetDrawObserver(DrawObserver*) noexcept`,
    `std::uint64_t Render::DrawHookCalls() noexcept`.

-   [ ] **Step 1: Test schreiben**

`tests/DrawObserverTests.cpp`:

```cpp
#include "Render/DrawObserver.h"

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

	Render::CurrentTechnique Current(std::size_t a_class, std::uint32_t a_technique, bool a_valid = true)
	{
		Render::CurrentTechnique current;
		current.classIndex = a_class;
		current.technique = a_technique;
		current.valid = a_valid;
		return current;
	}
}

int main()
{
	{
		// The filter the cloud capture uses: one class, one technique.
		const Render::TechniqueFilter clouds{ 7, 0x0005 };
		Check(clouds.Matches(Current(7, 0x0005)), "the exact pair matches");
		Check(!clouds.Matches(Current(7, 0x0004)), "another technique of the class does not");
		Check(!clouds.Matches(Current(6, 0x0005)), "the same technique id of another class does not");
		Check(!clouds.Matches(Current(7, 0x0005, false)), "nothing matches before the first SetupTechnique");
	}

	{
		// The filter the probe uses: every technique of one class.
		const Render::TechniqueFilter sky{ 7, Render::kAnyTechnique };
		Check(sky.Matches(Current(7, 0x0001)), "any technique of the class matches");
		Check(sky.Matches(Current(7, 0x0005)), "and any other");
		Check(!sky.Matches(Current(8, 0x0005)), "but not another class");
		Check(!sky.Matches(Current(7, 0x0001, false)), "and not an invalid state");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 2: Testziel anlegen**

In `CMakeLists.txt` nach dem Block `FrameTraceTests` (nach `add_test(NAME FrameTrace …)`):

```cmake
    add_executable(
        DrawObserverTests
        "${CMAKE_SOURCE_DIR}/tests/DrawObserverTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/DrawObserver.cpp"
    )

    target_include_directories(
        DrawObserverTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(DrawObserverTests PRIVATE cxx_std_23)
    target_precompile_headers(
        DrawObserverTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(DrawObserverTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            DrawObserverTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME DrawObserver COMMAND DrawObserverTests)
```

-   [ ] **Step 3: Bauen, Fehlschlag erwarten**

`cmake --build --preset FO4 --target DrawObserverTests` — erwartet: Fehler, Header fehlt.

-   [ ] **Step 4: Header schreiben**

`src/Render/DrawObserver.h`:

```cpp
#pragma once

#include "Render/TechniqueTracker.h"

#include <cstddef>
#include <cstdint>

namespace REX::W32
{
	struct ID3D11DeviceContext;
}

namespace Render
{
	/// Matches every technique of the class.
	inline constexpr std::uint32_t kAnyTechnique = 0xFFFFFFFFu;

	/// Which draws an observer wants: those issued while this class has this
	/// technique set up. Pure, so that it can be tested without a device.
	struct TechniqueFilter
	{
		std::size_t classIndex{ 0 };
		std::uint32_t technique{ kAnyTechnique };

		[[nodiscard]] bool Matches(const CurrentTechnique& a_current) const noexcept;
	};

	/// Somebody who wants to surround the engine's draw calls of one
	/// technique: swap targets and state before, put them back after, draw
	/// again in between. Called on the render thread from inside DrawIndexed.
	///
	/// A draw the observer does not want passes straight through, with one
	/// atomic load and one comparison spent on it.
	class DrawObserver
	{
	public:
		virtual ~DrawObserver() = default;

		[[nodiscard]] virtual bool Wants(const CurrentTechnique& a_current) const noexcept = 0;
		virtual void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept = 0;
		virtual void AfterDraw(
			REX::W32::ID3D11DeviceContext& a_context,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex) noexcept = 0;
	};
}
```

-   [ ] **Step 5: Implementierung schreiben**

`src/Render/DrawObserver.cpp`:

```cpp
#include "Render/DrawObserver.h"

namespace Render
{
	bool TechniqueFilter::Matches(const CurrentTechnique& a_current) const noexcept
	{
		return a_current.valid &&
		       a_current.classIndex == classIndex &&
		       (technique == kAnyTechnique || a_current.technique == technique);
	}
}
```

-   [ ] **Step 6: Bauen und Test laufen lassen**

`cmake --build --preset FO4 --target DrawObserverTests`, dann `build/FO4/Release/DrawObserverTests.exe`.
Erwartet: acht `ok`.

-   [ ] **Step 7: Mutation**

In `Matches` den Klassenvergleich weglassen. Bauen, Frische prüfen, laufen lassen. Erwartet fällt:
„the same technique id of another class does not" und „but not another class". Zurücknehmen.
Zweite Mutation: `a_current.valid &&` weglassen. Erwartet fällt: „nothing matches before the first
SetupTechnique" und „and not an invalid state". Zurücknehmen, grün.

-   [ ] **Step 8: Commit des reinen Teils**

```bash
git add src/Render/DrawObserver.h src/Render/DrawObserver.cpp tests/DrawObserverTests.cpp CMakeLists.txt
git commit -m "feat: a filter for the draws of one technique"
```

-   [ ] **Step 9: Draw-Hook Header**

`src/Render/DrawHook.h`:

```cpp
#pragma once

#include <cstdint>

namespace Render
{
	class DrawObserver;

	/// Replaces slot 12 - DrawIndexed - in the vtable of the immediate device
	/// context, once, for the life of the process, the way the Present hook
	/// takes IDXGISwapChain::Present. Every indexed draw of the game then
	/// passes through a thunk that costs one atomic load and one comparison
	/// when nobody is watching.
	///
	/// Installed from InstallSwapChainHook, after the context has been
	/// verified against the swap chain's device. Returns true when in place.
	[[nodiscard]] bool InstallDrawHook() noexcept;

	/// Exactly one observer. A feature sets itself from Setup and clears
	/// itself from Shutdown before it releases anything; both run on the
	/// render thread, as does the thunk, so nothing races. A second feature
	/// that needs one turns this into a list - not before.
	void SetDrawObserver(DrawObserver* a_observer) noexcept;

	/// Indexed draws seen since the hook went in. The probe reads it once a
	/// second: a number that does not move while the game renders means the
	/// engine draws through a context this hook does not see.
	[[nodiscard]] std::uint64_t DrawHookCalls() noexcept;
}
```

-   [ ] **Step 10: Draw-Hook Implementierung**

`src/Render/DrawHook.cpp`:

```cpp
#include "Render/DrawHook.h"

#include "Render/DrawObserver.h"
#include "Render/Renderer.h"
#include "Render/TechniqueTracker.h"
#include "Render/VTablePatch.h"

#include <REX/W32/D3D11.h>

#include <atomic>

namespace Render
{
	namespace
	{
		// ID3D11DeviceContext: IUnknown 0-2, ID3D11DeviceChild 3-6,
		// VSSetConstantBuffers 7, PSSetShaderResources 8, PSSetShader 9,
		// PSSetSamplers 10, VSSetShader 11, DrawIndexed 12.
		constexpr std::size_t kDrawIndexedSlot = 12;

		using DrawIndexedFn = void (*)(REX::W32::ID3D11DeviceContext*, std::uint32_t, std::uint32_t, std::int32_t);

		VTablePatch g_patch;
		DrawIndexedFn g_original = nullptr;
		std::atomic<DrawObserver*> g_observer{ nullptr };
		std::atomic<std::uint64_t> g_calls{ 0 };
		bool g_installed = false;

		void DrawIndexed(
			REX::W32::ID3D11DeviceContext* a_self,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex)
		{
			g_calls.fetch_add(1, std::memory_order_relaxed);

			auto* const observer = g_observer.load(std::memory_order_acquire);
			if (observer == nullptr || !observer->Wants(CurrentTechniqueOf())) {
				g_original(a_self, a_indexCount, a_startIndex, a_baseVertex);
				return;
			}

			observer->BeforeDraw(*a_self);
			g_original(a_self, a_indexCount, a_startIndex, a_baseVertex);
			observer->AfterDraw(*a_self, a_indexCount, a_startIndex, a_baseVertex);
		}
	}

	bool InstallDrawHook() noexcept
	{
		if (g_installed) {
			return true;
		}

		auto* const context = GetContext();
		if (context == nullptr) {
			REX::ERROR("draw hook: no device context");
			return false;
		}

		if (!g_patch.Install(context, kDrawIndexedSlot, reinterpret_cast<void*>(&DrawIndexed))) {
			REX::ERROR("draw hook: could not patch the device context vtable");
			return false;
		}

		g_original = reinterpret_cast<DrawIndexedFn>(g_patch.Original());
		g_installed = true;
		REX::INFO("draw hook: DrawIndexed patched, chaining to {}", g_patch.Original());
		return true;
	}

	void SetDrawObserver(DrawObserver* a_observer) noexcept
	{
		g_observer.store(a_observer, std::memory_order_release);
	}

	std::uint64_t DrawHookCalls() noexcept
	{
		return g_calls.load(std::memory_order_relaxed);
	}
}
```

-   [ ] **Step 11: Installieren**

In `src/Render/SwapChainHook.cpp` `#include "Render/DrawHook.h"` ergänzen und in
`InstallSwapChainHook` nach `g_installed = true;` und der Zeile `REX::INFO("Present hooked, …")`:

```cpp
		// The context was verified against this swap chain's device by
		// ValidateAndLog above, so this is the moment its vtable is known.
		static_cast<void>(InstallDrawHook());
```

-   [ ] **Step 12: Bauen, Spiel-Log prüfen**

Voller Bau. Der Nutzer startet **noch nicht**; die Zeile `draw hook: DrawIndexed patched` wird in
Lauf 1 (Task 4) geprüft.

-   [ ] **Step 13: Commit**

```bash
git add src/Render/DrawHook.h src/Render/DrawHook.cpp src/Render/SwapChainHook.cpp
git commit -m "feat: a hook around the engine's indexed draws"
```

---

## Task 3: Die Sonde (`diag`)

Ein Feature `SkyProbe`, vorbelegt **an**, das in Task 5 mit `git revert` verschwindet. Es ist der
einzige Beobachter des Draw-Hooks in Lauf 1 und schreibt je Sekunde, was Spec Abschnitt 4 verlangt.

**Files:**

-   Create: `src/Features/SkyProbe.h`, `src/Features/SkyProbe.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (Registrierung nach `FrameTrace`)

**Interfaces:**

-   Consumes: `Render::DrawObserver`, `Render::TechniqueFilter`, `Render::SetDrawObserver`,
    `Render::DrawHookCalls`, `Render::ClassIndexOf`, `Render::GetViewTargetName`,
    `Render::FrameCount`, `Render::GetDevice`, `Render::GetContext`, `RE::Main::WorldRootCamera`,
    `RE::BSGraphics::GetRendererData()->cubeMapRenderTargets[0]`.

-   [ ] **Step 1: Header schreiben**

`src/Features/SkyProbe.h`:

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Render/DrawObserver.h"

#include <REX/W32/D3D11.h>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Features
{
	/// diag: where does the engine draw its sky, with what blend state, and
	/// where in the vertex constants is the view projection? Answers the
	/// three questions the F4 spec asks before the capture is built. Goes
	/// away with the run.
	class SkyProbe final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "SkyProbe"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept override;
		void AfterDraw(
			REX::W32::ID3D11DeviceContext& a_context,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex) noexcept override;

	private:
		struct Row
		{
			std::uint64_t second{ 0 };
			std::uint64_t total{ 0 };
		};

		struct PendingBuffer
		{
			std::uint32_t slot{ 0 };
			REX::W32::ID3D11Buffer* staging{ nullptr };
			std::uint32_t bytes{ 0 };
		};

		void LogBlendState(REX::W32::ID3D11DeviceContext& a_context);
		void CopyVertexConstants(REX::W32::ID3D11DeviceContext& a_context);
		void ReadVertexConstants();
		void LogFormatSupport();

		Render::TechniqueFilter _sky{};
		std::map<std::string, Row> _rows;  // "0x0005 -> FO4_RT_004"
		std::uint64_t _frames{ 0 };
		std::uint64_t _lastFrameSeen{ 0 };
		std::uint64_t _lastCallsSeen{ 0 };
		std::uint64_t _cloudFrames{ 0 };
		std::uint64_t _blendLoggedFrame{ 0 };
		std::uint64_t _dumpFrame{ 0 };
		bool _dumpRequested{ false };
		std::vector<PendingBuffer> _pending;
		float _worldToCamAtDump[16]{};
	};
}
```

-   [ ] **Step 2: Implementierung schreiben**

`src/Features/SkyProbe.cpp`:

```cpp
#include "Features/SkyProbe.h"

#include "Render/DebugName.h"
#include "Render/DrawHook.h"
#include "Render/Renderer.h"
#include "Render/SwapChainHook.h"
#include "Render/TechniqueTracker.h"
#include "Settings/Settings.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>

#include <REX/W32/DXGI.h>

#include <cmath>
#include <cstring>
#include <format>

namespace Features
{
	namespace
	{
		constexpr std::uint32_t kCloudsTechnique = 0x0005;
		constexpr std::uint64_t kSecond = 180;
		constexpr std::uint32_t kConstantSlots = 14;

		const char* BlendName(REX::W32::D3D11_BLEND a_blend)
		{
			switch (a_blend) {
			case REX::W32::D3D11_BLEND_ZERO:
				return "ZERO";
			case REX::W32::D3D11_BLEND_ONE:
				return "ONE";
			case REX::W32::D3D11_BLEND_SRC_COLOR:
				return "SRC_COLOR";
			case REX::W32::D3D11_BLEND_INV_SRC_COLOR:
				return "INV_SRC_COLOR";
			case REX::W32::D3D11_BLEND_SRC_ALPHA:
				return "SRC_ALPHA";
			case REX::W32::D3D11_BLEND_INV_SRC_ALPHA:
				return "INV_SRC_ALPHA";
			case REX::W32::D3D11_BLEND_DEST_ALPHA:
				return "DEST_ALPHA";
			case REX::W32::D3D11_BLEND_INV_DEST_ALPHA:
				return "INV_DEST_ALPHA";
			case REX::W32::D3D11_BLEND_DEST_COLOR:
				return "DEST_COLOR";
			case REX::W32::D3D11_BLEND_INV_DEST_COLOR:
				return "INV_DEST_COLOR";
			default:
				return "other";
			}
		}

		/// Does the sixteen float window at a_data read as a_matrix, by rows
		/// or by columns, within a thousandth of its largest entry?
		const char* MatchMatrix(const float* a_data, const float (&a_matrix)[16])
		{
			float largest = 0.0f;
			for (const auto value : a_matrix) {
				largest = std::max(largest, std::abs(value));
			}
			const auto tolerance = std::max(largest * 1.0e-3f, 1.0e-3f);

			bool rows = true;
			bool columns = true;
			for (int r = 0; r < 4; ++r) {
				for (int c = 0; c < 4; ++c) {
					const auto expected = a_matrix[r * 4 + c];
					if (std::abs(a_data[r * 4 + c] - expected) > tolerance) {
						rows = false;
					}
					if (std::abs(a_data[c * 4 + r] - expected) > tolerance) {
						columns = false;
					}
				}
			}
			return rows ? "rows" : columns ? "columns" : nullptr;
		}
	}

	void SkyProbe::Declare()
	{
		Settings::DeclareFeature("SkyProbe", true)
			.Label("feature.sky_probe.name", "Sky Probe")
			.Help("feature.sky_probe.help", "Diagnostic for F4. Goes away after the run.");
	}

	bool SkyProbe::Setup()
	{
		const auto index = Render::ClassIndexOf("BSSkyShader");
		if (!index.has_value()) {
			REX::ERROR("SkyProbe: BSSkyShader is not among the shader classes");
			return false;
		}
		_sky = Render::TechniqueFilter{ *index, Render::kAnyTechnique };

		LogFormatSupport();

		_rows.clear();
		_frames = 0;
		_lastCallsSeen = Render::DrawHookCalls();
		Render::SetDrawObserver(this);
		REX::INFO("SkyProbe: watching every BSSkyShader draw");
		return true;
	}

	void SkyProbe::Shutdown()
	{
		Render::SetDrawObserver(nullptr);
		for (auto& pending : _pending) {
			if (pending.staging != nullptr) {
				pending.staging->Release();
			}
		}
		_pending.clear();
	}

	bool SkyProbe::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return _sky.Matches(a_current);
	}

	void SkyProbe::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		const auto current = Render::CurrentTechniqueOf();
		const auto frame = Render::FrameCount();

		REX::W32::ID3D11RenderTargetView* target = nullptr;
		a_context.OMGetRenderTargets(1, std::addressof(target), nullptr);
		std::string name = target != nullptr ?
		                       Render::GetViewTargetName(reinterpret_cast<REX::W32::ID3D11View*>(target)) :
		                       std::string{ "<none>" };
		if (name.empty()) {
			name = std::format("{}", static_cast<void*>(target));
		}

		// Which face of the engine's cubemap, if it is one.
		if (const auto* const data = RE::BSGraphics::GetRendererData(); data != nullptr && target != nullptr) {
			for (std::uint32_t face = 0; face < 6; ++face) {
				if (data->cubeMapRenderTargets[0].rtView[face] == target) {
					name += std::format(" face {}", face);
				}
			}
		}
		if (target != nullptr) {
			target->Release();
		}

		auto& row = _rows[std::format("0x{:04X} -> {}", current.technique, name)];
		++row.second;
		++row.total;

		if (current.technique == kCloudsTechnique && frame != _lastFrameSeen) {
			_lastFrameSeen = frame;
			++_cloudFrames;

			if (frame - _blendLoggedFrame >= kSecond) {
				_blendLoggedFrame = frame;
				LogBlendState(a_context);
			}

			// The main view only: that is the draw Weg B would repeat.
			if (_dumpRequested && name.starts_with("FO4_RT_004") && _pending.empty()) {
				_dumpRequested = false;
				CopyVertexConstants(a_context);
			}
		}
	}

	void SkyProbe::AfterDraw(
		REX::W32::ID3D11DeviceContext&, std::uint32_t, std::uint32_t, std::int32_t) noexcept
	{}

	void SkyProbe::LogBlendState(REX::W32::ID3D11DeviceContext& a_context)
	{
		REX::W32::ID3D11BlendState* state = nullptr;
		float factor[4]{};
		std::uint32_t mask = 0;
		a_context.OMGetBlendState(std::addressof(state), factor, std::addressof(mask));
		if (state == nullptr) {
			REX::INFO("SkyProbe: clouds draw with the default blend state (no blending)");
			return;
		}

		REX::W32::D3D11_BLEND_DESC desc{};
		state->GetDesc(std::addressof(desc));
		state->Release();

		const auto& target = desc.renderTarget[0];
		REX::INFO(
			"SkyProbe: clouds blend enable {} colour {} / {} op {} alpha {} / {} write mask 0x{:X}",
			target.blendEnable ? "yes" : "no",
			BlendName(target.srcBlend), BlendName(target.destBlend),
			static_cast<int>(target.blendOp),
			BlendName(target.srcBlendAlpha), BlendName(target.destBlendAlpha),
			target.renderTargetWriteMask);
	}

	void SkyProbe::CopyVertexConstants(REX::W32::ID3D11DeviceContext& a_context)
	{
		auto* const device = Render::GetDevice();
		auto* const world = RE::Main::WorldRootCamera();
		if (device == nullptr || world == nullptr) {
			return;
		}
		std::memcpy(_worldToCamAtDump, world->worldToCam, sizeof(_worldToCamAtDump));
		_dumpFrame = Render::FrameCount();

		REX::W32::ID3D11Buffer* buffers[kConstantSlots]{};
		a_context.VSGetConstantBuffers(0, kConstantSlots, buffers);

		for (std::uint32_t slot = 0; slot < kConstantSlots; ++slot) {
			auto* const source = buffers[slot];
			if (source == nullptr) {
				continue;
			}

			REX::W32::D3D11_BUFFER_DESC desc{};
			source->GetDesc(std::addressof(desc));

			REX::W32::D3D11_BUFFER_DESC stagingDesc{};
			stagingDesc.byteWidth = desc.byteWidth;
			stagingDesc.usage = REX::W32::D3D11_USAGE_STAGING;
			stagingDesc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;

			REX::W32::ID3D11Buffer* staging = nullptr;
			if (device->CreateBuffer(std::addressof(stagingDesc), nullptr, std::addressof(staging)) >= 0) {
				a_context.CopyResource(staging, source);
				_pending.push_back({ slot, staging, desc.byteWidth });
			}
			source->Release();
		}

		REX::INFO("SkyProbe: copied {} vertex constant buffer(s) at the clouds draw, frame {}", _pending.size(), _dumpFrame);
	}

	void SkyProbe::ReadVertexConstants()
	{
		auto* const context = Render::GetContext();
		if (context == nullptr || _pending.empty()) {
			return;
		}

		// Read a frame later: the copy was queued behind the engine's own
		// work, and a wait here would stall the render thread.
		if (Render::FrameCount() <= _dumpFrame + 1) {
			return;
		}

		for (auto& pending : _pending) {
			REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
			if (context->Map(pending.staging, 0, REX::W32::D3D11_MAP_READ, REX::W32::D3D11_MAP_FLAG_DO_NOT_WAIT, std::addressof(mapped)) < 0) {
				REX::INFO("SkyProbe: vs cb slot {} not readable yet", pending.slot);
				continue;
			}

			const auto* const floats = static_cast<const float*>(mapped.data);
			const auto count = pending.bytes / sizeof(float);
			const char* found = nullptr;
			std::size_t foundAt = 0;
			for (std::size_t offset = 0; offset + 16 <= count; offset += 4) {
				if (const auto* const how = MatchMatrix(floats + offset, _worldToCamAtDump); how != nullptr) {
					found = how;
					foundAt = offset * sizeof(float);
					break;
				}
			}

			if (found != nullptr) {
				REX::INFO("SkyProbe: worldToCam FOUND in vs cb slot {} at byte {} stored by {}", pending.slot, foundAt, found);
			} else {
				std::string head;
				for (std::size_t i = 0; i < std::min<std::size_t>(count, 64); ++i) {
					head += std::format("{}{:.4g}", i == 0 ? "" : " ", floats[i]);
				}
				REX::INFO("SkyProbe: vs cb slot {} ({} bytes) no match, head: {}", pending.slot, pending.bytes, head);
			}

			context->Unmap(pending.staging, 0);
			pending.staging->Release();
			pending.staging = nullptr;
		}

		std::erase_if(_pending, [](const PendingBuffer& a_pending) { return a_pending.staging == nullptr; });
	}

	void SkyProbe::LogFormatSupport()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return;
		}

		std::uint32_t support = 0;
		const auto hr = device->CheckFormatSupport(REX::W32::DXGI_FORMAT_A8_UNORM, std::addressof(support));
		REX::INFO(
			"SkyProbe: A8_UNORM support 0x{:X} (hr {}) render target {} blendable {}",
			support, hr,
			(support & REX::W32::D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0 ? "yes" : "no",
			(support & REX::W32::D3D11_FORMAT_SUPPORT_BLENDABLE) != 0 ? "yes" : "no");

		REX::W32::D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
		if (device->CheckFeatureSupport(REX::W32::D3D11_FEATURE_D3D11_OPTIONS, std::addressof(options), sizeof(options)) >= 0) {
			REX::INFO(
				"SkyProbe: constant buffer partial update {}, offsetting {}",
				options.constantBufferPartialUpdate ? "yes" : "no",
				options.constantBufferOffsetting ? "yes" : "no");
		}
	}

	void SkyProbe::Frame()
	{
		++_frames;
		ReadVertexConstants();

		if (_frames % kSecond != 0) {
			return;
		}

		const auto calls = Render::DrawHookCalls();
		REX::INFO(
			"SkyProbe: {} indexed draws in {} frames ({} per frame), clouds drawn in {} frame(s)",
			calls - _lastCallsSeen, kSecond, (calls - _lastCallsSeen) / kSecond, _cloudFrames);
		_lastCallsSeen = calls;
		_cloudFrames = 0;

		for (auto& [key, row] : _rows) {
			REX::INFO("  sky {}: {} this second, {} since start", key, row.second, row.total);
			row.second = 0;
		}

		_dumpRequested = true;
	}
}
```

Prüfen vor dem Bau: heißen die Felder von `REX::W32::D3D11_BUFFER_DESC` `byteWidth`, `usage`,
`cpuAccessFlags`, die von `D3D11_FEATURE_DATA_D3D11_OPTIONS` `constantBufferPartialUpdate` und
`constantBufferOffsetting`, `D3D11_MAPPED_SUBRESOURCE::data`? Mit `grep -n -A8 "struct
D3D11_BUFFER_DESC$" extern/CommonLibF4/lib/commonlib-shared/include/REX/W32/D3D11.h` nachsehen
und die Namen anpassen — nicht raten.

-   [ ] **Step 3: Registrieren**

In `src/Feature/FeatureSystem.cpp` `#include "Features/SkyProbe.h"` und nach `FrameTrace`:

```cpp
			TheRegistry().Register(std::make_unique<SkyProbe>());
```

-   [ ] **Step 4: Katalog, Bau**

`python tools/extract-i18n.py --write`, voller Bau, `ctest`, `verify-plugin.ps1`. Erwartet: grün,
deployt.

-   [ ] **Step 5: Commit**

```bash
git add src/Features/SkyProbe.h src/Features/SkyProbe.cpp src/Feature/FeatureSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "diag: probe where the engine draws its sky"
```

---

## Task 4: Lauf 1 — Sonde

Kein Code vor dem Lauf.

-   [ ] **Step 1: Anweisung an den Nutzer**

Sanctuary draußen, bewölkter Himmel wenn möglich. Einmal **F8** mit Wolken im Bild. Dann eine
Minute spielen, Himmel und Fluß ansehen, einmal um die eigene Achse drehen. Fertig.

-   [ ] **Step 2: Log auswerten**

```bash
grep -n "draw hook\|technique tracker\|SkyProbe: A8\|partial update" CommunityShadersFO4.log
grep "SkyProbe: .* indexed draws" CommunityShadersFO4.log | tail -5
grep "  sky 0x" CommunityShadersFO4.log | sed 's/.*sky //' | sort | uniq -c | sort -rn
grep "SkyProbe: clouds blend" CommunityShadersFO4.log | sort | uniq -c
grep "worldToCam FOUND\|no match" CommunityShadersFO4.log | head
```

Entscheidung:

| Befund                                                                             | Weg                                                                                                                         |
| ---------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| `0x0005 -> FO4_CUBE_000 face n` für alle sechs Flächen, je Sekunde jede Fläche ≥ 1 | **A** (Task 10 mit Weg-A-Beobachter)                                                                                        |
| Cubemap gezeichnet, aber seltener als alle zwei Sekunden je Fläche                 | A nicht gangbar, weiter wie „gar nicht"                                                                                     |
| Keine Cubemap-Zeile                                                                | **A′** (Task 11), dann zweiter Sondenlauf                                                                                   |
| A′ ändert nichts, `worldToCam FOUND`                                               | **B** (Task 12)                                                                                                             |
| A′ ändert nichts, kein Fund                                                        | F4 teilen: Erfassung als Forschung                                                                                          |
| Zahl der indexed draws bleibt 0 oder Wolkenzüge fehlen ganz                        | Engine zeichnet über einen anderen Kontext (deferred); Hook auf `DrawIndexedInstanced` (Slot 20) versuchen, sonst Forschung |
| Blend nicht `SRC_ALPHA / INV_SRC_ALPHA`                                            | Spec 10, erste Gabelung: eigener Pixelshader statt Blend-Trick, setzt B voraus                                              |

Befunde in einer Notiz für die Roadmap festhalten (Task 15) und die Zahlen des Draw-Hooks (Aufrufe
je Frame) notieren.

---

## Task 5: Sonde entfernen

-   [ ] **Step 1: Revert**

```bash
git revert --no-edit <hash von "diag: probe where the engine draws its sky">
python tools/extract-i18n.py --write
git add package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit --amend --no-edit
```

Prüfen: `git status` sauber, `ls src/Features/SkyProbe*` leer, `grep SkyProbe src -r` leer. Bauen.

---

## Task 6: `CloudProjection`, rein

Die Kugelprojektion der Vorlage in der Form ohne Auslöschung: mit `u = relativ + (0,0,R)` und
`s` zur Sonne ist `t = −(u·s) + sqrt((u·s)² − (|u|² − (R+H)²))`, wobei
`|u|² − (R+H)² = |relativ|² + 2R(relativ.z − H) − H²` ohne `R²` gerechnet wird. Ergebnis
`v = relativ + s·t`, die Richtung in die Cubemap. Dazu der Faktor.

**Files:**

-   Create: `src/Features/CloudShadows/CloudProjection.h`, `src/Features/CloudShadows/CloudProjection.cpp`
-   Create: `tests/CloudProjectionTests.cpp`
-   Modify: `CMakeLists.txt` (Testziel nach `DrawObserverTests`)
-   Modify: Spec 9.1, die Zeile „`H = 0` liefert den Punkt selbst" → „`H = 0` legt die Schale auf
    Kamerahöhe: die Richtung landet bei `z = 0`"

**Interfaces:**

-   Produces: `namespace Features::Clouds { struct Vec3 { float x, y, z; }; constexpr float kUnitsPerMetre = 70.0f; constexpr float kEarthRadiusMetres = 6371000.0f; Vec3 CloudSampleDirection(Vec3 a_relative, Vec3 a_toSun, float a_planetRadius, float a_cloudHeight) noexcept; float ShadowFactor(float a_coverage, float a_opacity) noexcept; }`

-   [ ] **Step 1: Test schreiben**

`tests/CloudProjectionTests.cpp`:

```cpp
#include "Features/CloudShadows/CloudProjection.h"

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

	bool Near(float a_value, float a_expected, float a_tolerance)
	{
		return std::abs(a_value - a_expected) <= a_tolerance;
	}

	constexpr float kRadius = Features::Clouds::kEarthRadiusMetres * Features::Clouds::kUnitsPerMetre;
}

int main()
{
	using namespace Features::Clouds;

	{
		// Sun in the zenith: the shadow of a cloud falls straight down, so the
		// sample direction stands right above the point, at cloud height.
		const auto v = CloudSampleDirection({ 100.0f, 200.0f, -50.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 140000.0f);
		Check(Near(v.x, 100.0f, 0.5f), "zenith: x stays");
		Check(Near(v.y, 200.0f, 0.5f), "zenith: y stays");
		Check(Near(v.z, 140000.0f, 1.0f), "zenith: z is the cloud height");
	}

	{
		// Sun 45 degrees up along +x, a point a thousand units below the
		// camera, clouds a thousand units above it: the sample point lies two
		// thousand units along x at cloud height. Small numbers so the
		// planet's curvature stays below the tolerance.
		const float s = 0.70710678f;
		const auto v = CloudSampleDirection({ 0.0f, 0.0f, -1000.0f }, { s, 0.0f, s }, kRadius, 1000.0f);
		Check(Near(v.x, 2000.0f, 0.5f), "45 degrees: offset equals the height climbed");
		Check(Near(v.y, 0.0f, 0.01f), "45 degrees: no sideways drift");
		Check(Near(v.z, 1000.0f, 0.5f), "45 degrees: at cloud height");
	}

	{
		// Behind the camera is no different from in front.
		const auto ahead = CloudSampleDirection({ 5000.0f, 0.0f, -1000.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 2000.0f);
		const auto behind = CloudSampleDirection({ -5000.0f, 0.0f, -1000.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 2000.0f);
		Check(Near(ahead.x, 5000.0f, 0.5f) && Near(behind.x, -5000.0f, 0.5f), "sign of x survives");
		Check(Near(ahead.z, behind.z, 0.01f), "same height either way");
	}

	{
		// Cloud height zero puts the shell at the camera's height.
		const auto v = CloudSampleDirection({ 300.0f, 0.0f, -700.0f }, { 0.0f, 0.0f, 1.0f }, kRadius, 0.0f);
		Check(Near(v.z, 0.0f, 0.5f), "height zero: the shell is at the camera");
	}

	{
		Check(Near(ShadowFactor(1.0f, 0.5f), 0.5f, 1e-6f), "full cover at opacity 0.5 halves the light");
		Check(Near(ShadowFactor(0.5f, 4.0f), 0.0f, 1e-6f), "opacity 4 clamps at black");
		Check(Near(ShadowFactor(0.0f, 4.0f), 1.0f, 1e-6f), "no cover leaves the light alone");
		Check(Near(ShadowFactor(0.25f, 1.0f), 0.75f, 1e-6f), "a quarter cover at opacity 1 takes a quarter");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 2: Testziel anlegen**

In `CMakeLists.txt` nach `add_test(NAME DrawObserver …)`:

```cmake
    add_executable(
        CloudProjectionTests
        "${CMAKE_SOURCE_DIR}/tests/CloudProjectionTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Features/CloudShadows/CloudProjection.cpp"
    )

    target_include_directories(
        CloudProjectionTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(CloudProjectionTests PRIVATE cxx_std_23)
    target_precompile_headers(
        CloudProjectionTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(CloudProjectionTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            CloudProjectionTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME CloudProjection COMMAND CloudProjectionTests)
```

-   [ ] **Step 3: Bauen, Fehlschlag erwarten**

`cmake --build --preset FO4 --target CloudProjectionTests` — erwartet: Fehler, Header fehlt.

-   [ ] **Step 4: Header und Implementierung**

`src/Features/CloudShadows/CloudProjection.h`:

```cpp
#pragma once

namespace Features::Clouds
{
	struct Vec3
	{
		float x;
		float y;
		float z;
	};

	/// Fallout 4's unit: seventy to the metre, the figure the Skyrim template
	/// carries as GAME_UNIT_TO_M.
	inline constexpr float kUnitsPerMetre = 70.0f;
	inline constexpr float kEarthRadiusMetres = 6371000.0f;

	/// Where the sun's ray through a_relative (camera relative, z up) meets a
	/// spherical cloud shell a_cloudHeight above the planet's surface, itself
	/// a sphere of a_planetRadius under the camera. The result, camera
	/// relative, is the direction the coverage cubemap is sampled in.
	///
	/// The template's formula, rearranged so that the planet radius squared
	/// never appears: at four hundred million units it would swallow every
	/// other term in a float.
	[[nodiscard]] Vec3 CloudSampleDirection(
		Vec3 a_relative,
		Vec3 a_toSun,
		float a_planetRadius,
		float a_cloudHeight) noexcept;

	/// 1 - coverage * opacity, held in [0, 1].
	[[nodiscard]] float ShadowFactor(float a_coverage, float a_opacity) noexcept;
}
```

`src/Features/CloudShadows/CloudProjection.cpp`:

```cpp
#include "Features/CloudShadows/CloudProjection.h"

#include <algorithm>
#include <cmath>

namespace Features::Clouds
{
	Vec3 CloudSampleDirection(Vec3 a_relative, Vec3 a_toSun, float a_planetRadius, float a_cloudHeight) noexcept
	{
		const auto R = a_planetRadius;
		const auto H = a_cloudHeight;

		// u = relative + (0, 0, R) is the point seen from the planet's centre.
		// The ray u + s t meets the shell of radius R + H where
		//   t^2 + 2 (u.s) t + (|u|^2 - (R+H)^2) = 0.
		// |u|^2 - (R+H)^2 expands to |rel|^2 + 2 R (rel.z - H) - H^2; the R^2
		// terms cancel on paper, so they are left out of the arithmetic.
		const auto dotUS = a_relative.x * a_toSun.x + a_relative.y * a_toSun.y + (a_relative.z + R) * a_toSun.z;
		const auto lengthSquared =
			a_relative.x * a_relative.x + a_relative.y * a_relative.y + a_relative.z * a_relative.z;
		const auto c = lengthSquared + 2.0f * R * (a_relative.z - H) - H * H;

		const auto discriminant = std::max(dotUS * dotUS - c, 0.0f);
		const auto t = -dotUS + std::sqrt(discriminant);

		return { a_relative.x + a_toSun.x * t, a_relative.y + a_toSun.y * t, a_relative.z + a_toSun.z * t };
	}

	float ShadowFactor(float a_coverage, float a_opacity) noexcept
	{
		return std::clamp(1.0f - a_coverage * a_opacity, 0.0f, 1.0f);
	}
}
```

-   [ ] **Step 5: Bauen und Test laufen lassen**

Erwartet: alle `ok`. Fällt „zenith: z is the cloud height" um mehr als eine Einheit, liegt es an
der Float-Rechnung von `dotUS * dotUS − c` (Größenordnung 10¹⁷ gegen 10¹⁷): dann `dotUS` und `c`
in `double` rechnen und das Ergebnis nach `float` wandeln — im Shader bleibt es `float`, und die
Debug-Ansicht `direction` zeigt, ob das dort reicht.

-   [ ] **Step 6: Mutation**

`2.0f * R * (a_relative.z - H)` → `2.0f * R * a_relative.z`. Erwartet fällt: „zenith: z is the
cloud height", „45 degrees: …" (zwei), „height zero" bleibt (H = 0). Zurücknehmen. Zweite
Mutation: `std::clamp` weglassen. Erwartet fällt: „opacity 4 clamps at black". Zurücknehmen, grün.

-   [ ] **Step 7: Spec korrigieren und Commit**

```bash
git add src/Features/CloudShadows/CloudProjection.h src/Features/CloudShadows/CloudProjection.cpp tests/CloudProjectionTests.cpp CMakeLists.txt docs/superpowers/specs/2026-09-12-fallout4-f4-cloud-shadows-design.md
git commit -m "feat: the cloud shell projection, pure"
```

---

## Task 7: `StateGuard` sichert Sampler und zwei Pixel-SRVs

Der Schattenpass bindet `t0` (Cubemap) und `t1` (Tiefe) und einen Sampler auf `s0`. Der Wächter
sichert bisher einen PS-SRV und keinen PS-Sampler.

**Files:**

-   Modify: `src/Render/StateGuard.h:41` (`kPixelResources = 2`, neues Feld), `src/Render/StateGuard.cpp`

-   [ ] **Step 1: Header**

`kPixelResources` auf `2` setzen, Kommentar anpassen („slots 0 and 1 of the pixel stage: F2's
mask, F4's cubemap and depth"), und nach `_csSamplers`:

```cpp
		/// F4 samples its cubemap; nothing before it used a pixel sampler.
		static constexpr std::uint32_t kPixelSamplers = 1;
		REX::W32::ID3D11SamplerState* _psSamplers[kPixelSamplers]{};
```

-   [ ] **Step 2: Implementierung**

Im Konstruktor nach `_context->CSGetSamplers(…)`:

```cpp
		_context->PSGetSamplers(0, kPixelSamplers, _psSamplers);
```

Im Destruktor nach `_context->CSSetSamplers(…)`:

```cpp
		_context->PSSetSamplers(0, kPixelSamplers, _psSamplers);
```

und bei den Freigaben `ReleaseAll(_psSamplers, kPixelSamplers);`.

-   [ ] **Step 3: Bauen, Commit**

```bash
git add src/Render/StateGuard.h src/Render/StateGuard.cpp
git commit -m "fix: guard the pixel sampler and a second resource slot"
```

---

## Task 8: `Render::CubeTarget`

Eine eigene Cubemap: `Texture2D` mit `arraySize 6` und `MISC_TEXTURECUBE`, sechs RTVs
(`TEXTURE2DARRAY`, je eine Scheibe), eine `TEXTURECUBE`-SRV. In `Render`, weil F9 sie wieder
braucht.

**Files:**

-   Create: `src/Render/CubeTarget.h`, `src/Render/CubeTarget.cpp`

**Interfaces:**

-   Produces: `class Render::CubeTarget { bool Create(std::uint32_t a_size, REX::W32::DXGI_FORMAT a_format, std::string_view a_debugName) noexcept; void Release() noexcept; void ClearFace(REX::W32::ID3D11DeviceContext&, std::uint32_t a_face, float a_alpha) noexcept; REX::W32::ID3D11RenderTargetView* FaceRTV(std::uint32_t) const noexcept; REX::W32::ID3D11ShaderResourceView* SRV() const noexcept; std::uint32_t Size() const noexcept; bool Valid() const noexcept; }`

-   [ ] **Step 1: Header**

```cpp
#pragma once

#include <REX/W32/D3D11.h>
#include <REX/W32/DXGI.h>

#include <cstdint>
#include <string_view>

namespace Render
{
	/// A cubemap we own: six faces to draw into, one view to sample. Square,
	/// one mip, no unordered access - the coverage map F4 collects the
	/// engine's cloud draws into, and the shape F9's cubemaps will take.
	class CubeTarget
	{
	public:
		static constexpr std::uint32_t kFaces = 6;

		CubeTarget() = default;
		~CubeTarget() { Release(); }
		CubeTarget(const CubeTarget&) = delete;
		CubeTarget& operator=(const CubeTarget&) = delete;

		/// Replaces whatever was there. a_format has to be a render target
		/// format on this device; the caller checks with CheckFormatSupport.
		[[nodiscard]] bool Create(std::uint32_t a_size, REX::W32::DXGI_FORMAT a_format, std::string_view a_debugName) noexcept;

		/// Idempotent.
		void Release() noexcept;

		/// Clears one face to (0, 0, 0, a_alpha).
		void ClearFace(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_face, float a_alpha) noexcept;

		[[nodiscard]] REX::W32::ID3D11RenderTargetView* FaceRTV(std::uint32_t a_face) const noexcept
		{
			return a_face < kFaces ? _faces[a_face] : nullptr;
		}
		[[nodiscard]] REX::W32::ID3D11ShaderResourceView* SRV() const noexcept { return _srv; }
		[[nodiscard]] std::uint32_t Size() const noexcept { return _size; }
		[[nodiscard]] bool Valid() const noexcept { return _texture != nullptr; }

	private:
		REX::W32::ID3D11Texture2D* _texture{ nullptr };
		REX::W32::ID3D11RenderTargetView* _faces[kFaces]{};
		REX::W32::ID3D11ShaderResourceView* _srv{ nullptr };
		std::uint32_t _size{ 0 };
	};
}
```

-   [ ] **Step 2: Implementierung**

```cpp
#include "Render/CubeTarget.h"

#include "Render/DebugName.h"
#include "Render/Renderer.h"

#include <format>

namespace Render
{
	bool CubeTarget::Create(std::uint32_t a_size, REX::W32::DXGI_FORMAT a_format, std::string_view a_debugName) noexcept
	{
		Release();

		auto* const device = GetDevice();
		if (device == nullptr || a_size == 0) {
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		desc.width = a_size;
		desc.height = a_size;
		desc.mipLevels = 1;
		desc.arraySize = kFaces;
		desc.format = a_format;
		desc.sampleDesc.count = 1;
		desc.usage = REX::W32::D3D11_USAGE_DEFAULT;
		desc.bindFlags = REX::W32::D3D11_BIND_RENDER_TARGET | REX::W32::D3D11_BIND_SHADER_RESOURCE;
		desc.miscFlags = REX::W32::D3D11_RESOURCE_MISC_TEXTURECUBE;

		if (device->CreateTexture2D(std::addressof(desc), nullptr, std::addressof(_texture)) < 0) {
			REX::ERROR("cube target {}: the texture could not be created", a_debugName);
			return false;
		}
		static_cast<void>(SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_texture), a_debugName));

		for (std::uint32_t face = 0; face < kFaces; ++face) {
			REX::W32::D3D11_RENDER_TARGET_VIEW_DESC rtv{};
			rtv.format = a_format;
			rtv.viewDimension = REX::W32::D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtv.texture2DArray.mipSlice = 0;
			rtv.texture2DArray.firstArraySlice = face;
			rtv.texture2DArray.arraySize = 1;
			if (device->CreateRenderTargetView(_texture, std::addressof(rtv), std::addressof(_faces[face])) < 0) {
				REX::ERROR("cube target {}: face {} has no render target view", a_debugName, face);
				Release();
				return false;
			}
			static_cast<void>(SetDebugName(
				reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_faces[face]), std::format("{}_face{}", a_debugName, face)));
		}

		REX::W32::D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.format = a_format;
		srv.viewDimension = REX::W32::D3D11_SRV_DIMENSION_TEXTURECUBE;
		srv.textureCube.mostDetailedMip = 0;
		srv.textureCube.mipLevels = 1;
		if (device->CreateShaderResourceView(_texture, std::addressof(srv), std::addressof(_srv)) < 0) {
			REX::ERROR("cube target {}: no shader resource view", a_debugName);
			Release();
			return false;
		}

		_size = a_size;
		return true;
	}

	void CubeTarget::Release() noexcept
	{
		if (_srv != nullptr) {
			_srv->Release();
			_srv = nullptr;
		}
		for (auto*& face : _faces) {
			if (face != nullptr) {
				face->Release();
				face = nullptr;
			}
		}
		if (_texture != nullptr) {
			_texture->Release();
			_texture = nullptr;
		}
		_size = 0;
	}

	void CubeTarget::ClearFace(REX::W32::ID3D11DeviceContext& a_context, std::uint32_t a_face, float a_alpha) noexcept
	{
		if (a_face < kFaces && _faces[a_face] != nullptr) {
			const float colour[4]{ 0.0f, 0.0f, 0.0f, a_alpha };
			a_context.ClearRenderTargetView(_faces[a_face], colour);
		}
	}
}
```

Vor dem Bau die REX-Namen prüfen: `D3D11_SHADER_RESOURCE_VIEW_DESC::textureCube`,
`D3D11_BIND_RENDER_TARGET`, `D3D11_BIND_SHADER_RESOURCE`, `DXGI_SAMPLE_DESC::count`
(`grep -n "textureCube\|D3D11_BIND_RENDER_TARGET\b" extern/CommonLibF4/lib/commonlib-shared/include/REX/W32/D3D11.h`).

-   [ ] **Step 3: Bauen, Commit**

```bash
git add src/Render/CubeTarget.h src/Render/CubeTarget.cpp
git commit -m "feat: a cubemap of our own to draw into"
```

---

## Task 9: Der Schattenshader

**Files:**

-   Create: `package/Features/CloudShadows/Shaders/FO4/CloudShadows/CloudShadows.hlsl`

-   [ ] **Step 1: Shader schreiben**

```hlsl
// Cloud shadows, multiplied onto the engine's two direct light targets.
//
// One full-screen triangle (Fullscreen.hlsl). The blend state is
// ZERO / SRC_COLOR, so what this returns is the factor the light is
// multiplied by; the shader itself never sees the light. Same arrangement
// as ScreenSpaceShadows/Modulate.hlsl, and it runs in the same phase.
//
// The coverage cubemap is A8: the engine's own cloud draws, composited by
// alpha alone, from wherever the capture caught them. A pixel's world
// position comes from the depth buffer and the camera rows (F3); the
// direction into the cubemap is where the sun's ray through that point
// meets a spherical cloud shell - the template's projection, rearranged in
// CloudProjection.cpp so that the planet radius squared never appears.
//
// The "Debug View" setting, carried in CameraUp.w:
//   1  coverage   the sampled coverage, black to white
//   2  direction  normalize(direction) * 0.5 + 0.5
// Both are drawn with a plain blend state onto RT_058 alone.

TextureCube<float4> Coverage : register(t0);
Texture2D<float> DepthTexture : register(t1);
SamplerState LinearClamp : register(s0);

cbuffer PerFrame : register(b1)
{
	float4 CameraForward;  // xyz forward, w unused
	float4 CameraRight;    // xyz right / sx, w near plane
	float4 CameraUp;       // xyz up / sy, w debug view
	float4 SunDirection;   // xyz towards the sun, w opacity
	float4 Params;         // x cloud height (units), y planet radius (units), zw unused
};

struct PixelInput
{
	float4 position: SV_POSITION;
	float2 uv: TEXCOORD0;
};

struct PixelOutput
{
	float4 diffuse: SV_TARGET0;
	float4 specular: SV_TARGET1;
};

// CloudProjection::CloudSampleDirection, line for line.
float3 CloudSampleDirection(float3 relative, float3 toSun, float planetRadius, float cloudHeight)
{
	const float dotUS = relative.x * toSun.x + relative.y * toSun.y + (relative.z + planetRadius) * toSun.z;
	const float lengthSquared = dot(relative, relative);
	const float c = lengthSquared + 2.0 * planetRadius * (relative.z - cloudHeight) - cloudHeight * cloudHeight;
	const float discriminant = max(dotUS * dotUS - c, 0.0);
	const float t = -dotUS + sqrt(discriminant);
	return relative + toSun * t;
}

PixelOutput Output(float3 value)
{
	PixelOutput output;
	// Alpha stays at one: the blend state multiplies alpha by SRC_ALPHA.
	output.diffuse = float4(value, 1.0);
	output.specular = float4(value, 1.0);
	return output;
}

PixelOutput main(PixelInput input)
{
	const float depth = DepthTexture.Load(int3(input.position.xy, 0));
	const int debugView = (int)(CameraUp.w + 0.5);

	// Sky: no surface, no shadow.
	if (depth >= 0.9999)
		return Output(debugView == 0 ? 1.0.xxx : 0.0.xxx);

	const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
	const float3 ray = CameraForward.xyz + ndc.x * CameraRight.xyz + ndc.y * CameraUp.xyz;
	const float nearPlane = CameraRight.w;
	const float viewDepth = nearPlane / max(1.0 - depth, 1e-6);
	const float3 relative = ray * viewDepth;

	const float3 direction = CloudSampleDirection(relative, SunDirection.xyz, Params.y, Params.x);
	const float coverage = Coverage.SampleLevel(LinearClamp, direction, 0).a;
	const float factor = saturate(1.0 - coverage * SunDirection.w);

	if (debugView == 1)
		return Output(coverage.xxx);
	if (debugView == 2)
		return Output(normalize(direction) * 0.5 + 0.5);

	return Output(factor.xxx);
}
```

-   [ ] **Step 2: Übersetzbarkeit prüfen**

`ShaderCompilerTests` übersetzt keine Feature-Shader; die Prüfung ist der Bau im Spiel (Task 10,
Log `CloudShadows: shader compiled`). Vorher `fxc` falls vorhanden:
`& "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /T ps_5_0 /E main package/Features/CloudShadows/Shaders/FO4/CloudShadows/CloudShadows.hlsl` — Pfad und Version mit `Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\bin" -Recurse -Filter fxc.exe` ermitteln. Fehlt fxc, entfällt der Schritt.

-   [ ] **Step 3: Commit**

```bash
git add package/Features/CloudShadows
git commit -m "feat: the cloud shadow pixel shader"
```

---

## Task 10: Das Feature `CloudShadows` mit Weg A

Erfassung und Pass in einer Klasse. Der Weg-A-Beobachter lenkt jeden Wolkenzug, dessen RTV0 eine
Fläche der Engine-Cubemap ist, in unsere Fläche um. Ist nach Lauf 1 Weg B nötig, ersetzt Task 12
`BeforeDraw`/`AfterDraw`; das übrige Feature bleibt.

**Files:**

-   Create: `src/Features/CloudShadows.h`, `src/Features/CloudShadows.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (vor `ScreenSpaceShadows`)
-   Modify: `tests/SettingsSchemaTests.cpp` — **nicht**: der Test prüft das Schema an synthetischen
    Blöcken, nicht an Features; es gibt nichts zu erweitern. Die Spec-Zeile ist damit hinfällig und
    wird in Task 15 in der Roadmap so benannt.

**Interfaces:**

-   Consumes: alles aus Tasks 1, 2, 6, 7, 8, 9; `Render::FogCameraFromMatrix`,
    `Render::IsPlausibleViewProjection`, `Render::Targets::{DepthSRV, RenderTargetView, kSceneDepth, kLightDiffuse, kLightSpecular}`,
    `Render::SubscribeFramePhase(kBeforeComposite, …)`, `Render::FramePhaseHits`, `Render::StateGuard`,
    `Render::PassScope`, `Render::DrawFullscreen`, `Render::InitFullscreenPass`, `Shader::LoadSource`,
    `Shader::CompilePixelShader`, `Util::FileWatch`, `RE::Sky`, `RE::NiLight`.

-   [ ] **Step 1: Header**

`src/Features/CloudShadows.h`:

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Render/CubeTarget.h"
#include "Render/DrawObserver.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Features
{
	/// Shadows of the clouds the engine draws, multiplied onto the direct
	/// light from Phase::kBeforeComposite.
	///
	/// Two halves. The capture watches the engine's BSSkyClouds draws through
	/// the draw hook and composites their alpha - the coverage - into an A8
	/// cubemap of our own, without touching the engine's shader. The pass
	/// reconstructs each pixel's position from the depth buffer, casts it
	/// along the sun onto a spherical cloud shell and samples the cubemap
	/// there.
	///
	/// The capture's way in was measured by the probe of run 1 and is recorded
	/// in the roadmap under F4; Weg A redirects the draw at the engine's own
	/// cubemap render.
	class CloudShadows final : public Feature, public Render::DrawObserver
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "CloudShadows"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		[[nodiscard]] bool Wants(const Render::CurrentTechnique& a_current) const noexcept override;
		void BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept override;
		void AfterDraw(
			REX::W32::ID3D11DeviceContext& a_context,
			std::uint32_t a_indexCount,
			std::uint32_t a_startIndex,
			std::int32_t a_baseVertex) noexcept override;

	private:
		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureStates();
		[[nodiscard]] bool EnsureCoverage();
		[[nodiscard]] int EngineCubeFace(REX::W32::ID3D11RenderTargetView* a_view) const noexcept;

		Render::TechniqueFilter _clouds{};
		Render::CubeTarget _coverage;
		Render::ConstantBuffer _constants;
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11BlendState* _multiply{ nullptr };
		REX::W32::ID3D11BlendState* _plain{ nullptr };
		REX::W32::ID3D11BlendState* _alphaComposite{ nullptr };
		REX::W32::ID3D11SamplerState* _linearClamp{ nullptr };
		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };

		// Saved around one redirected draw, on the render thread.
		REX::W32::ID3D11RenderTargetView* _savedTarget{ nullptr };
		REX::W32::ID3D11DepthStencilView* _savedDepth{ nullptr };
		REX::W32::ID3D11BlendState* _savedBlend{ nullptr };
		float _savedFactor[4]{};
		std::uint32_t _savedMask{ 0 };
		bool _redirected{ false };

		std::uint64_t _faceClearedFrame[Render::CubeTarget::kFaces]{};
		std::uint32_t _capturedDraws{ 0 };
		std::uint32_t _capturedFaces{ 0 };
		std::uint64_t _draws{ 0 };
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };
		bool _reportedLowSun{ false };
	};
}
```

-   [ ] **Step 2: Implementierung**

`src/Features/CloudShadows.cpp`:

```cpp
#include "Features/CloudShadows.h"

#include "Features/CloudShadows/CloudProjection.h"
#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/DrawHook.h"
#include "Render/FramePhase.h"
#include "Render/FullscreenPass.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/StateGuard.h"
#include "Render/SwapChainHook.h"
#include "Render/Targets.h"
#include "Render/TechniqueTracker.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>

// RE/S/Sun.h is not self-contained; see ScreenSpaceShadows.cpp.
#include <RE/S/SkyObject.h>

namespace RE
{
	class BSShaderAccumulator;
	class BSTriShape;
	class NiBillboardNode;
	class NiDirectionalLight;
}

#include <RE/S/Sun.h>

#include <REX/W32/DXGI.h>

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "CloudShadows/CloudShadows.hlsl";
		constexpr std::uint32_t kCloudsTechnique = 0x0005;
		constexpr std::uint64_t kLogInterval = 180;
		constexpr auto kCoverageFormat = REX::W32::DXGI_FORMAT_A8_UNORM;

		/// Field for field the cbuffer PerFrame of CloudShadows.hlsl.
		struct alignas(16) ShadowConstants
		{
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float sunDirection[4];
			float params[4];
		};
		static_assert(sizeof(ShadowConstants) == 5 * 16);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}

		float DebugViewIndex(std::string_view a_choice) noexcept
		{
			if (a_choice == "coverage") {
				return 1.0f;
			}
			if (a_choice == "direction") {
				return 2.0f;
			}
			return 0.0f;
		}
	}

	void CloudShadows::Declare()
	{
		Settings::DeclareFeature("CloudShadows", true)
			.Label("feature.cloud_shadows.name", "Cloud Shadows")
			.Help(
				"feature.cloud_shadows.help",
				"Shadows of the clouds overhead, moving across the landscape with them. Taken "
				"from the clouds the game draws, so they match the sky.");

		Settings::DeclareSlider("CloudShadows/opacity", 0.5, 0.0, 4.0)
			.Label("feature.cloud_shadows.opacity", "Opacity")
			.Help("feature.cloud_shadows.opacity_help", "How dark the cloud shadows are.");

		Settings::DeclareSlider("CloudShadows/cloudHeight", 2000.0, 500.0, 20000.0)
			.Label("feature.cloud_shadows.cloud_height", "Cloud Height")
			.Help(
				"feature.cloud_shadows.cloud_height_help",
				"Height of the cloud layer in metres. Sets how far the shadows are offset from "
				"the clouds when the sun is low.");

		Settings::DeclareChoice(
			"CloudShadows/debugView",
			"off",
			std::vector<std::string>{ "off", "coverage", "direction" })
			.Label("feature.cloud_shadows.debug_view", "Debug View")
			.Help(
				"feature.cloud_shadows.debug_view_help",
				"Shows the pass instead of the picture: the cloud coverage as the ground sees "
				"it, or the direction it is sampled in.");
	}

	bool CloudShadows::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;
		_reportedLowSun = false;
		_draws = 0;
		_capturedDraws = 0;
		_capturedFaces = 0;
		_redirected = false;

		const auto skyIndex = Render::ClassIndexOf("BSSkyShader");
		if (!skyIndex.has_value()) {
			REX::ERROR("CloudShadows: BSSkyShader is not among the shader classes");
			return false;
		}
		_clouds = Render::TechniqueFilter{ *skyIndex, kCloudsTechnique };

		auto* const device = Render::GetDevice();
		std::uint32_t support = 0;
		if (device == nullptr ||
			device->CheckFormatSupport(kCoverageFormat, std::addressof(support)) < 0 ||
			(support & REX::W32::D3D11_FORMAT_SUPPORT_RENDER_TARGET) == 0 ||
			(support & REX::W32::D3D11_FORMAT_SUPPORT_BLENDABLE) == 0) {
			REX::ERROR("CloudShadows: A8_UNORM is not a blendable render target here (support 0x{:X})", support);
			return false;
		}

		if (!Render::InitFullscreenPass() || !Compile() || !EnsureStates()) {
			return false;
		}
		if (!_constants.Create(sizeof(ShadowConstants), "FO4CS_CB_CloudShadows")) {
			return false;
		}

		// Sized from the engine's cubemap, whose depth view the redirected
		// draw keeps: a render target and a depth view have to agree in size.
		if (!EnsureCoverage()) {
			return false;
		}

		_phase = Render::SubscribeFramePhase(
			Render::Phase::kBeforeComposite, "CloudShadows/Draw", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("CloudShadows: the frame phase has no room left");
			return false;
		}

		Render::SetDrawObserver(this);
		return true;
	}

	void CloudShadows::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("CloudShadows: shader changed, recompiling");
			static_cast<void>(Compile());
		}
	}

	void CloudShadows::Shutdown()
	{
		// First, before anything it could touch goes away.
		Render::SetDrawObserver(nullptr);

		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, _phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		if (_shader != nullptr) {
			_shader->Release();
			_shader = nullptr;
		}
		for (auto** state : { std::addressof(_multiply), std::addressof(_plain), std::addressof(_alphaComposite) }) {
			if (*state != nullptr) {
				(*state)->Release();
				*state = nullptr;
			}
		}
		if (_linearClamp != nullptr) {
			_linearClamp->Release();
			_linearClamp = nullptr;
		}
		_constants.Release();
		_coverage.Release();
	}

	bool CloudShadows::Wants(const Render::CurrentTechnique& a_current) const noexcept
	{
		return _clouds.Matches(a_current);
	}

	int CloudShadows::EngineCubeFace(REX::W32::ID3D11RenderTargetView* a_view) const noexcept
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr || a_view == nullptr) {
			return -1;
		}
		for (int face = 0; face < static_cast<int>(Render::CubeTarget::kFaces); ++face) {
			if (data->cubeMapRenderTargets[0].rtView[face] == a_view) {
				return face;
			}
		}
		return -1;
	}

	void CloudShadows::BeforeDraw(REX::W32::ID3D11DeviceContext& a_context) noexcept
	{
		_redirected = false;
		if (!_coverage.Valid()) {
			return;
		}

		a_context.OMGetRenderTargets(1, std::addressof(_savedTarget), std::addressof(_savedDepth));
		const auto face = EngineCubeFace(_savedTarget);
		if (face < 0) {
			// The main view, or something else. Not ours to redirect.
			if (_savedTarget != nullptr) {
				_savedTarget->Release();
				_savedTarget = nullptr;
			}
			if (_savedDepth != nullptr) {
				_savedDepth->Release();
				_savedDepth = nullptr;
			}
			return;
		}

		// The first cloud layer into this face this frame starts from nothing.
		const auto frame = Render::FrameCount();
		if (_faceClearedFrame[face] != frame) {
			_faceClearedFrame[face] = frame;
			_coverage.ClearFace(a_context, static_cast<std::uint32_t>(face), 0.0f);
			_capturedFaces |= 1u << face;
		}

		a_context.OMGetBlendState(std::addressof(_savedBlend), _savedFactor, std::addressof(_savedMask));

		auto* const target = _coverage.FaceRTV(static_cast<std::uint32_t>(face));
		a_context.OMSetRenderTargets(1, std::addressof(target), _savedDepth);
		const float factor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		a_context.OMSetBlendState(_alphaComposite, factor, 0xFFFFFFFFu);
		_redirected = true;
		++_capturedDraws;
	}

	void CloudShadows::AfterDraw(
		REX::W32::ID3D11DeviceContext& a_context, std::uint32_t, std::uint32_t, std::int32_t) noexcept
	{
		if (!_redirected) {
			return;
		}
		_redirected = false;

		a_context.OMSetRenderTargets(1, std::addressof(_savedTarget), _savedDepth);
		a_context.OMSetBlendState(_savedBlend, _savedFactor, _savedMask);

		for (auto** view : { reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedTarget)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedDepth)),
				 reinterpret_cast<REX::W32::IUnknown**>(std::addressof(_savedBlend)) }) {
			if (*view != nullptr) {
				(*view)->Release();
				*view = nullptr;
			}
		}
	}

	void CloudShadows::Draw()
	{
		++_draws;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("CloudShadows: no sun, standing down");
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
				REX::ERROR(
					"CloudShadows: standing down, depth {}, diffuse {}, specular {}",
					depth != nullptr ? "ok" : "missing",
					diffuse != nullptr ? "ok" : "missing",
					specular != nullptr ? "ok" : "missing");
				_reportedNoTargets = true;
			}
			return;
		}
		_reportedNoTargets = false;

		auto* const world = RE::Main::WorldRootCamera();
		auto* const context = Render::GetContext();
		if (world == nullptr || context == nullptr || _shader == nullptr || !_coverage.Valid()) {
			return;
		}

		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return;
		}
		const auto camera = Render::FogCameraFromMatrix(world->worldToCam, world->GetWorldTransform().translate.z);

		const auto* const light = reinterpret_cast<const RE::NiLight*>(sky->sun->light.get());
		const auto& rotate = light->GetWorldRotate();
		float towardsSun[3]{ -rotate.entry[0][0], -rotate.entry[0][1], -rotate.entry[0][2] };
		const auto length = std::sqrt(
			towardsSun[0] * towardsSun[0] + towardsSun[1] * towardsSun[1] + towardsSun[2] * towardsSun[2]);
		if (length < 1.0e-4f) {
			return;
		}
		for (auto& component : towardsSun) {
			component /= length;
		}

		// Below the horizon there is no ray up to the clouds.
		if (towardsSun[2] <= 0.0f) {
			if (!_reportedLowSun) {
				REX::INFO("CloudShadows: sun below the horizon, standing down");
				_reportedLowSun = true;
			}
			return;
		}
		_reportedLowSun = false;

		const auto debugView = DebugViewIndex(Settings::GetString("CloudShadows/debugView"));
		const auto cloudHeight = static_cast<float>(Settings::GetDouble("CloudShadows/cloudHeight")) * Clouds::kUnitsPerMetre;

		ShadowConstants data{};
		data.cameraForward[0] = camera.forward[0];
		data.cameraForward[1] = camera.forward[1];
		data.cameraForward[2] = camera.forward[2];
		data.cameraRight[0] = camera.rightOverScaleX[0];
		data.cameraRight[1] = camera.rightOverScaleX[1];
		data.cameraRight[2] = camera.rightOverScaleX[2];
		data.cameraRight[3] = camera.near;
		data.cameraUp[0] = camera.upOverScaleY[0];
		data.cameraUp[1] = camera.upOverScaleY[1];
		data.cameraUp[2] = camera.upOverScaleY[2];
		data.cameraUp[3] = debugView;
		data.sunDirection[0] = towardsSun[0];
		data.sunDirection[1] = towardsSun[1];
		data.sunDirection[2] = towardsSun[2];
		data.sunDirection[3] = static_cast<float>(Settings::GetDouble("CloudShadows/opacity"));
		data.params[0] = cloudHeight;
		data.params[1] = Clouds::kEarthRadiusMetres * Clouds::kUnitsPerMetre;

		if (_draws % kLogInterval == 0) {
			REX::INFO(
				"clouds: {} draw(s) captured into faces 0x{:02X} in {} frames, cube {}, sun [{:.3f} {:.3f} {:.3f}], "
				"cloud height {:.0f}, near {:.2f}",
				_capturedDraws, _capturedFaces, kLogInterval, _coverage.Size(),
				towardsSun[0], towardsSun[1], towardsSun[2], cloudHeight, camera.near);
			_capturedDraws = 0;
			_capturedFaces = 0;
		}

		const Render::StateGuard guard;
		const Render::PassScope scope{ "CloudShadows/Shadow" };

		context->OMSetRenderTargets(0, nullptr, nullptr);
		static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));
		auto* const buffer = _constants.Buffer();

		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		if (debugView > 0.0f) {
			context->OMSetRenderTargets(1, std::addressof(diffuse), nullptr);
			context->OMSetBlendState(_plain, blendFactor, 0xFFFFFFFFu);
		} else {
			REX::W32::ID3D11RenderTargetView* targets[2]{ diffuse, specular };
			context->OMSetRenderTargets(2, targets, nullptr);
			context->OMSetBlendState(_multiply, blendFactor, 0xFFFFFFFFu);
		}

		REX::W32::ID3D11ShaderResourceView* resources[2]{ _coverage.SRV(), depth };
		context->PSSetShaderResources(0, 2, resources);
		context->PSSetSamplers(0, 1, std::addressof(_linearClamp));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);

		Render::DrawFullscreen();
	}

	bool CloudShadows::Compile()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kShaderFile };
			_watch.Reset(only);
			REX::ERROR("CloudShadows: {}", source.error());
			return false;
		}
		_watch.Reset(source->files);

		const auto compiled = Shader::CompilePixelShader(source->text, "CloudShadows.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("CloudShadows: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11PixelShader* shader = nullptr;
		if (device == nullptr ||
			device->CreatePixelShader(compiled.bytecode.data(), compiled.bytecode.size(), nullptr, std::addressof(shader)) < 0) {
			REX::ERROR("CloudShadows: the shadow shader could not be created");
			return false;
		}

		if (_shader != nullptr) {
			_shader->Release();
		}
		_shader = shader;
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_CloudShadows"));
		REX::INFO("CloudShadows: shader compiled");
		return true;
	}

	bool CloudShadows::EnsureStates()
	{
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		if (_multiply == nullptr) {
			// dest * src, the same state F2's Modulate uses.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = true;
			target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
			target.destBlend = REX::W32::D3D11_BLEND_SRC_COLOR;
			target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
			target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
			target.destBlendAlpha = REX::W32::D3D11_BLEND_SRC_ALPHA;
			target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_multiply)) < 0) {
				REX::ERROR("CloudShadows: the multiply blend state could not be created");
				return false;
			}
		}

		if (_plain == nullptr) {
			// Overwrite, for the debug views.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = false;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_plain)) < 0) {
				REX::ERROR("CloudShadows: the plain blend state could not be created");
				return false;
			}
		}

		if (_alphaComposite == nullptr) {
			// Alpha only: a = src.a + dest.a * (1 - src.a). Colour untouched,
			// and the shader's colour never reaches the target - which is what
			// lets the engine's own cloud shader write our coverage.
			REX::W32::D3D11_BLEND_DESC desc{};
			auto& target = desc.renderTarget[0];
			target.blendEnable = true;
			target.srcBlend = REX::W32::D3D11_BLEND_ZERO;
			target.destBlend = REX::W32::D3D11_BLEND_ONE;
			target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
			target.srcBlendAlpha = REX::W32::D3D11_BLEND_ONE;
			target.destBlendAlpha = REX::W32::D3D11_BLEND_INV_SRC_ALPHA;
			target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
			target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALPHA);
			if (device->CreateBlendState(std::addressof(desc), std::addressof(_alphaComposite)) < 0) {
				REX::ERROR("CloudShadows: the alpha composite blend state could not be created");
				return false;
			}
		}

		if (_linearClamp == nullptr) {
			REX::W32::D3D11_SAMPLER_DESC desc{};
			desc.filter = REX::W32::D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.addressU = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressV = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.addressW = REX::W32::D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.maxLOD = 3.402823466e+38f;
			if (device->CreateSamplerState(std::addressof(desc), std::addressof(_linearClamp)) < 0) {
				REX::ERROR("CloudShadows: the sampler could not be created");
				return false;
			}
		}

		return true;
	}

	bool CloudShadows::EnsureCoverage()
	{
		const auto* const data = RE::BSGraphics::GetRendererData();
		if (data == nullptr || data->cubeMapRenderTargets[0].texture == nullptr) {
			REX::ERROR("CloudShadows: the engine has no cubemap render target to size from");
			return false;
		}

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		data->cubeMapRenderTargets[0].texture->GetDesc(std::addressof(desc));

		if (_coverage.Valid() && _coverage.Size() == desc.width) {
			return true;
		}
		REX::INFO("CloudShadows: coverage cube sized {} like the engine's", desc.width);
		return _coverage.Create(desc.width, kCoverageFormat, "FO4CS_CUBE_CloudCoverage");
	}
}
```

Vor dem Bau prüfen: `REX::W32::IUnknown` (oder heißt es anders im Namensraum?),
`D3D11_SAMPLER_DESC::maxLOD`, `D3D11_BIND_*`. Wo ein Name fehlt, die REX-Deklaration lesen, nicht
raten.

-   [ ] **Step 3: Registrieren**

In `src/Feature/FeatureSystem.cpp` `#include "Features/CloudShadows.h"` und **vor**
`ScreenSpaceShadows`:

```cpp
			// Before the contact shadows: both multiply the same targets from
			// the same phase, and this order reads sensibly in the profiler.
			TheRegistry().Register(std::make_unique<CloudShadows>());
```

-   [ ] **Step 4: Katalog, Bau, Tests**

`python tools/extract-i18n.py --write`, voller Bau, `ctest`, `verify-plugin.ps1`, `extract-i18n.py`
ohne `--write`. Erwartet: grün, deployt.

-   [ ] **Step 5: Commit**

```bash
git add src/Features/CloudShadows.h src/Features/CloudShadows.cpp src/Feature/FeatureSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: cloud shadows from the engine's own clouds"
```

---

## Task 11: Weg A′ — `bReflectSky` (nur nach Befund „keine Cubemap-Zeile")

**Files:**

-   Modify: `src/Features/CloudShadows.h` (zwei Felder), `src/Features/CloudShadows.cpp`

-   [ ] **Step 1: Felder**

```cpp
		bool _reflectSkyChanged{ false };
```

-   [ ] **Step 2: Setup und Shutdown**

`#include <RE/S/Setting.h>`. In `Setup` vor `SetDrawObserver`:

```cpp
		// The engine renders its sky into the cubemap only with this on, and
		// every ini here has it off. Set in memory, never in the file, and
		// handed back in Shutdown.
		if (auto* const setting = RE::GetINISetting("bReflectSky:Water"); setting != nullptr) {
			if (!setting->GetBinary()) {
				setting->SetBinary(true);
				_reflectSkyChanged = true;
				REX::INFO("CloudShadows: bReflectSky turned on for the session");
			}
		} else {
			REX::WARN("CloudShadows: bReflectSky:Water is not a known setting");
		}
```

In `Shutdown` nach `SetDrawObserver(nullptr)`:

```cpp
		if (_reflectSkyChanged) {
			if (auto* const setting = RE::GetINISetting("bReflectSky:Water"); setting != nullptr) {
				setting->SetBinary(false);
			}
			_reflectSkyChanged = false;
		}
```

-   [ ] **Step 3: Zweiter Sondenlauf**

Die Sonde ist entfernt; die `clouds:`-Zeile des Features zählt selbst: `captured into faces
0x3F` je Sekunde heißt, alle sechs Flächen werden bedient. Bauen, Nutzer startet, eine Minute
draußen. Steht `faces 0x00`, ist A′ gescheitert → Task 12, und dieser Task wird zurückgenommen
(Commit revertieren).

-   [ ] **Step 4: Commit**

```bash
git add src/Features/CloudShadows.h src/Features/CloudShadows.cpp
git commit -m "feat: turn on sky reflection for the cloud capture"
```

---

## Task 12: Weg B — zweite Projektion (nur nach Befund „worldToCam FOUND")

**Files:**

-   Modify: `src/Render/Camera.h`, `src/Render/Camera.cpp` (zwei Funktionen)
-   Modify: `tests/CameraTests.cpp`
-   Modify: `src/Features/CloudShadows.h`, `src/Features/CloudShadows.cpp` (`BeforeDraw`/`AfterDraw`, `Frame`)

**Interfaces:**

-   Produces: `bool Render::CameraPositionFromMatrix(const float (&a_worldToCam)[4][4], float (&a_out)[3]) noexcept`,
    `void Render::CubeFaceViewProjection(const float (&a_position)[3], float a_near, std::uint32_t a_face, float (&a_out)[4][4]) noexcept`.

-   [ ] **Step 1: Test schreiben**

An `tests/CameraTests.cpp` vor der Abschlußzeile:

```cpp
	{
		// The loading screen camera stood at height 128, the log said so;
		// the matrix gives it back from its translation column.
		float position[3]{};
		Check(Render::CameraPositionFromMatrix(kLoadingScreen, position), "the camera position is recoverable");
		Check(Near(position[0], 0.0f, 0.1f) && Near(position[1], 0.0f, 0.1f), "loading screen: x and y are zero");
		Check(Near(position[2], 128.0f, 0.1f), "loading screen: z is 128");
	}

	{
		// Face +Z looks up world z with 90 degrees: a point straight above
		// the camera lands in the middle, one at 45 degrees on the edge, and
		// the near plane is the one asked for.
		const float position[3]{ 10.0f, 20.0f, 30.0f };
		float face[4][4]{};
		Render::CubeFaceViewProjection(position, 15.0f, 4, face);
		Check(Near(face[3][3] - face[2][3], 15.0f, 1e-4f), "face +Z: near plane as given");

		const float above[4]{ 10.0f, 20.0f, 130.0f, 1.0f };
		const auto centre = Render::ClipFromWorldToCam(face, above);
		Check(Near(centre[0] / centre[3], 0.0f, 1e-5f) && Near(centre[1] / centre[3], 0.0f, 1e-5f), "face +Z: straight up is the centre");
		Check(Near(centre[3], 100.0f, 1e-4f), "face +Z: w is the distance");

		const float edge[4]{ 110.0f, 20.0f, 130.0f, 1.0f };
		const auto right = Render::ClipFromWorldToCam(face, edge);
		Check(Near(right[0] / right[3], 1.0f, 1e-5f), "face +Z: 45 degrees along +x is the right edge");

		float faceX[4][4]{};
		Render::CubeFaceViewProjection(position, 15.0f, 0, faceX);
		const float alongX[4]{ 110.0f, 20.0f, 30.0f, 1.0f };
		const auto ahead = Render::ClipFromWorldToCam(faceX, alongX);
		Check(Near(ahead[0], 0.0f, 1e-5f) && Near(ahead[1], 0.0f, 1e-5f) && Near(ahead[3], 100.0f, 1e-4f), "face +X: along +x is its centre");
	}
```

`ClipFromWorldToCam` existiert in `Camera.h` (F2) und nimmt `(const float (&)[4][4], const float (&)[4])`
— Signatur vorher im Header nachlesen und den Aufruf anpassen, falls sie `float[3]` und implizites
`w = 0` nimmt: dann `ClipFromWorldToCam` mit Punkt-Variante ergänzen oder hier die Multiplikation
ausschreiben.

-   [ ] **Step 2: Deklarationen**

In `Camera.h`:

```cpp
	/// The camera's world position out of worldToCam: the translation column
	/// is -(axis . position) for each of the three scaled axes, and the axes
	/// are orthogonal, so position = -sum(m[i][3] * axis_i / |axis_i|^2).
	/// False when an axis is degenerate.
	[[nodiscard]] bool CameraPositionFromMatrix(const float (&a_worldToCam)[4][4], float (&a_out)[3]) noexcept;

	/// A world-to-clip matrix in the engine's own arrangement - rows right,
	/// up, forward-minus-near, forward - for one face of a cubemap centred on
	/// a_position, 90 degrees wide, D3D face order: +X, -X, +Y, -Y, +Z, -Z.
	void CubeFaceViewProjection(const float (&a_position)[3], float a_near, std::uint32_t a_face, float (&a_out)[4][4]) noexcept;
```

-   [ ] **Step 3: Implementierung**

In `Camera.cpp`:

```cpp
	bool CameraPositionFromMatrix(const float (&a_worldToCam)[4][4], float (&a_out)[3]) noexcept
	{
		a_out[0] = a_out[1] = a_out[2] = 0.0f;
		// Rows 0, 1 carry the scaled right and up axes; row 3 the unit forward.
		for (const int row : { 0, 1, 3 }) {
			const auto* const axis = a_worldToCam[row];
			const auto squared = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
			if (squared < 1.0e-12f) {
				return false;
			}
			const auto scale = -a_worldToCam[row][3] / squared;
			a_out[0] += axis[0] * scale;
			a_out[1] += axis[1] * scale;
			a_out[2] += axis[2] * scale;
		}
		return true;
	}

	void CubeFaceViewProjection(const float (&a_position)[3], float a_near, std::uint32_t a_face, float (&a_out)[4][4]) noexcept
	{
		// D3D's cube faces: look direction, up, right - so that sampling the
		// cube with a world direction lands on what this face drew.
		static constexpr float kAxes[6][3][3] = {
			{ { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, -1 } },   // +X: right is -Z
			{ { -1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } },   // -X: right is +Z
			{ { 0, 1, 0 }, { 0, 0, -1 }, { 1, 0, 0 } },   // +Y: up is -Z
			{ { 0, -1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } },   // -Y: up is +Z
			{ { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 } },    // +Z
			{ { 0, 0, -1 }, { 0, 1, 0 }, { -1, 0, 0 } },  // -Z
		};
		const auto& axes = kAxes[a_face < 6 ? a_face : 0];
		const auto* const forward = axes[0];
		const auto* const up = axes[1];
		const auto* const right = axes[2];

		const auto row = [&](const float* a_axis, float (&a_row)[4], float a_extra) {
			a_row[0] = a_axis[0];
			a_row[1] = a_axis[1];
			a_row[2] = a_axis[2];
			a_row[3] = -(a_axis[0] * a_position[0] + a_axis[1] * a_position[1] + a_axis[2] * a_position[2]) + a_extra;
		};
		row(right, a_out[0], 0.0f);
		row(up, a_out[1], 0.0f);
		row(forward, a_out[2], -a_near);
		row(forward, a_out[3], 0.0f);
	}
```

-   [ ] **Step 4: Bauen, Test, Mutation**

`CameraTests` bauen und laufen lassen: grün. Mutation: in `CubeFaceViewProjection` die Zeile
`row(forward, a_out[2], -a_near)` zu `row(forward, a_out[2], 0.0f)`. Erwartet fällt: „face +Z:
near plane as given". Zurücknehmen. Zweite: in `CameraPositionFromMatrix` das Minus vor
`a_worldToCam[row][3]` streichen. Erwartet fällt: „loading screen: z is 128". Zurücknehmen.

-   [ ] **Step 5: Commit der Kamera**

```bash
git add src/Render/Camera.h src/Render/Camera.cpp tests/CameraTests.cpp
git commit -m "feat: cube face matrices from the camera matrix"
```

-   [ ] **Step 6: Beobachter für Weg B**

**Dieser Schritt wird nach Lauf 1 als Code ausgeschrieben**, sobald Slot, Byte-Offset und
Anordnung der Matrix aus dem Sondenlog feststehen — der Plan wird dann ergänzt und committet,
bevor Task 12 beginnt. Bis dahin steht hier der Ablauf, damit die Spec-Entscheidung geprüft
werden kann.

Header-Felder in `CloudShadows.h` ergänzen:

```cpp
		// Weg B: the engine's vertex constants of the clouds draw, a frame old.
		std::uint32_t _matrixSlot{ 0 };          // aus der Sonde
		std::uint32_t _matrixByteOffset{ 0 };    // aus der Sonde
		bool _matrixByColumns{ false };          // aus der Sonde
		REX::W32::ID3D11Buffer* _stagingConstants{ nullptr };
		REX::W32::ID3D11Buffer* _faceConstants{ nullptr };
		REX::W32::ID3D11Buffer* _savedConstants{ nullptr };
		std::vector<std::uint8_t> _constantsImage;
		bool _constantsReady{ false };
		std::uint64_t _stagingFrame{ 0 };
```

Die drei Werte `_matrixSlot`, `_matrixByteOffset`, `_matrixByColumns` werden als `constexpr` mit
den Zahlen aus dem Sondenlog eingetragen und im Kommentar mit Datum belegt.

`BeforeDraw` (ersetzt Weg A): RTV0 muß `FO4_RT_004` sein (Name über `GetViewTargetName`), sonst
nichts. Dann `OMGetRenderTargets(1, …)`, `OMGetBlendState`, `VSGetConstantBuffers(_matrixSlot, 1, &_savedConstants)`;
falls `_stagingConstants` noch nicht existiert: mit `D3D11_USAGE_STAGING`, `CPU_ACCESS_READ` und
der `byteWidth` des Engine-Puffers anlegen, `_faceConstants` als `D3D11_USAGE_DEFAULT` ohne CPU-Zugriff
gleicher Größe; `CopyResource(_stagingConstants, _savedConstants)`, `_stagingFrame = FrameCount()`;
`_redirected = true`.

`AfterDraw`: wenn `_redirected && _constantsReady`: für `face` in 0..4: Fläche leeren, falls
`_faceClearedFrame[face] != frame`; Kameraposition über `CameraPositionFromMatrix(worldToCam)`,
Matrix über `CubeFaceViewProjection(position, camera.near, face, m)`; in `_constantsImage` an
`_matrixByteOffset` schreiben — zeilenweise oder transponiert nach `_matrixByColumns`;
`UpdateSubresource(_faceConstants, 0, nullptr, _constantsImage.data(), 0, 0)`;
`VSSetConstantBuffers(_matrixSlot, 1, &_faceConstants)`; `OMSetRenderTargets(1, &faceRTV, nullptr)`;
`OMSetBlendState(_alphaComposite, …)`; `g_original` ist hier **nicht** erreichbar — der Beobachter
ruft `a_context.DrawIndexed(a_indexCount, a_startIndex, a_baseVertex)`, was über den Hook läuft:
deshalb vor dem Nachzeichnen `_redirected = false` setzen, damit `Wants` dieselben Aufrufe nicht
noch einmal beobachtet (ein Flag `_drawingOurselves`, das `Wants` auf `false` zwingt). Danach den
gesicherten Zustand zurückgeben und freigeben.

`Frame()`: wenn `_stagingConstants != nullptr && FrameCount() > _stagingFrame + 1`: `Map(READ, DO_NOT_WAIT)`,
Inhalt in `_constantsImage` kopieren, `Unmap`, `_constantsReady = true`.

`Shutdown`: die drei Puffer freigeben, `_constantsReady = false`.

`EnsureCoverage`: die Größe ist frei wählbar (kein Engine-DSV), 512 fest.

Bauen, Commit:

```bash
git add src/Features/CloudShadows.h src/Features/CloudShadows.cpp
git commit -m "feat: redraw the clouds into a cubemap of our own"
```

---

## Task 13: Paket und Prüfung

**Files:**

-   Modify: `tools/verify-package.ps1`

-   [ ] **Step 1: Sechstes Archiv**

Neben `$fog`:

```powershell
$clouds = @($archives | Where-Object { $_.Name -like "CloudShadows-*" })
```

Neben `Check ($fog.Count -eq 1) …`:

```powershell
Check ($clouds.Count -eq 1) "exactly one CloudShadows archive"
```

Das große `if` um `-and $clouds.Count -eq 1` erweitern, darin
`$cloudsEntries = @(Get-Entries $clouds[0].FullName)`, nach dem Fog-Block:

```powershell
    Check (
        $cloudsEntries.Count -eq 1 -and $cloudsEntries[0] -eq "Shaders/FO4/CloudShadows/CloudShadows.hlsl"
    ) "the cloud addon carries its shader and nothing else"
    Check (
        -not ($baseEntries -contains "Shaders/FO4/CloudShadows/CloudShadows.hlsl")
    ) "and the base does not carry it"
```

`$union` um `$cloudsEntries` erweitern, in der `foreach`-Liste
`@{ Name = "cloud addon"; Entries = $cloudsEntries }`.

-   [ ] **Step 2: Paket bauen und prüfen**

`cmake --build --preset FO4 --target package`, dann `pwsh tools/verify-package.ps1`. Erwartet: sechs
Archive, alles grün.

-   [ ] **Step 3: Commit**

```bash
git add tools/verify-package.ps1
git commit -m "build: verify the cloud shadows addon archive"
```

---

## Task 14: Lauf 2 — Abnahme

Kein Code vor dem Lauf. Prüfliste an den Nutzer, nummeriert, mit „was schiefgehen kann":

1. Overlay: Block `Cloud Shadows` mit Schalter, `Opacity`, `Cloud Height`, `Debug View`. Fehlt er:
   `Declare` lief nicht.
2. Bewölkt, Blick über die Landschaft: Schalter aus und an. Kein Unterschied: Erfassung leer oder
   der Pass steht ab — dann die `clouds:`-Zeile lesen (`captured … faces 0x00`?).
3. `Debug View` auf `coverage`: das Bild wird zur Wolkenkarte, sie zieht mit dem Himmel. Schwarz:
   nichts erfaßt. Steht: die Cubemap wird nicht aufgefrischt.
4. `Debug View` auf `direction`: gleichmäßiger Farbverlauf ohne Sprünge. Sprünge: Kugelprojektion
   oder Kameravorzeichen.
5. `Debug View` aus, `Opacity` auf 4: harte Schatten, die in Zugrichtung der Wolken wandern. Falsche
   Richtung: Cubemap-Orientierung.
6. `Cloud Height` von 500 auf 20000 schieben: die Schatten versetzen sich gegen die Wolken. Nichts:
   `Params` kommt nicht an.
7. Performance-Tafel: `CloudShadows/Draw` und `CloudShadows/Shadow`, dann **F11**.
8. Root Cellar, Pip-Boy, Alt-Tab und zurück.
9. Feature im Overlay aus und wieder an.
10. Sag mir Bescheid, wenn du draußen stehst: ich stoße den Hot-Reload für `CloudShadows.hlsl`
    **und** `Fog.hlsl` an (`touch` auf beide deployten Dateien). Im Spiel darf sich nichts ändern;
    im Log je eine Zeile „recompiling".
11. Nebenbei aus F3: `Exponential Height Fog` → `Debug View` auf `color`, Horizont mit dem
    Spielnebel vergleichen, wieder aus.

Log auswerten: keine `[E]`-Zeilen, `clouds:`-Zeilen je Sekunde mit `faces 0x3F` (Weg A) oder
`0x1F` (Weg B), Schnappschuß mit beiden Passzeilen, `draw hook` Aufrufe je Frame aus Lauf 1 als
Kosten notieren. Fällt ein Schritt: systematic-debugging, ein Lauf je Hypothese.

---

## Task 15: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: Roadmap**

In `docs/fallout4-port/ROADMAP.md`: Statuszeile (F4 abgeschlossen, als Nächstes F5), Tabelle (F4
**abgeschlossen**), neuer Abschnitt „Aus Teilprojekt F4 bestätigt" nach dem F3-Abschnitt: die
Zahlen (Schnappschuß, Draw-Hook-Kosten), der Befund der Sonde (Tabelle Technik × Ziel, Blend-State,
Matrixfund, Formatunterstützung), der gewählte Weg mit Begründung und die nicht gewählten, die
Gabelungen der Spec, die Abnahme mit dem, was ungeprüft blieb (darunter: `SettingsSchemaTests`
prüft synthetische Blöcke, die Spec-Zeile 9.1 war ein Irrtum), Zugeständnisse (Alter der Karte,
Wasserreflexion ohne Wolken bei Weg A). `frame-order.md` um die Sondenbefunde zur Cubemap ergänzen.

-   [ ] **Step 2: Commit**

```bash
git add docs/fallout4-port/ROADMAP.md docs/fallout4-port/frame-order.md
git commit -m "docs: record what F4 confirmed"
```

-   [ ] **Step 3: Erinnerungen**

`fallout4-engine-facts.md`: Cubemap-Befund, Blend-State der Wolken, Fundort der Matrix,
Draw-Hook-Kosten. `fallout4-port-roadmap.md`: F4 fertig, F5 als Nächstes, was F5 bis F11 von F4
erben (`TechniqueTracker`, `DrawHook`, `CubeTarget`). `fallout4-f3-stand.md` → als Nachschlagwerk
lassen; eine `fallout4-f4-stand.md` nur, falls etwas offen bleibt.

-   [ ] **Step 4: Branch abschließen**

finishing-a-development-branch: volle Testsuite, Paket, Menü per AskUserQuestion, nach Wahl des
Nutzers Fast-Forward nach `dev`, Branch löschen, Push nur auf Ansage.

---

## Reihenfolge und Abhängigkeiten

```
1 Tracker ──┐
2 Observer/Hook ─┼─► 3 Sonde ─► 4 Lauf 1 ─► 5 Sonde raus ─┐
                 │                                          │
6 Projektion ────┤                                          ├─► 10 Feature (Weg A) ─► 13 Paket ─► 14 Lauf 2 ─► 15 Abschluß
7 StateGuard ────┤                                          │        │
8 CubeTarget ────┤                                          │        ├─► 11 Weg A′ (bei Bedarf, dann Lauf 1b)
9 Shader ────────┘                                          │        └─► 12 Weg B  (bei Bedarf)
```

Tasks 6 bis 9 hängen nicht an Lauf 1 und werden gebaut, während der Nutzer ihn macht. Zwei
Spielstarts mindestens, drei bei A′ oder B.
