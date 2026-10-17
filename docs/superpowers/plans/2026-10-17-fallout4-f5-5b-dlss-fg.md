# F5.5b — DLSS Frame Generation: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein erzeugtes Bild nach jedem gerenderten (DLSS-G 2×) mit Reflex, über eine D3D12-Brücke
hinter der D3D11-Swapchain von Fallout 4, im laufenden Spiel schaltbar und meßbar als angezeigte
gegen gerenderte Bilder.

**Architecture:** Ein IAT-Hook auf `D3D11CreateDeviceAndSwapChain` gibt dem Spiel — wenn der
Startschalter steht, das Fenster randlos ist und Streamline lädt — ein eigenes D3D11-Gerät und
einen `SwapChainProxy`. Dahinter steht ein `Presenter` mit D3D12-Gerät, einer Swapchain aus
Streamlines Fabrik, einem geteilten Fence und einem Ring aus zwei Slots geteilter Texturen
(Endbild, HUD-los, Tiefe, Bewegung). Der D3D12-Teil ist eine „native Insel" ohne PCH, die mit dem
Rest nur über `Bridge/Bridge.h` (reine Typen) spricht. Das Feature `FrameGeneration` füllt die
Slots über eine zweite Hörstelle an den F5.5a-Hooks, reicht Kamera und Jitter weiter und schaltet
DLSS-G an und aus; der Frame-Beginn für Reflex kommt aus `Main::OnIdle`.

**Tech Stack:** C++23, MSVC `/W4 /WX`, D3D11 über REX, D3D12/DXGI/D3D11 aus dem Windows SDK nur in
`src/Bridge/Native/`, NVIDIA Streamline 2.14.16 (manuell geladen, kein Import), glaze (über die
PCH), HLSL `cs_5_0`; Host-Tests als `main` mit `Check`.

**Spec:** `docs/superpowers/specs/2026-10-17-fallout4-f5-5b-dlss-fg-design.md`

**Quellen:** Streamline v2.14.16 (`include/`, `docs/ProgrammingGuide*.md`), Skyrim-CS
`git show skyrim-base:src/Features/Upscaling/DX12SwapChain.cpp`, doodlum `fo4test` Zweig `main`
`4096d7e` (`src/DX12SwapChain.cpp`, `src/Upscaling.cpp`).

### Abweichungen von der Spec (aus der Prüfung beim Planen)

1. **Streamline und das D3D12-Gerät leben bis zum Prozeßende** (`Backend`). Streamline arbeitet
   mit genau einem Gerät, und `slShutdown` muß **vor** dem Abbau der Geräte laufen
   (ManualHooking.md 3.0). Spec 4.6 sah `slShutdown` nach der Swapchain vor; das wäre falsch
   herum. Legt das Spiel sein Gerät ein zweites Mal an, wird das `Backend` wiederverwendet.
2. **Tiefe und Bewegungsvektoren nach `RenderEffectRange` (`2318322+0x83`)**, das HUD-lose Bild
   nach `+0xC5`. An `+0x83` liest DLSS SR beides nachweislich; zwischen den beiden Stellen liegt
   nur wenig Code, aber gemessen ist nur die erste.
3. **Kein Abschalten auf einen DLSS-G-Status hin.** Ein Status ungleich `eOk` würde sonst FG
   abschalten, ohne daß ein neuer Status entsteht — eine Selbstsperre. Streamline erzeugt bei
   Fehlerstatus selbst nichts; wir protokollieren jeden Wechsel. `minWidthOrHeight` sperrt
   weiterhin.
4. **Die CPU wartet genau dann, wenn D3D12 zwei Frames zurückliegt** — der Command-Allocator
   eines Slots darf erst danach zurückgesetzt werden. Gezählt und je Sekunde protokolliert.
5. **`Flush` nach dem D3D11-Signal.** Ohne das Present des Spiels leert nichts mehr die
   D3D11-Warteschlange; die D3D12-Queue würde auf ein Signal warten, das noch im Puffer steht.
6. **Das Overlay bleibt unverändert.** `ResizeBuffers` ohne Größenänderung behält den privaten
   Backbuffer; mit Änderung legt das Overlay seine RTV ohnehin neu an (`Overlay.cpp:107`).
7. **Das Frame-Token wird als Index übergeben** (`slGetNewFrameToken(token, &index)`,
   ProgrammingGuide.md 2.10) — die Warteschlange hält Zahlen, keine Zeiger.
8. **Statt einer Beobachterliste ein zweiter Hörer** (`Upscale::Hooks::Tap`) neben dem `Handler`.
9. **Debug-Ansicht `split`** belegt in Lauf 1, daß `RT_000` an `+0xC5` HUD-los ist: die linke
   Bildhälfte zeigt das HUD-lose Bild, die rechte das fertige.

## Global Constraints

-   Nur **AE 1.11.240**. Jeder Hook wird **nach** `Runtime::IsSupported` angelegt; eine Aufrufstelle
    wird vorher auf `E8` **und** auf die erwartete Ziel-ID geprüft.
-   `/W4 /WX` auf unserem Ziel; nie lockern. Fremde Header werden einzeln eingeklammert
    (`#pragma warning(push, 0)` … `pop`) mit Kommentar, der den Header nennt.
-   **`<Windows.h>`, `<d3d12.h>`, `<dxgi1_6.h>`, `<d3d11_4.h>` und Streamline-Header nur in
    `src/Bridge/Native/`** (ohne PCH übersetzt) und in `src/Upscale/LensFlareDetour.cpp`. Die native
    Insel loggt über `Bridge::Log`, nie über REX. In der Insel keine Bezeichner `near`/`far`
    (Makros aus `windef.h`). Kein `/Zc:__cplusplus`: `sl_reflex.h` bricht bei `__cplusplus == 202302L`
    (`using to_underlying = std::to_underlying;`).
-   Reine Header (`src/Bridge/*.h` außer `SimulationHook.h`, `Log.h`) enthalten nur die
    Standardbibliothek — die native Insel bindet sie ein.
-   Jede sichtbare Zeichenkette über `T("key", "English")`, `.Label` und `.Help`; danach
    `python tools/extract-i18n.py --write`. Kein ImGui in `src/Features/` oder `src/Bridge/`.
-   Bauen: `& "$cmake\cmake.exe" --build --preset FO4` (deployt ins Spiel); Tests:
    `& "$cmake\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure`. In jeder PowerShell
    vorher `$env:VCPKG_ROOT = "C:\vcpkg"; $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"`.
-   Jeder Host-Test wird nach dem Grünwerden gebrochen; vorher benennen, welche Prüfung fällt;
    **erst prüfen, daß der Bau geglückt ist**. Mutationen per Edit setzen und zurücknehmen, nie
    `git checkout <datei>`.
-   Conventional Commits, Titel ≤ 50, Body bei 72, Abschluß
    `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; nach „files were modified by this
    hook" erneut `git add` und committen.
-   Quelltext und HLSL mit Write/Edit schreiben, nie über die Shell.
-   Diagnose je Sekunde (`kLogInterval = 180` Frames), nie einmalig; jede Ablehnung nennt ihren
    Grund.
-   Branch `port/f5-5b-dlss-fg`, Basis `dev` 58bc4ae9.
-   Spielstarts: jeder bekommt eine nummerierte Liste konkreter Handlungen für den Nutzer, mit
    „was schiefgehen kann und was es bedeutet". Dateien im Spielordner ändere ich, nicht er —
    und nur nach Rückfrage.

## Review Focus

1. **Das Spiel legt sein Gerät zweimal an** (doodlum hakt dafür `WindowSizeChanged` aus): der
   zweite Aufruf muß die Brücke entweder wieder aufbauen (erster Proxy schon freigegeben) oder
   sauber ablehnen — nie zwei Swapchains auf ein Fenster. → Task 9, Lauf 1 Schritt 3 (Log).
2. **Minimieren und Alt-Tab** rufen `ResizeBuffers` mit 0×0 bzw. Fenstergröße 0: keine Texturen
   der Größe 0, kein Absturz, Bild nach Rückkehr wieder da. → Task 8 (Code), Lauf 1 Schritt 7.
3. **Schalter im laufenden Spiel an, ohne Neustart**: die Featureseite sagt „Neustart nötig",
   nichts stürzt. → Task 11, Lauf 1 Schritte 1–2.
4. **Exklusives Vollbild geblieben**: Ablehnung mit Grund, Spiel läuft normal. → Task 2
   (Host-Test), Lauf 1 Schritt 3.
5. **Overlay offen, während FG läuft**: FG aus, Overlay bedienbar, danach wieder an. → Task 11,
   Lauf 2 Schritt 5.

---

## Dateien

| Datei                                                                         | Aufgabe                                                      |
| ----------------------------------------------------------------------------- | ------------------------------------------------------------ |
| `cmake/Streamline.cmake` (neu), `.gitignore`                                  | Release-Zip holen, prüfen, Auszug nach `extern/Streamline`   |
| `CMakeLists.txt`                                                              | Streamline, native Insel ohne PCH, Bibliotheken, sechs Tests |
| `src/Settings/Settings.h`, `src/Settings/Store.cpp`                           | `Settings::DefaultFile()`                                    |
| `src/Bridge/StartupSwitch.h/.cpp`, `tests/StartupSwitchTests.cpp`             | Startschalter vor `Settings::Init`                           |
| `src/Bridge/Decision.h/.cpp`, `tests/BridgeDecisionTests.cpp`                 | ob die Brücke entsteht, sonst warum nicht                    |
| `src/Bridge/SlotRing.h`, `tests/SlotRingTests.cpp`                            | Ring aus zwei Slots, Fence-Werte                             |
| `src/Bridge/FrameTokenQueue.h/.cpp`, `tests/FrameTokenQueueTests.cpp`         | Frame-Index vom Spielframe zum Present                       |
| `src/Bridge/CameraConstants.h/.cpp`, `tests/CameraConstantsTests.cpp`         | `sl::Constants` aus der Engine-Kamera                        |
| `src/Bridge/PresentStats.h/.cpp`, `tests/PresentStatsTests.cpp`               | gerendert gegen angezeigt je Sekunde                         |
| `src/Bridge/Bridge.h`                                                         | Schnittstelle zwischen Plugin und nativer Insel              |
| `src/Bridge/Log.h/.cpp`                                                       | Logging der Insel über REX                                   |
| `src/Bridge/Native/NativeSdk.h`, `Streamline.h/.cpp`                          | Windows-SDK- und Streamline-Header, Laden                    |
| `src/Bridge/Native/Presenter.h/.cpp`                                          | D3D12, Swapchain, Fence, Slots, Present                      |
| `src/Bridge/Native/SwapChainProxy.h/.cpp`                                     | was das Spiel als Swapchain bekommt                          |
| `src/Bridge/Native/DeviceHook.cpp`                                            | IAT-Hook, Entscheidung, `Bridge.h`-Funktionen                |
| `src/Bridge/SimulationHook.h/.cpp`                                            | `Main::OnIdle` → Frame-Token, Reflex-Sleep                   |
| `src/Upscale/EngineHooks.h/.cpp`                                              | `Tap`                                                        |
| `src/XSEPlugin.cpp`                                                           | Brücke und Simulations-Hook beim Laden                       |
| `src/Features/FrameGeneration.h/.cpp`, `src/Feature/FeatureSystem.cpp`        | das Feature                                                  |
| `package/Features/FrameGeneration/Shaders/FO4/FrameGeneration/CopyDepth.hlsl` | Tiefe nach `R32_FLOAT`                                       |
| `src/Menu/MenuSystem.h/.cpp`, `src/Menu/PerformancePanel.h/.cpp`              | `IsOverlayOpen`, „angezeigt" in der Ecke                     |
| `package/Features/FrameGeneration/EXTERNAL`, `tools/verify-package.ps1`       | Laufzeitdateien im Addon                                     |

---

## Task 1: `Settings::DefaultFile` und `Bridge::StartupSwitch`

**Files:**

-   Modify: `src/Settings/Settings.h`, `src/Settings/Store.cpp:215-241,470-477`
-   Create: `src/Bridge/StartupSwitch.h`, `src/Bridge/StartupSwitch.cpp`, `tests/StartupSwitchTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock)

**Interfaces:**

-   Produces: `std::filesystem::path Settings::DefaultFile();`
    `struct Bridge::StartupSwitch { bool on; std::string reason; };`
    `Bridge::StartupSwitch Bridge::ParseStartupSwitch(std::string_view);`
    `Bridge::StartupSwitch Bridge::ReadStartupSwitch(const std::filesystem::path&);`

-   [ ] **Step 1: `DefaultFile` öffentlich machen**

In `src/Settings/Settings.h` direkt vor `void Init(const std::filesystem::path& a_file);` einfügen:

```cpp
	/// <Documents>/My Games/<save folder>/F4SE/<plugin>.json, or empty when the
	/// documents folder cannot be resolved. What Init without an argument
	/// loads; public because the frame generation bridge has to read one switch
	/// from it when the game creates its device, long before Init runs.
	[[nodiscard]] std::filesystem::path DefaultFile();
```

In `src/Settings/Store.cpp` die bestehende `void Init()` ersetzen durch:

```cpp
	std::filesystem::path DefaultFile()
	{
		return ResolveDefaultFile();
	}

	void Init()
	{
		const auto file = DefaultFile();
		if (file.empty()) {
			return;
		}
		Init(file);
	}
```

-   [ ] **Step 2: Den Test schreiben**

`tests/StartupSwitchTests.cpp`:

```cpp
#include "Bridge/StartupSwitch.h"

#include <cstdio>
#include <filesystem>
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
}

int main()
{
	using Bridge::ParseStartupSwitch;

	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":true}})").on, "true switches the bridge on");

	const auto off = ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false}})");
	Check(!off.on && off.reason == "FrameGeneration/enabled is false", "false keeps it off and says so");

	Check(ParseStartupSwitch(R"({"DLSS":{"enabled":true}})").reason == "the settings file has no FrameGeneration block",
		"a file written before F5.5b has no block");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"reflex":"on"}})").reason == "FrameGeneration/enabled is missing",
		"a block without the switch");
	Check(ParseStartupSwitch(R"({"FrameGeneration":true})").reason == "FrameGeneration/enabled is missing",
		"a block that is not an object");
	Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":1.0}})").reason == "FrameGeneration/enabled is not true or false",
		"a number is not a switch");
	Check(ParseStartupSwitch("{ not json").reason == "the settings file is not valid JSON", "broken JSON");
	Check(ParseStartupSwitch("[]").reason == "the settings file is not valid JSON", "an array at the root");
	Check(ParseStartupSwitch(R"({"Menu":{"key":45.0},"FrameGeneration":{"reflex":"boost","enabled":true}})").on,
		"other blocks and keys around it do not matter");

	const auto directory = std::filesystem::temp_directory_path();
	const auto missing = Bridge::ReadStartupSwitch(directory / "fo4cs-no-such-settings.json");
	Check(!missing.on && missing.reason == "no settings file yet", "no file means off");
	Check(Bridge::ReadStartupSwitch({}).reason == "the settings file could not be located", "no path means off");

	const auto file = directory / "fo4cs-startup-switch.json";
	{
		std::ofstream out(file, std::ios::binary | std::ios::trunc);
		out << "{\n    \"FrameGeneration\": {\n        \"enabled\": true\n    }\n}\n";
	}
	Check(Bridge::ReadStartupSwitch(file).on, "the file route reads what the string route reads");
	std::filesystem::remove(file);

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

In `CMakeLists.txt` nach dem Block `add_test(NAME Camera COMMAND CameraTests)` anfügen:

```cmake
    add_executable(
        StartupSwitchTests
        "${CMAKE_SOURCE_DIR}/tests/StartupSwitchTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Bridge/StartupSwitch.cpp"
    )

    target_include_directories(
        StartupSwitchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(StartupSwitchTests PRIVATE cxx_std_23)
    target_precompile_headers(
        StartupSwitchTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(StartupSwitchTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            StartupSwitchTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME StartupSwitch COMMAND StartupSwitchTests)
```

-   [ ] **Step 3: Bauen, Fehlschlag sehen**

Run: `& "$cmake\cmake.exe" --build --preset FO4 --target StartupSwitchTests`
Expected: FAIL — `Bridge/StartupSwitch.h` nicht gefunden.

-   [ ] **Step 4: Implementieren**

`src/Bridge/StartupSwitch.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Bridge
{
	/// FrameGeneration/enabled as the settings file says it at load time. The
	/// game creates its device long before kGameDataReady, and whether the
	/// bridge exists is decided then - before Settings::Init has run and
	/// before any feature has declared anything. So this reads the one key
	/// itself instead of going through the store.
	struct StartupSwitch
	{
		bool on{ false };
		/// Why it is off, for the log and the feature page; empty when on.
		std::string reason;
	};

	[[nodiscard]] StartupSwitch ParseStartupSwitch(std::string_view a_json);
	[[nodiscard]] StartupSwitch ReadStartupSwitch(const std::filesystem::path& a_file);
}
```

`src/Bridge/StartupSwitch.cpp`:

```cpp
#include "Bridge/StartupSwitch.h"

#include <glaze/glaze.hpp>

#include <fstream>
#include <sstream>

namespace Bridge
{
	namespace
	{
		constexpr std::string_view kBlock = "FrameGeneration";
		constexpr std::string_view kKey = "enabled";
	}

	StartupSwitch ParseStartupSwitch(std::string_view a_json)
	{
		// A copy: glaze reads null-terminated buffers by default, and a view
		// promises no terminator.
		const std::string buffer{ a_json };
		glz::generic root{};
		if (glz::read_json(root, buffer) || !root.is_object()) {
			return { false, "the settings file is not valid JSON" };
		}
		if (!root.contains(kBlock)) {
			return { false, "the settings file has no FrameGeneration block" };
		}
		const auto& block = root[kBlock];
		if (!block.is_object() || !block.contains(kKey)) {
			return { false, "FrameGeneration/enabled is missing" };
		}
		const auto& value = block[kKey];
		if (!value.is_boolean()) {
			return { false, "FrameGeneration/enabled is not true or false" };
		}
		if (!value.get<bool>()) {
			return { false, "FrameGeneration/enabled is false" };
		}
		return { true, {} };
	}

	StartupSwitch ReadStartupSwitch(const std::filesystem::path& a_file)
	{
		if (a_file.empty()) {
			return { false, "the settings file could not be located" };
		}
		std::ifstream stream(a_file, std::ios::binary);
		if (!stream) {
			return { false, "no settings file yet" };
		}
		std::ostringstream text;
		text << stream.rdbuf();
		return ParseStartupSwitch(text.str());
	}
}
```

-   [ ] **Step 5: Bauen und testen**

Run: `& "$cmake\cmake.exe" --build --preset FO4 --target StartupSwitchTests` — Bau erfolgreich
prüfen, dann `& "$cmake\ctest.exe" --test-dir build/FO4 -C Release -R StartupSwitch --output-on-failure`
Expected: `all checks passed`.

-   [ ] **Step 6: Mutation**

`if (!value.get<bool>()) {` per Edit zu `if (false) {`. Vorher benannt: „false keeps it off and
says so" fällt. Bauen (Erfolg prüfen), Test laufen lassen, FAIL sehen, per Edit zurück, grün.

-   [ ] **Step 7: Commit**

```bash
git add src/Settings/Settings.h src/Settings/Store.cpp src/Bridge/StartupSwitch.h src/Bridge/StartupSwitch.cpp tests/StartupSwitchTests.cpp CMakeLists.txt
git commit -m "feat: read the frame generation switch early" -m "The game creates its device long before Settings::Init. The bridge
reads FrameGeneration/enabled from the file itself, with a named
reason whenever the answer is no.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 2: `Bridge::Decide`

**Files:**

-   Create: `src/Bridge/Decision.h`, `src/Bridge/Decision.cpp`, `tests/BridgeDecisionTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock)

**Interfaces:**

-   Produces: `struct Bridge::DecisionFacts`, `struct Bridge::Verdict { bool build; std::string reason; }`,
    `Bridge::Verdict Bridge::Decide(const DecisionFacts&)`, `Bridge::kVendorNvidia`.

-   [ ] **Step 1: Den Test schreiben**

`tests/BridgeDecisionTests.cpp`:

```cpp
#include "Bridge/Decision.h"

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

	Bridge::DecisionFacts Good()
	{
		Bridge::DecisionFacts facts{};
		facts.switchOn = true;
		facts.swapChainRequested = true;
		facts.windowed = true;
		facts.vendorId = Bridge::kVendorNvidia;
		return facts;
	}
}

int main()
{
	using Bridge::Decide;

	const auto yes = Decide(Good());
	Check(yes.build && yes.reason.empty(), "everything in place builds the bridge");

	auto facts = Good();
	facts.switchOn = false;
	facts.switchReason = "FrameGeneration/enabled is false";
	Check(Decide(facts).reason ==
			  "the switch was off when the game started (FrameGeneration/enabled is false); a restart applies it",
		"switch off, with the switch's own reason inside");

	facts = Good();
	facts.swapChainRequested = false;
	Check(Decide(facts).reason == "the game created a device without a swap chain", "no swap chain asked for");

	facts = Good();
	facts.windowed = false;
	Check(Decide(facts).reason == "exclusive fullscreen; frame generation needs borderless windowed mode",
		"exclusive fullscreen is refused by name");

	facts = Good();
	facts.vendorId = 0x1002;
	Check(Decide(facts).reason == "not an NVIDIA adapter (vendor 0x1002)", "another vendor, with its id");

	facts = Good();
	facts.enb = true;
	Check(Decide(facts).reason == "ENB is loaded", "ENB");

	facts = Good();
	facts.foreignModule = "AAAFrameGeneration.dll";
	Check(Decide(facts).reason == "AAAFrameGeneration.dll is loaded", "a foreign frame generation mod, by name");

	facts = Good();
	facts.switchOn = false;
	facts.switchReason = "no settings file yet";
	facts.windowed = false;
	facts.enb = true;
	const auto first = Decide(facts);
	Check(!first.build && first.reason.starts_with("the switch was off"),
		"the first failed check is the one reported");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

CMake-Block wie in Task 1, mit `BridgeDecisionTests`, Quellen
`tests/BridgeDecisionTests.cpp` und `src/Bridge/Decision.cpp`, `add_test(NAME BridgeDecision COMMAND BridgeDecisionTests)`:

```cmake
    add_executable(
        BridgeDecisionTests
        "${CMAKE_SOURCE_DIR}/tests/BridgeDecisionTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Bridge/Decision.cpp"
    )

    target_include_directories(
        BridgeDecisionTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(BridgeDecisionTests PRIVATE cxx_std_23)
    target_precompile_headers(
        BridgeDecisionTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(BridgeDecisionTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            BridgeDecisionTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME BridgeDecision COMMAND BridgeDecisionTests)
```

-   [ ] **Step 2: Bauen, Fehlschlag sehen**

Run: `& "$cmake\cmake.exe" --build --preset FO4 --target BridgeDecisionTests`
Expected: FAIL — `Bridge/Decision.h` nicht gefunden.

-   [ ] **Step 3: Implementieren**

`src/Bridge/Decision.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Bridge
{
	inline constexpr std::uint32_t kVendorNvidia = 0x10DE;

	/// What the device hook knows before it touches Streamline. Streamline's
	/// own refusals (not loadable, not signed, slInit, no DLSS-G on this
	/// adapter) come afterwards, from the hook itself.
	struct DecisionFacts
	{
		bool switchOn{ false };
		std::string_view switchReason;
		bool swapChainRequested{ false };
		bool windowed{ false };
		std::uint32_t vendorId{ 0 };
		bool enb{ false };
		/// Empty when no other frame generation or upscaling module is loaded.
		std::string_view foreignModule;
	};

	struct Verdict
	{
		bool build{ false };
		std::string reason;
	};

	/// The first check that fails names the verdict; the order is the order
	/// a player would fix things in.
	[[nodiscard]] Verdict Decide(const DecisionFacts& a_facts);
}
```

`src/Bridge/Decision.cpp`:

```cpp
#include "Bridge/Decision.h"

#include <format>

namespace Bridge
{
	Verdict Decide(const DecisionFacts& a_facts)
	{
		if (!a_facts.switchOn) {
			return { false, std::format("the switch was off when the game started ({}); a restart applies it", a_facts.switchReason) };
		}
		if (!a_facts.swapChainRequested) {
			return { false, "the game created a device without a swap chain" };
		}
		if (!a_facts.windowed) {
			return { false, "exclusive fullscreen; frame generation needs borderless windowed mode" };
		}
		if (a_facts.vendorId != kVendorNvidia) {
			return { false, std::format("not an NVIDIA adapter (vendor 0x{:04X})", a_facts.vendorId) };
		}
		if (a_facts.enb) {
			return { false, "ENB is loaded" };
		}
		if (!a_facts.foreignModule.empty()) {
			return { false, std::format("{} is loaded", a_facts.foreignModule) };
		}
		return { true, {} };
	}
}
```

-   [ ] **Step 4: Bauen und testen** — Bau prüfen, `ctest … -R BridgeDecision`, Expected `all checks passed`.

-   [ ] **Step 5: Mutation** — die Prüfung `if (!a_facts.windowed) {` per Edit zu `if (false) {`.
        Vorher benannt: „exclusive fullscreen is refused by name" fällt. Bauen, FAIL, zurück, grün.

-   [ ] **Step 6: Commit**

```bash
git add src/Bridge/Decision.h src/Bridge/Decision.cpp tests/BridgeDecisionTests.cpp CMakeLists.txt
git commit -m "feat: decide whether the bridge is built" -m "Switch, swap chain, windowed mode, vendor, ENB and foreign mods, in
the order a player would fix them, each refusal with its reason.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 3: `Bridge::SlotRing`

**Files:**

-   Create: `src/Bridge/SlotRing.h`, `tests/SlotRingTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock, nur die Testdatei als Quelle)

**Interfaces:**

-   Produces: `class Bridge::SlotRing { kSlots; Current(); ReleaseValue(); Reusable(uint64); Submit(uint64); Reset(); }`

-   [ ] **Step 1: Den Test schreiben**

`tests/SlotRingTests.cpp`:

```cpp
#include "Bridge/SlotRing.h"

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
	Bridge::SlotRing ring;
	Check(ring.Current() == 0 && ring.ReleaseValue() == 0, "a fresh ring starts at slot 0 with nothing to wait for");
	Check(ring.Reusable(0), "and its allocator may be reset at once");

	ring.Submit(5);
	Check(ring.Current() == 1 && ring.ReleaseValue() == 0, "the second slot was never handed over either");

	ring.Submit(9);
	Check(ring.Current() == 0 && ring.ReleaseValue() == 5, "back at slot 0, D3D11 waits for what D3D12 signals after reading it");
	Check(!ring.Reusable(4), "before that value completes the allocator is still in use");
	Check(ring.Reusable(5), "once it has, it is free");

	ring.Submit(12);
	Check(ring.Current() == 1 && ring.ReleaseValue() == 9, "slot 1 waits for its own value, not for slot 0's");

	ring.Reset();
	Check(ring.Current() == 0 && ring.ReleaseValue() == 0 && ring.Reusable(0), "Reset forgets every handed-over value");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

CMake-Block wie in Task 1 mit `SlotRingTests`, einziger Quelle `tests/SlotRingTests.cpp`,
`add_test(NAME SlotRing COMMAND SlotRingTests)`.

-   [ ] **Step 2: Bauen, Fehlschlag sehen** — FAIL, `Bridge/SlotRing.h` fehlt.

-   [ ] **Step 3: Implementieren**

`src/Bridge/SlotRing.h`:

```cpp
#pragma once

#include <array>
#include <cstdint>

namespace Bridge
{
	/// Which of two shared slots a frame writes, and which fence value frees
	/// it. D3D11 writes slot k, D3D12 reads it; D3D11 may write k again only
	/// once D3D12 has signalled the value it was given when it took k. Every
	/// one of those waits is on the GPU. The one exception is the CPU's: the
	/// D3D12 command allocator of k may be reset only once that value has
	/// completed, which with two slots means D3D12 is two frames behind.
	class SlotRing
	{
	public:
		static constexpr std::uint32_t kSlots = 2;

		[[nodiscard]] std::uint32_t Current() const noexcept { return _current; }

		/// What D3D11 waits for before writing Current(); 0 when the slot was
		/// never handed over.
		[[nodiscard]] std::uint64_t ReleaseValue() const noexcept { return _release[_current]; }

		[[nodiscard]] bool Reusable(std::uint64_t a_completed) const noexcept { return a_completed >= _release[_current]; }

		/// D3D12 will signal a_value once it is done with Current(); the next
		/// frame writes the other slot.
		void Submit(std::uint64_t a_value) noexcept
		{
			_release[_current] = a_value;
			_current = (_current + 1) % kSlots;
		}

		void Reset() noexcept
		{
			_current = 0;
			_release = {};
		}

	private:
		std::uint32_t _current{ 0 };
		std::array<std::uint64_t, kSlots> _release{};
	};
}
```

-   [ ] **Step 4: Bauen und testen** — grün.

-   [ ] **Step 5: Mutation** — in `Submit` die Zeile `_release[_current] = a_value;` per Edit
        entfernen. Vorher benannt: „back at slot 0, D3D11 waits …" und „before that value completes …"
        fallen. Bauen, FAIL, zurück, grün.

-   [ ] **Step 6: Commit** — `feat: two-slot ring for the bridge's handover`, Body: „Which slot a
        frame writes and which fence value frees it; GPU waits only, except the allocator reset."

---

## Task 4: `Bridge::FrameTokenQueue`

**Files:**

-   Create: `src/Bridge/FrameTokenQueue.h`, `src/Bridge/FrameTokenQueue.cpp`, `tests/FrameTokenQueueTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock mit beiden Quellen)

**Interfaces:**

-   Produces: `class Bridge::FrameTokenQueue { kInFlight = 3; Push(uint32); std::optional<uint32> Pop(); uint64 Dropped() const; Clear(); }`

-   [ ] **Step 1: Den Test schreiben**

`tests/FrameTokenQueueTests.cpp`:

```cpp
#include "Bridge/FrameTokenQueue.h"

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
	Bridge::FrameTokenQueue queue;
	Check(!queue.Pop().has_value(), "an empty queue hands out nothing, never an invented index");

	queue.Push(10);
	queue.Push(11);
	Check(queue.Pop() == 10u, "the oldest frame comes first");
	Check(queue.Pop() == 11u, "then the next");
	Check(!queue.Pop().has_value(), "and then nothing");

	for (std::uint32_t index = 20; index < 25; ++index) {
		queue.Push(index);
	}
	Check(queue.Dropped() == 2, "beyond three in flight the oldest are dropped and counted");
	Check(queue.Pop() == 22u, "what remains starts at the oldest kept");
	Check(queue.Pop() == 23u && queue.Pop() == 24u, "in order");

	queue.Push(30);
	queue.Clear();
	Check(!queue.Pop().has_value() && queue.Dropped() == 2, "Clear empties the queue and keeps the count");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

CMake-Block wie in Task 1 mit `FrameTokenQueueTests`, Quellen `tests/FrameTokenQueueTests.cpp`
und `src/Bridge/FrameTokenQueue.cpp`, `add_test(NAME FrameTokenQueue COMMAND FrameTokenQueueTests)`.

-   [ ] **Step 2: Bauen, Fehlschlag sehen.**

-   [ ] **Step 3: Implementieren**

`src/Bridge/FrameTokenQueue.h`:

```cpp
#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace Bridge
{
	/// Streamline frame indices from the start of the game's frame (input
	/// polling in Main::OnIdle) to where that frame is rendered and presented.
	/// The log of 2026-10-04 shows both on one thread, but not their order
	/// within one loop iteration: a queue serves either order, a single
	/// variable only one. The index, not the token pointer, travels:
	/// slGetNewFrameToken gives the same frame's token back for its index
	/// (ProgrammingGuide.md 2.10).
	class FrameTokenQueue
	{
	public:
		/// Streamline expects no more than three frames in flight
		/// (sl_core_api.h, slGetNewFrameToken).
		static constexpr std::size_t kInFlight = 3;

		void Push(std::uint32_t a_index) noexcept;
		[[nodiscard]] std::optional<std::uint32_t> Pop() noexcept;
		[[nodiscard]] std::uint64_t Dropped() const noexcept;
		void Clear() noexcept;

	private:
		mutable std::mutex _lock;
		std::deque<std::uint32_t> _indices;
		std::uint64_t _dropped{ 0 };
	};
}
```

`src/Bridge/FrameTokenQueue.cpp`:

```cpp
#include "Bridge/FrameTokenQueue.h"

namespace Bridge
{
	void FrameTokenQueue::Push(std::uint32_t a_index) noexcept
	{
		const std::scoped_lock lock{ _lock };
		_indices.push_back(a_index);
		while (_indices.size() > kInFlight) {
			_indices.pop_front();
			++_dropped;
		}
	}

	std::optional<std::uint32_t> FrameTokenQueue::Pop() noexcept
	{
		const std::scoped_lock lock{ _lock };
		if (_indices.empty()) {
			return std::nullopt;
		}
		const auto index = _indices.front();
		_indices.pop_front();
		return index;
	}

	std::uint64_t FrameTokenQueue::Dropped() const noexcept
	{
		const std::scoped_lock lock{ _lock };
		return _dropped;
	}

	void FrameTokenQueue::Clear() noexcept
	{
		const std::scoped_lock lock{ _lock };
		_indices.clear();
	}
}
```

-   [ ] **Step 4: Bauen und testen** — grün.

-   [ ] **Step 5: Mutation** — `_indices.pop_front();` in `Pop` zu `_indices.pop_back();` und
        `_indices.front()` zu `_indices.back()`. Vorher benannt: „the oldest frame comes first" fällt.

-   [ ] **Step 6: Commit** — `feat: carry frame indices to the presenter`.

---

## Task 5: `Bridge::CameraConstants`

**Files:**

-   Create: `src/Bridge/CameraConstants.h`, `src/Bridge/CameraConstants.cpp`, `tests/CameraConstantsTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock mit beiden Quellen)

**Interfaces:**

-   Produces: `using Bridge::Matrix = std::array<std::array<float,4>,4>;`
    `struct Bridge::CameraFrame { Matrix worldToClip; std::array<float,3> position; };`
    `struct Bridge::Constants { … }` (Felder unten), `Invert`, `Multiply`, `Transpose`,
    `JitterFromEngineOffset(float, float, uint32, uint32) -> std::array<float,2>`,
    `BuildConstants(const CameraFrame&, const CameraFrame*, float a_nearPlane, float a_farPlane, std::array<float,2> a_jitter, bool a_reset) -> std::optional<Constants>`.

Konvention: die Engine rechnet `clip = M · (p, 1)` (Spaltenvektor, `Render::ClipFromWorldToCam`).
Streamline erwartet Zeilenvektoren links (`clipToPrevClip = clipToView * viewToViewPrev *
viewToClipPrev`, `sl_consts.h:194`). Jede Matrix in `Constants` ist deshalb die **Transponierte**
ihrer Spaltenform.

-   [ ] **Step 1: Den Test schreiben**

`tests/CameraConstantsTests.cpp`:

```cpp
#include "Bridge/CameraConstants.h"

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

	bool Near(float a_left, float a_right, float a_tolerance = 1e-4f)
	{
		return std::abs(a_left - a_right) <= a_tolerance;
	}

	bool Near(const Bridge::Matrix& a_left, const Bridge::Matrix& a_right, float a_tolerance = 1e-4f)
	{
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				if (!Near(a_left[i][j], a_right[i][j], a_tolerance)) {
					return false;
				}
			}
		}
		return true;
	}

	using Vec3 = std::array<float, 3>;

	float Dot(const Vec3& a_left, const Vec3& a_right)
	{
		return a_left[0] * a_right[0] + a_left[1] * a_right[1] + a_left[2] * a_right[2];
	}

	/// World to view written out by hand, independent of the code under
	/// test: rows right, up, forward, translation minus axis dot position.
	Bridge::Matrix View(const Vec3& a_right, const Vec3& a_up, const Vec3& a_forward, const Vec3& a_position)
	{
		return { { { a_right[0], a_right[1], a_right[2], -Dot(a_right, a_position) },
			{ a_up[0], a_up[1], a_up[2], -Dot(a_up, a_position) },
			{ a_forward[0], a_forward[1], a_forward[2], -Dot(a_forward, a_position) },
			{ 0.0f, 0.0f, 0.0f, 1.0f } } };
	}

	/// Fallout 4's projection has no far plane: z = w - near (Render::Camera.h).
	Bridge::Matrix Projection(float a_scaleX, float a_scaleY, float a_nearPlane)
	{
		return { { { a_scaleX, 0.0f, 0.0f, 0.0f },
			{ 0.0f, a_scaleY, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f, -a_nearPlane },
			{ 0.0f, 0.0f, 1.0f, 0.0f } } };
	}

	std::array<float, 4> Apply(const Bridge::Matrix& a_columnForm, const std::array<float, 4>& a_point)
	{
		std::array<float, 4> out{};
		for (int i = 0; i < 4; ++i) {
			for (int j = 0; j < 4; ++j) {
				out[i] += a_columnForm[i][j] * a_point[j];
			}
		}
		return out;
	}

	/// A row vector on the left, as Streamline multiplies.
	std::array<float, 4> ApplyRow(const std::array<float, 4>& a_point, const Bridge::Matrix& a_rowForm)
	{
		std::array<float, 4> out{};
		for (int j = 0; j < 4; ++j) {
			for (int i = 0; i < 4; ++i) {
				out[j] += a_point[i] * a_rowForm[i][j];
			}
		}
		return out;
	}
}

int main()
{
	using namespace Bridge;

	const Matrix identity{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
	const Matrix sample{ { { 2, 0, 0, 1 }, { 0, 3, 0, 2 }, { 0, 0, 4, 3 }, { 0, 0, 0, 1 } } };
	const auto inverse = Invert(sample);
	Check(inverse.has_value() && Near(Multiply(sample, *inverse), identity), "Invert gives a matrix that multiplies back to the identity");
	const Matrix singular{ { { 1, 2, 3, 4 }, { 2, 4, 6, 8 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } };
	Check(!Invert(singular).has_value(), "a singular matrix is refused, not inverted into nonsense");

	const Vec3 right{ 1, 0, 0 };
	const Vec3 up{ 0, 0, 1 };
	const Vec3 forward{ 0, 1, 0 };
	const Vec3 position{ 100, -50, 20 };
	const float scaleX = 1.0f;
	const float scaleY = 16.0f / 9.0f;
	const auto projection = Projection(scaleX, scaleY, 10.0f);
	const CameraFrame current{ Multiply(projection, View(right, up, forward, position)), position };

	const auto built = BuildConstants(current, nullptr, 10.0f, 353840.0f, { 0.25f, -0.125f }, true);
	Check(built.has_value(), "a plain camera builds");
	if (built) {
		Check(Near(built->cameraViewToClip, Transpose(projection)), "the projection comes back, transposed for Streamline");
		Check(Near(Multiply(built->cameraViewToClip, built->clipToCameraView), identity), "clipToCameraView is its inverse");
		Check(Near(built->cameraFOV, 2.0f * std::atan(1.0f / scaleY)), "the vertical field of view from the projection");
		Check(Near(built->cameraAspectRatio, scaleY / scaleX), "the aspect ratio from the projection");
		Check(Near(built->cameraPos[0], 100.0f) && Near(built->cameraPos[1], -50.0f) && Near(built->cameraPos[2], 20.0f), "the camera position");
		Check(Near(built->cameraRight[0], 1.0f) && Near(built->cameraUp[2], 1.0f) && Near(built->cameraFwd[1], 1.0f), "the three axes");
		Check(Near(built->clipToPrevClip, identity) && Near(built->prevClipToClip, identity), "without a previous frame there is no motion");
		Check(Near(built->jitterOffset[0], 0.25f) && Near(built->jitterOffset[1], -0.125f), "the jitter is passed through");
		Check(built->reset && Near(built->cameraNear, 10.0f) && Near(built->cameraFar, 353840.0f), "reset and the planes");
		Check(Near(built->diagnosticResidue, 0.0f), "a consistent camera leaves no translation in the projection");
	}

	const Vec3 moved{ 100, -45, 20 };
	const CameraFrame previous{ Multiply(projection, View(right, up, forward, moved)), moved };
	const auto motion = BuildConstants(current, &previous, 10.0f, 353840.0f, { 0.0f, 0.0f }, false);
	Check(motion.has_value(), "two frames build");
	if (motion) {
		const std::array<float, 4> world{ 130, 200, 35, 1 };
		const auto clipNow = Apply(current.worldToClip, world);
		const auto clipThen = Apply(previous.worldToClip, world);
		const auto mapped = ApplyRow(clipNow, motion->clipToPrevClip);
		Check(Near(mapped[0], clipThen[0], 1e-2f) && Near(mapped[1], clipThen[1], 1e-2f) && Near(mapped[3], clipThen[3], 1e-2f),
			"clipToPrevClip takes a point's clip position now to where it was");
		Check(Near(Multiply(motion->clipToPrevClip, motion->prevClipToClip), identity, 1e-3f), "prevClipToClip undoes it");
	}

	// A jittered projection tilts row zero towards forward; the axes must
	// still come out orthonormal.
	auto jittered = current;
	for (int j = 0; j < 4; ++j) {
		jittered.worldToClip[0][j] += 0.0004f * jittered.worldToClip[3][j];
	}
	const auto tilted = BuildConstants(jittered, nullptr, 10.0f, 353840.0f, { 0.0f, 0.0f }, false);
	Check(tilted.has_value() && Near(Dot(tilted->cameraRight, tilted->cameraFwd), 0.0f) && Near(tilted->cameraRight[0], 1.0f),
		"right stays perpendicular to forward when row zero leans on it");

	const CameraFrame flat{ Matrix{}, position };
	Check(!BuildConstants(flat, nullptr, 10.0f, 353840.0f, { 0.0f, 0.0f }, false).has_value(), "a zero matrix is refused");

	const auto jitter = JitterFromEngineOffset(2.0f * -0.25f / 2560.0f, 2.0f * -0.125f / 1440.0f, 2560, 1440);
	Check(Near(jitter[0], 0.25f) && Near(jitter[1], -0.125f), "the engine offset turns back into DLSS's jitter (Upscale::ToEngineOffset inverted)");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

CMake-Block wie in Task 1 mit `CameraConstantsTests`, Quellen `tests/CameraConstantsTests.cpp`
und `src/Bridge/CameraConstants.cpp`, `add_test(NAME CameraConstants COMMAND CameraConstantsTests)`.

-   [ ] **Step 2: Bauen, Fehlschlag sehen.**

-   [ ] **Step 3: Implementieren**

`src/Bridge/CameraConstants.h`:

```cpp
#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace Bridge
{
	using Matrix = std::array<std::array<float, 4>, 4>;

	/// One frame of the engine's camera in its own convention: clip =
	/// worldToClip * (p, 1), the point on the right (Render::ClipFromWorldToCam).
	/// worldToClip is NiCamera::worldToCam of Main::WorldRootCamera, world to
	/// clip whole, whatever its name says.
	struct CameraFrame
	{
		Matrix worldToClip{};
		std::array<float, 3> position{};
	};

	/// Field for field what sl::Constants needs, already in Streamline's
	/// convention - a row vector on the left (sl_consts.h, clipToPrevClip),
	/// so each matrix is the transpose of its column form.
	struct Constants
	{
		Matrix cameraViewToClip{};
		Matrix clipToCameraView{};
		Matrix clipToPrevClip{};
		Matrix prevClipToClip{};
		std::array<float, 2> jitterOffset{};
		/// RT_029 holds uv units, already normalised (F5.5a, first run).
		std::array<float, 2> mvecScale{ 1.0f, 1.0f };
		std::array<float, 3> cameraPos{};
		std::array<float, 3> cameraUp{};
		std::array<float, 3> cameraRight{};
		std::array<float, 3> cameraFwd{};
		float cameraNear{ 0.0f };
		float cameraFar{ 0.0f };
		/// Vertical, in radians.
		float cameraFOV{ 0.0f };
		float cameraAspectRatio{ 0.0f };
		bool reset{ false };
		/// Largest translation left in the derived projection; near zero when
		/// the position and the matrix describe the same camera. Logged, never
		/// sent: the first run shows whether NiCamera's translation is the one
		/// the matrix was built from.
		float diagnosticResidue{ 0.0f };
	};

	[[nodiscard]] std::optional<Matrix> Invert(const Matrix& a_matrix) noexcept;
	[[nodiscard]] Matrix Multiply(const Matrix& a_left, const Matrix& a_right) noexcept;
	[[nodiscard]] Matrix Transpose(const Matrix& a_matrix) noexcept;

	/// In render pixels from BSGraphics::State::offsetX/Y: the inverse of
	/// Upscale::ToEngineOffset, relative to the display size like it.
	[[nodiscard]] std::array<float, 2> JitterFromEngineOffset(
		float a_offsetX,
		float a_offsetY,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight) noexcept;

	/// Nothing when a matrix cannot be inverted or the axes degenerate.
	/// Without a previous frame (the first one, or after a reset) the current
	/// one stands for both.
	[[nodiscard]] std::optional<Constants> BuildConstants(
		const CameraFrame& a_current,
		const CameraFrame* a_previous,
		float a_nearPlane,
		float a_farPlane,
		std::array<float, 2> a_jitter,
		bool a_reset) noexcept;
}
```

`src/Bridge/CameraConstants.cpp`:

```cpp
#include "Bridge/CameraConstants.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Bridge
{
	namespace
	{
		using Vec3 = std::array<float, 3>;

		constexpr float kDegenerate = 1e-6f;

		float Dot(const Vec3& a_left, const Vec3& a_right) noexcept
		{
			return a_left[0] * a_right[0] + a_left[1] * a_right[1] + a_left[2] * a_right[2];
		}

		Vec3 Scaled(const Vec3& a_vector, float a_factor) noexcept
		{
			return { a_vector[0] * a_factor, a_vector[1] * a_factor, a_vector[2] * a_factor };
		}

		Vec3 Minus(const Vec3& a_left, const Vec3& a_right) noexcept
		{
			return { a_left[0] - a_right[0], a_left[1] - a_right[1], a_left[2] - a_right[2] };
		}

		std::optional<Vec3> Normalised(const Vec3& a_vector) noexcept
		{
			const auto length = std::sqrt(Dot(a_vector, a_vector));
			if (!(length > kDegenerate)) {
				return std::nullopt;
			}
			return Scaled(a_vector, 1.0f / length);
		}

		Vec3 Row(const Matrix& a_matrix, std::size_t a_row) noexcept
		{
			return { a_matrix[a_row][0], a_matrix[a_row][1], a_matrix[a_row][2] };
		}
	}

	Matrix Multiply(const Matrix& a_left, const Matrix& a_right) noexcept
	{
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				for (std::size_t k = 0; k < 4; ++k) {
					out[i][j] += a_left[i][k] * a_right[k][j];
				}
			}
		}
		return out;
	}

	Matrix Transpose(const Matrix& a_matrix) noexcept
	{
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				out[i][j] = a_matrix[j][i];
			}
		}
		return out;
	}

	std::optional<Matrix> Invert(const Matrix& a_matrix) noexcept
	{
		// Gauss-Jordan with partial pivoting, in double: a projection with
		// no far plane has entries far apart in magnitude.
		std::array<std::array<double, 8>, 4> work{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				work[i][j] = a_matrix[i][j];
			}
			work[i][4 + i] = 1.0;
		}
		for (std::size_t column = 0; column < 4; ++column) {
			std::size_t pivot = column;
			for (std::size_t row = column + 1; row < 4; ++row) {
				if (std::abs(work[row][column]) > std::abs(work[pivot][column])) {
					pivot = row;
				}
			}
			if (!(std::abs(work[pivot][column]) > 1e-12)) {
				return std::nullopt;
			}
			std::swap(work[pivot], work[column]);
			const auto divisor = work[column][column];
			for (auto& value : work[column]) {
				value /= divisor;
			}
			for (std::size_t row = 0; row < 4; ++row) {
				if (row == column) {
					continue;
				}
				const auto factor = work[row][column];
				for (std::size_t k = 0; k < 8; ++k) {
					work[row][k] -= factor * work[column][k];
				}
			}
		}
		Matrix out{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				out[i][j] = static_cast<float>(work[i][4 + j]);
			}
		}
		return out;
	}

	std::array<float, 2> JitterFromEngineOffset(
		float a_offsetX,
		float a_offsetY,
		std::uint32_t a_displayWidth,
		std::uint32_t a_displayHeight) noexcept
	{
		return { -a_offsetX * static_cast<float>(a_displayWidth) / 2.0f, a_offsetY * static_cast<float>(a_displayHeight) / 2.0f };
	}

	std::optional<Constants> BuildConstants(
		const CameraFrame& a_current,
		const CameraFrame* a_previous,
		float a_nearPlane,
		float a_farPlane,
		std::array<float, 2> a_jitter,
		bool a_reset) noexcept
	{
		const auto& world = a_current.worldToClip;

		// Row three is forward, because w is forward dotted with the point
		// (Render::FogCameraFromMatrix). Rows zero and one are the scaled
		// right and up, plus whatever jitter leaned them towards forward;
		// Gram-Schmidt takes that lean out.
		const auto forward = Normalised(Row(world, 3));
		if (!forward) {
			return std::nullopt;
		}
		const auto rowRight = Row(world, 0);
		const auto right = Normalised(Minus(rowRight, Scaled(*forward, Dot(rowRight, *forward))));
		if (!right) {
			return std::nullopt;
		}
		const auto rowUp = Row(world, 1);
		const auto up = Normalised(Minus(Minus(rowUp, Scaled(*forward, Dot(rowUp, *forward))), Scaled(*right, Dot(rowUp, *right))));
		if (!up) {
			return std::nullopt;
		}

		const auto& position = a_current.position;
		Matrix viewToWorld{};
		for (std::size_t i = 0; i < 3; ++i) {
			viewToWorld[i][0] = (*right)[i];
			viewToWorld[i][1] = (*up)[i];
			viewToWorld[i][2] = (*forward)[i];
			viewToWorld[i][3] = position[i];
		}
		viewToWorld[3] = { 0.0f, 0.0f, 0.0f, 1.0f };

		const auto viewToClip = Multiply(world, viewToWorld);
		const auto clipToView = Invert(viewToClip);
		const auto clipToWorld = Invert(world);
		if (!clipToView || !clipToWorld) {
			return std::nullopt;
		}
		const auto& previous = a_previous != nullptr ? a_previous->worldToClip : world;
		const auto clipToPrevious = Multiply(previous, *clipToWorld);
		const auto previousToClip = Invert(clipToPrevious);
		if (!previousToClip) {
			return std::nullopt;
		}

		const auto scaleX = viewToClip[0][0];
		const auto scaleY = viewToClip[1][1];
		if (!(std::abs(scaleX) > kDegenerate) || !(std::abs(scaleY) > kDegenerate)) {
			return std::nullopt;
		}

		Constants out{};
		out.cameraViewToClip = Transpose(viewToClip);
		out.clipToCameraView = Transpose(*clipToView);
		out.clipToPrevClip = Transpose(clipToPrevious);
		out.prevClipToClip = Transpose(*previousToClip);
		out.jitterOffset = a_jitter;
		out.cameraPos = position;
		out.cameraRight = *right;
		out.cameraUp = *up;
		out.cameraFwd = *forward;
		out.cameraNear = a_nearPlane;
		out.cameraFar = a_farPlane;
		out.cameraFOV = 2.0f * std::atan(1.0f / scaleY);
		out.cameraAspectRatio = scaleY / scaleX;
		out.reset = a_reset;
		out.diagnosticResidue = std::max({ std::abs(viewToClip[0][3]), std::abs(viewToClip[1][3]), std::abs(viewToClip[3][3]) });
		return out;
	}
}
```

-   [ ] **Step 4: Bauen und testen** — grün.

-   [ ] **Step 5: Mutation** — `const auto clipToPrevious = Multiply(previous, *clipToWorld);` per
        Edit zu `Multiply(*clipToWorld, previous)`. Vorher benannt: „clipToPrevClip takes a point's clip
        position now to where it was" fällt. Zweite Mutation: `out.cameraViewToClip = Transpose(viewToClip);`
        zu `= viewToClip;` — „the projection comes back, transposed" fällt.

-   [ ] **Step 6: Commit** — `feat: Streamline camera constants from the engine`, Body: „Projection,
        inverse and the clip to previous clip pair derived from worldToCam and the camera position,
        transposed into Streamline's row-vector convention, with a residue that shows whether both
        describe one camera."

---

## Task 6: `Bridge::PresentStats`

**Files:**

-   Create: `src/Bridge/PresentStats.h`, `src/Bridge/PresentStats.cpp`, `tests/PresentStatsTests.cpp`
-   Modify: `CMakeLists.txt` (Testblock mit beiden Quellen)

**Interfaces:**

-   Produces: `class Bridge::PresentStats { struct Rates { double rendered; double shown; }; void Frame(bool a_generating, std::uint32_t a_actuallyPresented); std::optional<Rates> Sample(double a_nowSeconds); Rates Last() const; }`

-   [ ] **Step 1: Den Test schreiben**

`tests/PresentStatsTests.cpp`:

```cpp
#include "Bridge/PresentStats.h"

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
	Bridge::PresentStats stats;
	Check(!stats.Sample(10.0).has_value(), "the first sample only sets the clock");

	for (int i = 0; i < 90; ++i) {
		stats.Frame(false, 0);
	}
	Check(!stats.Sample(10.5).has_value(), "half a second is not a sample");

	for (int i = 0; i < 90; ++i) {
		stats.Frame(false, 7);
	}
	const auto off = stats.Sample(11.0);
	Check(off && std::abs(off->rendered - 180.0) < 1e-9 && std::abs(off->shown - 180.0) < 1e-9,
		"without generation every rendered frame is one shown, whatever Streamline counts");

	for (int i = 0; i < 70; ++i) {
		stats.Frame(true, 2);
	}
	const auto on = stats.Sample(12.0);
	Check(on && std::abs(on->rendered - 70.0) < 1e-9 && std::abs(on->shown - 140.0) < 1e-9,
		"with generation the shown count is what Streamline presented");
	Check(std::abs(stats.Last().shown - 140.0) < 1e-9, "Last keeps the latest sample for the display");

	for (int i = 0; i < 100; ++i) {
		stats.Frame(true, 2);
	}
	const auto longer = stats.Sample(14.0);
	Check(longer && std::abs(longer->rendered - 50.0) < 1e-9, "a longer stretch is divided by its own length");

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

CMake-Block wie in Task 1 mit `PresentStatsTests`, Quellen `tests/PresentStatsTests.cpp` und
`src/Bridge/PresentStats.cpp`, `add_test(NAME PresentStats COMMAND PresentStatsTests)`.

-   [ ] **Step 2: Bauen, Fehlschlag sehen.**

-   [ ] **Step 3: Implementieren**

`src/Bridge/PresentStats.h`:

```cpp
#pragma once

#include <cstdint>
#include <optional>

namespace Bridge
{
	/// Rendered against shown frames, per second. A rendered frame is one
	/// Present of the game's. Shown is what Streamline reports as actually
	/// presented since the last query, generated frames included - or one per
	/// rendered frame while nothing is generated, whatever Streamline counts
	/// then. The profiler keeps measuring rendered frames only.
	class PresentStats
	{
	public:
		struct Rates
		{
			double rendered{ 0.0 };
			double shown{ 0.0 };
		};

		void Frame(bool a_generating, std::uint32_t a_actuallyPresented) noexcept;

		/// The rates since the last sample, once at least a second has passed
		/// since it. The very first call only starts the clock.
		[[nodiscard]] std::optional<Rates> Sample(double a_nowSeconds) noexcept;

		[[nodiscard]] Rates Last() const noexcept { return _last; }

	private:
		double _since{ -1.0 };
		std::uint64_t _rendered{ 0 };
		std::uint64_t _shown{ 0 };
		Rates _last{};
	};
}
```

`src/Bridge/PresentStats.cpp`:

```cpp
#include "Bridge/PresentStats.h"

namespace Bridge
{
	void PresentStats::Frame(bool a_generating, std::uint32_t a_actuallyPresented) noexcept
	{
		++_rendered;
		_shown += a_generating ? a_actuallyPresented : 1u;
	}

	std::optional<PresentStats::Rates> PresentStats::Sample(double a_nowSeconds) noexcept
	{
		if (_since < 0.0) {
			_since = a_nowSeconds;
			_rendered = 0;
			_shown = 0;
			return std::nullopt;
		}
		const auto span = a_nowSeconds - _since;
		if (span < 1.0) {
			return std::nullopt;
		}
		_last = Rates{ static_cast<double>(_rendered) / span, static_cast<double>(_shown) / span };
		_since = a_nowSeconds;
		_rendered = 0;
		_shown = 0;
		return _last;
	}
}
```

-   [ ] **Step 4: Bauen und testen** — grün.

-   [ ] **Step 5: Mutation** — `_shown += a_generating ? a_actuallyPresented : 1u;` zu
        `_shown += a_actuallyPresented;`. Vorher benannt: „without generation every rendered frame is one
        shown" fällt.

-   [ ] **Step 6: Commit** — `feat: count rendered against shown frames`.

---

## Task 7: Streamline holen, native Insel, Laden

**Files:**

-   Create: `cmake/Streamline.cmake`, `src/Bridge/Log.h`, `src/Bridge/Log.cpp`,
    `src/Bridge/Native/NativeSdk.h`, `src/Bridge/Native/Streamline.h`, `src/Bridge/Native/Streamline.cpp`
-   Modify: `.gitignore`, `CMakeLists.txt`

**Interfaces:**

-   Produces: `Bridge::Log::Info/Warn/Error(std::string_view)`;
    `class Bridge::Native::Streamline` mit `Load(path)`, `SupportsFrameGeneration(IDXGIAdapter*)`,
    `BindDevice(ID3D12Device*)`, `Refusal()`, öffentliche Funktionszeiger
    `setTagForFrame, setConstants, getNewFrameToken, upgradeInterface, getNativeInterface,
dlssgGetState, dlssgSetOptions, reflexSetOptions, reflexSleep, pclSetMarker`;
    `Bridge::Native::TheStreamline()`.

-   [ ] **Step 1: Download-Regel**

`cmake/Streamline.cmake`:

```cmake
# NVIDIA Streamline for DLSS Frame Generation on the D3D12 bridge. NVIDIA's
# git repository carries no binaries, the release archive does. It is fetched
# once into extern/.downloads, checked against the SHA256 GitHub publishes for
# it, and only the headers, six runtime DLLs and three licences are extracted
# into extern/Streamline. Neither directory is tracked; the archive is 276 MB.
set(STREAMLINE_VERSION "2.14.16")
set(STREAMLINE_SHA256
    "32041e51db0cab709a271b72a352281b690c2abbd669a827809401a5dd23956b"
)
set(STREAMLINE_URL
    "https://github.com/NVIDIA-RTX/Streamline/releases/download/v${STREAMLINE_VERSION}/streamline-sdk-v${STREAMLINE_VERSION}.zip"
)
set(STREAMLINE_DIR "${CMAKE_SOURCE_DIR}/extern/Streamline")
set(STREAMLINE_ARCHIVE
    "${CMAKE_SOURCE_DIR}/extern/.downloads/streamline-sdk-v${STREAMLINE_VERSION}.zip"
)
set(STREAMLINE_STAMP "${STREAMLINE_DIR}/VERSION")

set(_streamline_present "")
if(EXISTS "${STREAMLINE_STAMP}")
    file(READ "${STREAMLINE_STAMP}" _streamline_present)
    string(STRIP "${_streamline_present}" _streamline_present)
endif()

if(NOT _streamline_present STREQUAL STREAMLINE_VERSION)
    if(EXISTS "${STREAMLINE_ARCHIVE}")
        file(SHA256 "${STREAMLINE_ARCHIVE}" _streamline_hash)
        if(NOT _streamline_hash STREQUAL STREAMLINE_SHA256)
            file(REMOVE "${STREAMLINE_ARCHIVE}")
        endif()
    endif()
    if(NOT EXISTS "${STREAMLINE_ARCHIVE}")
        message(
            STATUS
            "Downloading Streamline ${STREAMLINE_VERSION} (276 MB, once)"
        )
        file(
            DOWNLOAD "${STREAMLINE_URL}" "${STREAMLINE_ARCHIVE}"
            EXPECTED_HASH SHA256=${STREAMLINE_SHA256}
            STATUS _streamline_status
        )
        list(GET _streamline_status 0 _streamline_code)
        if(NOT _streamline_code EQUAL 0)
            file(REMOVE "${STREAMLINE_ARCHIVE}")
            message(FATAL_ERROR "Streamline download failed: ${_streamline_status}")
        endif()
    endif()
    file(REMOVE_RECURSE "${STREAMLINE_DIR}")
    file(
        ARCHIVE_EXTRACT
        INPUT "${STREAMLINE_ARCHIVE}"
        DESTINATION "${STREAMLINE_DIR}"
        PATTERNS
            "include/*"
            "bin/x64/sl.interposer.dll"
            "bin/x64/sl.common.dll"
            "bin/x64/sl.dlss_g.dll"
            "bin/x64/sl.reflex.dll"
            "bin/x64/sl.pcl.dll"
            "bin/x64/nvngx_dlssg.dll"
            "bin/x64/nvngx_dlss.license.txt"
            "bin/x64/reflex.license.txt"
            "license.txt"
    )
    file(WRITE "${STREAMLINE_STAMP}" "${STREAMLINE_VERSION}\n")
endif()

if(NOT EXISTS "${STREAMLINE_DIR}/include/sl.h")
    message(FATAL_ERROR "extern/Streamline is incomplete; delete it and configure again")
endif()
```

`.gitignore` ergänzen:

```
extern/Streamline/
extern/.downloads/
```

-   [ ] **Step 2: CMake einbinden**

In `CMakeLists.txt` direkt nach dem `DLSS_SDK_DIR`-Block:

```cmake
include("${CMAKE_SOURCE_DIR}/cmake/Streamline.cmake")
```

Nach dem `set_source_files_properties(…LensFlareDetour.cpp…)`-Block:

```cmake
# The bridge's D3D12 half needs the Windows SDK's own D3D12, DXGI and D3D11
# headers and Streamline's, none of which the rest of the plugin may see. These
# files are compiled without the precompiled header that brings REX in, and
# talk to the rest only through src/Bridge/Bridge.h.
file(
    GLOB BRIDGE_NATIVE_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/Bridge/Native/*.cpp"
)
set_source_files_properties(
    ${BRIDGE_NATIVE_SOURCES}
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)
```

In `target_include_directories(${PROJECT_NAME} …)` die Zeile
`"${STREAMLINE_DIR}/include"` anfügen; in `target_link_libraries(${PROJECT_NAME} …)` die Zeilen
`d3d12`, `dxgi`, `d3d11` anfügen.

-   [ ] **Step 3: Konfigurieren, Download belegen**

Run: `& "$cmake\cmake.exe" -S . --preset FO4`
Expected: „Downloading Streamline 2.14.16 (276 MB, once)", danach Konfiguration ohne Fehler;
`extern/Streamline/bin/x64/` enthält genau sechs DLLs und zwei Lizenztexte, `extern/Streamline/include/sl.h`
existiert, `git status` zeigt `extern/Streamline` nicht.

-   [ ] **Step 4: Log-Brücke**

`src/Bridge/Log.h`:

```cpp
#pragma once

#include <string_view>

namespace Bridge::Log
{
	/// The native half of the bridge cannot see REX - its headers clash with
	/// the Windows SDK's - so it logs through these, which do.
	void Info(std::string_view a_text) noexcept;
	void Warn(std::string_view a_text) noexcept;
	void Error(std::string_view a_text) noexcept;
}
```

`src/Bridge/Log.cpp`:

```cpp
#include "Bridge/Log.h"

namespace Bridge::Log
{
	void Info(std::string_view a_text) noexcept
	{
		REX::INFO("{}", a_text);
	}

	void Warn(std::string_view a_text) noexcept
	{
		REX::WARN("{}", a_text);
	}

	void Error(std::string_view a_text) noexcept
	{
		REX::ERROR("{}", a_text);
	}
}
```

-   [ ] **Step 5: Die SDK-Kopfdatei der Insel**

`src/Bridge/Native/NativeSdk.h`:

```cpp
#pragma once

// Only for src/Bridge/Native, which is compiled without the precompiled
// header (CMakeLists.txt): D3D12 and Streamline need the Windows SDK's own
// headers, and nothing else in the plugin may see them. windef.h defines near
// and far as empty macros - neither is a usable name in this directory.
#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Streamline's headers paste tokens in SL_FEATURE_FUN_IMPORT, which
// /Zc:preprocessor reports as C5103 (Skyrim CS built them with /wd5103), and
// declare what they never use. Their warnings are theirs.
#pragma warning(push, 0)
#include <sl.h>
#include <sl_dlss_g.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#pragma warning(pop)

namespace Bridge::Native
{
	using Microsoft::WRL::ComPtr;
}
```

-   [ ] **Step 6: Laden**

`src/Bridge/Native/Streamline.h`:

```cpp
#pragma once

#include "Bridge/Native/NativeSdk.h"

#include <filesystem>
#include <string>

namespace Bridge::Native
{
	/// sl.interposer.dll, loaded by hand: the plugin links neither it nor
	/// Streamline's helpers, which would need slGetFeatureFunction at link
	/// time (ProgrammingGuideManualHooking.md 1.1). Lives for the process:
	/// Streamline works with exactly one device, and slShutdown would have to
	/// come before that device goes - neither happens while the game runs.
	class Streamline
	{
	public:
		/// Verifies NVIDIA's signature, loads the interposer, runs slInit with
		/// manual hooking on D3D12. Idempotent.
		[[nodiscard]] bool Load(const std::filesystem::path& a_directory) noexcept;
		[[nodiscard]] bool SupportsFrameGeneration(IDXGIAdapter* a_adapter) noexcept;
		/// slSetD3DDevice, then the feature functions, which exist only after it.
		[[nodiscard]] bool BindDevice(ID3D12Device* a_device) noexcept;

		[[nodiscard]] const std::string& Refusal() const noexcept { return _refusal; }

		PFun_slSetTagForFrame* setTagForFrame{};
		PFun_slSetConstants* setConstants{};
		PFun_slGetNewFrameToken* getNewFrameToken{};
		PFun_slUpgradeInterface* upgradeInterface{};
		PFun_slGetNativeInterface* getNativeInterface{};
		PFun_slDLSSGGetState* dlssgGetState{};
		PFun_slDLSSGSetOptions* dlssgSetOptions{};
		PFun_slReflexSetOptions* reflexSetOptions{};
		PFun_slReflexSleep* reflexSleep{};
		PFun_slPCLSetMarker* pclSetMarker{};

	private:
		bool Fail(std::string a_reason) noexcept;

		HMODULE _module{ nullptr };
		bool _initialised{ false };
		std::wstring _pluginPath;
		std::string _refusal;
		PFun_slInit* _init{};
		PFun_slIsFeatureSupported* _isFeatureSupported{};
		PFun_slSetD3DDevice* _setD3DDevice{};
		PFun_slGetFeatureFunction* _getFeatureFunction{};
	};

	[[nodiscard]] Streamline& TheStreamline() noexcept;

	/// UTF-16 path or text to the log's UTF-8.
	[[nodiscard]] std::string Narrow(const std::wstring& a_text);
}
```

`src/Bridge/Native/Streamline.cpp`:

```cpp
#include "Bridge/Native/Streamline.h"

#include "Bridge/Log.h"

// Defines its functions in the header, so exactly one translation unit may
// include it - this one. Its warnings are NVIDIA's.
#pragma warning(push, 0)
#include <sl_security.h>
#pragma warning(pop)

#include <format>
#include <iterator>
#include <type_traits>

namespace Bridge::Native
{
	namespace
	{
		/// The project id and engine version NGX knows us by since F5.5a
		/// (src/Upscale/Ngx.cpp); one identity for both halves of DLSS.
		constexpr const char* kProjectId = "99023707-327d-45dc-ba45-0c47e8445542";
		constexpr const char* kEngineVersion = "Fallout 4 1.11.240";

		void OnStreamlineLog(sl::LogType a_type, const char* a_message)
		{
			std::string_view text{ a_message != nullptr ? a_message : "" };
			while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
				text.remove_suffix(1);
			}
			const auto line = std::format("streamline: {}", text);
			switch (a_type) {
			case sl::LogType::eError:
				Log::Error(line);
				break;
			case sl::LogType::eWarn:
				Log::Warn(line);
				break;
			default:
				Log::Info(line);
				break;
			}
		}

		template <class T>
		bool Resolve(HMODULE a_module, const char* a_name, T*& a_function) noexcept
		{
			a_function = reinterpret_cast<T*>(GetProcAddress(a_module, a_name));
			return a_function != nullptr;
		}
	}

	std::string Narrow(const std::wstring& a_text)
	{
		if (a_text.empty()) {
			return {};
		}
		const auto size = WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), static_cast<int>(a_text.size()), out.data(), size, nullptr, nullptr);
		return out;
	}

	Streamline& TheStreamline() noexcept
	{
		static Streamline instance;
		return instance;
	}

	bool Streamline::Fail(std::string a_reason) noexcept
	{
		_refusal = std::move(a_reason);
		return false;
	}

	bool Streamline::Load(const std::filesystem::path& a_directory) noexcept
	{
		if (_initialised) {
			return true;
		}
		const auto interposer = a_directory / L"sl.interposer.dll";
		if (!std::filesystem::exists(interposer)) {
			return Fail(std::format("{} is missing - is the FrameGeneration archive installed?", Narrow(interposer.wstring())));
		}
		// Full path, as sl_security.h insists: a relative one would verify
		// whatever the search order finds first.
		if (!sl::security::verifyEmbeddedSignature(interposer.c_str())) {
			return Fail("sl.interposer.dll carries no valid NVIDIA signature");
		}
		if (_module == nullptr) {
			_module = LoadLibraryExW(interposer.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			if (_module == nullptr) {
				return Fail(std::format("sl.interposer.dll did not load (error {})", GetLastError()));
			}
		}

		const bool resolved =
			Resolve(_module, "slInit", _init) &&
			Resolve(_module, "slIsFeatureSupported", _isFeatureSupported) &&
			Resolve(_module, "slSetD3DDevice", _setD3DDevice) &&
			Resolve(_module, "slGetFeatureFunction", _getFeatureFunction) &&
			Resolve(_module, "slSetTagForFrame", setTagForFrame) &&
			Resolve(_module, "slSetConstants", setConstants) &&
			Resolve(_module, "slGetNewFrameToken", getNewFrameToken) &&
			Resolve(_module, "slUpgradeInterface", upgradeInterface) &&
			Resolve(_module, "slGetNativeInterface", getNativeInterface);
		if (!resolved) {
			return Fail("sl.interposer.dll lacks an export this plugin needs");
		}

		_pluginPath = a_directory.wstring();
		const wchar_t* paths[]{ _pluginPath.c_str() };
		const sl::Feature features[]{ sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };

		sl::Preferences preferences{};
		preferences.showConsole = false;
		preferences.logLevel = sl::LogLevel::eDefault;
		preferences.pathsToPlugins = paths;
		preferences.numPathsToPlugins = 1;
		preferences.logMessageCallback = &OnStreamlineLog;
		// Manual hooking: otherwise Streamline patches the DXGI factory class
		// for the whole process (dxgi.cpp:318), the game's D3D11 path included.
		// No OTA: the files that ship are the files that run.
		preferences.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking |
		                    sl::PreferenceFlags::eUseFrameBasedResourceTagging;
		preferences.featuresToLoad = features;
		preferences.numFeaturesToLoad = static_cast<std::uint32_t>(std::size(features));
		preferences.engine = sl::EngineType::eCustom;
		preferences.engineVersion = kEngineVersion;
		preferences.projectId = kProjectId;
		preferences.renderAPI = sl::RenderAPI::eD3D12;

		const auto result = _init(preferences, sl::kSDKVersion);
		if (result != sl::Result::eOk) {
			return Fail(std::format("slInit failed (result {})", static_cast<std::uint32_t>(result)));
		}
		_initialised = true;
		Log::Info("bridge: Streamline 2.14.16 initialised, manual hooking on D3D12");
		return true;
	}

	bool Streamline::SupportsFrameGeneration(IDXGIAdapter* a_adapter) noexcept
	{
		DXGI_ADAPTER_DESC description{};
		if (a_adapter == nullptr || FAILED(a_adapter->GetDesc(&description))) {
			return Fail("the adapter could not be described");
		}
		sl::AdapterInfo adapter{};
		adapter.deviceLUID = reinterpret_cast<std::uint8_t*>(&description.AdapterLuid);
		adapter.deviceLUIDSizeInBytes = sizeof(LUID);
		if (const auto result = _isFeatureSupported(sl::kFeatureDLSS_G, adapter); result != sl::Result::eOk) {
			return Fail(std::format(
				"DLSS Frame Generation is not supported here (result {}) - it needs an RTX 40 or newer, a current driver "
				"and hardware-accelerated GPU scheduling",
				static_cast<std::uint32_t>(result)));
		}
		if (const auto result = _isFeatureSupported(sl::kFeatureReflex, adapter); result != sl::Result::eOk) {
			return Fail(std::format("NVIDIA Reflex is not supported here (result {})", static_cast<std::uint32_t>(result)));
		}
		return true;
	}

	bool Streamline::BindDevice(ID3D12Device* a_device) noexcept
	{
		if (const auto result = _setD3DDevice(a_device); result != sl::Result::eOk) {
			return Fail(std::format("slSetD3DDevice failed (result {})", static_cast<std::uint32_t>(result)));
		}
		const auto feature = [this](sl::Feature a_feature, const char* a_name, auto*& a_function) {
			void* function = nullptr;
			if (_getFeatureFunction(a_feature, a_name, function) != sl::Result::eOk || function == nullptr) {
				return false;
			}
			a_function = reinterpret_cast<std::remove_reference_t<decltype(a_function)>>(function);
			return true;
		};
		const bool bound =
			feature(sl::kFeatureDLSS_G, "slDLSSGGetState", dlssgGetState) &&
			feature(sl::kFeatureDLSS_G, "slDLSSGSetOptions", dlssgSetOptions) &&
			feature(sl::kFeatureReflex, "slReflexSetOptions", reflexSetOptions) &&
			feature(sl::kFeatureReflex, "slReflexSleep", reflexSleep) &&
			feature(sl::kFeaturePCL, "slPCLSetMarker", pclSetMarker);
		if (!bound) {
			return Fail("a DLSS-G, Reflex or PCL function is missing from Streamline's plugins");
		}
		return true;
	}
}
```

-   [ ] **Step 7: Bauen**

Run: `& "$cmake\cmake.exe" --build --preset FO4`
Expected: Erfolg. Meldet der Compiler eine Warnung, deren Ort in `extern/Streamline/include` liegt,
dann ist die Klammer in `NativeSdk.h`/`Streamline.cpp` an der falschen Stelle — Warnungen aus
unseren eigenen Zeilen werden behoben, nicht unterdrückt. Danach alle Tests:
`& "$cmake\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure` — grün.

-   [ ] **Step 8: Commit**

```bash
git add cmake/Streamline.cmake .gitignore CMakeLists.txt src/Bridge/Log.h src/Bridge/Log.cpp src/Bridge/Native/NativeSdk.h src/Bridge/Native/Streamline.h src/Bridge/Native/Streamline.cpp
git commit -m "build: fetch Streamline and load it by hand" -m "The release archive is verified against its published SHA256 and
only headers, six DLLs and licences are extracted. The bridge's
native half compiles without the PCH and loads sl.interposer.dll
after checking NVIDIA's signature.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 8: `Bridge::Native::Presenter` und `Bridge.h`

**Files:**

-   Create: `src/Bridge/Bridge.h`, `src/Bridge/Native/Presenter.h`, `src/Bridge/Native/Presenter.cpp`

**Interfaces:**

-   Consumes: `SlotRing`, `FrameTokenQueue`, `PresentStats`, `Constants` (Tasks 3–6), `Streamline` (Task 7).
-   Produces (`src/Bridge/Bridge.h`, reine Typen):

```cpp
#pragma once

#include "Bridge/CameraConstants.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace Bridge
{
	struct StartupConfig
	{
		bool switchOn{ false };
		std::string switchReason;
		/// Data/F4SE/Plugins/CommunityShadersFO4; Streamline's files are in its
		/// Streamline subdirectory.
		std::filesystem::path pluginDirectory;
	};

	/// Patches Fallout4.exe's import of D3D11CreateDeviceAndSwapChain. Once,
	/// from F4SEPlugin_Load, after the runtime check.
	[[nodiscard]] bool InstallDeviceHook(const StartupConfig& a_config) noexcept;

	/// True while the game holds our swap chain.
	[[nodiscard]] bool Active() noexcept;
	/// Why the bridge is not there; empty while it is, and before the game
	/// created its device.
	[[nodiscard]] std::string Refusal();

	/// D3D11 objects of the slot this frame writes. REX::W32 and the Windows
	/// SDK declare the same interfaces with the same layout; void* carries
	/// them across without either side seeing the other's headers.
	struct InputSlot
	{
		void* hudless{ nullptr };   // ID3D11Texture2D*, back buffer format and size
		void* depthUAV{ nullptr };  // ID3D11UnorderedAccessView*, R32_FLOAT
		void* motion{ nullptr };    // ID3D11Texture2D*, R16G16_FLOAT
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
	};
	[[nodiscard]] bool CurrentSlot(InputSlot& a_slot) noexcept;
	/// The proxy's private D3D11 back buffer (ID3D11Texture2D*), for the probe.
	[[nodiscard]] void* BackBuffer() noexcept;

	/// Depth, motion and camera of this frame are in the slot.
	struct FrameInputs
	{
		Constants constants{};
		std::uint32_t depthWidth{ 0 };
		std::uint32_t depthHeight{ 0 };
		std::uint32_t motionWidth{ 0 };
		std::uint32_t motionHeight{ 0 };
	};
	void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept;
	/// The picture before the HUD is in the slot.
	void SubmitHudless() noexcept;

	/// Applied at the next Present, on the presenting thread.
	void SetGeneration(bool a_on) noexcept;
	/// 1 low latency, 2 low latency with boost.
	void SetReflexMode(std::uint32_t a_mode) noexcept;
	void SetSplitView(bool a_on) noexcept;

	/// From Main::OnIdle, once per game frame: frame token, Reflex sleep,
	/// simulation start.
	void OnSimulationStart() noexcept;
	/// From UpdateTemporalData: the frame's rendering begins.
	void OnRenderStart() noexcept;

	struct Status
	{
		bool generating{ false };
		double renderedPerSecond{ 0.0 };
		double shownPerSecond{ 0.0 };
		std::uint32_t dlssgStatus{ 0 };
		std::uint32_t minWidthOrHeight{ 0 };
		std::uint32_t width{ 0 };
		std::uint32_t height{ 0 };
		bool vsyncSupported{ false };
	};
	[[nodiscard]] Status CurrentStatus() noexcept;
}
```

-   [ ] **Step 1: `Bridge.h` schreiben** — wie oben.

-   [ ] **Step 2: `Presenter.h`**

```cpp
#pragma once

#include "Bridge/Bridge.h"
#include "Bridge/FrameTokenQueue.h"
#include "Bridge/PresentStats.h"
#include "Bridge/SlotRing.h"
#include "Bridge/Native/NativeSdk.h"

#include <array>
#include <cstdint>
#include <string>

namespace Bridge::Native
{
	/// What lives for the process, created with the first bridged device and
	/// reused should the game create its device again: Streamline is bound to
	/// one device, and the proxies it hands out are its own to free.
	struct Backend
	{
		ComPtr<ID3D12Device> device;
		ComPtr<ID3D12CommandQueue> queue;      // created through Streamline's device proxy
		ComPtr<IDXGIFactory5> nativeFactory;   // for what Streamline does not intercept
		IDXGIFactory2* factory{ nullptr };     // Streamline's proxy, never released by us
		LUID adapter{};
		bool tearing{ false };
	};
	[[nodiscard]] Backend& TheBackend() noexcept;

	struct SharedTexture
	{
		ComPtr<ID3D11Texture2D> texture11;
		ComPtr<ID3D11UnorderedAccessView> uav11;
		ComPtr<ID3D12Resource> resource12;
	};

	struct Slot
	{
		SharedTexture final;
		SharedTexture hudless;
		SharedTexture depth;
		SharedTexture motion;
	};

	/// The D3D12 side of one swap chain of the game's. Every call arrives on
	/// the thread that presents, which in Fallout 4 is the main thread.
	class Presenter
	{
	public:
		Presenter() = default;
		~Presenter();
		Presenter(const Presenter&) = delete;
		Presenter& operator=(const Presenter&) = delete;

		[[nodiscard]] bool Create(ID3D11Device* a_device, IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc) noexcept;
		void Destroy() noexcept;
		[[nodiscard]] const std::string& Failure() const noexcept { return _failure; }

		[[nodiscard]] HRESULT Present(UINT a_syncInterval, UINT a_flags) noexcept;
		[[nodiscard]] HRESULT ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) noexcept;

		[[nodiscard]] ID3D11Device* Device11() const noexcept { return _device11.Get(); }
		[[nodiscard]] ID3D11Texture2D* BackBuffer() const noexcept { return _backBuffer.Get(); }
		[[nodiscard]] IDXGISwapChain1* Native() const noexcept { return _native; }
		[[nodiscard]] DXGI_SWAP_CHAIN_DESC Describe() const noexcept;

		void OnSimulationStart() noexcept;
		void OnRenderStart() noexcept;
		[[nodiscard]] bool CurrentSlot(InputSlot& a_slot) noexcept;
		void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept;
		void SubmitHudless() noexcept { _hudlessThisFrame = true; }
		void SetGeneration(bool a_on) noexcept { _generationWanted = a_on; }
		void SetReflexMode(std::uint32_t a_mode) noexcept;
		void SetSplitView(bool a_on) noexcept { _splitView = a_on; }
		[[nodiscard]] Status CurrentStatus() const noexcept;

	private:
		bool Fail(std::string a_reason, HRESULT a_result = S_OK) noexcept;
		bool EnsureBackend(IDXGIAdapter* a_adapter) noexcept;
		bool CreateSwapChain(UINT a_width, UINT a_height) noexcept;
		bool CreateTextures(UINT a_width, UINT a_height) noexcept;
		bool CreateShared(const D3D11_TEXTURE2D_DESC& a_desc, SharedTexture& a_texture) noexcept;
		bool AcquireBuffers() noexcept;
		void ReleaseBuffers() noexcept;
		void WaitForQueueIdle() noexcept;
		void WaitSlotOnD3D11() noexcept;
		void ApplyOptions(bool a_wanted) noexcept;
		void Tag(const sl::FrameToken& a_token, bool a_generate) noexcept;
		void Mark(sl::PCLMarker a_marker, const sl::FrameToken& a_token) noexcept;
		sl::FrameToken* TakeRenderToken() noexcept;
		void PollState() noexcept;
		void LogSecond() noexcept;

		std::string _failure;
		DXGI_SWAP_CHAIN_DESC _desc{};
		HWND _window{ nullptr };
		UINT _width{ 0 };
		UINT _height{ 0 };
		DXGI_FORMAT _swapFormat{ DXGI_FORMAT_UNKNOWN };
		UINT _swapFlags{ 0 };

		ComPtr<ID3D11Device5> _device11;
		ComPtr<ID3D11DeviceContext4> _context11;
		ComPtr<ID3D11Fence> _fence11;
		ComPtr<ID3D12Fence> _fence12;
		HANDLE _event{ nullptr };
		std::uint64_t _fenceValue{ 0 };
		std::uint64_t _waitedFor{ 0 };

		std::array<ComPtr<ID3D12CommandAllocator>, SlotRing::kSlots> _allocators;
		std::array<ComPtr<ID3D12GraphicsCommandList>, SlotRing::kSlots> _lists;
		ComPtr<IDXGISwapChain3> _swapChain;     // Streamline's proxy
		IDXGISwapChain1* _native{ nullptr };    // what Streamline does not intercept
		std::array<ComPtr<ID3D12Resource>, 2> _buffers;

		ComPtr<ID3D11Texture2D> _backBuffer;
		std::array<Slot, SlotRing::kSlots> _slots;
		SlotRing _ring;

		FrameTokenQueue _tokens;
		sl::FrameToken* _renderToken{ nullptr };
		sl::ViewportHandle _viewport{ 0u };

		FrameInputs _inputs{};
		bool _inputsThisFrame{ false };
		bool _hudlessThisFrame{ false };
		bool _generationWanted{ false };
		bool _generationApplied{ false };
		bool _optionsSet{ false };
		bool _splitView{ false };
		std::uint32_t _reflexMode{ 0 };

		PresentStats _stats;
		std::uint32_t _dlssgStatus{ 0 };
		std::uint32_t _minWidthOrHeight{ 0 };
		bool _vsyncSupported{ false };

		std::uint64_t _presents{ 0 };
		std::uint64_t _cpuWaits{ 0 };
		std::uint64_t _framesWithoutToken{ 0 };
		DWORD _simulationThread{ 0 };
		std::uint32_t _loggedStatus{ UINT32_MAX };
	};
}
```

-   [ ] **Step 3: `Presenter.cpp`**

```cpp
#include "Bridge/Native/Presenter.h"

#include "Bridge/Log.h"
#include "Bridge/Native/Streamline.h"

#include <format>
#include <iterator>

namespace Bridge::Native
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// A flip-model swap chain takes no sRGB back buffer; the copy from the
		/// game's sRGB texture into a UNORM one of the same group is legal.
		DXGI_FORMAT StripSrgb(DXGI_FORMAT a_format) noexcept
		{
			switch (a_format) {
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
				return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
				return DXGI_FORMAT_B8G8R8A8_UNORM;
			default:
				return a_format;
			}
		}

		void Barrier(ID3D12GraphicsCommandList* a_list, ID3D12Resource* a_resource, D3D12_RESOURCE_STATES a_before, D3D12_RESOURCE_STATES a_after) noexcept
		{
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = a_resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = a_before;
			barrier.Transition.StateAfter = a_after;
			a_list->ResourceBarrier(1, &barrier);
		}

		sl::float4x4 ToStreamline(const Matrix& a_matrix) noexcept
		{
			sl::float4x4 out{};
			for (std::uint32_t row = 0; row < 4; ++row) {
				out.setRow(row, sl::float4(a_matrix[row][0], a_matrix[row][1], a_matrix[row][2], a_matrix[row][3]));
			}
			return out;
		}

		sl::float3 ToStreamline(const std::array<float, 3>& a_vector) noexcept
		{
			return sl::float3(a_vector[0], a_vector[1], a_vector[2]);
		}

		double Seconds() noexcept
		{
			LARGE_INTEGER frequency{};
			LARGE_INTEGER now{};
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&now);
			return static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
		}
	}

	Backend& TheBackend() noexcept
	{
		static Backend backend;
		return backend;
	}

	Presenter::~Presenter()
	{
		Destroy();
	}

	bool Presenter::Fail(std::string a_reason, HRESULT a_result) noexcept
	{
		_failure = a_result == S_OK ? std::move(a_reason) : std::format("{} (0x{:08X})", a_reason, static_cast<std::uint32_t>(a_result));
		return false;
	}

	bool Presenter::Create(ID3D11Device* a_device, IDXGIAdapter* a_adapter, const DXGI_SWAP_CHAIN_DESC& a_desc) noexcept
	{
		_desc = a_desc;
		_desc.Windowed = TRUE;
		_window = a_desc.OutputWindow;

		if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(&_device11)))) {
			return Fail("the D3D11 device has no ID3D11Device5 (Windows 10 1703 or newer is needed)");
		}
		ComPtr<ID3D11DeviceContext> context;
		_device11->GetImmediateContext(&context);
		if (FAILED(context.As(&_context11))) {
			return Fail("the immediate context has no ID3D11DeviceContext4");
		}
		if (!EnsureBackend(a_adapter)) {
			return false;
		}
		auto& backend = TheBackend();

		HRESULT result = backend.device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&_fence12));
		if (FAILED(result)) {
			return Fail("CreateFence", result);
		}
		HANDLE handle = nullptr;
		result = backend.device->CreateSharedHandle(_fence12.Get(), nullptr, GENERIC_ALL, nullptr, &handle);
		if (FAILED(result)) {
			return Fail("CreateSharedHandle for the fence", result);
		}
		result = _device11->OpenSharedFence(handle, IID_PPV_ARGS(&_fence11));
		CloseHandle(handle);
		if (FAILED(result)) {
			return Fail("OpenSharedFence", result);
		}
		_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (_event == nullptr) {
			return Fail("CreateEvent");
		}

		for (std::size_t i = 0; i < SlotRing::kSlots; ++i) {
			result = backend.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_allocators[i]));
			if (FAILED(result)) {
				return Fail("CreateCommandAllocator", result);
			}
			result = backend.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _allocators[i].Get(), nullptr, IID_PPV_ARGS(&_lists[i]));
			if (FAILED(result)) {
				return Fail("CreateCommandList", result);
			}
			_lists[i]->Close();
		}

		UINT width = a_desc.BufferDesc.Width;
		UINT height = a_desc.BufferDesc.Height;
		if (width == 0 || height == 0) {
			RECT client{};
			GetClientRect(_window, &client);
			width = static_cast<UINT>(client.right - client.left);
			height = static_cast<UINT>(client.bottom - client.top);
		}
		if (!CreateSwapChain(width, height) || !CreateTextures(width, height)) {
			return false;
		}
		SetReflexMode(1);
		return true;
	}

	bool Presenter::EnsureBackend(IDXGIAdapter* a_adapter) noexcept
	{
		auto& backend = TheBackend();
		DXGI_ADAPTER_DESC description{};
		a_adapter->GetDesc(&description);
		if (backend.device) {
			if (backend.adapter.LowPart != description.AdapterLuid.LowPart || backend.adapter.HighPart != description.AdapterLuid.HighPart) {
				return Fail("the game created its device again, on another adapter");
			}
			return true;
		}

		ComPtr<ID3D12Device> device;
		HRESULT result = D3D12CreateDevice(a_adapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
		if (FAILED(result)) {
			return Fail("D3D12CreateDevice", result);
		}
		auto& streamline = TheStreamline();
		if (!streamline.BindDevice(device.Get())) {
			return Fail(streamline.Refusal());
		}

		// Through the proxy only to create the queue - the one device call
		// Streamline intercepts (sl_hooks.h, eID3D12Device_CreateCommandQueue).
		ID3D12Device* proxyDevice = device.Get();
		if (streamline.upgradeInterface(reinterpret_cast<void**>(&proxyDevice)) != sl::Result::eOk) {
			return Fail("slUpgradeInterface on the D3D12 device");
		}
		D3D12_COMMAND_QUEUE_DESC queueDesc{};
		queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		ComPtr<ID3D12CommandQueue> queue;
		result = proxyDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue));
		if (FAILED(result)) {
			return Fail("CreateCommandQueue", result);
		}

		ComPtr<IDXGIFactory5> nativeFactory;
		result = CreateDXGIFactory2(0, IID_PPV_ARGS(&nativeFactory));
		if (FAILED(result)) {
			return Fail("CreateDXGIFactory2", result);
		}
		BOOL tearing = FALSE;
		nativeFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing));

		ComPtr<IDXGIFactory2> forProxy;
		result = CreateDXGIFactory2(0, IID_PPV_ARGS(&forProxy));
		if (FAILED(result)) {
			return Fail("CreateDXGIFactory2", result);
		}
		IDXGIFactory2* proxyFactory = forProxy.Detach();
		if (streamline.upgradeInterface(reinterpret_cast<void**>(&proxyFactory)) != sl::Result::eOk) {
			return Fail("slUpgradeInterface on the DXGI factory");
		}

		backend.device = device;
		backend.queue = queue;
		backend.nativeFactory = nativeFactory;
		backend.factory = proxyFactory;
		backend.adapter = description.AdapterLuid;
		backend.tearing = tearing != FALSE;
		Log::Info(std::format("bridge: D3D12 device on {}, tearing {}", Narrow(description.Description), backend.tearing));
		return true;
	}

	bool Presenter::CreateSwapChain(UINT a_width, UINT a_height) noexcept
	{
		auto& backend = TheBackend();
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.Format = StripSrgb(_desc.BufferDesc.Format);
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = 2;
		desc.Scaling = DXGI_SCALING_STRETCH;
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		// No waitable object and no SetMaximumFrameLatency: Streamline paces
		// (ProgrammingGuideDLSS_G.md, item 39).
		desc.Flags = backend.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> swapChain;
		HRESULT result = backend.factory->CreateSwapChainForHwnd(backend.queue.Get(), _window, &desc, nullptr, nullptr, &swapChain);
		if (FAILED(result)) {
			return Fail("CreateSwapChainForHwnd", result);
		}
		// Exclusive fullscreen is not offered, so Alt+Enter must not reach DXGI.
		backend.nativeFactory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);
		result = swapChain.As(&_swapChain);
		if (FAILED(result)) {
			return Fail("the swap chain has no IDXGISwapChain3", result);
		}
		if (TheStreamline().getNativeInterface(_swapChain.Get(), reinterpret_cast<void**>(&_native)) != sl::Result::eOk || _native == nullptr) {
			return Fail("slGetNativeInterface on the swap chain");
		}
		_width = a_width;
		_height = a_height;
		_swapFormat = desc.Format;
		_swapFlags = desc.Flags;
		return AcquireBuffers();
	}

	bool Presenter::AcquireBuffers() noexcept
	{
		for (UINT i = 0; i < 2; ++i) {
			// Through the proxy: GetBuffer is how Streamline learns the back buffers.
			const auto result = _swapChain->GetBuffer(i, IID_PPV_ARGS(&_buffers[i]));
			if (FAILED(result)) {
				return Fail("GetBuffer", result);
			}
		}
		return true;
	}

	void Presenter::ReleaseBuffers() noexcept
	{
		for (auto& buffer : _buffers) {
			buffer.Reset();
		}
	}

	bool Presenter::CreateShared(const D3D11_TEXTURE2D_DESC& a_desc, SharedTexture& a_texture) noexcept
	{
		auto desc = a_desc;
		desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
		HRESULT result = _device11->CreateTexture2D(&desc, nullptr, &a_texture.texture11);
		if (FAILED(result)) {
			return Fail("CreateTexture2D for a shared slot", result);
		}
		ComPtr<IDXGIResource1> resource;
		result = a_texture.texture11.As(&resource);
		if (FAILED(result)) {
			return Fail("the shared texture has no IDXGIResource1", result);
		}
		HANDLE handle = nullptr;
		result = resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle);
		if (FAILED(result)) {
			return Fail("CreateSharedHandle for a slot texture", result);
		}
		result = TheBackend().device->OpenSharedHandle(handle, IID_PPV_ARGS(&a_texture.resource12));
		CloseHandle(handle);
		if (FAILED(result)) {
			return Fail("OpenSharedHandle for a slot texture", result);
		}
		if ((desc.BindFlags & D3D11_BIND_UNORDERED_ACCESS) != 0) {
			result = _device11->CreateUnorderedAccessView(a_texture.texture11.Get(), nullptr, &a_texture.uav11);
			if (FAILED(result)) {
				return Fail("CreateUnorderedAccessView for the depth slot", result);
			}
		}
		return true;
	}

	bool Presenter::CreateTextures(UINT a_width, UINT a_height) noexcept
	{
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = a_width;
		desc.Height = a_height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = _desc.BufferDesc.Format;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		// The game's back buffer: private, never shared, so the game never
		// writes a texture D3D12 is reading (doodlum d510212).
		_backBuffer.Reset();
		HRESULT result = _device11->CreateTexture2D(&desc, nullptr, &_backBuffer);
		if (FAILED(result)) {
			return Fail("CreateTexture2D for the back buffer", result);
		}

		for (auto& slot : _slots) {
			slot = Slot{};
			if (!CreateShared(desc, slot.final) || !CreateShared(desc, slot.hudless)) {
				return false;
			}
			auto depth = desc;
			depth.Format = DXGI_FORMAT_R32_FLOAT;
			depth.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			auto motion = desc;
			motion.Format = DXGI_FORMAT_R16G16_FLOAT;
			if (!CreateShared(depth, slot.depth) || !CreateShared(motion, slot.motion)) {
				return false;
			}
		}
		_ring.Reset();
		_waitedFor = 0;
		return true;
	}

	DXGI_SWAP_CHAIN_DESC Presenter::Describe() const noexcept
	{
		auto desc = _desc;
		desc.BufferDesc.Width = _width;
		desc.BufferDesc.Height = _height;
		return desc;
	}

	void Presenter::WaitForQueueIdle() noexcept
	{
		auto& backend = TheBackend();
		if (!backend.queue || !_fence12) {
			return;
		}
		const auto value = ++_fenceValue;
		backend.queue->Signal(_fence12.Get(), value);
		if (_fence12->GetCompletedValue() < value) {
			_fence12->SetEventOnCompletion(value, _event);
			WaitForSingleObject(_event, 5000);
		}
	}

	void Presenter::WaitSlotOnD3D11() noexcept
	{
		const auto value = _ring.ReleaseValue();
		if (value != 0 && value != _waitedFor) {
			_context11->Wait(_fence11.Get(), value);
			_waitedFor = value;
		}
	}

	void Presenter::Mark(sl::PCLMarker a_marker, const sl::FrameToken& a_token) noexcept
	{
		TheStreamline().pclSetMarker(a_marker, a_token);
	}

	void Presenter::OnSimulationStart() noexcept
	{
		auto& streamline = TheStreamline();
		sl::FrameToken* token = nullptr;
		if (streamline.getNewFrameToken(token, nullptr) != sl::Result::eOk || token == nullptr) {
			return;
		}
		// Sleep first, then the marker: Reflex holds the frame back before its
		// simulation begins (ProgrammingGuideReflex.md).
		streamline.reflexSleep(*token);
		Mark(sl::PCLMarker::eSimulationStart, *token);
		_tokens.Push(static_cast<std::uint32_t>(*token));
		_simulationThread = GetCurrentThreadId();
	}

	sl::FrameToken* Presenter::TakeRenderToken() noexcept
	{
		if (_renderToken != nullptr) {
			return _renderToken;
		}
		const auto index = _tokens.Pop();
		if (!index) {
			return nullptr;
		}
		sl::FrameToken* token = nullptr;
		const std::uint32_t value = *index;
		if (TheStreamline().getNewFrameToken(token, &value) != sl::Result::eOk || token == nullptr) {
			return nullptr;
		}
		// The simulation of this frame is over once its rendering begins; the
		// engine shows no closer point (spec, section 5).
		Mark(sl::PCLMarker::eSimulationEnd, *token);
		Mark(sl::PCLMarker::eRenderSubmitStart, *token);
		_renderToken = token;
		return token;
	}

	void Presenter::OnRenderStart() noexcept
	{
		static_cast<void>(TakeRenderToken());
		WaitSlotOnD3D11();
	}

	bool Presenter::CurrentSlot(InputSlot& a_slot) noexcept
	{
		auto& slot = _slots[_ring.Current()];
		if (!slot.hudless.texture11 || !slot.depth.uav11 || !slot.motion.texture11) {
			return false;
		}
		WaitSlotOnD3D11();
		a_slot.hudless = slot.hudless.texture11.Get();
		a_slot.depthUAV = slot.depth.uav11.Get();
		a_slot.motion = slot.motion.texture11.Get();
		a_slot.width = _width;
		a_slot.height = _height;
		return true;
	}

	void Presenter::SubmitFrameInputs(const FrameInputs& a_inputs) noexcept
	{
		_inputs = a_inputs;
		_inputsThisFrame = true;
	}

	void Presenter::SetReflexMode(std::uint32_t a_mode) noexcept
	{
		if (a_mode == _reflexMode) {
			return;
		}
		sl::ReflexOptions options{};
		options.mode = a_mode == 2 ? sl::ReflexMode::eLowLatencyWithBoost : sl::ReflexMode::eLowLatency;
		const auto result = TheStreamline().reflexSetOptions(options);
		Log::Info(std::format("bridge: Reflex {} (result {})", a_mode == 2 ? "low latency with boost" : "low latency", static_cast<std::uint32_t>(result)));
		_reflexMode = a_mode;
	}

	void Presenter::ApplyOptions(bool a_wanted) noexcept
	{
		if (_optionsSet && a_wanted == _generationApplied) {
			return;
		}
		sl::DLSSGOptions options{};
		options.mode = a_wanted ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
		options.numFramesToGenerate = 1;
		// Off without freeing: menus switch it off and on all the time
		// (ProgrammingGuideDLSS_G.md, eRetainResourcesWhenOff).
		options.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
		const auto result = TheStreamline().dlssgSetOptions(_viewport, options);
		if (result != sl::Result::eOk) {
			Log::Error(std::format("bridge: slDLSSGSetOptions {} failed (result {})", a_wanted ? "on" : "off", static_cast<std::uint32_t>(result)));
		}
		_generationApplied = a_wanted;
		_optionsSet = true;
	}

	void Presenter::Tag(const sl::FrameToken& a_token, bool a_generate) noexcept
	{
		auto& streamline = TheStreamline();
		auto& slot = _slots[_ring.Current()];
		// COMMON: a shared texture decays to it after every submission, and a
		// texture without simultaneous access is promoted from it to the read
		// states Streamline needs.
		sl::Resource depth{ sl::ResourceType::eTex2d, slot.depth.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		sl::Resource motion{ sl::ResourceType::eTex2d, slot.motion.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		sl::Resource hudless{ sl::ResourceType::eTex2d, slot.hudless.resource12.Get(), static_cast<std::uint32_t>(D3D12_RESOURCE_STATE_COMMON) };
		const sl::Extent depthExtent{ 0, 0, _inputs.depthWidth, _inputs.depthHeight };
		const sl::Extent motionExtent{ 0, 0, _inputs.motionWidth, _inputs.motionHeight };
		const sl::Extent full{ 0, 0, _width, _height };
		// Null resources when nothing is generated: tags of a frame that
		// produced none must not survive into it (ProgrammingGuideDLSS_G.md 351).
		sl::ResourceTag tags[]{
			sl::ResourceTag{ a_generate ? &depth : nullptr, sl::kBufferTypeDepth, sl::ResourceLifecycle::eValidUntilPresent, &depthExtent },
			sl::ResourceTag{ a_generate ? &motion : nullptr, sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eValidUntilPresent, &motionExtent },
			sl::ResourceTag{ a_generate ? &hudless : nullptr, sl::kBufferTypeHUDLessColor, sl::ResourceLifecycle::eValidUntilPresent, &full },
		};
		streamline.setTagForFrame(a_token, _viewport, tags, static_cast<std::uint32_t>(std::size(tags)), nullptr);

		if (!a_generate) {
			return;
		}
		const auto& c = _inputs.constants;
		sl::Constants constants{};
		constants.cameraViewToClip = ToStreamline(c.cameraViewToClip);
		constants.clipToCameraView = ToStreamline(c.clipToCameraView);
		constants.clipToLensClip = ToStreamline(Matrix{ { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } } });
		constants.clipToPrevClip = ToStreamline(c.clipToPrevClip);
		constants.prevClipToClip = ToStreamline(c.prevClipToClip);
		constants.jitterOffset = sl::float2(c.jitterOffset[0], c.jitterOffset[1]);
		constants.mvecScale = sl::float2(c.mvecScale[0], c.mvecScale[1]);
		constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
		constants.cameraPos = ToStreamline(c.cameraPos);
		constants.cameraUp = ToStreamline(c.cameraUp);
		constants.cameraRight = ToStreamline(c.cameraRight);
		constants.cameraFwd = ToStreamline(c.cameraFwd);
		constants.cameraNear = c.cameraNear;
		constants.cameraFar = c.cameraFar;
		constants.cameraFOV = c.cameraFOV;
		constants.cameraAspectRatio = c.cameraAspectRatio;
		constants.depthInverted = sl::Boolean::eFalse;
		constants.cameraMotionIncluded = sl::Boolean::eTrue;
		constants.motionVectors3D = sl::Boolean::eFalse;
		constants.reset = c.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
		constants.motionVectorsJittered = sl::Boolean::eFalse;
		streamline.setConstants(constants, a_token, _viewport);
	}

	HRESULT Presenter::Present(UINT a_syncInterval, UINT a_flags) noexcept
	{
		auto& backend = TheBackend();
		auto* const token = TakeRenderToken();
		if (token == nullptr) {
			++_framesWithoutToken;
		}
		auto& slot = _slots[_ring.Current()];

		// D3D11: the finished picture into the slot, then the handover.
		WaitSlotOnD3D11();
		_context11->CopyResource(slot.final.texture11.Get(), _backBuffer.Get());
		const auto ready = ++_fenceValue;
		_context11->Signal(_fence11.Get(), ready);
		// Without the game's own Present nothing flushes D3D11's queue any
		// more, and D3D12 would wait for a signal still sitting in it.
		_context11->Flush();
		if (token != nullptr) {
			Mark(sl::PCLMarker::eRenderSubmitEnd, *token);
		}

		backend.queue->Wait(_fence12.Get(), ready);
		if (!_ring.Reusable(_fence12->GetCompletedValue())) {
			// D3D12 is two frames behind: the allocator is still in use.
			_fence12->SetEventOnCompletion(_ring.ReleaseValue(), _event);
			WaitForSingleObject(_event, INFINITE);
			++_cpuWaits;
		}
		const auto k = _ring.Current();
		auto* const allocator = _allocators[k].Get();
		auto* const list = _lists[k].Get();
		allocator->Reset();
		list->Reset(allocator, nullptr);

		const auto index = _swapChain->GetCurrentBackBufferIndex();
		auto* const target = _buffers[index].Get();
		Barrier(list, target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
		list->CopyResource(target, slot.final.resource12.Get());
		if (_splitView && _hudlessThisFrame) {
			D3D12_TEXTURE_COPY_LOCATION destination{};
			destination.pResource = target;
			destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			D3D12_TEXTURE_COPY_LOCATION source{};
			source.pResource = slot.hudless.resource12.Get();
			source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			const D3D12_BOX left{ 0, 0, 0, _width / 2, _height, 1 };
			list->CopyTextureRegion(&destination, 0, 0, 0, &source, &left);
		}
		Barrier(list, target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
		list->Close();
		ID3D12CommandList* const lists[]{ list };
		backend.queue->ExecuteCommandLists(1, lists);

		const bool inputs = _inputsThisFrame && _hudlessThisFrame;
		const bool generate = token != nullptr && _generationWanted && inputs;
		ApplyOptions(generate);
		if (token != nullptr) {
			Tag(*token, generate);
			Mark(sl::PCLMarker::ePresentStart, *token);
		}
		UINT flags = a_flags & ~static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING);
		if (a_syncInterval == 0 && backend.tearing) {
			flags |= DXGI_PRESENT_ALLOW_TEARING;
		}
		// Streamline clamps the interval to 0 or 1 (ProgrammingGuideDLSS_G.md, VSync).
		const auto result = _swapChain->Present(a_syncInterval > 1 ? 1 : a_syncInterval, flags);
		if (token != nullptr) {
			Mark(sl::PCLMarker::ePresentEnd, *token);
		}

		const auto done = ++_fenceValue;
		backend.queue->Signal(_fence12.Get(), done);
		_ring.Submit(done);

		PollState();
		++_presents;
		if (_presents % kLogInterval == 0) {
			LogSecond();
		}
		_inputsThisFrame = false;
		_hudlessThisFrame = false;
		_renderToken = nullptr;
		return result;
	}

	void Presenter::PollState() noexcept
	{
		sl::DLSSGState state{};
		if (TheStreamline().dlssgGetState(_viewport, state, nullptr) == sl::Result::eOk) {
			_dlssgStatus = static_cast<std::uint32_t>(state.status);
			_minWidthOrHeight = state.minWidthOrHeight;
			_vsyncSupported = state.bIsVsyncSupportAvailable == sl::Boolean::eTrue;
			_stats.Frame(_generationApplied, state.numFramesActuallyPresented);
		} else {
			_stats.Frame(false, 0);
		}
		static_cast<void>(_stats.Sample(Seconds()));
		if (_dlssgStatus != _loggedStatus) {
			Log::Info(std::format("bridge: DLSS-G status 0x{:X}", _dlssgStatus));
			_loggedStatus = _dlssgStatus;
		}
	}

	void Presenter::LogSecond() noexcept
	{
		const auto rates = _stats.Last();
		Log::Info(std::format(
			"bridge: rendered {:.0f}/s, shown {:.0f}/s, generation {}, cpu waits {}, frames without token {}, tokens dropped {}, "
			"simulation thread {}, present thread {}",
			rates.rendered, rates.shown, _generationApplied ? "on" : "off", _cpuWaits, _framesWithoutToken, _tokens.Dropped(),
			_simulationThread, GetCurrentThreadId()));
	}

	Status Presenter::CurrentStatus() const noexcept
	{
		const auto rates = _stats.Last();
		return Status{ _generationApplied, rates.rendered, rates.shown, _dlssgStatus, _minWidthOrHeight, _width, _height, _vsyncSupported };
	}

	HRESULT Presenter::ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) noexcept
	{
		static_cast<void>(a_count);
		static_cast<void>(a_flags);
		UINT width = a_width;
		UINT height = a_height;
		if (width == 0 || height == 0) {
			RECT client{};
			GetClientRect(_window, &client);
			width = static_cast<UINT>(client.right - client.left);
			height = static_cast<UINT>(client.bottom - client.top);
		}
		const auto format = a_format == DXGI_FORMAT_UNKNOWN ? _desc.BufferDesc.Format : a_format;
		// Minimised: a window of size zero. Nothing to resize to, and textures
		// of size zero cannot exist; the next real size comes with the restore.
		if (width == 0 || height == 0) {
			return S_OK;
		}
		if (width == _width && height == _height && format == _desc.BufferDesc.Format) {
			return S_OK;
		}

		// Off before the resize, or Streamline's pacer may deadlock on it
		// (ProgrammingGuideDLSS_G.md, items 753-759).
		const bool wanted = _generationWanted;
		ApplyOptions(false);
		WaitForQueueIdle();
		ReleaseBuffers();
		const auto result = _swapChain->ResizeBuffers(2, width, height, StripSrgb(format), _swapFlags);
		if (FAILED(result)) {
			Log::Error(std::format("bridge: ResizeBuffers to {}x{} failed (0x{:08X})", width, height, static_cast<std::uint32_t>(result)));
			static_cast<void>(AcquireBuffers());
			return result;
		}
		_desc.BufferDesc.Format = format;
		_width = width;
		_height = height;
		_swapFormat = StripSrgb(format);
		if (!AcquireBuffers() || !CreateTextures(width, height)) {
			Log::Error(std::format("bridge: after the resize: {}", _failure));
			return E_FAIL;
		}
		_generationWanted = wanted;
		Log::Info(std::format("bridge: resized to {}x{}", width, height));
		return S_OK;
	}

	void Presenter::Destroy() noexcept
	{
		if (_optionsSet) {
			ApplyOptions(false);
		}
		WaitForQueueIdle();
		ReleaseBuffers();
		_swapChain.Reset();
		_native = nullptr;
		for (auto& slot : _slots) {
			slot = Slot{};
		}
		_backBuffer.Reset();
		for (auto& list : _lists) {
			list.Reset();
		}
		for (auto& allocator : _allocators) {
			allocator.Reset();
		}
		_fence11.Reset();
		_fence12.Reset();
		if (_event != nullptr) {
			CloseHandle(_event);
			_event = nullptr;
		}
		_context11.Reset();
		_device11.Reset();
	}
}
```

-   [ ] **Step 4: Bauen** — `& "$cmake\cmake.exe" --build --preset FO4`, Erfolg. Danach alle Tests grün.

-   [ ] **Step 5: Commit**

```bash
git add src/Bridge/Bridge.h src/Bridge/Native/Presenter.h src/Bridge/Native/Presenter.cpp
git commit -m "feat: D3D12 presenter behind the game's swap chain" -m "A D3D12 device on the game's adapter, a swap chain from Streamline's
factory, a shared fence and two slots of shared textures. D3D11
flushes after its signal; the CPU waits only when D3D12 is two frames
behind, and says so once a second.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 9: `SwapChainProxy`, IAT-Hook, Anschluß beim Laden

**Files:**

-   Create: `src/Bridge/Native/SwapChainProxy.h`, `src/Bridge/Native/SwapChainProxy.cpp`, `src/Bridge/Native/DeviceHook.cpp`
-   Modify: `src/XSEPlugin.cpp`

**Interfaces:**

-   Consumes: `Presenter` (Task 8), `Decide` (Task 2), `ReadStartupSwitch`/`Settings::DefaultFile` (Task 1).
-   Produces: alle Funktionen aus `Bridge.h` außer denen, die Task 10 und 11 aufrufen, sind hier
    definiert; `Bridge::Native::ActivePresenter()`.

-   [ ] **Step 1: Der Stellvertreter**

`src/Bridge/Native/SwapChainProxy.h`:

```cpp
#pragma once

#include "Bridge/Native/NativeSdk.h"
#include "Bridge/Native/Presenter.h"

#include <atomic>
#include <memory>

namespace Bridge::Native
{
	/// What the game holds as its IDXGISwapChain. Only that interface: asking
	/// for IDXGISwapChain1 or later gets E_NOINTERFACE rather than this object
	/// under a vtable it does not have - both templates return themselves for
	/// any interface the real swap chain knows, which crashes whoever calls a
	/// method beyond the first version. Our own Present hook (Render::
	/// InstallSwapChainHook) patches this class's vtable like any other.
	class SwapChainProxy final : public IDXGISwapChain
	{
	public:
		explicit SwapChainProxy(std::unique_ptr<Presenter> a_presenter) noexcept;

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID a_iid, void** a_object) override;
		ULONG STDMETHODCALLTYPE AddRef() override;
		ULONG STDMETHODCALLTYPE Release() override;

		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data) override;
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown) override;
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data) override;
		HRESULT STDMETHODCALLTYPE GetParent(REFIID a_iid, void** a_parent) override;

		HRESULT STDMETHODCALLTYPE GetDevice(REFIID a_iid, void** a_device) override;

		HRESULT STDMETHODCALLTYPE Present(UINT a_syncInterval, UINT a_flags) override;
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface) override;
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL a_fullscreen, IDXGIOutput* a_target) override;
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target) override;
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc) override;
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags) override;
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* a_parameters) override;
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** a_output) override;
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* a_statistics) override;
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* a_count) override;

	private:
		~SwapChainProxy();

		std::atomic<ULONG> _references{ 1 };
		std::unique_ptr<Presenter> _presenter;
	};

	/// The presenter of the swap chain the game holds now; null when none.
	[[nodiscard]] Presenter* ActivePresenter() noexcept;
}
```

`src/Bridge/Native/SwapChainProxy.cpp`:

```cpp
#include "Bridge/Native/SwapChainProxy.h"

#include "Bridge/Log.h"

namespace Bridge::Native
{
	namespace
	{
		std::atomic<Presenter*> g_active{ nullptr };
	}

	Presenter* ActivePresenter() noexcept
	{
		return g_active.load(std::memory_order_acquire);
	}

	SwapChainProxy::SwapChainProxy(std::unique_ptr<Presenter> a_presenter) noexcept :
		_presenter(std::move(a_presenter))
	{
		g_active.store(_presenter.get(), std::memory_order_release);
	}

	SwapChainProxy::~SwapChainProxy()
	{
		g_active.store(nullptr, std::memory_order_release);
		_presenter->Destroy();
		Log::Info("bridge: the game released its swap chain");
	}

	HRESULT SwapChainProxy::QueryInterface(REFIID a_iid, void** a_object)
	{
		if (a_object == nullptr) {
			return E_POINTER;
		}
		if (a_iid == __uuidof(IUnknown) || a_iid == __uuidof(IDXGIObject) || a_iid == __uuidof(IDXGIDeviceSubObject) ||
			a_iid == __uuidof(IDXGISwapChain)) {
			*a_object = static_cast<IDXGISwapChain*>(this);
			AddRef();
			return S_OK;
		}
		*a_object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG SwapChainProxy::AddRef()
	{
		return ++_references;
	}

	ULONG SwapChainProxy::Release()
	{
		const auto left = --_references;
		if (left == 0) {
			delete this;
		}
		return left;
	}

	HRESULT SwapChainProxy::SetPrivateData(REFGUID a_name, UINT a_size, const void* a_data)
	{
		return _presenter->Native()->SetPrivateData(a_name, a_size, a_data);
	}

	HRESULT SwapChainProxy::SetPrivateDataInterface(REFGUID a_name, const IUnknown* a_unknown)
	{
		return _presenter->Native()->SetPrivateDataInterface(a_name, a_unknown);
	}

	HRESULT SwapChainProxy::GetPrivateData(REFGUID a_name, UINT* a_size, void* a_data)
	{
		return _presenter->Native()->GetPrivateData(a_name, a_size, a_data);
	}

	HRESULT SwapChainProxy::GetParent(REFIID a_iid, void** a_parent)
	{
		return _presenter->Native()->GetParent(a_iid, a_parent);
	}

	HRESULT SwapChainProxy::GetDevice(REFIID a_iid, void** a_device)
	{
		// The D3D11 device for every version of ID3D11Device and for
		// IDXGIDevice alike; QueryInterface adds the reference the caller owns.
		return _presenter->Device11()->QueryInterface(a_iid, a_device);
	}

	HRESULT SwapChainProxy::Present(UINT a_syncInterval, UINT a_flags)
	{
		return _presenter->Present(a_syncInterval, a_flags);
	}

	HRESULT SwapChainProxy::GetBuffer(UINT a_buffer, REFIID a_iid, void** a_surface)
	{
		if (a_buffer != 0) {
			return DXGI_ERROR_INVALID_CALL;
		}
		return _presenter->BackBuffer()->QueryInterface(a_iid, a_surface);
	}

	HRESULT SwapChainProxy::SetFullscreenState(BOOL a_fullscreen, IDXGIOutput*)
	{
		return a_fullscreen != FALSE ? DXGI_ERROR_NOT_CURRENTLY_AVAILABLE : S_OK;
	}

	HRESULT SwapChainProxy::GetFullscreenState(BOOL* a_fullscreen, IDXGIOutput** a_target)
	{
		if (a_fullscreen != nullptr) {
			*a_fullscreen = FALSE;
		}
		if (a_target != nullptr) {
			*a_target = nullptr;
		}
		return S_OK;
	}

	HRESULT SwapChainProxy::GetDesc(DXGI_SWAP_CHAIN_DESC* a_desc)
	{
		if (a_desc == nullptr) {
			return E_INVALIDARG;
		}
		*a_desc = _presenter->Describe();
		return S_OK;
	}

	HRESULT SwapChainProxy::ResizeBuffers(UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
	{
		return _presenter->ResizeBuffers(a_count, a_width, a_height, a_format, a_flags);
	}

	HRESULT SwapChainProxy::ResizeTarget(const DXGI_MODE_DESC* a_parameters)
	{
		return _presenter->Native()->ResizeTarget(a_parameters);
	}

	HRESULT SwapChainProxy::GetContainingOutput(IDXGIOutput** a_output)
	{
		return _presenter->Native()->GetContainingOutput(a_output);
	}

	HRESULT SwapChainProxy::GetFrameStatistics(DXGI_FRAME_STATISTICS* a_statistics)
	{
		return _presenter->Native()->GetFrameStatistics(a_statistics);
	}

	HRESULT SwapChainProxy::GetLastPresentCount(UINT* a_count)
	{
		return _presenter->Native()->GetLastPresentCount(a_count);
	}
}
```

-   [ ] **Step 2: Der Hook und die `Bridge.h`-Funktionen**

`src/Bridge/Native/DeviceHook.cpp`:

```cpp
#include "Bridge/Bridge.h"

#include "Bridge/Decision.h"
#include "Bridge/Log.h"
#include "Bridge/Native/NativeSdk.h"
#include "Bridge/Native/Presenter.h"
#include "Bridge/Native/Streamline.h"
#include "Bridge/Native/SwapChainProxy.h"

#include <cstring>
#include <format>
#include <iterator>
#include <mutex>

namespace Bridge
{
	namespace
	{
		using Create_t = HRESULT(WINAPI*)(
			IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
			const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**, ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

		StartupConfig g_config;
		Create_t g_original{ nullptr };
		std::mutex g_refusalLock;
		std::string g_refusal;

		void SetRefusal(std::string a_reason)
		{
			const std::scoped_lock lock{ g_refusalLock };
			g_refusal = std::move(a_reason);
		}

		/// Fallout4.exe's import slot for a function of a DLL. Matches by name
		/// through the original thunks; where a binder dropped those, by the
		/// address the slot already holds.
		void** FindImport(HMODULE a_module, const char* a_dll, const char* a_function) noexcept
		{
			auto* const base = reinterpret_cast<std::uint8_t*>(a_module);
			const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
			const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (directory.VirtualAddress == 0) {
				return nullptr;
			}
			for (auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); descriptor->Name != 0; ++descriptor) {
				if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), a_dll) != 0) {
					continue;
				}
				auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
				if (descriptor->OriginalFirstThunk != 0) {
					const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
					for (; names->u1.AddressOfData != 0; ++names, ++slots) {
						if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
							continue;
						}
						const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
						if (std::strcmp(reinterpret_cast<const char*>(byName->Name), a_function) == 0) {
							return reinterpret_cast<void**>(&slots->u1.Function);
						}
					}
					continue;
				}
				const auto exported = reinterpret_cast<ULONG_PTR>(GetProcAddress(GetModuleHandleA(a_dll), a_function));
				for (; slots->u1.Function != 0; ++slots) {
					if (slots->u1.Function == exported) {
						return reinterpret_cast<void**>(&slots->u1.Function);
					}
				}
			}
			return nullptr;
		}

		std::string ModuleOf(const void* a_address)
		{
			HMODULE module = nullptr;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(a_address), &module) == 0) {
				return "an unknown module";
			}
			std::wstring path(MAX_PATH, L'\0');
			const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
			path.resize(length);
			return Native::Narrow(path);
		}

		bool EnbLoaded() noexcept
		{
			const auto d3d11 = GetModuleHandleW(L"d3d11.dll");
			return d3d11 != nullptr && GetProcAddress(d3d11, "ENBGetSDKVersion") != nullptr;
		}

		/// Another module that replaces the swap chain or the TAA chain.
		const char* ForeignModule() noexcept
		{
			for (const auto* name : { "AAAFrameGeneration.dll", "Upscaling.dll", "OptiScaler.dll", "nvngx.dll" }) {
				if (GetModuleHandleA(name) != nullptr) {
					return name;
				}
			}
			return "";
		}

		Native::ComPtr<IDXGIAdapter> ResolveAdapter(IDXGIAdapter* a_adapter) noexcept
		{
			if (a_adapter != nullptr) {
				return a_adapter;
			}
			Native::ComPtr<IDXGIFactory1> factory;
			Native::ComPtr<IDXGIAdapter1> adapter;
			if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumAdapters1(0, &adapter))) {
				return nullptr;
			}
			return adapter;
		}

		void LogDescription(const DXGI_SWAP_CHAIN_DESC* a_desc)
		{
			if (a_desc == nullptr) {
				Log::Info("bridge: the game asks for a device without a swap chain");
				return;
			}
			Log::Info(std::format(
				"bridge: the game asks for {}x{}, format {}, windowed {}, swap effect {}, flags 0x{:X}, {} buffer(s)",
				a_desc->BufferDesc.Width, a_desc->BufferDesc.Height, static_cast<int>(a_desc->BufferDesc.Format), a_desc->Windowed != FALSE,
				static_cast<int>(a_desc->SwapEffect), a_desc->Flags, a_desc->BufferCount));
		}

		HRESULT WINAPI CreateDeviceAndSwapChain(
			IDXGIAdapter* a_adapter,
			D3D_DRIVER_TYPE a_driverType,
			HMODULE a_software,
			UINT a_flags,
			const D3D_FEATURE_LEVEL* a_levels,
			UINT a_levelCount,
			UINT a_sdkVersion,
			const DXGI_SWAP_CHAIN_DESC* a_desc,
			IDXGISwapChain** a_swapChain,
			ID3D11Device** a_device,
			D3D_FEATURE_LEVEL* a_level,
			ID3D11DeviceContext** a_context)
		{
			const auto original = [&]() {
				return g_original(a_adapter, a_driverType, a_software, a_flags, a_levels, a_levelCount, a_sdkVersion, a_desc, a_swapChain,
					a_device, a_level, a_context);
			};
			const auto refuse = [&](std::string a_reason) {
				Log::Info(std::format("bridge: not built - {}", a_reason));
				SetRefusal(std::move(a_reason));
				return original();
			};

			LogDescription(a_desc);
			const auto adapter = ResolveAdapter(a_adapter);
			DXGI_ADAPTER_DESC adapterDesc{};
			if (adapter) {
				adapter->GetDesc(&adapterDesc);
			}
			DecisionFacts facts{};
			facts.switchOn = g_config.switchOn;
			facts.switchReason = g_config.switchReason;
			facts.swapChainRequested = a_desc != nullptr && a_swapChain != nullptr;
			facts.windowed = a_desc != nullptr && a_desc->Windowed != FALSE;
			facts.vendorId = adapterDesc.VendorId;
			facts.enb = EnbLoaded();
			facts.foreignModule = ForeignModule();
			if (const auto verdict = Decide(facts); !verdict.build) {
				return refuse(verdict.reason);
			}
			if (Native::ActivePresenter() != nullptr) {
				return refuse("the game asked for a second swap chain while the first is still alive");
			}

			auto& streamline = Native::TheStreamline();
			if (!streamline.Load(g_config.pluginDirectory / L"Streamline")) {
				return refuse(streamline.Refusal());
			}
			if (!streamline.SupportsFrameGeneration(adapter.Get())) {
				return refuse(streamline.Refusal());
			}

			// 11.1 at least: the shared fence and the context's Signal and Wait
			// need it, and the default list stops at 11.0 (doodlum forces it too).
			static constexpr D3D_FEATURE_LEVEL kLevels[]{ D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
			Native::ComPtr<ID3D11Device> device;
			Native::ComPtr<ID3D11DeviceContext> context;
			D3D_FEATURE_LEVEL level{};
			const auto result = D3D11CreateDevice(a_adapter, a_driverType, a_software, a_flags, kLevels,
				static_cast<UINT>(std::size(kLevels)), a_sdkVersion, &device, &level, &context);
			if (FAILED(result)) {
				return refuse(std::format("D3D11CreateDevice failed (0x{:08X})", static_cast<std::uint32_t>(result)));
			}
			Native::ComPtr<IDXGIDevice> dxgiDevice;
			Native::ComPtr<IDXGIAdapter> deviceAdapter;
			if (FAILED(device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&deviceAdapter))) {
				return refuse("the D3D11 device does not name its adapter");
			}

			auto presenter = std::make_unique<Native::Presenter>();
			if (!presenter->Create(device.Get(), deviceAdapter.Get(), *a_desc)) {
				auto reason = presenter->Failure();
				presenter->Destroy();
				return refuse(std::move(reason));
			}

			*a_swapChain = new Native::SwapChainProxy(std::move(presenter));
			if (a_device != nullptr) {
				*a_device = device.Detach();
			}
			if (a_context != nullptr) {
				*a_context = context.Detach();
			}
			if (a_level != nullptr) {
				*a_level = level;
			}
			SetRefusal({});
			Log::Info(std::format("bridge: in place, D3D11 feature level 0x{:X}", static_cast<std::uint32_t>(level)));
			return S_OK;
		}
	}

	bool InstallDeviceHook(const StartupConfig& a_config) noexcept
	{
		g_config = a_config;
		SetRefusal(std::format("the game has not created its device yet (switch {})", a_config.switchOn ? "on" : "off"));
		void** const slot = FindImport(GetModuleHandleW(nullptr), "d3d11.dll", "D3D11CreateDeviceAndSwapChain");
		if (slot == nullptr) {
			SetRefusal("Fallout4.exe imports no D3D11CreateDeviceAndSwapChain");
			Log::Error("bridge: Fallout4.exe imports no D3D11CreateDeviceAndSwapChain; frame generation will refuse");
			return false;
		}
		g_original = reinterpret_cast<Create_t>(*slot);
		// Names who was there first: another plugin's hook here would now sit
		// below ours, and is passed by when the bridge is built.
		Log::Info(std::format("bridge: D3D11CreateDeviceAndSwapChain leads into {}; patching the import (switch {}{}{})",
			ModuleOf(*slot), a_config.switchOn ? "on" : "off", a_config.switchOn ? "" : ": ", a_config.switchReason));
		DWORD previous = 0;
		if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous) == 0) {
			SetRefusal("the import table could not be made writable");
			return false;
		}
		*slot = reinterpret_cast<void*>(&CreateDeviceAndSwapChain);
		VirtualProtect(slot, sizeof(void*), previous, &previous);
		return true;
	}

	bool Active() noexcept
	{
		return Native::ActivePresenter() != nullptr;
	}

	std::string Refusal()
	{
		const std::scoped_lock lock{ g_refusalLock };
		return g_refusal;
	}

	bool CurrentSlot(InputSlot& a_slot) noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr && presenter->CurrentSlot(a_slot);
	}

	void* BackBuffer() noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr ? presenter->BackBuffer() : nullptr;
	}

	void SubmitFrameInputs(const FrameInputs& a_inputs) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SubmitFrameInputs(a_inputs);
		}
	}

	void SubmitHudless() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SubmitHudless();
		}
	}

	void SetGeneration(bool a_on) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetGeneration(a_on);
		}
	}

	void SetReflexMode(std::uint32_t a_mode) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetReflexMode(a_mode);
		}
	}

	void SetSplitView(bool a_on) noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->SetSplitView(a_on);
		}
	}

	void OnSimulationStart() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->OnSimulationStart();
		}
	}

	void OnRenderStart() noexcept
	{
		if (auto* const presenter = Native::ActivePresenter()) {
			presenter->OnRenderStart();
		}
	}

	Status CurrentStatus() noexcept
	{
		auto* const presenter = Native::ActivePresenter();
		return presenter != nullptr ? presenter->CurrentStatus() : Status{};
	}
}
```

-   [ ] **Step 3: Beim Laden anschließen**

In `src/XSEPlugin.cpp` Includes ergänzen:

```cpp
#include "Bridge/Bridge.h"
#include "Bridge/StartupSwitch.h"
#include "Settings/Settings.h"
#include "Util/GamePaths.h"
```

Nach dem `Upscale::Hooks::Install()`-Block, vor `REX::INFO("loaded");`:

```cpp
	// Long before the game creates its device, which is where the bridge is
	// decided - from the switch as the file holds it now, since Settings::Init
	// comes only with kGameDataReady.
	const auto startupSwitch = Bridge::ReadStartupSwitch(Settings::DefaultFile());
	static_cast<void>(Bridge::InstallDeviceHook({ startupSwitch.on, startupSwitch.reason, Util::PluginDataDirectory() }));
```

-   [ ] **Step 4: Bauen, Tests, Paket** — `& "$cmake\cmake.exe" --build --preset FO4` (deployt),
        alle Tests grün, `pwsh tools/verify-plugin.ps1` grün.

-   [ ] **Step 5: Commit**

```bash
git add src/Bridge/Native/SwapChainProxy.h src/Bridge/Native/SwapChainProxy.cpp src/Bridge/Native/DeviceHook.cpp src/XSEPlugin.cpp
git commit -m "feat: hand the game a bridged swap chain" -m "An import hook on D3D11CreateDeviceAndSwapChain builds the D3D12
presenter when switch, window, vendor and Streamline allow it, and
passes the call through with a named reason otherwise. The proxy
answers only for IDXGISwapChain and counts its own references.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 10: Hörstellen — `Upscale::Hooks::Tap` und `Main::OnIdle`

**Files:**

-   Modify: `src/Upscale/EngineHooks.h`, `src/Upscale/EngineHooks.cpp`, `src/XSEPlugin.cpp`
-   Create: `src/Bridge/SimulationHook.h`, `src/Bridge/SimulationHook.cpp`

**Interfaces:**

-   Produces: `class Upscale::Hooks::Tap { AfterTemporalData(); AfterEffectRange(); AfterDynamicViewportDefault(bool); }`,
    `void Upscale::Hooks::SetTap(Tap*)`, `bool Bridge::InstallSimulationHook()`, `bool Bridge::SimulationHookInstalled()`.

-   [ ] **Step 1: `Tap` deklarieren**

In `src/Upscale/EngineHooks.h` nach der Klasse `Handler`:

```cpp
	/// A second listener on three of the same sites, for frame generation:
	/// independent of the handler so that either feature runs without the
	/// other. Called after the handler at every site.
	class Tap
	{
	public:
		virtual ~Tap() = default;

		/// After UpdateTemporalData: the frame's rendering begins.
		virtual void AfterTemporalData() noexcept {}
		/// After RenderEffectRange: depth and motion vectors are still the
		/// frame's own - DLSS reads them here.
		virtual void AfterEffectRange() noexcept {}
		/// After SetUseDynamicResolutionViewportAsDefaultViewport: post-
		/// processing is done and the HUD not yet drawn (doodlum takes his
		/// HUD-less picture here, after the original call).
		virtual void AfterDynamicViewportDefault(bool a_enabled) noexcept { static_cast<void>(a_enabled); }
	};
```

und nach `void SetHandler(Handler* a_handler) noexcept;`:

```cpp
	/// nullptr silences it.
	void SetTap(Tap* a_tap) noexcept;
```

-   [ ] **Step 2: In den drei Thunks rufen**

In `src/Upscale/EngineHooks.cpp` neben `g_handler`:

```cpp
		std::atomic<Tap*> g_tap{ nullptr };

		Tap* CurrentTap() noexcept
		{
			return g_tap.load(std::memory_order_acquire);
		}
```

`TemporalData`, `EffectRange` und `ViewportDefault` ersetzen durch:

```cpp
		void TemporalData(void* a_state)
		{
			(*g_temporalData)(a_state);
			if (auto* const handler = Current()) {
				handler->AfterTemporalData();
			}
			if (auto* const tap = CurrentTap()) {
				tap->AfterTemporalData();
			}
		}

		void EffectRange(void* a_self, std::uint32_t a_1, std::uint32_t a_2, std::uint32_t a_3, std::uint32_t a_4)
		{
			(*g_effectRange)(a_self, a_1, a_2, a_3, a_4);
			if (auto* const handler = Current()) {
				handler->AfterEffectRange();
			}
			if (auto* const tap = CurrentTap()) {
				tap->AfterEffectRange();
			}
		}

		void ViewportDefault(void* a_self, bool a_enabled)
		{
			if (auto* const handler = Current()) {
				handler->BeforeDynamicViewportDefault();
			}
			(*g_viewportDefault)(a_self, a_enabled);
			if (auto* const tap = CurrentTap()) {
				tap->AfterDynamicViewportDefault(a_enabled);
			}
		}
```

und nach `SetHandler`:

```cpp
	void SetTap(Tap* a_tap) noexcept
	{
		g_tap.store(a_tap, std::memory_order_release);
	}
```

-   [ ] **Step 3: Frame-Beginn aus `Main::OnIdle`**

`src/Bridge/SimulationHook.h`:

```cpp
#pragma once

namespace Bridge
{
	/// Main::OnIdle's call into BSInputDeviceManager::PollInputDevices, once
	/// per game frame before its simulation: where Streamline's frame token
	/// and Reflex's sleep belong. Installed from F4SEPlugin_Load after the
	/// runtime check; it forwards to Bridge::OnSimulationStart, which does
	/// nothing while there is no bridge.
	[[nodiscard]] bool InstallSimulationHook() noexcept;
	[[nodiscard]] bool SimulationHookInstalled() noexcept;
}
```

`src/Bridge/SimulationHook.cpp`:

```cpp
#include "Bridge/SimulationHook.h"

#include "Bridge/Bridge.h"
#include "Upscale/CallSite.h"

#include <REL/THook.h>

#include <array>
#include <cstring>
#include <memory>

namespace Bridge
{
	namespace
	{
		/// Verified offline on 1.11.240 (2026-10-17, against version-1-11-240-0.bin
		/// and Fallout4.exe): E8 at Main::OnIdle + 0x8E5 into PollInputDevices,
		/// one of its three callers. doodlum hooked the callee for the same
		/// reason ("Reduce input latency", fo4test d510212).
		constexpr std::uint64_t kOnIdle = 2228917;
		constexpr std::ptrdiff_t kOffset = 0x8E5;
		constexpr std::uint64_t kPollInputDevices = 2268382;

		using PollHook = REL::THook<void(void*, float)>;
		std::unique_ptr<PollHook> g_poll;
		bool g_installed{ false };

		void Poll(void* a_manager, float a_delta)
		{
			OnSimulationStart();
			(*g_poll)(a_manager, a_delta);
		}
	}

	bool InstallSimulationHook() noexcept
	{
		const auto address = REL::ID(kOnIdle).address() + kOffset;
		std::array<std::uint8_t, 5> bytes{};
		std::memcpy(bytes.data(), reinterpret_cast<const void*>(address), bytes.size());
		const auto target = Upscale::CallTarget(bytes, address);
		if (!target || *target != REL::ID(kPollInputDevices).address()) {
			REX::ERROR("bridge: Main::OnIdle does not call PollInputDevices where 1.11.240 does; no frame start for Reflex");
			return false;
		}
		g_poll = std::make_unique<PollHook>("Main::OnIdle PollInputDevices", REL::ID(kOnIdle), kOffset, &Poll);
		if (!g_poll->Init() || !g_poll->Enable()) {
			REX::ERROR("bridge: the verified PollInputDevices site refused its hook");
			return false;
		}
		g_installed = true;
		return true;
	}

	bool SimulationHookInstalled() noexcept
	{
		return g_installed;
	}
}
```

In `src/XSEPlugin.cpp` Include `#include "Bridge/SimulationHook.h"` und direkt nach dem
`InstallDeviceHook`-Aufruf aus Task 9:

```cpp
	if (!Bridge::InstallSimulationHook()) {
		REX::ERROR("frame generation will refuse: no frame start for Reflex");
	}
```

Die Kommentarzeile über `initInfo.trampolineSize = 256;` lautet danach: „Nine call hooks of F5.5a
and F5.5b, fourteen bytes of branch each; the rest is headroom."

-   [ ] **Step 4: Bauen und Tests** — Erfolg, alle Tests grün.

-   [ ] **Step 5: Commit** — `feat: frame start and a second listener`, Body: „Upscale::Hooks gets a
        Tap beside its handler for frame generation, and Main::OnIdle's input polling becomes the frame
        start for Streamline and Reflex, verified offline on 1.11.240."

---

## Task 11: Das Feature `FrameGeneration`

**Files:**

-   Create: `src/Features/FrameGeneration.h`, `src/Features/FrameGeneration.cpp`,
    `package/Features/FrameGeneration/Shaders/FO4/FrameGeneration/CopyDepth.hlsl`
-   Modify: `src/Feature/FeatureSystem.cpp`, `src/Menu/MenuSystem.h`, `src/Menu/MenuSystem.cpp`,
    `src/Menu/PerformancePanel.h`, `src/Menu/PerformancePanel.cpp`

**Interfaces:**

-   Consumes: `Bridge.h` (Task 8/9), `Tap` (Task 10), `BuildConstants`/`JitterFromEngineOffset` (Task 5).
-   Produces: `bool Menu::IsOverlayOpen() noexcept;`, Einstellungen `FrameGeneration/enabled`,
    `FrameGeneration/reflex` (`on`/`boost`), `FrameGeneration/view` (`off`/`split`).

-   [ ] **Step 1: Der Shader**

`package/Features/FrameGeneration/Shaders/FO4/FrameGeneration/CopyDepth.hlsl`:

```hlsl
// The depth for DLSS Frame Generation: the rectangle of DS_002 that holds
// this frame into an R32_FLOAT texture D3D12 can open. A depth-stencil
// texture cannot be shared between the devices, a plain float one can.
Texture2D<float> DepthInput : register(t0);

RWTexture2D<float> DepthOutput : register(u0);

cbuffer CopyDepth : register(b0)
{
	uint2 Size;
	uint2 Padding;
};

[numthreads(8, 8, 1)] void main(uint3 dispatchID : SV_DispatchThreadID) {
	if (any(dispatchID.xy >= Size))
		return;
	DepthOutput[dispatchID.xy] = DepthInput[dispatchID.xy];
}
```

-   [ ] **Step 2: `Menu::IsOverlayOpen`**

`src/Menu/MenuSystem.h`, nach `TickSystem`:

```cpp
	/// Whether the overlay is open, as of the last tick. Frame generation
	/// switches itself off while it is: generated frames of a menu are input
	/// latency and nothing else.
	[[nodiscard]] bool IsOverlayOpen() noexcept;
```

`src/Menu/MenuSystem.cpp`, im Namensraum `Menu` nach `TickSystem`:

```cpp
	bool IsOverlayOpen() noexcept
	{
		return TheGate().IsOpen();
	}
```

-   [ ] **Step 3: „angezeigt" in der Ecke**

`src/Menu/PerformancePanel.h`, in `PerformanceContext` nach `frameCpuMs`:

```cpp
		/// Frames per second Streamline actually presented, generated ones
		/// included; shown only while frame generation runs.
		float shownFps{ 0.0f };
		bool generating{ false };
```

`src/Menu/PerformancePanel.cpp`, in `DrawCompact` im `else`-Zweig nach dem zweiten `ImGui::Text`:

```cpp
				if (a_context.generating) {
					ImGui::Text("%s %.0f fps", T("performance.shown", "shown"), static_cast<double>(a_context.shownFps));
				}
```

`src/Menu/MenuSystem.cpp`: `#include "Bridge/Bridge.h"`; nach `performance.frameCpuMs = …;`:

```cpp
		const auto bridge = Bridge::CurrentStatus();
		performance.generating = bridge.generating;
		performance.shownFps = static_cast<float>(bridge.shownPerSecond);
```

-   [ ] **Step 4: `FrameGeneration.h`**

```cpp
#pragma once

#include "Bridge/Bridge.h"
#include "Bridge/CameraConstants.h"
#include "Feature/Feature.h"
#include "Render/Resources.h"
#include "Upscale/CameraCut.h"
#include "Upscale/EngineHooks.h"

#include <REX/W32/D3D11.h>

#include <cstdint>
#include <string>

namespace Features
{
	/// NVIDIA DLSS Frame Generation through the D3D12 bridge (src/Bridge).
	/// Whether the bridge exists was decided when the game created its device;
	/// this feature switches generation on and off within it, captures what
	/// DLSS-G needs from the engine's frame - depth, motion vectors, the
	/// picture before the HUD - and hands it over with the camera. It owns
	/// nothing of the engine's and changes nothing in it: it only reads.
	class FrameGeneration final : public Feature, public Upscale::Hooks::Tap
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameGeneration"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

		void AfterTemporalData() noexcept override;
		void AfterEffectRange() noexcept override;
		void AfterDynamicViewportDefault(bool a_enabled) noexcept override;

	private:
		/// Why generation is off this frame; nullptr when nothing stands in the way.
		[[nodiscard]] const char* Blocker() const noexcept;
		[[nodiscard]] bool CompileShader() noexcept;
		void CaptureDepthAndMotion() noexcept;
		void SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept;
		[[nodiscard]] bool TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept;

		REX::W32::ID3D11ComputeShader* _copyDepth{ nullptr };
		Render::ConstantBuffer _constants;
		bool _tapSet{ false };

		bool _generating{ false };
		std::string _blocker{ "<start>" };
		bool _reset{ true };
		bool _havePrevious{ false };
		Bridge::CameraFrame _previous{};
		bool _haveCamera{ false };
		Upscale::CameraState _camera{};

		// For the once-a-second line.
		std::uint64_t _frames{ 0 };
		std::uint64_t _captures{ 0 };
		std::uint64_t _hudless{ 0 };
		std::uint64_t _rt0IsBackBuffer{ 0 };
		std::uint64_t _refusedCaptures{ 0 };
		float _matrixLean[2]{ 0.0f, 0.0f };
		float _residue{ 0.0f };
	};
}
```

-   [ ] **Step 5: `FrameGeneration.cpp`**

```cpp
#include "Features/FrameGeneration.h"

#include "Menu/MenuSystem.h"
#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Render/Resolution.h"
#include "Render/Targets.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Bridge/SimulationHook.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/U/UI.h>

#include <cmath>
#include <format>
#include <vector>

namespace Features
{
	namespace
	{
		constexpr std::uint64_t kLogInterval = 180;

		/// FO4_RT_029, R16G16_FLOAT: the engine's motion vectors, in uv units.
		constexpr std::size_t kMotionVectors = 29;
		/// FO4_RT_000: whatever the game got from GetBuffer(0) - with the bridge,
		/// the proxy's private back buffer (Lauf 1 counts it).
		constexpr std::size_t kFrameBuffer = 0;

		/// The engine's camera planes; DLSS reads the same two floats.
		constexpr std::uint64_t kCameraNear = 2712882;
		constexpr std::uint64_t kCameraFar = 2712883;
		/// The engine's TAA switch: without it the engine writes no motion
		/// vectors (F5.5a); DLSS forces it on while it runs.
		constexpr std::uint64_t kEnableTAA = 2704658;

		struct CopyDepthConstants
		{
			std::uint32_t size[2];
			std::uint32_t padding[2];
		};
		static_assert(sizeof(CopyDepthConstants) == 16);

		std::uint32_t Groups(std::uint32_t a_pixels) noexcept
		{
			return (a_pixels + 7) / 8;
		}
	}

	void FrameGeneration::Declare()
	{
		Settings::DeclareFeature("FrameGeneration", false)
			.Label("feature.framegeneration.name", "Frame Generation")
			.Help(
				"feature.framegeneration.help",
				"NVIDIA DLSS Frame Generation adds a generated frame after every rendered one for smoother motion, with "
				"NVIDIA Reflex keeping the delay down. Needs borderless windowed mode; switching it on takes effect at the "
				"next game start. Uses NVIDIA DLSS and Reflex technology.");
		Settings::DeclareChoice("FrameGeneration/reflex", "on", std::vector<std::string>{ "on", "boost" })
			.Label("feature.framegeneration.reflex", "Reflex")
			.Help("feature.framegeneration.reflex_help", "Low latency mode, or with boost, which also keeps the GPU clock up.");
		Settings::DeclareChoice("FrameGeneration/view", "off", std::vector<std::string>{ "off", "split" })
			.Label("feature.framegeneration.view", "Debug view")
			.Help(
				"feature.framegeneration.view_help",
				"Split shows the picture frame generation sees on the left half: it must have no HUD there.");
	}

	bool FrameGeneration::CompileShader() noexcept
	{
		constexpr const char* kFile = "FrameGeneration/CopyDepth.hlsl";
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			REX::ERROR("FrameGeneration: no device");
			return false;
		}
		const auto text = Shader::LoadSource(Util::DataDirectory() / "Shaders" / "FO4", kFile);
		if (!text) {
			REX::ERROR("FrameGeneration: {}", text.error());
			return false;
		}
		const auto result = Shader::CompileComputeShader(text->text, kFile, "main");
		if (!result.Succeeded()) {
			REX::ERROR("FrameGeneration: {}", result.diagnostics);
			return false;
		}
		if (device->CreateComputeShader(result.bytecode.data(), result.bytecode.size(), nullptr, std::addressof(_copyDepth)) < 0) {
			REX::ERROR("FrameGeneration: CreateComputeShader failed");
			return false;
		}
		static_cast<void>(Render::SetDebugName(reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_copyDepth), "FO4CS_CS_FrameGenerationCopyDepth"));
		return true;
	}

	bool FrameGeneration::Setup()
	{
		_frames = 0;
		_captures = 0;
		_hudless = 0;
		_rt0IsBackBuffer = 0;
		_refusedCaptures = 0;
		_generating = false;
		_blocker = "<start>";
		_reset = true;
		_havePrevious = false;
		_haveCamera = false;

		if (!Bridge::Active()) {
			REX::ERROR("FrameGeneration: no bridge - {}", Bridge::Refusal());
			return false;
		}
		if (!Upscale::Hooks::Installed()) {
			REX::ERROR("FrameGeneration: the engine hooks are not installed ({})", Upscale::Hooks::FailedSite());
			return false;
		}
		if (!Bridge::SimulationHookInstalled()) {
			REX::ERROR("FrameGeneration: no frame start for Reflex (Main::OnIdle hook missing)");
			return false;
		}
		if (!CompileShader()) {
			return false;
		}
		if (!_constants.Create(sizeof(CopyDepthConstants), "FO4CS_CB_FrameGenerationCopyDepth")) {
			return false;
		}
		Upscale::Hooks::SetTap(this);
		_tapSet = true;
		REX::INFO("FrameGeneration: running");
		return true;
	}

	const char* FrameGeneration::Blocker() const noexcept
	{
		auto* const ui = RE::UI::GetSingleton();
		if (ui != nullptr && ui->GetMenuOpen("LoadingMenu")) {
			return "loading screen";
		}
		if (ui != nullptr && ui->GetMenuOpen("PipboyMenu")) {
			return "Pip-Boy";
		}
		if (auto* const main = RE::Main::GetSingleton(); main != nullptr && main->inMenuMode) {
			return "a game menu is open";
		}
		if (Menu::IsOverlayOpen()) {
			return "the overlay is open";
		}
		if (!*reinterpret_cast<const bool*>(REL::ID(kEnableTAA).address())) {
			return "the game's TAA is off, so it writes no motion vectors";
		}
		const auto status = Bridge::CurrentStatus();
		if (status.minWidthOrHeight != 0 && (status.width < status.minWidthOrHeight || status.height < status.minWidthOrHeight)) {
			return "the picture is smaller than DLSS-G accepts";
		}
		return nullptr;
	}

	void FrameGeneration::Frame()
	{
		++_frames;
		const char* const blocker = Blocker();
		const bool generate = blocker == nullptr;
		if (generate && !_generating) {
			// History from before an off stretch is not this frame's.
			_reset = true;
		}
		_generating = generate;
		Bridge::SetGeneration(generate);
		Bridge::SetReflexMode(Settings::GetString("FrameGeneration/reflex") == "boost" ? 2u : 1u);
		Bridge::SetSplitView(Settings::GetString("FrameGeneration/view") == "split");

		const std::string reason = blocker != nullptr ? blocker : "";
		if (reason != _blocker) {
			REX::INFO("FrameGeneration: {}", reason.empty() ? std::string{ "generating" } : std::format("off - {}", reason));
			_blocker = reason;
		}

		if (_frames % kLogInterval == 0) {
			const auto status = Bridge::CurrentStatus();
			REX::INFO(
				"frame generation: rendered {:.0f}/s, shown {:.0f}/s, {}, DLSS-G status 0x{:X}, vsync support {}, captures {} "
				"depth+motion / {} hud-less / {} refused, RT_000 is the proxy back buffer in {} of them, matrix lean [{:.6f} {:.6f}], "
				"projection residue {:.5f}",
				status.renderedPerSecond, status.shownPerSecond, status.generating ? "generating" : "not generating", status.dlssgStatus,
				status.vsyncSupported, _captures, _hudless, _refusedCaptures, _rt0IsBackBuffer, _matrixLean[0], _matrixLean[1], _residue);
			_captures = 0;
			_hudless = 0;
			_rt0IsBackBuffer = 0;
			_refusedCaptures = 0;
		}
	}

	void FrameGeneration::AfterTemporalData() noexcept
	{
		Bridge::OnRenderStart();
	}

	void FrameGeneration::AfterEffectRange() noexcept
	{
		if (_generating) {
			CaptureDepthAndMotion();
		}
	}

	void FrameGeneration::CaptureDepthAndMotion() noexcept
	{
		Bridge::InputSlot slot{};
		auto* const context = Render::GetContext();
		auto* const motion = Render::Targets::RenderTargetTexture(kMotionVectors);
		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		if (!Bridge::CurrentSlot(slot) || context == nullptr || motion == nullptr || depth == nullptr || _copyDepth == nullptr) {
			++_refusedCaptures;
			return;
		}
		const auto frame = Render::Resolution::Current();
		const auto depthExtent = Render::Resolution::DepthExtent(frame);
		const auto motionExtent = frame.render;

		REX::W32::D3D11_TEXTURE2D_DESC motionDesc{};
		motion->GetDesc(std::addressof(motionDesc));
		if (motionDesc.format != REX::W32::DXGI_FORMAT_R16G16_FLOAT || motionExtent.width > slot.width || motionExtent.height > slot.height ||
			depthExtent.width > slot.width || depthExtent.height > slot.height) {
			++_refusedCaptures;
			return;
		}

		const REX::W32::D3D11_BOX box{ 0, 0, 0, motionExtent.width, motionExtent.height, 1 };
		context->CopySubresourceRegion(static_cast<REX::W32::ID3D11Resource*>(slot.motion), 0, 0, 0, 0, motion, 0, std::addressof(box));

		const CopyDepthConstants constants{ { depthExtent.width, depthExtent.height }, { 0, 0 } };
		static_cast<void>(_constants.Update(std::addressof(constants), sizeof(constants)));
		auto* const buffer = _constants.Buffer();
		auto* uav = static_cast<REX::W32::ID3D11UnorderedAccessView*>(slot.depthUAV);
		context->CSSetShaderResources(0, 1, std::addressof(depth));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(uav), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(buffer));
		context->CSSetShader(_copyDepth, nullptr, 0);
		context->Dispatch(Groups(depthExtent.width), Groups(depthExtent.height), 1);
		REX::W32::ID3D11ShaderResourceView* noView = nullptr;
		REX::W32::ID3D11UnorderedAccessView* noUAV = nullptr;
		REX::W32::ID3D11Buffer* noBuffer = nullptr;
		context->CSSetShaderResources(0, 1, std::addressof(noView));
		context->CSSetUnorderedAccessViews(0, 1, std::addressof(noUAV), nullptr);
		context->CSSetConstantBuffers(0, 1, std::addressof(noBuffer));
		context->CSSetShader(nullptr, nullptr, 0);

		++_captures;
		SubmitCamera(depthExtent.width, depthExtent.height, motionExtent.width, motionExtent.height);
	}

	bool FrameGeneration::TakeCameraCut(const float (&a_worldToCam)[4][4], const float (&a_position)[3]) noexcept
	{
		const auto camera = Render::FogCameraFromMatrix(a_worldToCam, a_position[2]);
		const auto right = camera.rightOverScaleX;
		const auto halfWidth = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
		const Upscale::CameraState current{
			{ a_position[0], a_position[1], a_position[2] },
			{ camera.forward[0], camera.forward[1], camera.forward[2] },
			2.0 * std::atan(halfWidth) * 180.0 / 3.14159265358979323846,
		};
		const bool cut = _haveCamera && Upscale::IsCameraCut(_camera, current);
		_camera = current;
		_haveCamera = true;
		return cut;
	}

	void FrameGeneration::SubmitCamera(std::uint32_t a_depthWidth, std::uint32_t a_depthHeight, std::uint32_t a_motionWidth, std::uint32_t a_motionHeight) noexcept
	{
		auto* const world = RE::Main::WorldRootCamera();
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (world == nullptr || state == nullptr) {
			return;
		}
		const auto& translate = world->GetWorldTransform().translate;
		const float position[3]{ translate.x, translate.y, translate.z };

		Bridge::CameraFrame current{};
		for (std::size_t i = 0; i < 4; ++i) {
			for (std::size_t j = 0; j < 4; ++j) {
				current.worldToClip[i][j] = world->worldToCam[i][j];
			}
		}
		current.position = { position[0], position[1], position[2] };

		if (TakeCameraCut(world->worldToCam, position)) {
			_reset = true;
		}
		// How far rows zero and one lean towards forward: if the engine folds
		// the jitter into worldToCam, these follow the jitter frame by frame.
		const float* const forward = world->worldToCam[3];
		_matrixLean[0] = world->worldToCam[0][0] * forward[0] + world->worldToCam[0][1] * forward[1] + world->worldToCam[0][2] * forward[2];
		_matrixLean[1] = world->worldToCam[1][0] * forward[0] + world->worldToCam[1][1] * forward[1] + world->worldToCam[1][2] * forward[2];

		const auto nearPlane = *reinterpret_cast<const float*>(REL::ID(kCameraNear).address());
		const auto farPlane = *reinterpret_cast<const float*>(REL::ID(kCameraFar).address());
		const auto jitter = Bridge::JitterFromEngineOffset(state->offsetX, state->offsetY, state->screenWidth, state->screenHeight);
		const auto built = Bridge::BuildConstants(current, _havePrevious ? &_previous : nullptr, nearPlane, farPlane, jitter, _reset);
		_previous = current;
		_havePrevious = true;
		if (!built) {
			++_refusedCaptures;
			return;
		}
		_residue = built->diagnosticResidue;
		Bridge::FrameInputs inputs{};
		inputs.constants = *built;
		inputs.depthWidth = a_depthWidth;
		inputs.depthHeight = a_depthHeight;
		inputs.motionWidth = a_motionWidth;
		inputs.motionHeight = a_motionHeight;
		Bridge::SubmitFrameInputs(inputs);
		_reset = false;
	}

	void FrameGeneration::AfterDynamicViewportDefault(bool a_enabled) noexcept
	{
		// doodlum takes the picture at the call that switches the dynamic
		// viewport off; the other calls of this site are not the end of
		// post-processing.
		if (a_enabled || !_generating) {
			return;
		}
		Bridge::InputSlot slot{};
		auto* const context = Render::GetContext();
		auto* const frameBuffer = Render::Targets::RenderTargetTexture(kFrameBuffer);
		if (!Bridge::CurrentSlot(slot) || context == nullptr || frameBuffer == nullptr) {
			++_refusedCaptures;
			return;
		}
		if (static_cast<void*>(frameBuffer) == Bridge::BackBuffer()) {
			++_rt0IsBackBuffer;
		}
		auto* const hudless = static_cast<REX::W32::ID3D11Texture2D*>(slot.hudless);
		REX::W32::D3D11_TEXTURE2D_DESC source{};
		REX::W32::D3D11_TEXTURE2D_DESC target{};
		frameBuffer->GetDesc(std::addressof(source));
		hudless->GetDesc(std::addressof(target));
		if (source.width != target.width || source.height != target.height || source.format != target.format) {
			++_refusedCaptures;
			return;
		}
		context->CopyResource(hudless, frameBuffer);
		Bridge::SubmitHudless();
		++_hudless;
	}

	void FrameGeneration::Shutdown()
	{
		Bridge::SetGeneration(false);
		Bridge::SetSplitView(false);
		if (_tapSet) {
			Upscale::Hooks::SetTap(nullptr);
			_tapSet = false;
		}
		if (_copyDepth != nullptr) {
			_copyDepth->Release();
			_copyDepth = nullptr;
		}
		_constants.Release();
		_generating = false;
	}
}
```

-   [ ] **Step 6: Registrieren**

`src/Feature/FeatureSystem.cpp`: `#include "Features/FrameGeneration.h"`; direkt nach
`TheRegistry().Register(std::make_unique<DLSS>());` (mit dessen Kommentarblock):

```cpp
			// After DLSS: it reads the jitter DLSS sets and the render size it
			// publishes, and is torn down before it.
			TheRegistry().Register(std::make_unique<FrameGeneration>());
```

-   [ ] **Step 7: i18n und Bauen**

Run: `python tools/extract-i18n.py --write`, dann `python tools/extract-i18n.py` (muß bestehen).
`& "$cmake\cmake.exe" --build --preset FO4` — Erfolg; alle Tests grün.

-   [ ] **Step 8: Commit**

```bash
git add src/Features/FrameGeneration.h src/Features/FrameGeneration.cpp package/Features/FrameGeneration src/Feature/FeatureSystem.cpp src/Menu/MenuSystem.h src/Menu/MenuSystem.cpp src/Menu/PerformancePanel.h src/Menu/PerformancePanel.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: DLSS frame generation feature" -m "Switches generation within the bridge, off in menus, the Pip-Boy, on
loading screens and while the overlay is open. Captures depth and
motion after RenderEffectRange and the picture before the HUD, hands
over the camera, and shows the shown frame rate in the corner.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Task 12: Paket

**Files:**

-   Create: `package/Features/FrameGeneration/EXTERNAL`
-   Modify: `tools/verify-package.ps1`

-   [ ] **Step 1: `EXTERNAL`**

```
extern/Streamline/bin/x64/sl.interposer.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/sl.interposer.dll
extern/Streamline/bin/x64/sl.common.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/sl.common.dll
extern/Streamline/bin/x64/sl.dlss_g.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/sl.dlss_g.dll
extern/Streamline/bin/x64/sl.reflex.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/sl.reflex.dll
extern/Streamline/bin/x64/sl.pcl.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/sl.pcl.dll
extern/Streamline/bin/x64/nvngx_dlssg.dll => F4SE/Plugins/CommunityShadersFO4/Streamline/nvngx_dlssg.dll
extern/Streamline/bin/x64/nvngx_dlss.license.txt => F4SE/Plugins/CommunityShadersFO4/Streamline/nvngx_dlss.license.txt
extern/Streamline/bin/x64/reflex.license.txt => F4SE/Plugins/CommunityShadersFO4/Streamline/reflex.license.txt
extern/Streamline/license.txt => F4SE/Plugins/CommunityShadersFO4/Streamline/streamline.license.txt
```

-   [ ] **Step 2: `verify-package.ps1`**

Nach `Check ($staged -contains "F4SE/Plugins/CommunityShadersFO4/nvngx_dlss.dll") …`:

```powershell
# Brought in through package/Features/FrameGeneration/EXTERNAL from the
# Streamline release archive (cmake/Streamline.cmake).
$streamlineFiles = @(
    "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.interposer.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.common.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.dlss_g.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.reflex.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.pcl.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/nvngx_dlssg.dll",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/nvngx_dlss.license.txt",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/reflex.license.txt",
    "F4SE/Plugins/CommunityShadersFO4/Streamline/streamline.license.txt"
)
foreach ($file in $streamlineFiles) {
    Check ($staged -contains $file) "and $file"
}
Check ($staged -contains "Shaders/FO4/FrameGeneration/CopyDepth.hlsl") "and the frame generation shader"
```

Neben `$dlss = …`: `$framegen = @($archives | Where-Object { $_.Name -like "FrameGeneration-*" })`;
neben `Check ($dlss.Count -eq 1) …`: `Check ($framegen.Count -eq 1) "exactly one FrameGeneration archive"`;
in der großen `if`-Bedingung `-and $framegen.Count -eq 1` ergänzen; neben `$dlssEntries = …`:
`$framegenEntries = @(Get-Entries $framegen[0].FullName)`. Nach dem DLSS-Block:

```powershell
    # Streamline's files travel in the FrameGeneration addon and the
    # all-in-one only, and the addon carries nothing else but its shader.
    $framegenExpected = @($streamlineFiles + "Shaders/FO4/FrameGeneration/CopyDepth.hlsl" | Sort-Object)
    Check (
        $null -eq (Compare-Object $framegenExpected (@($framegenEntries) | Sort-Object -Unique))
    ) "the FrameGeneration addon carries the Streamline runtime and its shader, nothing else"
    foreach ($pair in @(
            @{ Name = "base"; Entries = $baseEntries },
            @{ Name = "DLSS addon"; Entries = $dlssEntries },
            @{ Name = "skylighting addon"; Entries = $skyEntries })) {
        Check (-not ($pair.Entries | Where-Object { $_ -like "F4SE/Plugins/CommunityShadersFO4/Streamline/*" })) "the $($pair.Name) does not carry Streamline"
    }
    Check ($aioEntries -contains "F4SE/Plugins/CommunityShadersFO4/Streamline/sl.interposer.dll") "the all-in-one carries Streamline"
```

In der Vereinigung `$union = @(… + $dlssEntries + $framegenEntries | Sort-Object -Unique)`, und in
der letzten `foreach ($pair …)`-Liste `@{ Name = "FrameGeneration addon"; Entries = $framegenEntries },` ergänzen.

-   [ ] **Step 3: Packen und prüfen**

Run: `& "$cmake\cmake.exe" --build --preset FO4 --target package`, dann `pwsh tools/verify-package.ps1`
Expected: alle Prüfungen grün, ein Archiv `FrameGeneration-*.zip` in `dist/`.

-   [ ] **Step 4: Mutation** — in `EXTERNAL` die Zeile für `sl.pcl.dll` per Edit entfernen, packen,
        `verify-package.ps1` muß „and …/sl.pcl.dll" und „the FrameGeneration addon carries …" als FAIL
        melden. Zeile zurück, packen, grün.

-   [ ] **Step 5: Commit** — `build: ship Streamline in its own addon`.

---

## Task 13: Lauf 1 — Brücke an, FG aus (Nutzer)

-   [ ] **Step 1: Fenster umstellen — erst fragen.** Per AskUserQuestion: „Darf ich in
        `F:\SteamLibrary\steamapps\common\Fallout 4\Data\F4SE\Plugins\HighFPSPhysicsFix.ini` die Zeilen
        `#Fullscreen=false` und `#Borderless=true` einkommentieren (randloses Fenster)?" Erst nach Ja per
        Edit ändern; vorher die beiden Zeilen lesen.

-   [ ] **Step 2: Bauen** — `& "$cmake\cmake.exe" --build --preset FO4` (deployt Plugin, Shader und
        Streamline-Dateien). Prüfen, daß `Data\F4SE\Plugins\CommunityShadersFO4\Streamline\` sechs DLLs hat.

-   [ ] **Step 3: Dem Nutzer diese Liste geben**

> **Lauf 1: die Brücke steht, Frame Generation noch aus.** Wozu: belegen, daß das Spiel über die
> D3D12-Brücke läuft, ohne daß sich am Bild etwas ändert, und messen, ob das Bild ohne HUD dort
> liegt, wo wir es abgreifen.
>
> 1. Spiel starten, Spielstand in Sanctuary laden. Overlay öffnen (Taste wie bisher), Seite
>    **Frame Generation**: oben steht „Neustart nötig …". Häkchen **an**. Spiel beenden.
>    _Wenn etwas anderes als „Neustart nötig" dasteht (etwa „exclusive fullscreen"), Bescheid
>    geben — dann hat die Fensterumstellung nicht gegriffen._
> 2. Spiel neu starten, denselben Spielstand laden. _Startet das Spiel nicht oder bleibt
>    schwarz: Bescheid geben, dann liegt der Fehler im Aufbau der Brücke — das Log sagt, wo._
> 3. Umsehen, ein paar Schritte gehen: sieht das Bild aus wie immer? Ist das Spiel randlos im
>    Fenster?
> 4. Overlay öffnen: läßt es sich wie gewohnt bedienen? Unter **Frame Generation** die
>    **Debug view** auf **split** stellen, Overlay schließen. Links muß das HUD fehlen
>    (Kompass, Lebensbalken unten links), rechts muß es da sein. _Fehlt es nirgends oder überall,
>    liegt das HUD-lose Bild nicht da, wo doodlum es abgreift — das ist Risiko R1._ Debug view
>    zurück auf **off**.
> 5. Kurz **DLSS** an (falls aus), Mode **quality**, eine halbe Minute laufen, wieder wie vorher.
> 6. Pip-Boy auf und zu.
> 7. **Alt+Tab** heraus und zurück; einmal **minimieren** (Windows+D) und zurück.
>    _Bild danach schwarz oder eingefroren: Bescheid geben — das ist der Größenwechsel._
> 8. **F11** an einer ruhigen Stelle (Schnappschuß für die Kosten der Brücke).
> 9. Spiel beenden.

-   [ ] **Step 4: Log auswerten** — `CommunityShadersFO4.log` lesen. Belegt sein muß:

    -   `bridge: D3D11CreateDeviceAndSwapChain leads into …` — wer vorher eingehängt war (High FPS
        Physics Fix?), notieren.
    -   `bridge: the game asks for …, windowed true …` und `bridge: in place` — genau einmal; ein
        zweites `the game asks for` bedeutet Review-Focus 1: nachsehen, ob der erste Proxy vorher
        `released its swap chain` meldete.
    -   `Present hooked` nach `kGameDataReady` und `Render::ValidateAndLog` ohne `[E]`.
    -   `bridge: rendered …/s, shown …/s, generation off` je Sekunde; `cpu waits` nahe 0;
        `frames without token` nach dem Laden 0 oder konstant; `simulation thread` = `present thread`.
    -   `frame generation: … RT_000 is the proxy back buffer in N of them` mit N = Anzahl der
        HUD-los-Kopien (sonst R1); `matrix lean` — schwankt er mit dem Jitter (Werte ~1e-4 im
        Wechsel), steckt der Jitter in `worldToCam`, und `BuildConstants` nimmt ihn per Gram-Schmidt
        heraus; `projection residue` nahe 0 (sonst ist die Kameraposition nicht die der Matrix — dann
        `cameraPos` aus `-R^T·t` der Matrix ableiten statt aus `NiCamera`).
    -   Kosten: F11-Schnappschuß gegen die F5.5a-Grundlinie (TAA 6,99 ms).
    -   Keine `[E]`; jede `[W]` erklärt.

-   [ ] **Step 5: Befunde notieren** — in die Stand-Notiz `fallout4-f5-5b-stand.md` und, wenn ein
        Risiko zuschlug, Fix vor Lauf 2 (eigener Commit, Ursache benannt).

---

## Task 14: Lauf 2 — FG an, UI-Meßgate (Nutzer)

-   [ ] **Step 1: Dem Nutzer diese Liste geben**

> **Lauf 2: Frame Generation an.** Wozu: sehen, daß mehr Bilder angezeigt als gerendert werden,
> und prüfen, ob das HUD dabei sauber bleibt.
>
> 1. Spiel starten, Spielstand laden. Die Ecke zeigt jetzt eine dritte Zeile **shown … fps**.
>    _Fehlt sie, erzeugt Frame Generation nichts — Overlay-Seite Frame Generation ansehen und mir
>    sagen, was oben steht._
> 2. Eine Minute normal spielen und umsehen. Fühlt sich das Bild flüssiger an? Zieht etwas
>    Schlieren, flackert etwas an Kanten?
> 3. **Das UI-Meßgate:** schnell nach links und rechts drehen und dabei auf **Kompass**,
>    **Munitionsanzeige** und **Fadenkreuz** achten. Bleiben sie ruhig und scharf, oder zittern,
>    verdoppeln oder verschmieren sie?
> 4. Pip-Boy auf, etwas blättern, zu. Danach wieder **shown … fps**?
> 5. Overlay auf: die Zeile **shown** verschwindet (FG pausiert), Overlay normal bedienbar; zu —
>    sie kommt zurück.
> 6. Ein Ladebildschirm (Tür in ein Gebäude), danach wieder **shown**?
> 7. Unter Frame Generation **Reflex** auf **boost**, eine halbe Minute, zurück auf **on**.
> 8. Spiel beenden.

-   [ ] **Step 2: Log auswerten**
    -   `FrameGeneration: generating`, Wechsel mit Gründen (Pip-Boy, overlay, loading screen).
    -   `shown` ≈ 2 × `rendered`, solange unter 180 Hz; bei 180 fps angezeigt hält VSync die Ausgabe.
    -   Keine Streamline-Zeile „common constants cannot be found for frame N" (sonst R2: Token-Zuordnung).
    -   `DLSS-G status 0x0`; ein anderer Wert wird nach `sl_dlss_g.h` (`DLSSGStatus`) gedeutet.
    -   `bridge: Reflex low latency with boost (result 0)` und zurück.
-   [ ] **Step 3: Meßgate entscheiden** — HUD ruhig: in die Stand-Notiz „UI-Meßgate bestanden,
        UI-Puffer entfällt" und weiter mit Task 16. HUD unruhig: Task 15.

---

## Task 15: UI-Puffer — nur wenn das Meßgate in Lauf 2 fällt

**Files:**

-   Modify: `src/Features/FrameGeneration.h/.cpp`, `src/Bridge/Bridge.h`, `src/Bridge/Native/Presenter.h/.cpp`,
    `src/Bridge/Native/DeviceHook.cpp`; neue Aufrufstelle in `src/Upscale/EngineHooks.cpp` oder
    `src/Bridge/` nach Messung.

-   [ ] **Step 1: Den Beginn des HUD-Zeichnens messen.** `FrameTrace` (F8, Taste im Overlay)
        einen Frame aufzeichnen lassen; im Log den ersten Zeichenaufruf **nach** dem `+0xC5`-Aufruf
        suchen, dessen Render-Ziel `RT_000` ist, und dessen Technik/Klasse notieren (Scaleform zeichnet
        mit eigenen Shadern). Die aufrufende Funktion über die Techniktabelle (`ShaderCatalog`) bestimmen.
-   [ ] **Step 2: Aufrufstelle offline belegen** — mit dem Skript aus dem Scratchpad
        (`addrcheck2.py`, Logik: Address Library v0 + Suche nach `E8` auf das Ziel) die Aufrufstelle des
        HUD-Zeichnens in seinem Aufrufer finden; Ziel-ID und Offset in die Stand-Notiz.
-   [ ] **Step 3: Umlenken.** Im Hook vor dem Original: Slot-Textur `ui` (R8G8B8A8_UNORM, Größe des
        Backbuffers, wie `hudless` in `Presenter::CreateTextures` angelegt, zusätzlich `RTV`) auf 0 löschen
        und `RE::BSGraphics::GetRendererData()->renderTargets[0].rtView` für die Dauer des Aufrufs auf deren
        RTV setzen; danach zurück, und `ui` vormultipliziert über `RT_000` legen (ein Vollbild-Pass mit
        `FullscreenPass`, Blend `One, InvSrcAlpha`), damit das Endbild unverändert bleibt.
-   [ ] **Step 4: Taggen.** In `Presenter::Tag` ein viertes Tag
        `sl::ResourceTag{ a_generate ? &ui : nullptr, sl::kBufferTypeUIColorAndAlpha, sl::ResourceLifecycle::eValidUntilPresent, &full }`.
-   [ ] **Step 5: Lauf 2 wiederholen** (Schritte 1–3 aus Task 14) und Meßgate erneut entscheiden.
-   [ ] **Step 6: Commit** — `feat: hand DLSS-G the HUD as its own layer`, Body mit Aufrufstelle und
        Meßbefund.

---

## Task 16: Lauf 3 — Abnahme (Nutzer)

-   [ ] **Step 1: Dem Nutzer diese Liste geben**

> **Lauf 3: Abnahme.** Wozu: die Zahlen für die Roadmap — dieselbe Stelle mit Frame Generation
> aus und an.
>
> 1. Spiel starten, an eine **schwere Stelle** gehen (Diamond City Markt oder Downtown Boston),
>    stehenbleiben, Blick nicht bewegen.
> 2. Overlay → Frame Generation → Häkchen **aus**, Overlay zu, zehn Sekunden warten, **F11**.
> 3. Overlay → Häkchen **an**, Overlay zu, zehn Sekunden warten, **F11**.
> 4. Noch einmal eine Minute spielen; fällt dir irgendwo etwas auf (HUD, Menüs, schnelle
>    Kameraschnitte wie VATS oder Dialog)?
> 5. Spiel beenden.

-   [ ] **Step 2: Auswerten** — beide F11-Schnappschüsse und die `bridge: rendered/shown`-Zeilen der
        jeweiligen zehn Sekunden: bestanden, wenn `shown` > `rendered` mit FG an, und `rendered` mit FG an
        um nicht mehr als die Kosten von DLSS-G unter `rendered` mit FG aus liegt; keine `[E]`, jede `[W]`
        erklärt, der Nutzer meldet keine Fehler. Was nicht ausgeführt wurde, wird als ungeprüft notiert.

---

## Task 17: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: `docs/fallout4-port/ROADMAP.md`** — Zeile F5.5b auf „abgeschlossen"; Abschnitt
        „Aus Teilprojekt F5.5b bestätigt" mit: wer `D3D11CreateDeviceAndSwapChain` vorher hielt; ob
        `RT_000` an `+0xC5` HUD-los ist; ob der Jitter in `worldToCam` steckt (`matrix lean`); Restwert
        der Projektion; `cpu waits`; gerendert/angezeigt aus Lauf 3 in einer Tabelle wie bei F5.5a;
        Ergebnis des UI-Meßgates; was ungeprüft blieb; die Abweichungen von der Spec (Planabschnitt oben).
        Kopfzeile: als Nächstes F6.
-   [ ] **Step 2: `.claude/CLAUDE.md`** — Abschnitt „Bridge" (kurz): native Insel ohne PCH,
        `Bridge.h` als einzige Naht, Streamline per `cmake/Streamline.cmake`, Backend lebt bis Prozeßende,
        FG-Schalter wirkt nach Neustart.
-   [ ] **Step 3: Erinnerung** — `fallout4-f5-5b-stand.md` auf „abgeschlossen" mit den Zahlen;
        `fallout4-port-roadmap.md` Stand-Zeile; `fallout4-engine-facts.md` um die gemessenen Fakten
        (OnIdle-Aufrufstelle, Thread-Befund, RT_000-Befund).
-   [ ] **Step 4: Commit** — `docs: record what F5.5b confirmed`.
-   [ ] **Step 5:** superpowers:finishing-a-development-branch.
