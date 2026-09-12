# Teilprojekt F3 — Exponential Height Fog: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Analytischer Höhennebel als Vollbildpass hinter der opaken Szene, mit einem Regler, der
den Nebel des Spiels zurücknimmt — und davor ein Frame-Trace, der den Platz dafür mißt.

**Architecture:** `Render::FramePhase` bekommt eine zweite Phase hinter Composite und Himmel; welche
Klasse sie trägt, sagt ein neues Diagnose-Feature `FrameTrace`, das einen Frame lang jede
`SetupTechnique`-Folge der zwölf Shader-Singletons mit gebundenen Zielen protokolliert. Der Nebel
selbst ist ein Pixelshader auf dem Dreieck aus F2: Sichtstrahl aus den in F2 vermessenen
Kameragrößen, Unreal-Integral, Farbe aus `fogState`, Alpha-Mischung auf das HDR-Ziel. Der Spielnebel
wird über `fogState.clamp` zurückgenommen, mit gemessenem Fallback.

**Tech Stack:** C++23, MSVC, CommonLibF4 (`REX::W32`-D3D11, kein `<d3d11.h>`), `D3DCompile`,
ImGui-freies Feature, HLSL `ps_5_0`.

**Spec:** `docs/superpowers/specs/2026-09-12-fallout4-f3-exponential-height-fog-design.md`

## Global Constraints

-   **C++23, MSVC, `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`.** Eine neue Warnung aus einem
    Fremdheader wird eng auf unserem Ziel unterdrückt, mit einem Kommentar, der den Header nennt —
    nie durch Lockern von `/WX`.
-   **`<d3d11.h>`, `<dxgi.h>` und `<Windows.h>` sind verboten.** D3D-Typen kommen aus `REX::W32`;
    der PCH bringt nur `REL` und `REX` mit, also `REX/W32/D3D11.h` selbst einbinden.
-   **Kein ImGui in `src/Features/`.** Ein Feature bekommt seine Oberfläche allein dadurch, daß es
    Einstellungen deklariert.
-   **Einstellungspfade sind `"Block/Key"`.** Ganze Zahlen werden als `double` gespeichert.
-   **Einstellungen werden bei jeder Verwendung frisch über `Settings::Get*` gelesen.**
-   **`Setup` schlägt nur an Dingen fehl, die wieder fehlschlügen.** Warten auf die Engine gehört in
    `Frame`.
-   **`Shutdown` läuft im laufenden Spiel** und muß nach einem halb gescheiterten `Setup`
    aufrufbar sein. Ein Feature, das Engine-Speicher beschreibt, gibt ihn dort zurück.
-   **Ein Profiler-Bereich heißt nie wie das Feature selbst.** Der Rückruf einer Phase heißt
    `<Feature>/Draw`; der Profiler schlüsselt nach Namen (F2).
-   **Werte, die sich ändern, werden periodisch protokolliert, nie einmalig.** Eine Ablehnung nennt
    die Prüfung, die riß.
-   **Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen.** Vorher wird geprüft, daß der
    Bau geglückt ist — eine Mutation, die nicht übersetzt, hinterläßt das alte Executable. Eine
    Mutation, die nicht fällt, ist ein Befund über den Test. Mutationen mit dem Edit-Werkzeug setzen
    **und zurücknehmen**, nie mit `git checkout`.
-   **Commits:** Conventional Commits, Titel höchstens 50 Zeichen, Rumpf auf 72 umbrochen, Englisch,
    Abschluß `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. Dokumente unter `docs/`
    sind Deutsch. pre-commit formatiert; schlägt prettier an, dieselben Dateien erneut `add` und
    noch einmal committen.
-   **Branch:** `port/f3-exponential-height-fog`, bereits angelegt. Nicht nach `dev` mergen, bevor
    die Abnahme steht.
-   **Bauen:**
    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build --preset FO4
    & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure
    ```
    Für ein einzelnes Ziel `--target <Name>`. Der volle Bau schreibt über den Deploy-Schritt direkt
    nach `F:/SteamLibrary/steamapps/common/Fallout 4/Data`. Quellen laufen über `GLOB_RECURSE`;
    eine neue `.cpp` unter `src/` braucht keinen CMake-Eintrag, aber einen Konfigurationslauf, den
    der Bau selbst anstößt.
-   **Quelltext niemals durch eine Shell-Pipeline schreiben.** Für Dateiinhalte Write und Edit.
-   **Spiel starten über `f4se_loader.exe`**, nie über Steam. Log unter
    `C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`, die letzten fünf
    Läufe als `.1.log` bis `.5.log`. Die Maschine hält rund 180 fps.

---

## Dateiübersicht

| Datei                                                                                                                                                    | Verantwortung                                                               |
| -------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `src/Menu/Hotkeys.{h,cpp}` (neu)                                                                                                                         | Feste Tabelle von `KeyLatch`-Zeigern, denen das Menü übrige Tasten anbietet |
| `src/Menu/MenuSystem.cpp`                                                                                                                                | Bietet übrige Tasten zusätzlich den `Hotkeys` an                            |
| `src/Shader/ShaderCatalog.{h,cpp}`                                                                                                                       | `TechniqueName` wird öffentlich                                             |
| `src/Features/FrameTrace/FrameTraceLog.{h,cpp}` (neu)                                                                                                    | Reiner Teil des Trace: Rohaufzeichnung mit Deckel, Zeilenaufbau, Zählung    |
| `src/Features/FrameTrace.{h,cpp}` (neu)                                                                                                                  | Das Diagnose-Feature: Patches, Thunks, Aufzeichnung, Auflösung, Log         |
| `src/Render/FramePhase.{h,cpp}`                                                                                                                          | Zwei Phasen statt einer                                                     |
| `src/Render/Targets.h`, `Targets.cpp`                                                                                                                    | `kSceneHDR`                                                                 |
| `src/Render/StateGuard.{h,cpp}`                                                                                                                          | Sichert zusätzlich den Pixel-Konstantenpuffer auf Platz 1                   |
| `src/Render/Camera.{h,cpp}`                                                                                                                              | `FogCamera` und `FogCameraFromMatrix`                                       |
| `src/Features/ExponentialHeightFog.{h,cpp}` (neu)                                                                                                        | Das Feature                                                                 |
| `src/Features/ScreenSpaceShadows.cpp`                                                                                                                    | Übergibt `Phase::kBeforeComposite`                                          |
| `src/Feature/FeatureSystem.cpp`                                                                                                                          | Registriert `FrameTrace` und `ExponentialHeightFog`                         |
| `package/Features/ExponentialHeightFog/Shaders/FO4/ExponentialHeightFog/Fog.hlsl` (neu)                                                                  | Der Nebelpass                                                               |
| `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json`                                                                                          | Katalog, erzeugt                                                            |
| `tests/KeyLatchTests.cpp`, `tests/FrameTraceTests.cpp` (neu), `tests/CameraTests.cpp`, `tests/PhaseDispatcherTests.cpp`, `tests/SettingsSchemaTests.cpp` | Host-Tests                                                                  |
| `CMakeLists.txt`                                                                                                                                         | `FrameTraceTests`, `KeyLatchTests` linkt `Hotkeys.cpp`                      |
| `tools/verify-package.ps1`                                                                                                                               | Fünftes Archiv                                                              |
| `docs/fallout4-port/ROADMAP.md`                                                                                                                          | Status und „Aus Teilprojekt F3 bestätigt"                                   |

---

## Task 1: Tasten für Features

Ein Feature kann bisher keine Taste bekommen: `WindowHook` reicht übrige Tasten an genau einen
Rückruf, und der füttert `TheLogLatch()` des Menüs. `Menu::Hotkeys` ist eine feste Tabelle von
`KeyLatch`-Zeigern, der das Menü dieselben Tasten anbietet. Der Fensterthread liest die Tabelle,
der Render-Thread schreibt sie; deshalb atomare Zeiger. Ein eingetragener Latch muß den Prozeß
überleben oder vor seiner Zerstörung ausgetragen werden — `FrameTrace` hält seinen als `static`.

**Files:**

-   Create: `src/Menu/Hotkeys.h`, `src/Menu/Hotkeys.cpp`
-   Modify: `src/Menu/MenuSystem.cpp:182-188`
-   Modify: `CMakeLists.txt:574-577` (KeyLatchTests)
-   Test: `tests/KeyLatchTests.cpp`

**Interfaces:**

-   Consumes: `Menu::KeyLatch` (`SetKey`, `Offer`, `Take`).
-   Produces: `Menu::Hotkeys` mit `bool Register(KeyLatch&)`, `void Unregister(KeyLatch&)`,
    `void Offer(std::uint32_t)`, `std::size_t Count() const`; `Menu::Hotkeys& Menu::TheHotkeys()`.

-   [ ] **Step 1: Test schreiben**

An `tests/KeyLatchTests.cpp` vor der Abschlußzeile (`std::printf("\n%s\n", …)`) anfügen,
`#include "Menu/Hotkeys.h"` oben ergänzen:

```cpp
	{
		Menu::Hotkeys hotkeys;
		Menu::KeyLatch a;
		Menu::KeyLatch b;
		a.SetKey(0x79);
		b.SetKey(0x7A);

		Check(hotkeys.Register(a), "a latch registers");
		Check(hotkeys.Register(b), "and a second");
		Check(hotkeys.Count() == 2, "both are counted");
		Check(!hotkeys.Register(a), "registering the same latch twice is refused");

		hotkeys.Offer(0x79);
		Check(a.Take(), "an offered key reaches the latch bound to it");
		Check(!b.Take(), "and not the other");

		hotkeys.Unregister(a);
		Check(hotkeys.Count() == 1, "unregistering removes exactly one");
		hotkeys.Offer(0x79);
		Check(!a.Take(), "an unregistered latch is offered nothing");

		hotkeys.Unregister(a);
		Check(hotkeys.Count() == 1, "unregistering twice is harmless");
	}

	{
		// The table is fixed; the ninth is refused rather than dropped silently.
		Menu::Hotkeys hotkeys;
		Menu::KeyLatch latches[Menu::Hotkeys::kMaxLatches + 1];
		bool allIn = true;
		for (std::size_t i = 0; i < Menu::Hotkeys::kMaxLatches; ++i) {
			allIn = hotkeys.Register(latches[i]) && allIn;
		}
		Check(allIn, "the table takes kMaxLatches latches");
		Check(!hotkeys.Register(latches[Menu::Hotkeys::kMaxLatches]), "and refuses one more");
	}
```

-   [ ] **Step 2: Bauen, Fehlschlag erwarten**

`cmake --build --preset FO4 --target KeyLatchTests` — erwartet: Fehler, `Menu/Hotkeys.h` fehlt.

-   [ ] **Step 3: Header schreiben**

`src/Menu/Hotkeys.h`:

```cpp
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace Menu
{
	class KeyLatch;

	/// Latches outside the menu that want the key presses nothing above took.
	///
	/// WindowHook hands every remaining press to one callback; the menu owns
	/// that callback and used it for its own log key alone. A feature that
	/// wants a key of its own registers a latch here, and the menu offers the
	/// press to every one of them. The latch decides whether it matches.
	///
	/// Read on the window thread, written on the render thread, which is why
	/// the entries are atomic pointers. A registered latch has to outlive the
	/// process or be unregistered before it is destroyed: Offer may be
	/// reading it at that moment. FrameTrace keeps its latch static.
	class Hotkeys
	{
	public:
		static constexpr std::size_t kMaxLatches = 8;

		/// False when the table is full or the latch is already in it.
		[[nodiscard]] bool Register(KeyLatch& a_latch) noexcept;

		/// Harmless for a latch that is not registered.
		void Unregister(KeyLatch& a_latch) noexcept;

		/// From the window thread: every latch is offered the press.
		void Offer(std::uint32_t a_key) noexcept;

		[[nodiscard]] std::size_t Count() const noexcept;

	private:
		std::array<std::atomic<KeyLatch*>, kMaxLatches> _latches{};
	};

	[[nodiscard]] Hotkeys& TheHotkeys() noexcept;
}
```

-   [ ] **Step 4: Implementierung schreiben**

`src/Menu/Hotkeys.cpp`:

```cpp
#include "Menu/Hotkeys.h"

#include "Menu/KeyLatch.h"

namespace Menu
{
	bool Hotkeys::Register(KeyLatch& a_latch) noexcept
	{
		for (auto& slot : _latches) {
			if (slot.load(std::memory_order_acquire) == std::addressof(a_latch)) {
				return false;
			}
		}

		for (auto& slot : _latches) {
			KeyLatch* expected = nullptr;
			if (slot.compare_exchange_strong(
					expected, std::addressof(a_latch), std::memory_order_acq_rel)) {
				return true;
			}
		}

		return false;
	}

	void Hotkeys::Unregister(KeyLatch& a_latch) noexcept
	{
		for (auto& slot : _latches) {
			KeyLatch* expected = std::addressof(a_latch);
			if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
				return;
			}
		}
	}

	void Hotkeys::Offer(std::uint32_t a_key) noexcept
	{
		for (auto& slot : _latches) {
			if (auto* const latch = slot.load(std::memory_order_acquire); latch != nullptr) {
				latch->Offer(a_key);
			}
		}
	}

	std::size_t Hotkeys::Count() const noexcept
	{
		std::size_t count = 0;
		for (const auto& slot : _latches) {
			if (slot.load(std::memory_order_acquire) != nullptr) {
				++count;
			}
		}
		return count;
	}

	Hotkeys& TheHotkeys() noexcept
	{
		static Hotkeys hotkeys;
		return hotkeys;
	}
}
```

-   [ ] **Step 5: Test verdrahten**

In `CMakeLists.txt` beim Ziel `KeyLatchTests` die Quelle ergänzen:

```cmake
    add_executable(
        KeyLatchTests
        "${CMAKE_SOURCE_DIR}/tests/KeyLatchTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Menu/KeyLatch.cpp"
        "${CMAKE_SOURCE_DIR}/src/Menu/Hotkeys.cpp"
    )
```

-   [ ] **Step 6: Menü anschließen**

In `src/Menu/MenuSystem.cpp` `#include "Menu/Hotkeys.h"` ergänzen und den letzten Rückruf von
`InstallWindowHook` ändern:

```cpp
			[](std::uint32_t a_key) {
				TheLogLatch().Offer(a_key);
				TheHotkeys().Offer(a_key);
			});
```

-   [ ] **Step 7: Bauen und Test laufen lassen**

`cmake --build --preset FO4 --target KeyLatchTests`, dann `build/FO4/Release/KeyLatchTests.exe`.
Erwartet: alle Zeilen `ok`, darunter die zehn neuen.

-   [ ] **Step 8: Mutation**

In `Hotkeys::Offer` die Schleife auf den ersten Eintrag beschränken (`break;` nach dem ersten
`Offer`). Bauen, Frische der Exe prüfen (`ls -la --time-style=+%H:%M:%S`), Test laufen lassen.
Erwartet fällt: „an offered key reaches the latch bound to it" bleibt `ok`, aber nach `Unregister(a)`
steht `b` allein an erster Stelle — die Mutation zeigt sich nicht dort. Deshalb **zweite
Mutation**: `Register` läßt den Doppelt-Check weg. Erwartet fällt: „registering the same latch
twice is refused" und „both are counted" bleibt, aber „unregistering removes exactly one" fällt,
weil zwei Einträge auf `a` zeigen. Beide Mutationen mit Edit zurücknehmen, Test wieder grün.

-   [ ] **Step 9: Commit**

```bash
git add src/Menu/Hotkeys.h src/Menu/Hotkeys.cpp src/Menu/MenuSystem.cpp CMakeLists.txt tests/KeyLatchTests.cpp
git commit -m "feat: let a feature register a hotkey latch"
```

---

## Task 2: Der reine Teil des Trace

Alles am Trace, das ohne D3D auskommt: die Rohaufzeichnung mit festem Deckel, der Zeilenaufbau
aus aufgelösten Namen, die Zählung je Klasse. Der Thunk schreibt nur Zeiger in ein festes Feld;
Namen werden später aufgelöst, aus `Present`, wenn der Frame vorbei ist.

**Files:**

-   Create: `src/Features/FrameTrace/FrameTraceLog.h`, `src/Features/FrameTrace/FrameTraceLog.cpp`
-   Create: `tests/FrameTraceTests.cpp`
-   Modify: `CMakeLists.txt` (neues Testziel nach `BendDispatchTests`)

**Interfaces:**

-   Produces: `Features::Trace::RawRecord`, `Features::Trace::Recorder` (`Push`, `Records`,
    `Overflow`, `Clear`, `kMaxRecords = 512`), `Features::Trace::Entry`, `Features::Trace::Binding`,
    `std::string Features::Trace::FormatLine(std::size_t, const Entry&)`,
    `std::vector<std::size_t> Features::Trace::CountByClass(std::span<const Entry>, std::size_t)`.

-   [ ] **Step 1: Test schreiben**

`tests/FrameTraceTests.cpp`:

```cpp
#include "Features/FrameTrace/FrameTraceLog.h"

#include <cstdio>
#include <string>
#include <vector>

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

	bool Contains(const std::string& a_text, const char* a_part)
	{
		return a_text.find(a_part) != std::string::npos;
	}
}

int main()
{
	using namespace Features::Trace;

	{
		// A line carries everything that identifies the call: order, class,
		// technique id in hex, its name, and what was bound, by slot.
		Entry entry;
		entry.className = "BSDFCompositeShader";
		entry.technique = 0x209;
		entry.techniqueName = "DFComposite_Base_ApplyAO";
		entry.targets = { { 0, "FO4_RT_058" }, { 1, "FO4_RT_059" } };
		entry.depth = "FO4_DS_002";
		entry.resources = { { 0, "FO4_RT_022" }, { 3, "FO4_RT_020" } };

		const auto line = FormatLine(17, entry);
		Check(Contains(line, "#17"), "the order is in the line");
		Check(Contains(line, "BSDFCompositeShader"), "and the class");
		Check(Contains(line, "0x0209"), "and the technique id, four hex digits");
		Check(Contains(line, "DFComposite_Base_ApplyAO"), "and its name");
		Check(Contains(line, "RTV0 FO4_RT_058"), "and the first target with its slot");
		Check(Contains(line, "RTV1 FO4_RT_059"), "and the second");
		Check(Contains(line, "DSV FO4_DS_002"), "and the depth view");
		Check(Contains(line, "SRV3 FO4_RT_020"), "and a resource with the slot it sat in");
	}

	{
		// Nothing bound reads as nothing, not as an empty list of slots.
		Entry entry;
		entry.className = "BSUtilityShader";
		entry.technique = 1;
		const auto line = FormatLine(1, entry);
		Check(Contains(line, "RTV -"), "no targets reads as a dash");
		Check(Contains(line, "DSV -"), "so does no depth view");
		Check(Contains(line, "SRV -"), "and no resources");
		Check(!Contains(line, "RTV0"), "and no slot is invented");
	}

	{
		// An unnamed technique still gets a line; the id stands alone.
		Entry entry;
		entry.className = "BSWaterShader";
		entry.technique = 0x1F;
		const auto line = FormatLine(2, entry);
		Check(Contains(line, "0x001F"), "an unnamed technique shows its id");
	}

	{
		std::vector<Entry> entries(5);
		entries[0].classIndex = 2;
		entries[1].classIndex = 0;
		entries[2].classIndex = 2;
		entries[3].classIndex = 2;
		entries[4].classIndex = 1;

		const auto counts = CountByClass(entries, 3);
		Check(counts.size() == 3, "one count per class");
		Check(counts[0] == 1 && counts[1] == 1 && counts[2] == 3, "each class counted where it ran");
	}

	{
		Recorder recorder;
		Check(recorder.Records().empty(), "a fresh recorder holds nothing");

		RawRecord record{};
		record.classIndex = 4;
		record.technique = 7;
		Check(recorder.Push(record), "a record is taken");
		Check(recorder.Records().size() == 1, "and counted");
		Check(recorder.Records()[0].technique == 7, "and kept as given");

		for (std::size_t i = 1; i < Recorder::kMaxRecords; ++i) {
			static_cast<void>(recorder.Push(record));
		}
		Check(recorder.Records().size() == Recorder::kMaxRecords, "the recorder fills to its cap");
		Check(!recorder.Push(record), "the next is refused");
		Check(!recorder.Push(record), "and the one after");
		Check(recorder.Overflow() == 2, "and both are counted as overflow");

		recorder.Clear();
		Check(recorder.Records().empty() && recorder.Overflow() == 0, "Clear empties both");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 2: Testziel anlegen**

In `CMakeLists.txt` nach dem Block `BendDispatchTests` (vor `CameraTests`):

```cmake
    add_executable(
        FrameTraceTests
        "${CMAKE_SOURCE_DIR}/tests/FrameTraceTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Features/FrameTrace/FrameTraceLog.cpp"
    )

    target_include_directories(
        FrameTraceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(FrameTraceTests PRIVATE cxx_std_23)
    target_precompile_headers(
        FrameTraceTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(FrameTraceTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            FrameTraceTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME FrameTrace COMMAND FrameTraceTests)
```

-   [ ] **Step 3: Bauen, Fehlschlag erwarten**

`cmake --build --preset FO4 --target FrameTraceTests` — erwartet: Fehler, Header fehlt.

-   [ ] **Step 4: Header schreiben**

`src/Features/FrameTrace/FrameTraceLog.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Features::Trace
{
	/// What the thunk writes down: pointers and numbers, nothing that
	/// allocates. The views are held with a reference each and released by
	/// whoever resolves them.
	struct RawRecord
	{
		static constexpr std::size_t kTargets = 8;
		static constexpr std::size_t kResources = 16;

		std::size_t classIndex{ 0 };
		const void* self{ nullptr };
		std::uint32_t technique{ 0 };
		void* targets[kTargets]{};
		void* depth{ nullptr };
		void* resources[kResources]{};
	};

	/// A fixed field of records, written on the render thread inside the hook
	/// and read on the same thread from Present. No lock is needed because both
	/// sides are that one thread; what matters is that Push never allocates.
	class Recorder
	{
	public:
		static constexpr std::size_t kMaxRecords = 512;

		/// False once the field is full; the refusal is counted.
		bool Push(const RawRecord& a_record) noexcept;

		[[nodiscard]] std::span<const RawRecord> Records() const noexcept;
		[[nodiscard]] std::size_t Overflow() const noexcept { return _overflow; }

		/// Forgets the records. It does not release anything - the caller
		/// releases the views before calling this.
		void Clear() noexcept;

	private:
		std::array<RawRecord, kMaxRecords> _records{};
		std::size_t _count{ 0 };
		std::size_t _overflow{ 0 };
	};

	/// One bound object, by the slot it sat in.
	struct Binding
	{
		std::uint32_t slot{ 0 };
		std::string name;
	};

	/// A record with its names resolved.
	struct Entry
	{
		std::size_t classIndex{ 0 };
		std::string_view className;
		std::uint32_t technique{ 0 };
		std::string techniqueName;
		std::vector<Binding> targets;
		std::string depth;
		std::vector<Binding> resources;
	};

	/// The line the log carries for one call. Slots that were empty are left
	/// out; a stage with nothing bound reads as a dash.
	[[nodiscard]] std::string FormatLine(std::size_t a_order, const Entry& a_entry);

	/// How often each class ran, indexed by classIndex.
	[[nodiscard]] std::vector<std::size_t> CountByClass(
		std::span<const Entry> a_entries,
		std::size_t a_classCount);
}
```

-   [ ] **Step 5: Implementierung schreiben**

`src/Features/FrameTrace/FrameTraceLog.cpp`:

```cpp
#include "Features/FrameTrace/FrameTraceLog.h"

#include <format>

namespace Features::Trace
{
	bool Recorder::Push(const RawRecord& a_record) noexcept
	{
		if (_count >= kMaxRecords) {
			++_overflow;
			return false;
		}

		_records[_count] = a_record;
		++_count;
		return true;
	}

	std::span<const RawRecord> Recorder::Records() const noexcept
	{
		return { _records.data(), _count };
	}

	void Recorder::Clear() noexcept
	{
		_count = 0;
		_overflow = 0;
	}

	namespace
	{
		std::string Bindings(std::string_view a_prefix, std::span<const Binding> a_bindings)
		{
			if (a_bindings.empty()) {
				return "-";
			}

			std::string text;
			for (const auto& binding : a_bindings) {
				if (!text.empty()) {
					text += ' ';
				}
				text += std::format("{}{} {}", a_prefix, binding.slot, binding.name);
			}
			return text;
		}
	}

	std::string FormatLine(std::size_t a_order, const Entry& a_entry)
	{
		return std::format(
			"  #{:<3} {:<24} 0x{:04X} {:<36} RTV {}  DSV {}  SRV {}",
			a_order,
			a_entry.className,
			a_entry.technique,
			a_entry.techniqueName,
			Bindings("RTV", a_entry.targets),
			a_entry.depth.empty() ? std::string{ "-" } : a_entry.depth,
			Bindings("SRV", a_entry.resources));
	}

	std::vector<std::size_t> CountByClass(
		std::span<const Entry> a_entries,
		std::size_t a_classCount)
	{
		std::vector<std::size_t> counts(a_classCount, 0);
		for (const auto& entry : a_entries) {
			if (entry.classIndex < a_classCount) {
				++counts[entry.classIndex];
			}
		}
		return counts;
	}
}
```

-   [ ] **Step 6: Bauen und Test laufen lassen**

`cmake --build --preset FO4 --target FrameTraceTests`, dann `build/FO4/Release/FrameTraceTests.exe`.
Erwartet: alle `ok`. Beachte: `RTV -` entsteht aus `"RTV {}"` mit `"-"`; `SRV3 FO4_RT_020` aus
Präfix und Slot ohne Leerzeichen dazwischen — genau so ist es im Test verlangt.

-   [ ] **Step 7: Mutation**

In `Recorder::Push` den Deckel auf `kMaxRecords + 1` setzen. Bauen, Frische prüfen, laufen lassen.
Erwartet fällt: „the next is refused" und „and both are counted as overflow". Zurücknehmen. Zweite
Mutation: in `Bindings` das Präfix weglassen. Erwartet fällt: „and the first target with its slot",
„and a resource with the slot it sat in". Zurücknehmen, grün.

-   [ ] **Step 8: Commit**

```bash
git add src/Features/FrameTrace/FrameTraceLog.h src/Features/FrameTrace/FrameTraceLog.cpp tests/FrameTraceTests.cpp CMakeLists.txt
git commit -m "feat: the pure half of a frame trace"
```

---

## Task 3: Techniknamen öffentlich

`ShaderCatalog.cpp` hält `TechniqueName` in einem anonymen Namensraum. Der Trace braucht es.

**Files:**

-   Modify: `src/Shader/ShaderCatalog.h` (nach `ShaderClasses()`), `src/Shader/ShaderCatalog.cpp:38`

**Interfaces:**

-   Produces: `std::string Shader::TechniqueName(const void* a_shader, std::uint32_t a_id) noexcept`.

-   [ ] **Step 1: Deklaration ergänzen**

In `src/Shader/ShaderCatalog.h` nach `ShaderClasses()`:

```cpp
	/// The engine's own name for one technique of one shader object, through
	/// vtable slot 09. Empty when the engine gives none.
	///
	/// A call into the game, not a read of it: callers pass only an object
	/// whose class Util::DescribeObject confirmed - the slot numbering is
	/// commonlibf4's, and on a wrong object it would call something else.
	[[nodiscard]] std::string TechniqueName(const void* a_shader, std::uint32_t a_id) noexcept;
```

-   [ ] **Step 2: Definition herausziehen**

In `src/Shader/ShaderCatalog.cpp` die Funktion `TechniqueName` unverändert aus dem anonymen
Namensraum in den Namensraum `Shader` verschieben (der Aufruf in `ReportShaderTechniques` bleibt
gültig). Bauen: `cmake --build --preset FO4 --target CommunityShadersFO4`. Erwartet: grün.

-   [ ] **Step 3: Commit**

```bash
git add src/Shader/ShaderCatalog.h src/Shader/ShaderCatalog.cpp
git commit -m "refactor: expose the technique name lookup"
```

---

## Task 4: Das Feature `FrameTrace`

**Files:**

-   Create: `src/Features/FrameTrace.h`, `src/Features/FrameTrace.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (Registrierung vor `ShaderCensus`)

**Interfaces:**

-   Consumes: `Shader::ShaderClasses()`, `Shader::TechniqueName`, `Util::DescribeVTable`,
    `Util::DescribeObject`, `Render::VTablePatch`, `Render::GetContext`, `Render::GetViewTargetName`,
    `Render::FrameCount`, `Menu::TheHotkeys`, `Menu::KeyLatch`, `Features::Trace::*`.
-   Produces: das Feature; Einstellungen `FrameTrace/enabled` (aus), `FrameTrace/key` (F10).

-   [ ] **Step 1: Header schreiben**

`src/Features/FrameTrace.h`:

```cpp
#pragma once

#include "Feature/Feature.h"

namespace Features
{
	/// One frame of the engine as a list, on a key press.
	///
	/// A measurement, not an effect, and the sibling of ShaderCensus: the
	/// census answered which techniques exist, this answers in what order the
	/// twelve shader classes run inside a frame and what each had bound. That
	/// is the question every screen space feature asks before it picks its
	/// anchor, so it is a permanent feature rather than a probe written four
	/// times over.
	///
	/// Slot 02 of each class's vtable is patched once and never restored - see
	/// FramePhase for why a vtable entry is not taken back in a running game.
	/// Disarmed, the thunk costs one atomic load. Armed by the key, it records
	/// the next whole frame between two Presents into a fixed field of raw
	/// pointers, and the frame after that resolves the names and writes the
	/// block.
	class FrameTrace final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameTrace"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;
	};
}
```

-   [ ] **Step 2: Implementierung schreiben**

`src/Features/FrameTrace.cpp`:

```cpp
#include "Features/FrameTrace.h"

#include "Features/FrameTrace/FrameTraceLog.h"
#include "Menu/Hotkeys.h"
#include "Menu/KeyLatch.h"
#include "Render/DebugName.h"
#include "Render/Renderer.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/S/Sky.h>

#include <REX/W32/D3D11.h>

#include <array>
#include <atomic>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Features
{
	namespace
	{
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;
		constexpr std::size_t kClassCount = 13;
		constexpr std::uint32_t kDefaultKey = 0x79;  // VK_F10

		std::array<Render::VTablePatch, kClassCount> g_patches{};
		std::array<void*, kClassCount> g_original{};
		bool g_installed = false;

		/// Set by the key, cleared when recording starts.
		std::atomic<bool> g_armed{ false };

		/// True for exactly the frame being recorded. Read in the thunk.
		std::atomic<bool> g_recording{ false };

		Trace::Recorder g_recorder;

		Menu::KeyLatch& TheLatch() noexcept
		{
			// Static, never destroyed: Hotkeys may be offering it a key on the
			// window thread at any moment, so it must outlive the process.
			static Menu::KeyLatch latch;
			return latch;
		}

		void ReleaseViews(Trace::RawRecord& a_record) noexcept
		{
			for (auto*& view : a_record.targets) {
				if (view != nullptr) {
					static_cast<REX::W32::ID3D11View*>(view)->Release();
					view = nullptr;
				}
			}
			if (a_record.depth != nullptr) {
				static_cast<REX::W32::ID3D11View*>(a_record.depth)->Release();
				a_record.depth = nullptr;
			}
			for (auto*& view : a_record.resources) {
				if (view != nullptr) {
					static_cast<REX::W32::ID3D11View*>(view)->Release();
					view = nullptr;
				}
			}
		}

		void Capture(std::size_t a_index, void* a_self, std::uint32_t a_pass) noexcept
		{
			auto* const context = Render::GetContext();
			if (context == nullptr) {
				return;
			}

			Trace::RawRecord record{};
			record.classIndex = a_index;
			record.self = a_self;
			record.technique = a_pass;

			// Every Get hands out a reference. They are kept until Present
			// resolves the names, then released there. A record the field
			// refuses releases its own right away.
			context->OMGetRenderTargets(
				static_cast<std::uint32_t>(Trace::RawRecord::kTargets),
				reinterpret_cast<REX::W32::ID3D11RenderTargetView**>(record.targets),
				reinterpret_cast<REX::W32::ID3D11DepthStencilView**>(std::addressof(record.depth)));
			context->PSGetShaderResources(
				0,
				static_cast<std::uint32_t>(Trace::RawRecord::kResources),
				reinterpret_cast<REX::W32::ID3D11ShaderResourceView**>(record.resources));

			if (!g_recorder.Push(record)) {
				ReleaseViews(record);
			}
		}

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			if (g_recording.load(std::memory_order_relaxed)) {
				Capture(N, a_self, a_pass);
			}
			return reinterpret_cast<SetupTechniqueFn>(g_original[N])(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kClassCount>{});

		std::string NameOfView(void* a_view) noexcept
		{
			if (a_view == nullptr) {
				return {};
			}
			auto name = Render::GetViewTargetName(static_cast<REX::W32::ID3D11View*>(a_view));
			return name.empty() ? std::format("{}", a_view) : name;
		}

		/// Turns the raw records into named entries and releases every view.
		std::vector<Trace::Entry> Resolve(std::span<const Shader::ShaderClass> a_classes) noexcept
		{
			std::vector<Trace::Entry> entries;
			entries.reserve(g_recorder.Records().size());

			// The class we patched has to be the class we caught, once per
			// class and object, before slot 09 is called on it - the census's
			// own rule.
			std::array<const void*, kClassCount> confirmed{};

			for (const auto& raw : g_recorder.Records()) {
				Trace::Entry entry;
				entry.classIndex = raw.classIndex;
				entry.className = a_classes[raw.classIndex].className;
				entry.technique = raw.technique;

				if (confirmed[raw.classIndex] != raw.self) {
					const auto info = Util::DescribeObject(raw.self);
					if (info.has_value() &&
						info->className == a_classes[raw.classIndex].className &&
						info->subobjectOffset == 0) {
						confirmed[raw.classIndex] = raw.self;
					}
				}
				if (confirmed[raw.classIndex] == raw.self) {
					entry.techniqueName = Shader::TechniqueName(raw.self, raw.technique);
				}

				for (std::uint32_t i = 0; i < Trace::RawRecord::kTargets; ++i) {
					if (raw.targets[i] != nullptr) {
						entry.targets.push_back({ i, NameOfView(raw.targets[i]) });
					}
				}
				entry.depth = NameOfView(raw.depth);
				for (std::uint32_t i = 0; i < Trace::RawRecord::kResources; ++i) {
					if (raw.resources[i] != nullptr) {
						entry.resources.push_back({ i, NameOfView(raw.resources[i]) });
					}
				}

				entries.push_back(std::move(entry));
			}

			// Records() is const; release through a copy of each pointer set.
			for (auto raw : g_recorder.Records()) {
				ReleaseViews(raw);
			}
			g_recorder.Clear();

			return entries;
		}

		void LogHeader() noexcept
		{
			const auto* const sky = RE::Sky::GetSingleton();
			const auto* const state = RE::BSGraphics::State::GetSingleton();
			const auto* const camera = RE::Main::WorldRootCamera();

			REX::INFO("=== frame trace: frame {} ===", Render::FrameCount());

			if (sky != nullptr) {
				REX::INFO(
					"  sky mode {}, fog distances [{:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f}], "
					"height {:.1f}, power {:.3f}, clamp {:.3f}, high density scale {:.3f}",
					static_cast<std::uint32_t>(sky->mode.get()),
					sky->fogDistances[0], sky->fogDistances[1], sky->fogDistances[2], sky->fogDistances[3],
					sky->fogDistances[4], sky->fogDistances[5], sky->fogDistances[6], sky->fogDistances[7],
					sky->fogHeight, sky->fogPower, sky->fogClamp, sky->fogHighDensityScale);
			}

			if (state != nullptr) {
				const auto& fog = state->fogState;
				REX::INFO(
					"  fogState range [{:.6f} {:.6f} {:.6f} {:.6f}] highLow [{:.6f} {:.6f} {:.6f} {:.6f}] "
					"power {:.3f} clamp {:.3f} highDensityScale {:.3f}",
					fog.rangeData.x, fog.rangeData.y, fog.rangeData.z, fog.rangeData.w,
					fog.highLowRangeData.x, fog.highLowRangeData.y, fog.highLowRangeData.z, fog.highLowRangeData.w,
					fog.power, fog.clamp, fog.highDensityScale);
				REX::INFO(
					"  fogState colours nearLow [{:.3f} {:.3f} {:.3f}] nearHigh [{:.3f} {:.3f} {:.3f}] "
					"farLow [{:.3f} {:.3f} {:.3f}] farHigh [{:.3f} {:.3f} {:.3f}]",
					fog.nearLowColor.r, fog.nearLowColor.g, fog.nearLowColor.b,
					fog.nearHighColor.r, fog.nearHighColor.g, fog.nearHighColor.b,
					fog.farLowColor.r, fog.farLowColor.g, fog.farLowColor.b,
					fog.farHighColor.r, fog.farHighColor.g, fog.farHighColor.b);
			}

			if (camera != nullptr) {
				const auto& t = camera->GetWorldTransform().translate;
				REX::INFO("  camera at [{:.1f} {:.1f} {:.1f}]", t.x, t.y, t.z);
			}
		}
	}

	void FrameTrace::Declare()
	{
		Settings::DeclareFeature("FrameTrace", false)
			.Label("feature.frame_trace.name", "Frame Trace")
			.Help(
				"feature.frame_trace.help",
				"Diagnostic. Writes one frame of the engine's shader calls to the log, with "
				"what each had bound, when the key is pressed.");

		Settings::DeclareKey("FrameTrace/key", kDefaultKey)
			.Label("feature.frame_trace.key", "Trace Key")
			.Help("feature.frame_trace.key_help", "Records the next frame.");
	}

	bool FrameTrace::Setup()
	{
		// The census restores vtable entries once a class has reported, and
		// what it would write back over ours is its own remembered original.
		// Refused rather than raced.
		if (Settings::GetBool("ShaderCensus/enabled")) {
			REX::ERROR("FrameTrace: refusing while ShaderCensus is enabled, it restores the same vtable slots");
			return false;
		}

		if (!g_installed) {
			const auto classes = Shader::ShaderClasses();
			std::size_t installed = 0;

			for (std::size_t i = 0; i < classes.size() && i < kClassCount; ++i) {
				auto** const table = reinterpret_cast<void**>(classes[i].vtable);
				if (table == nullptr) {
					REX::WARN("FrameTrace: no vtable address for {}", classes[i].className);
					continue;
				}

				const auto identity = Util::DescribeVTable(table);
				if (!identity.has_value() ||
					identity->className != classes[i].className ||
					identity->subobjectOffset != 0) {
					REX::WARN(
						"FrameTrace: the vtable id for {} names {} at +0x{:X}, leaving it alone",
						classes[i].className,
						identity.has_value() ? identity->className : std::string{ "nothing" },
						identity.has_value() ? identity->subobjectOffset : 0);
					continue;
				}

				if (!g_patches[i].InstallAtTable(
						table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[i]))) {
					REX::WARN("FrameTrace: could not patch {}", classes[i].className);
					continue;
				}

				g_original[i] = g_patches[i].Original();
				++installed;
			}

			if (installed == 0) {
				REX::ERROR("FrameTrace: no class could be patched");
				return false;
			}

			g_installed = true;
			REX::INFO("FrameTrace: watching {} of {} shader classes, patches stay for the process", installed, classes.size());
		}

		TheLatch().SetKey(Settings::GetUInt32("FrameTrace/key"));
		if (!Menu::TheHotkeys().Register(TheLatch())) {
			REX::ERROR("FrameTrace: no room in the hotkey table");
			return false;
		}

		return true;
	}

	void FrameTrace::Frame()
	{
		// Read every frame, so a rebind through the overlay takes effect.
		TheLatch().SetKey(Settings::GetUInt32("FrameTrace/key"));

		if (TheLatch().Take()) {
			g_armed.store(true, std::memory_order_relaxed);
		}

		// Present is the frame boundary. Armed here means: record everything
		// from now to the next Present. Recording here means: that frame is
		// over, write it out.
		if (g_recording.load(std::memory_order_relaxed)) {
			g_recording.store(false, std::memory_order_relaxed);

			const auto classes = Shader::ShaderClasses();
			const auto overflow = g_recorder.Overflow();
			const auto entries = Resolve(classes);

			LogHeader();
			for (std::size_t i = 0; i < entries.size(); ++i) {
				REX::INFO("{}", Trace::FormatLine(i + 1, entries[i]));
			}

			const auto counts = Trace::CountByClass(entries, classes.size());
			for (std::size_t i = 0; i < counts.size(); ++i) {
				if (counts[i] > 0) {
					REX::INFO("  {:<24} {} call(s)", classes[i].className, counts[i]);
				}
			}
			REX::INFO(
				"=== frame trace: {} call(s){} ===",
				entries.size(),
				overflow > 0 ? std::format(", {} more not recorded", overflow) : std::string{});
			return;
		}

		if (g_armed.exchange(false, std::memory_order_relaxed)) {
			g_recorder.Clear();
			g_recording.store(true, std::memory_order_relaxed);
			REX::INFO("FrameTrace: recording the next frame");
		}
	}

	void FrameTrace::Shutdown()
	{
		Menu::TheHotkeys().Unregister(TheLatch());
		g_armed.store(false, std::memory_order_relaxed);

		// A recording in flight is dropped, and its references with it. The
		// patches stay: taking them back in a running game is the race
		// FramePhase.h describes.
		if (g_recording.exchange(false, std::memory_order_relaxed)) {
			for (auto raw : g_recorder.Records()) {
				ReleaseViews(raw);
			}
			g_recorder.Clear();
		}
	}
}
```

Dazu `#include <span>` für `Resolve`. Die Schleifen `for (std::uint32_t i …)` über
`kTargets`/`kResources` vergleichen `std::uint32_t` mit `std::size_t`; MSVC nimmt das unter `/W4`
hin, weil beide vorzeichenlos sind.

-   [ ] **Step 3: Registrieren**

In `src/Feature/FeatureSystem.cpp` `#include "Features/FrameTrace.h"` ergänzen und **vor**
`ShaderCensus` registrieren:

```cpp
			TheRegistry().Register(std::make_unique<FrameTrace>());
```

-   [ ] **Step 4: Katalog und Bau**

`python tools/extract-i18n.py --write`, dann `python tools/extract-i18n.py` (muß grün sein). Voller
Bau, `ctest`, `pwsh tools/verify-plugin.ps1`. Erwartet: alles grün; der Bau deployt.

-   [ ] **Step 5: Commit**

```bash
git add src/Features/FrameTrace.h src/Features/FrameTrace.cpp src/Feature/FeatureSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: trace one frame of shader calls on a key"
```

---

## Task 5: Zwei Phasen in `FramePhase`

**Files:**

-   Modify: `src/Render/FramePhase.h`, `src/Render/FramePhase.cpp`
-   Modify: `src/Features/ScreenSpaceShadows.cpp` (drei Aufrufe)
-   Test: `tests/PhaseDispatcherTests.cpp`

**Interfaces:**

-   Produces: `enum class Render::Phase { kBeforeComposite, kAfterOpaque, kCount }`,
    `SubscribeFramePhase(Phase, std::string_view, std::function<void()>)`,
    `UnsubscribeFramePhase(Phase, Token)`, `FramePhaseHits(Phase)`.
-   Consumes: `Shader::ShaderClasses()` für die vtable-Adresse je Klasse.

-   [ ] **Step 1: Test erweitern**

An `tests/PhaseDispatcherTests.cpp` vor der Abschlußzeile anfügen:

```cpp
	{
		// Two phases are two dispatchers. A subscriber of one never runs in
		// the other, and each counts its own frames.
		Render::PhaseDispatcher before;
		Render::PhaseDispatcher after;
		int ranBefore = 0;
		int ranAfter = 0;

		const auto tokenBefore = before.Subscribe("before", [&ranBefore] { ++ranBefore; });
		const auto tokenAfter = after.Subscribe("after", [&ranAfter] { ++ranAfter; });

		Check(before.Dispatch(1), "the first phase runs on frame one");
		Check(ranBefore == 1 && ranAfter == 0, "and only its own subscriber ran");
		Check(after.Dispatch(1), "the second phase runs on the same frame independently");
		Check(ranBefore == 1 && ranAfter == 1, "and only its own subscriber ran");

		after.Unsubscribe(tokenBefore);
		Check(after.Count() == 1, "a token of the other phase unsubscribes nothing here");
		before.Unsubscribe(tokenBefore);
		after.Unsubscribe(tokenAfter);
		Check(before.Count() == 0 && after.Count() == 0, "and each token unsubscribes in its own");
	}
```

Bauen, laufen lassen: erwartet grün (der Dispatcher kann das schon; der Test belegt die
Unabhängigkeit, auf der die Tabelle beruht). Mutation: in `Unsubscribe` den Tokenvergleich
weglassen (jeden aktiven Eintrag austragen) — erwartet fällt „a token of the other phase
unsubscribes nothing here". Zurücknehmen.

-   [ ] **Step 2: Header neu schreiben**

`src/Render/FramePhase.h`:

```cpp
#pragma once

#include "Render/PhaseDispatcher.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace Render
{
	/// The points inside a frame where a screen space feature does its work.
	///
	/// Each is reached by replacing slot 02 - SetupTechnique - in the main
	/// vtable of one BSShader class. The first call of that class in each
	/// frame is the moment; every later call in the same frame passes straight
	/// through.
	enum class Phase : std::uint8_t
	{
		/// BSDFCompositeShader. The G-buffer is complete and kDFLight has
		/// written the direct light to RT_058/059, but the composite has not
		/// drawn. Where a feature modulates the lighting - F2.
		kBeforeComposite,

		/// The first class after composite and sky. The opaque scene is
		/// finished in the HDR target; transparents have not drawn. Where a
		/// feature draws onto the picture - F3. Which class that is was
		/// measured by FrameTrace; see the table in FramePhase.cpp.
		kAfterOpaque,

		kCount
	};

	/// Installs every phase's patch. Once, for the life of the process, like
	/// the Present hook from B1: the overlay can switch a feature off in the
	/// middle of a running game, and taking a vtable entry back while another
	/// thread stands in it is a race nothing can win. Features subscribe and
	/// unsubscribe instead.
	///
	/// **Install it before any feature runs.** ShaderCensus patches the same
	/// slots while it counts and restores them as soon as a class has
	/// reported. Underneath it, our thunk is what its Restore writes back;
	/// on top of it, our thunk is what its Restore overwrites. This runs from
	/// kGameDataReady, a feature's Setup runs from Present, which is later.
	///
	/// Returns true when at least kBeforeComposite is in place; a phase that
	/// could not be patched says so in the log and its subscribers never run.
	[[nodiscard]] bool InstallFramePhase() noexcept;

	/// The callback runs inside a Render::PassScope named after a_name, so it
	/// is measured by F1 and named in a capture without the caller doing
	/// anything about it. Returns PhaseDispatcher::kNoToken when the cap is
	/// reached or the phase is out of range.
	///
	/// **The name must not be the feature's own.** The profiler keys its rows
	/// by name alone, and the registry already measures every feature's Frame
	/// under that name from Present. A phase subscribed under the same one
	/// lands in the same row, where the two samples a frame average against
	/// each other: F2's first snapshot showed the draw at half its cost, with
	/// a p95 of exactly twice the mean. "<Feature>/Draw" is the convention.
	PhaseDispatcher::Token SubscribeFramePhase(
		Phase a_phase,
		std::string_view a_name,
		std::function<void()> a_callback);

	void UnsubscribeFramePhase(Phase a_phase, PhaseDispatcher::Token a_token) noexcept;

	/// How often the phase has fired. A number that stays put while the game
	/// renders and something is subscribed means the entry has been
	/// overwritten - see the ordering note above. It is the only symptom.
	[[nodiscard]] std::uint64_t FramePhaseHits(Phase a_phase) noexcept;
}
```

-   [ ] **Step 3: Implementierung neu schreiben**

`src/Render/FramePhase.cpp`:

```cpp
#include "Render/FramePhase.h"

#include "Render/Profiler.h"
#include "Render/SwapChainHook.h"
#include "Render/VTablePatch.h"
#include "Shader/ShaderCatalog.h"
#include "Util/ObjectRTTI.h"

#include <array>
#include <string>
#include <utility>

namespace Render
{
	namespace
	{
		// bool SetupTechnique(std::uint32_t), slot 02 of the BSShader vtable.
		// The slot numbers commonlibf4 gives BSShader are right even though its
		// data offsets are not. Nothing here reads a field, and the self
		// pointer is passed on untouched, so void* is the honest type.
		using SetupTechniqueFn = bool (*)(void*, std::uint32_t);

		constexpr std::size_t kSetupTechniqueSlot = 2;
		constexpr auto kPhaseCount = static_cast<std::size_t>(Phase::kCount);

		struct Anchor
		{
			/// As the RTTI spells it, and as Shader::ShaderClasses lists it.
			const char* className;
			VTablePatch patch;
			void* original{ nullptr };
			PhaseDispatcher dispatcher;
			std::uint64_t hits{ 0 };
			bool installed{ false };
		};

		// kAfterOpaque names the class FrameTrace found first after composite
		// and sky on 2026-09-12. A different measurement changes this string
		// and nothing else.
		std::array<Anchor, kPhaseCount> g_anchors{ {
			{ "BSDFCompositeShader" },
			{ "BSEffectShader" },
		} };

		template <std::size_t N>
		bool ThunkSetupTechnique(void* a_self, std::uint32_t a_pass) noexcept
		{
			auto& anchor = g_anchors[N];
			if (anchor.dispatcher.Dispatch(FrameCount())) {
				++anchor.hits;
			}
			return reinterpret_cast<SetupTechniqueFn>(anchor.original)(a_self, a_pass);
		}

		template <std::size_t... I>
		constexpr std::array<SetupTechniqueFn, sizeof...(I)> MakeThunks(std::index_sequence<I...>)
		{
			return { &ThunkSetupTechnique<I>... };
		}

		constexpr auto kThunks = MakeThunks(std::make_index_sequence<kPhaseCount>{});

		std::uintptr_t VTableOf(std::string_view a_className) noexcept
		{
			for (const auto& shaderClass : Shader::ShaderClasses()) {
				if (shaderClass.className == a_className) {
					return shaderClass.vtable;
				}
			}
			return 0;
		}

		bool InstallOne(std::size_t a_index) noexcept
		{
			auto& anchor = g_anchors[a_index];
			if (anchor.installed) {
				return true;
			}

			auto** const table = reinterpret_cast<void**>(VTableOf(anchor.className));
			if (table == nullptr) {
				REX::ERROR("frame phase: no vtable address for {}", anchor.className);
				return false;
			}

			// The table has to be the one the id promised, and it has to be a
			// primary table, before an entry of it is touched. Finding out
			// afterwards is not an option: the first call through a wrongly
			// patched entry ends the process.
			const auto identity = Util::DescribeVTable(table);
			if (!identity.has_value() ||
				identity->className != anchor.className ||
				identity->subobjectOffset != 0) {
				REX::ERROR(
					"frame phase: the vtable id for {} names {} at +0x{:X}, leaving it alone",
					anchor.className,
					identity.has_value() ? identity->className : std::string{ "nothing" },
					identity.has_value() ? identity->subobjectOffset : 0);
				return false;
			}

			if (!anchor.patch.InstallAtTable(
					table, kSetupTechniqueSlot, reinterpret_cast<void*>(kThunks[a_index]))) {
				REX::ERROR("frame phase: could not patch {}::SetupTechnique", anchor.className);
				return false;
			}

			anchor.original = anchor.patch.Original();
			anchor.installed = true;
			REX::INFO("frame phase {} installed on {}, chaining to {}", a_index, anchor.className, anchor.original);
			return true;
		}

		bool InRange(Phase a_phase) noexcept
		{
			return static_cast<std::size_t>(a_phase) < kPhaseCount;
		}
	}

	bool InstallFramePhase() noexcept
	{
		bool before = false;
		for (std::size_t i = 0; i < kPhaseCount; ++i) {
			const bool ok = InstallOne(i);
			if (i == static_cast<std::size_t>(Phase::kBeforeComposite)) {
				before = ok;
			}
		}
		return before;
	}

	PhaseDispatcher::Token SubscribeFramePhase(
		Phase a_phase,
		std::string_view a_name,
		std::function<void()> a_callback)
	{
		if (!InRange(a_phase)) {
			return PhaseDispatcher::kNoToken;
		}

		// The scope is opened here rather than by the caller, so that every
		// subscriber is measured and not only the ones that remembered to ask.
		std::string name{ a_name };
		return g_anchors[static_cast<std::size_t>(a_phase)].dispatcher.Subscribe(
			a_name,
			[name = std::move(name), callback = std::move(a_callback)] {
				const PassScope scope{ name };
				callback();
			});
	}

	void UnsubscribeFramePhase(Phase a_phase, PhaseDispatcher::Token a_token) noexcept
	{
		if (InRange(a_phase)) {
			g_anchors[static_cast<std::size_t>(a_phase)].dispatcher.Unsubscribe(a_token);
		}
	}

	std::uint64_t FramePhaseHits(Phase a_phase) noexcept
	{
		return InRange(a_phase) ? g_anchors[static_cast<std::size_t>(a_phase)].hits : 0;
	}
}
```

`ShaderClasses()` löst seine Adressen beim ersten Aufruf auf; `InstallFramePhase` läuft aus
`kGameDataReady`, wo die Adressbibliothek steht. Wo `InstallFramePhase` bisher gerufen wird
(`src/XSEPlugin.cpp`), ändert sich nichts.

-   [ ] **Step 4: `ScreenSpaceShadows` anpassen**

Drei Stellen in `src/Features/ScreenSpaceShadows.cpp`:

```cpp
		_phase = Render::SubscribeFramePhase(
			Render::Phase::kBeforeComposite, "ScreenSpaceShadows/Draw", [this] { Draw(); });
```

```cpp
		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, _phase);
```

```cpp
			const auto hits = Render::FramePhaseHits(Render::Phase::kBeforeComposite);
```

-   [ ] **Step 5: Bauen, Tests, Commit**

Voller Bau, `ctest`. Erwartet grün.

```bash
git add src/Render/FramePhase.h src/Render/FramePhase.cpp src/Features/ScreenSpaceShadows.cpp tests/PhaseDispatcherTests.cpp
git commit -m "feat: a second frame phase behind the opaque scene"
```

---

## Task 6: Die Vanilla-Probe (diag, wird wieder entfernt)

Ein Commit mit `diag:`-Präfix, der nach Lauf 1 in Task 8 rückgängig gemacht wird. Er hängt an
`FrameTrace`, weil das ohnehin eingeschaltet wird.

**Files:**

-   Modify: `src/Features/FrameTrace.cpp`

-   [ ] **Step 1: Probe einbauen**

In `FrameTrace.cpp` im anonymen Namensraum ergänzen:

```cpp
		// diag: does a value written into fogState.clamp from Present survive
		// to the composite? Written every frame with the last-writer guard from
		// the spec, read back once a second from kBeforeComposite.
		float g_clampOriginal = 0.0f;
		float g_clampWritten = 0.0f;
		bool g_clampOverridden = false;
		std::uint64_t g_probeFrames = 0;
		Render::PhaseDispatcher::Token g_probeToken = Render::PhaseDispatcher::kNoToken;

		void ProbeWrite() noexcept
		{
			auto* const state = RE::BSGraphics::State::GetSingleton();
			if (state == nullptr) {
				return;
			}
			float& clamp = state->fogState.clamp;
			if (!g_clampOverridden || clamp != g_clampWritten) {
				g_clampOriginal = clamp;
			}
			g_clampWritten = g_clampOriginal * 0.5f;
			clamp = g_clampWritten;
			g_clampOverridden = true;
		}

		void ProbeRead() noexcept
		{
			if (++g_probeFrames % 180 != 0) {
				return;
			}
			const auto* const state = RE::BSGraphics::State::GetSingleton();
			const auto* const sky = RE::Sky::GetSingleton();
			if (state == nullptr || sky == nullptr) {
				return;
			}
			REX::INFO(
				"clamp probe: wrote {:.4f} from original {:.4f}, composite sees {:.4f}, Sky::fogClamp {:.4f} - {}",
				g_clampWritten,
				g_clampOriginal,
				state->fogState.clamp,
				sky->fogClamp,
				state->fogState.clamp == g_clampWritten ? "SURVIVED" : "overwritten before the composite");
		}
```

`#include "Render/FramePhase.h"` ergänzen. In `Setup` am Ende vor `return true;`:

```cpp
		if (g_probeToken == Render::PhaseDispatcher::kNoToken) {
			g_probeToken = Render::SubscribeFramePhase(
				Render::Phase::kBeforeComposite, "FrameTrace/ClampProbe", [] { ProbeRead(); });
		}
```

In `Frame()` als erste Zeile `ProbeWrite();`. In `Shutdown()` am Anfang:

```cpp
		Render::UnsubscribeFramePhase(Render::Phase::kBeforeComposite, g_probeToken);
		g_probeToken = Render::PhaseDispatcher::kNoToken;
		if (g_clampOverridden) {
			if (auto* const state = RE::BSGraphics::State::GetSingleton(); state != nullptr) {
				state->fogState.clamp = g_clampOriginal;
			}
			g_clampOverridden = false;
		}
```

-   [ ] **Step 2: Bauen, Commit**

Voller Bau (deployt). `ctest` grün.

```bash
git add src/Features/FrameTrace.cpp
git commit -m "diag: probe whether fogState.clamp survives to the composite"
```

---

## Task 7: Lauf 1 — Trace und Probe

Kein Code. Vor dem Lauf in `CommunityShadersFO4.json` `FrameTrace/enabled` auf `true` setzen
(oder im Overlay), `ShaderCensus/enabled` muß `false` sein.

-   [ ] **Step 1: Prüfliste an den Nutzer**

1. Sanctuary draußen, Blick über den Ort, Overlay zu: **F10**. Im Log muß „FrameTrace: recording
   the next frame" und danach ein Block `=== frame trace: frame N ===` stehen.
2. Zur Brücke gehen, Wasser im Bild: **F10** noch einmal.
3. Zwei Sekunden stehen, dann beenden. Die Probe schreibt von selbst je Sekunde `clamp probe:`.

Was schiefgehen kann: kein Block nach F10 heißt, die Taste erreicht den Latch nicht (Task 1) oder
`Setup` hat abgelehnt (Log lesen). Ein Block mit lauter Adressen statt `FO4_RT_…` heißt, das
Inventar aus B2 lief nicht.

-   [ ] **Step 2: Log auswerten**

Aus dem ersten Block:

-   Die Zeile mit dem **letzten** `BSDFCompositeShader`-Aufruf und die Zeile mit dem **letzten**
    `BSSkyShader`-Aufruf: welche RTVs stehen dort? Das Ziel, das nach dem Himmel gebunden ist und
    `R11G11B10_FLOAT` in voller Auflösung hat (`render-targets.md`), ist `kSceneHDR`. Erwartet
    `FO4_RT_004`.
-   Die erste Zeile **nach** dem letzten Sky-Aufruf: ihre Klasse ist der Anker `kAfterOpaque`.
    Erwartet `BSEffectShader`. Prüfen, daß dieselbe Klasse auch im zweiten Block (mit Wasser) als
    erste nach dem Himmel steht — sonst den nächsten stabilen Aufruf nehmen, der in beiden Blöcken
    an derselben Stelle steht.
-   Die `clamp probe`-Zeilen: steht dort durchgehend `SURVIVED`, ist Ausgang 1 aus Spec 7.3.
    Steht `overwritten`, Ausgang 2: dann in Task 8 das Schreiben in den `kBeforeComposite`-Rückruf
    verlegen und `Sky::fogClamp` als zweite Quelle beschreiben, ein weiterer Lauf. Hält auch das
    nicht, Ausgang 3.

Befunde als Notiz in `docs/fallout4-port/ROADMAP.md` unter einem vorläufigen Abschnitt
„F3 — Zwischenstand nach Lauf 1" festhalten (wird in Task 13 in „Aus Teilprojekt F3 bestätigt"
aufgelöst). Den Trace-Block selbst dorthin kopieren, gekürzt auf die Zeilen um Composite, Sky und
den Anker.

---

## Task 8: Anker, HDR-Ziel, Probe raus

**Files:**

-   Modify: `src/Render/FramePhase.cpp` (Klassenname in `g_anchors[1]`, falls anders gemessen)
-   Modify: `src/Render/Targets.h`, `src/Render/Targets.cpp`
-   Modify: `src/Features/FrameTrace.cpp` (Probe entfernen)

-   [ ] **Step 1: Anker eintragen**

Steht im Trace eine andere Klasse als `BSEffectShader` als erste nach dem Himmel, den String in
`g_anchors` ändern und den Kommentar darüber mit Datum und Befund fortschreiben.

-   [ ] **Step 2: `kSceneHDR`**

In `src/Render/Targets.h` nach `kGBufferNormal`:

```cpp
	/// The finished HDR scene after composite and sky, R11G11B10_FLOAT. The
	/// slot FrameTrace found bound when the first class after the sky ran,
	/// 2026-09-12. What a feature draws onto in Phase::kAfterOpaque.
	inline constexpr std::size_t kSceneHDR = 4;
```

Die `4` durch den gemessenen Slot ersetzen, falls es nicht `FO4_RT_004` war. In `LogSlots` die
erste `REX::INFO` um `, scene hdr {} is {}` mit `kSceneHDR` und
`NameOf(RenderTargetTexture(kSceneHDR))` erweitern.

-   [ ] **Step 3: Probe entfernen**

`git revert --no-edit <hash des diag-Commits>`; danach prüfen, daß `FrameTrace.cpp` wieder dem
Stand aus Task 4 entspricht, und bauen.

-   [ ] **Step 4: Bauen, Tests, Commit**

```bash
git add src/Render/FramePhase.cpp src/Render/Targets.h src/Render/Targets.cpp
git commit -m "feat: name the scene HDR target and the anchor behind it"
```

Rumpf: den Befund aus Lauf 1 in zwei Sätzen.

---

## Task 9: Der Sichtstrahl aus der Kamera

**Files:**

-   Modify: `src/Render/Camera.h`, `src/Render/Camera.cpp`
-   Test: `tests/CameraTests.cpp`

**Interfaces:**

-   Produces:
    ```cpp
    struct Render::FogCamera {
        float forward[3]; float height;
        float rightOverScaleX[3]; float near;
        float upOverScaleY[3];
    };
    [[nodiscard]] FogCamera FogCameraFromMatrix(const float (&a_worldToCam)[4][4], float a_cameraHeight) noexcept;
    ```

Herleitung, die auch in den Kommentar gehört: `worldToCam[3].xyz` ist vorwärts (Einheitsvektor,
`w = vorwärts·(p − t)`); `worldToCam[0].xyz = sx · rechts`, also `rechts / sx = M[0].xyz / |M[0].xyz|²`;
ebenso oben aus `M[1]`; `near = M[3][3] − M[2][3]`, weil `M[2][3] = −vorwärts·t − near` und
`M[3][3] = −vorwärts·t`. Weder Rotation noch Position werden gebraucht.

-   [ ] **Step 1: Test schreiben**

An `tests/CameraTests.cpp` vor der Abschlußzeile:

```cpp
	{
		const auto fog = Render::FogCameraFromMatrix(kLoadingScreen, 128.0f);

		Check(Near(fog.forward[0], 0.0f, 1e-6f) && Near(fog.forward[1], 1.0f, 1e-6f) && Near(fog.forward[2], 0.0f, 1e-6f),
			"loading screen: forward is world y");
		Check(Near(fog.near, 15.0f, 1e-4f), "and the near plane is fifteen");
		Check(Near(fog.height, 128.0f, 1e-6f), "and the height is the camera's z");
		Check(Near(fog.rightOverScaleX[0], 1.0f / kScaleX, 1e-5f) && Near(fog.rightOverScaleX[1], 0.0f, 1e-6f),
			"right over the horizontal scale points along world x");
		Check(Near(fog.upOverScaleY[2], 1.0f / kScaleY, 1e-5f) && Near(fog.upOverScaleY[0], 0.0f, 1e-6f),
			"up over the vertical scale points along world z");
	}

	{
		// The ray of the right screen edge, pushed back through the matrix,
		// has to land at x over w of one: that is what "over the scale" is for.
		const auto fog = Render::FogCameraFromMatrix(kLoadingScreen, 128.0f);
		const float ray[3]{
			fog.forward[0] + fog.rightOverScaleX[0],
			fog.forward[1] + fog.rightOverScaleX[1],
			fog.forward[2] + fog.rightOverScaleX[2]
		};
		const auto clip = Render::ClipFromWorldToCam(kLoadingScreen, ray);
		Check(Near(clip[0] / clip[3], 1.0f, 1e-5f), "the right edge ray projects to x over w of one");
		Check(Near(clip[1] / clip[3], 0.0f, 1e-5f), "and stays on the horizon");
	}

	{
		// On a real frame the centre ray is the camera's forward row, and the
		// scales survive the rotation.
		const auto& frame = kFrames[0];
		float worldToCam[4][4]{};
		BuildWorldToCam(frame.rotation[0], frame.rotation[1], frame.rotation[2], worldToCam);
		const auto fog = Render::FogCameraFromMatrix(worldToCam, 7904.8f);

		Check(
			Near(fog.forward[0], frame.rotation[0][0], 1e-5f) &&
				Near(fog.forward[1], frame.rotation[0][1], 1e-5f) &&
				Near(fog.forward[2], frame.rotation[0][2], 1e-5f),
			"sun ahead frame: forward is row zero of the rotation");
		Check(Near(fog.near, kNear, 1e-4f), "and the near plane is still fifteen");

		const float up[3]{ fog.upOverScaleY[0], fog.upOverScaleY[1], fog.upOverScaleY[2] };
		const float length = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
		Check(Near(length, 1.0f / kScaleY, 1e-4f), "and up over scale has the length one over the vertical scale");
	}
```

-   [ ] **Step 2: Bauen, Fehlschlag erwarten**

`--target CameraTests`: Fehler, `FogCameraFromMatrix` unbekannt.

-   [ ] **Step 3: Deklaration**

In `src/Render/Camera.h` nach `ClipFromWorldToCam`:

```cpp
	/// What a full-screen pass needs to turn a pixel and its depth into a
	/// place in the world, relative to the camera.
	///
	/// A pixel's ray is forward + ndc.x * rightOverScaleX + ndc.y * upOverScaleY,
	/// unnormalised; the depth buffer's z gives d = near / (1 - z) along
	/// forward; the point is ray * d, and its height is height + that z.
	struct FogCamera
	{
		float forward[3];
		float height;
		float rightOverScaleX[3];
		float near;
		float upOverScaleY[3];
	};

	/// All of it from the world-to-clip matrix alone. Row three is forward,
	/// a unit vector, because w is forward dotted with the point; row zero is
	/// the horizontal scale times right, so right over the scale is that row
	/// divided by its own squared length, and row one the same for up; and
	/// the near plane is m[3][3] - m[2][3], because z is w minus near.
	/// Neither the camera's rotation nor its position is needed - only the
	/// height, which is the caller's to supply.
	[[nodiscard]] FogCamera FogCameraFromMatrix(
		const float (&a_worldToCam)[4][4],
		float a_cameraHeight) noexcept;
```

-   [ ] **Step 4: Definition**

In `src/Render/Camera.cpp` nach `ClipFromWorldToCam`:

```cpp
	FogCamera FogCameraFromMatrix(const float (&a_worldToCam)[4][4], float a_cameraHeight) noexcept
	{
		const auto overSquaredLength = [](const float (&a_row)[4], float (&a_out)[3]) {
			const auto squared = a_row[0] * a_row[0] + a_row[1] * a_row[1] + a_row[2] * a_row[2];
			const auto scale = squared > 0.0f ? 1.0f / squared : 0.0f;
			a_out[0] = a_row[0] * scale;
			a_out[1] = a_row[1] * scale;
			a_out[2] = a_row[2] * scale;
		};

		FogCamera camera{};
		camera.forward[0] = a_worldToCam[3][0];
		camera.forward[1] = a_worldToCam[3][1];
		camera.forward[2] = a_worldToCam[3][2];
		camera.height = a_cameraHeight;
		overSquaredLength(a_worldToCam[0], camera.rightOverScaleX);
		overSquaredLength(a_worldToCam[1], camera.upOverScaleY);
		camera.near = a_worldToCam[3][3] - a_worldToCam[2][3];
		return camera;
	}
```

-   [ ] **Step 5: Bauen, Test, Mutation**

`--target CameraTests`, laufen lassen: grün. Mutation: `camera.near = -a_worldToCam[2][3];` (die
Translation vergessen). Erwartet: auf dem Ladebildschirm besteht der Test weiter (dort ist
`forward·t = 0`) — **das ist ein Befund über die Fixtures**: `kLoadingScreen` hat `M[3][3] = 0`.
Deshalb im Test der Frame-Fall: `BuildWorldToCam` setzt `M[3][3] = 0` ebenfalls. Test verschärfen:
in `BuildWorldToCam` die Translation echt füllen, `a_out[3][3] = -(forward·t)` und
`a_out[2][3] = -(forward·t) - kNear` mit `t = (-80352, 89600, 7904.8)`; für eine Richtung ändert
das nichts an den bestehenden Prüfungen, aber „and the near plane is still fifteen" fällt nun
unter der Mutation. Mutation zurücknehmen, grün.

-   [ ] **Step 6: Commit**

```bash
git add src/Render/Camera.h src/Render/Camera.cpp tests/CameraTests.cpp
git commit -m "feat: a pixel's world ray from the camera matrix"
```

---

## Task 10: Der Wächter sichert den Pixel-Konstantenpuffer

Der Nebelpass bindet `b1` der Pixelstufe. `StateGuard` sichert bisher nur den der Compute-Stufe.

**Files:**

-   Modify: `src/Render/StateGuard.h`, `src/Render/StateGuard.cpp`

-   [ ] **Step 1: Feld und Sicherung**

Im Header neben `_csConstantBuffers`:

```cpp
		REX::W32::ID3D11Buffer* _psConstantBuffers[kComputeSlots]{};
```

Im Konstruktor nach `CSGetConstantBuffers`:

```cpp
		// Slot 1 of the pixel stage as well: the fog pass binds its constants
		// there, and the composite that follows binds its own.
		_context->PSGetConstantBuffers(1, kComputeSlots, _psConstantBuffers);
```

Im Destruktor nach `CSSetConstantBuffers`: `_context->PSSetConstantBuffers(1, kComputeSlots, _psConstantBuffers);`
und bei den Freigaben `ReleaseAll(_psConstantBuffers, kComputeSlots);`.

-   [ ] **Step 2: Bauen, Commit**

```bash
git add src/Render/StateGuard.h src/Render/StateGuard.cpp
git commit -m "fix: guard the pixel stage's constant buffer slot"
```

---

## Task 11: Der Nebelshader

**Files:**

-   Create: `package/Features/ExponentialHeightFog/Shaders/FO4/ExponentialHeightFog/Fog.hlsl`

-   [ ] **Step 1: Shader schreiben**

```hlsl
// Exponential height fog, drawn onto the finished HDR scene.
//
// One full-screen triangle (Fullscreen.hlsl), depth in, colour and opacity
// out, blended with SRC_ALPHA / INV_SRC_ALPHA by the blend state. The
// integral is Unreal's, as the Skyrim version carries it; the fog colour is
// the game's own for this pixel, rebuilt from fogState the way the composite
// builds it, so that our layer and the game's distance fog agree in hue.
//
// Everything is relative to the camera. World coordinates in Fallout 4 run
// to eighty thousand, and a float has seven digits; only the height needs
// the camera's z, and that is one number.

Texture2D<float> DepthTexture : register(t0);

cbuffer PerFrame : register(b1)
{
	float4 CameraForward;      // xyz forward, w camera height
	float4 CameraRight;        // xyz right / sx, w near plane
	float4 CameraUp;           // xyz up / sy, w sky distance
	float4 SunDirection;       // xyz towards the sun, w sun inscattering
	float4 SunColor;           // rgb, w anisotropy
	float4 FogRange;           // fogState.rangeData
	float4 FogHeightRange;     // fogState.highLowRangeData
	float4 FogNearLow;         // rgb, w power
	float4 FogNearHigh;        // rgb
	float4 FogFarLow;          // rgb
	float4 FogFarHigh;         // rgb
	float4 Params;             // density, height, heightFalloff, startDistance
	float4 Screen;             // xy size, zw one over size
};

struct PixelInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

float HenyeyGreenstein(float cosTheta, float g)
{
	float g2 = g * g;
	float denominator = 1.0 + g2 - 2.0 * g * cosTheta;
	return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denominator, 1e-4), 1.5));
}

// The game's fog colour at this distance and height, as the composite
// mixes it: a distance ramp with a power between near and far, and a height
// ramp between the low and high pair.
float3 VanillaFogColor(float distance, float height)
{
	float distanceFactor = saturate(distance * FogRange.x - FogRange.z);
	float distancePow = pow(distanceFactor, FogNearLow.w);
	float2 heightPair = saturate(height.xx * FogHeightRange.xy - FogHeightRange.zw);
	float3 low = lerp(FogNearLow.rgb, FogFarLow.rgb, distancePow);
	float3 high = lerp(FogNearHigh.rgb, FogFarHigh.rgb, distancePow);
	return lerp(low, high, heightPair.x);
}

float4 main(PixelInput input) : SV_TARGET0
{
	const float depth = DepthTexture.Load(int3(input.position.xy, 0));

	// Pixel to NDC; y up, as clip space has it and Fullscreen.hlsl's uv does not.
	const float2 ndc = float2(input.uv.x * 2.0 - 1.0, 1.0 - input.uv.y * 2.0);
	const float3 ray = CameraForward.xyz + ndc.x * CameraRight.xyz + ndc.y * CameraUp.xyz;

	// z = 1 - near / d along forward; the sky reads as one and gets the
	// horizon distance instead.
	const float nearPlane = CameraRight.w;
	const float skyDistance = CameraUp.w;
	const float viewDepth = depth >= 0.99999 ? skyDistance : nearPlane / max(1.0 - depth, 1e-6);

	const float3 relative = ray * viewDepth;
	const float rayLength = length(relative);
	const float3 viewDirection = relative / max(rayLength, 1e-4);
	const float cameraHeight = CameraForward.w;

	const float density = Params.x * 0.001;
	const float fogHeight = Params.y;
	const float heightFalloff = Params.z * 0.001;
	const float startDistance = Params.w;

	if (density <= 0.0)
		discard;

	// Unreal's exponential height fog line integral.
	float rayOriginTerms = density * exp2(-heightFalloff * max(cameraHeight - fogHeight, 0.0));
	float integralLength = rayLength;
	float rayDirectionZ = relative.z;

	if (startDistance > 0.0) {
		const float excludeDistance = min(startDistance, rayLength);
		const float excludeTime = excludeDistance / max(rayLength, 1e-4);
		const float cameraToExclusionZ = excludeTime * relative.z;
		const float exclusionZ = cameraHeight + cameraToExclusionZ;
		integralLength = (1.0 - excludeTime) * rayLength;
		rayDirectionZ = relative.z - cameraToExclusionZ;
		rayOriginTerms = density * exp2(-heightFalloff * max(exclusionZ - fogHeight, 0.0));
	}

	const float falloff = heightFalloff * rayDirectionZ;
	const float lineIntegral = (1.0 - exp2(-falloff)) / falloff;
	const float lineIntegralTaylor = 0.69314718056 - 0.24022650695 * falloff;
	const float integral = rayOriginTerms * (abs(falloff) > 0.01 ? lineIntegral : lineIntegralTaylor) * integralLength;
	const float transmittance = saturate(exp2(-integral));
	const float opacity = 1.0 - transmittance;

	float3 color = VanillaFogColor(rayLength, cameraHeight + relative.z);

	// The sun's glow through the fog: a Henyey-Greenstein lobe around it.
	const float sunInscattering = SunDirection.w;
	if (sunInscattering > 0.0) {
		const float cosTheta = dot(SunDirection.xyz, viewDirection);
		const float phase = HenyeyGreenstein(cosTheta, SunColor.w);
		color += SunColor.rgb * phase * sunInscattering;
	}

	return float4(color, opacity);
}
```

Das Zusatzfeld `Screen` steht im Puffer, auch wenn der Shader es nicht liest: es hält die Größe
für den Abgleich mit `FOG_DEBUG_COLOR` bereit und kostet nichts.

Für den Abgleich aus Spec 6.4 zeitweilig **von Hand** über `return` einfügen:

```hlsl
#if defined(FOG_DEBUG_COLOR)
	return float4(VanillaFogColor(rayLength, cameraHeight + relative.z), 1.0);
#endif
```

Die Datei im Repo trägt dieses Define nicht; wer prüft, setzt `#define FOG_DEBUG_COLOR 1` in der
Datei im Spielordner und nimmt es danach heraus.

-   [ ] **Step 2: Commit**

```bash
git add package/Features/ExponentialHeightFog/Shaders/FO4/ExponentialHeightFog/Fog.hlsl
git commit -m "feat: the height fog pixel shader"
```

---

## Task 12: Das Feature `ExponentialHeightFog`

**Files:**

-   Create: `src/Features/ExponentialHeightFog.h`, `src/Features/ExponentialHeightFog.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (nach `ScreenSpaceShadows`)
-   Modify: `tests/SettingsSchemaTests.cpp` (Formate der neuen Bereiche)

**Interfaces:**

-   Consumes: `Render::Phase::kAfterOpaque`, `Render::Targets::kSceneHDR`, `kSceneDepth`,
    `Render::FogCameraFromMatrix`, `Render::ConstantBuffer`, `Render::StateGuard`,
    `Render::DrawFullscreen`, `Render::InitFullscreenPass`, `Shader::LoadSource`,
    `Shader::CompilePixelShader`, `Util::FileWatch`, `RE::BSGraphics::State::fogState`,
    `RE::Sky`, `RE::NiLight::diff`, `RE::Main::WorldRootCamera`.

-   [ ] **Step 1: Schematest erweitern**

An `tests/SettingsSchemaTests.cpp` im Block der Formate:

```cpp
		// F3's ranges. Density is 0 to 0.1 and not 0 to 1 for exactly this
		// reason: at two decimals its default of 0.005 could not be set.
		Check(same(Settings::SliderFormat(0.0, 0.1), "%.3f"), "density gets three decimals");
		Check(same(Settings::SliderFormat(-22000.0, 22000.0), "%.0f"), "a height in world units gets none");
		Check(same(Settings::SliderFormat(0.001, 2.0), "%.2f"), "the height falloff gets two");
		Check(same(Settings::SliderFormat(-0.99, 0.99), "%.2f"), "and so does the anisotropy");
```

Bauen, laufen lassen: grün (die Regel besteht). Mutation: in `SliderFormat` die Schwelle `0.1`
auf `0.2` heben — erwartet fällt „density gets three decimals". Zurücknehmen.

-   [ ] **Step 2: Header schreiben**

`src/Features/ExponentialHeightFog.h`:

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Render/PhaseDispatcher.h"
#include "Render/Resources.h"
#include "Util/FileWatch.h"

#include <REX/W32/D3D11.h>

#include <cstdint>

namespace Features
{
	/// Exponential height fog, drawn onto the finished HDR scene.
	///
	/// The second pass of our own and the first behind the opaque scene: it
	/// runs from Phase::kAfterOpaque, reads the scene depth, and blends fog
	/// onto Targets::kSceneHDR. The game's own fog stays and is dimmed through
	/// fogState.clamp, a value in engine memory that Shutdown hands back.
	class ExponentialHeightFog final : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ExponentialHeightFog"; }
		void Declare() override;
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		void Draw();
		[[nodiscard]] bool Compile();
		[[nodiscard]] bool EnsureBlendState();

		/// Dims the game's fog by the vanillaFog setting, remembering what
		/// the engine had so that a value the engine did not refresh is not
		/// multiplied a second time, and so that Shutdown can restore it.
		void ApplyVanillaFog();
		void RestoreVanillaFog() noexcept;

		Render::ConstantBuffer _constants;
		REX::W32::ID3D11PixelShader* _shader{ nullptr };
		REX::W32::ID3D11BlendState* _alpha{ nullptr };
		Util::FileWatch _watch;
		Render::PhaseDispatcher::Token _phase{ Render::PhaseDispatcher::kNoToken };

		std::uint64_t _draws{ 0 };
		bool _reportedNoSun{ false };
		bool _reportedNoTargets{ false };
		bool _reportedNoCamera{ false };

		float _clampOriginal{ 0.0f };
		float _clampWritten{ 0.0f };
		bool _clampOverridden{ false };
	};
}
```

-   [ ] **Step 3: Implementierung schreiben**

`src/Features/ExponentialHeightFog.cpp`:

```cpp
#include "Features/ExponentialHeightFog.h"

#include "Render/Camera.h"
#include "Render/DebugName.h"
#include "Render/FramePhase.h"
#include "Render/FullscreenPass.h"
#include "Render/Profiler.h"
#include "Render/Renderer.h"
#include "Render/StateGuard.h"
#include "Render/Targets.h"
#include "Settings/Settings.h"
#include "Shader/ShaderCompiler.h"
#include "Shader/ShaderSource.h"
#include "Util/GamePaths.h"

#include <RE/B/BSGraphics.h>
#include <RE/M/Main.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiLight.h>
#include <RE/S/Sky.h>

// RE/S/Sun.h is not self-contained; see ScreenSpaceShadows.cpp for the
// account. Only NiAVObject is read through the pointers it declares.
#include <RE/S/SkyObject.h>

namespace RE
{
	class BSShaderAccumulator;
	class BSTriShape;
	class NiBillboardNode;
	class NiDirectionalLight;
}

#include <RE/S/Sun.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Features
{
	namespace
	{
		constexpr auto kShaderFile = "ExponentialHeightFog/Fog.hlsl";
		constexpr std::uint64_t kLogInterval = 180;
		constexpr float kSkyDistance = 500000.0f;

		/// Field for field the cbuffer PerFrame of Fog.hlsl: thirteen float4.
		struct alignas(16) FogConstants
		{
			float cameraForward[4];
			float cameraRight[4];
			float cameraUp[4];
			float sunDirection[4];
			float sunColor[4];
			float fogRange[4];
			float fogHeightRange[4];
			float fogNearLow[4];
			float fogNearHigh[4];
			float fogFarLow[4];
			float fogFarHigh[4];
			float params[4];
			float screen[4];
		};
		static_assert(sizeof(FogConstants) == 13 * 16);

		std::filesystem::path ShaderRoot()
		{
			return Util::DataDirectory() / "Shaders" / "FO4";
		}
	}

	void ExponentialHeightFog::Declare()
	{
		Settings::DeclareFeature("ExponentialHeightFog", true)
			.Label("feature.exponential_height_fog.name", "Exponential Height Fog")
			.Help(
				"feature.exponential_height_fog.help",
				"Fog that lies in low ground and thins with height, drawn over the finished "
				"scene in the game's own fog colour, with the sun glowing through it.");

		Settings::DeclareSlider("ExponentialHeightFog/density", 0.005, 0.0, 0.1)
			.Label("feature.exponential_height_fog.density", "Density")
			.Help("feature.exponential_height_fog.density_help", "How thick the fog is at its base.");

		Settings::DeclareSlider("ExponentialHeightFog/height", 0.0, -22000.0, 22000.0)
			.Label("feature.exponential_height_fog.height", "Height")
			.Help(
				"feature.exponential_height_fog.height_help",
				"World height above which the fog thins. Sanctuary lies at about 7900.");

		Settings::DeclareSlider("ExponentialHeightFog/heightFalloff", 0.2, 0.001, 2.0)
			.Label("feature.exponential_height_fog.height_falloff", "Height Falloff")
			.Help("feature.exponential_height_fog.height_falloff_help", "How quickly the fog thins with height.");

		Settings::DeclareSlider("ExponentialHeightFog/startDistance", 0.0, 0.0, 100000.0)
			.Label("feature.exponential_height_fog.start_distance", "Start Distance")
			.Help("feature.exponential_height_fog.start_distance_help", "Distance from the camera kept free of fog.");

		Settings::DeclareSlider("ExponentialHeightFog/sunInscattering", 1.0, 0.0, 10.0)
			.Label("feature.exponential_height_fog.sun_inscattering", "Sun Inscattering")
			.Help("feature.exponential_height_fog.sun_inscattering_help", "How strongly the sun glows through the fog.");

		Settings::DeclareSlider("ExponentialHeightFog/sunAnisotropy", 0.2, -0.99, 0.99)
			.Label("feature.exponential_height_fog.sun_anisotropy", "Sun Anisotropy")
			.Help(
				"feature.exponential_height_fog.sun_anisotropy_help",
				"How tightly the glow gathers around the sun. Positive is forward scattering.");

		Settings::DeclareSlider("ExponentialHeightFog/vanillaFog", 1.0, 0.0, 1.0)
			.Label("feature.exponential_height_fog.vanilla_fog", "Vanilla Fog")
			.Help(
				"feature.exponential_height_fog.vanilla_fog_help",
				"How much of the game's own fog remains. One leaves it untouched.");
	}

	bool ExponentialHeightFog::Setup()
	{
		_reportedNoSun = false;
		_reportedNoTargets = false;
		_reportedNoCamera = false;
		_draws = 0;

		if (!Render::InitFullscreenPass()) {
			return false;
		}
		if (!Compile()) {
			return false;
		}
		if (!EnsureBlendState()) {
			return false;
		}
		if (!_constants.Create(sizeof(FogConstants), "FO4CS_CB_ExponentialHeightFog")) {
			return false;
		}

		_phase = Render::SubscribeFramePhase(
			Render::Phase::kAfterOpaque, "ExponentialHeightFog/Draw", [this] { Draw(); });
		if (_phase == Render::PhaseDispatcher::kNoToken) {
			REX::ERROR("ExponentialHeightFog: the frame phase has no room left");
			return false;
		}

		return true;
	}

	void ExponentialHeightFog::Frame()
	{
		if (_watch.Poll()) {
			REX::INFO("ExponentialHeightFog: shader changed, recompiling");
			static_cast<void>(Compile());
		}

		// Outcome 1 of the spec's measurement: written from Present, it
		// survives to the composite with one frame of delay. If Lauf 1 said
		// otherwise, this call moves into Draw.
		ApplyVanillaFog();
	}

	void ExponentialHeightFog::Shutdown()
	{
		Render::UnsubscribeFramePhase(Render::Phase::kAfterOpaque, _phase);
		_phase = Render::PhaseDispatcher::kNoToken;

		RestoreVanillaFog();

		if (_shader != nullptr) {
			_shader->Release();
			_shader = nullptr;
		}
		if (_alpha != nullptr) {
			_alpha->Release();
			_alpha = nullptr;
		}
		_constants.Release();
	}

	void ExponentialHeightFog::Draw()
	{
		++_draws;

		const auto* const sky = RE::Sky::GetSingleton();
		if (sky == nullptr || sky->mode.get() != RE::Sky::Mode::kFull ||
			sky->sun == nullptr || sky->sun->light.get() == nullptr) {
			if (!_reportedNoSun) {
				REX::INFO("ExponentialHeightFog: no sun, standing down");
				_reportedNoSun = true;
			}
			return;
		}
		_reportedNoSun = false;

		auto* const depth = Render::Targets::DepthSRV(Render::Targets::kSceneDepth);
		auto* const scene = Render::Targets::RenderTargetView(Render::Targets::kSceneHDR);
		auto* const sceneTexture = Render::Targets::RenderTargetTexture(Render::Targets::kSceneHDR);
		if (depth == nullptr || scene == nullptr || sceneTexture == nullptr) {
			if (!_reportedNoTargets) {
				REX::ERROR(
					"ExponentialHeightFog: standing down, depth {}, scene {}",
					depth != nullptr ? "ok" : "missing",
					scene != nullptr ? "ok" : "missing");
				_reportedNoTargets = true;
			}
			return;
		}
		_reportedNoTargets = false;

		auto* const world = RE::Main::WorldRootCamera();
		const auto* const state = RE::BSGraphics::State::GetSingleton();
		if (world == nullptr || state == nullptr) {
			if (!_reportedNoCamera) {
				REX::ERROR("ExponentialHeightFog: no world camera or renderer state");
				_reportedNoCamera = true;
			}
			return;
		}
		_reportedNoCamera = false;

		auto* const context = Render::GetContext();
		if (context == nullptr || _shader == nullptr) {
			return;
		}

		float matrix[16]{};
		std::memcpy(matrix, world->worldToCam, sizeof(matrix));
		if (!Render::IsPlausibleViewProjection(matrix)) {
			return;
		}

		const auto camera = Render::FogCameraFromMatrix(
			world->worldToCam, world->GetWorldTransform().translate.z);

		// Row zero of the light's rotation is the direction the light travels;
		// negated it points at the sun. Measured in F2.
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

		REX::W32::D3D11_TEXTURE2D_DESC desc{};
		sceneTexture->GetDesc(std::addressof(desc));

		const auto& fog = state->fogState;

		FogConstants data{};
		data.cameraForward[0] = camera.forward[0];
		data.cameraForward[1] = camera.forward[1];
		data.cameraForward[2] = camera.forward[2];
		data.cameraForward[3] = camera.height;
		data.cameraRight[0] = camera.rightOverScaleX[0];
		data.cameraRight[1] = camera.rightOverScaleX[1];
		data.cameraRight[2] = camera.rightOverScaleX[2];
		data.cameraRight[3] = camera.near;
		data.cameraUp[0] = camera.upOverScaleY[0];
		data.cameraUp[1] = camera.upOverScaleY[1];
		data.cameraUp[2] = camera.upOverScaleY[2];
		data.cameraUp[3] = kSkyDistance;
		data.sunDirection[0] = towardsSun[0];
		data.sunDirection[1] = towardsSun[1];
		data.sunDirection[2] = towardsSun[2];
		data.sunDirection[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/sunInscattering"));
		data.sunColor[0] = light->diff.r;
		data.sunColor[1] = light->diff.g;
		data.sunColor[2] = light->diff.b;
		data.sunColor[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/sunAnisotropy"));
		data.fogRange[0] = fog.rangeData.x;
		data.fogRange[1] = fog.rangeData.y;
		data.fogRange[2] = fog.rangeData.z;
		data.fogRange[3] = fog.rangeData.w;
		data.fogHeightRange[0] = fog.highLowRangeData.x;
		data.fogHeightRange[1] = fog.highLowRangeData.y;
		data.fogHeightRange[2] = fog.highLowRangeData.z;
		data.fogHeightRange[3] = fog.highLowRangeData.w;
		data.fogNearLow[0] = fog.nearLowColor.r;
		data.fogNearLow[1] = fog.nearLowColor.g;
		data.fogNearLow[2] = fog.nearLowColor.b;
		data.fogNearLow[3] = fog.power;
		data.fogNearHigh[0] = fog.nearHighColor.r;
		data.fogNearHigh[1] = fog.nearHighColor.g;
		data.fogNearHigh[2] = fog.nearHighColor.b;
		data.fogFarLow[0] = fog.farLowColor.r;
		data.fogFarLow[1] = fog.farLowColor.g;
		data.fogFarLow[2] = fog.farLowColor.b;
		data.fogFarHigh[0] = fog.farHighColor.r;
		data.fogFarHigh[1] = fog.farHighColor.g;
		data.fogFarHigh[2] = fog.farHighColor.b;
		data.params[0] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/density"));
		data.params[1] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/height"));
		data.params[2] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/heightFalloff"));
		data.params[3] = static_cast<float>(Settings::GetDouble("ExponentialHeightFog/startDistance"));
		data.screen[0] = static_cast<float>(desc.width);
		data.screen[1] = static_cast<float>(desc.height);
		data.screen[2] = 1.0f / static_cast<float>(desc.width);
		data.screen[3] = 1.0f / static_cast<float>(desc.height);

		if (_draws % kLogInterval == 0) {
			REX::INFO(
				"fog: camera height {:.1f}, near {:.2f}, forward [{:.3f} {:.3f} {:.3f}], "
				"vanilla clamp {:.3f}, nearLow [{:.3f} {:.3f} {:.3f}], sun [{:.3f} {:.3f} {:.3f}] "
				"colour [{:.3f} {:.3f} {:.3f}]",
				camera.height, camera.near,
				camera.forward[0], camera.forward[1], camera.forward[2],
				fog.clamp,
				fog.nearLowColor.r, fog.nearLowColor.g, fog.nearLowColor.b,
				towardsSun[0], towardsSun[1], towardsSun[2],
				light->diff.r, light->diff.g, light->diff.b);
		}

		const Render::StateGuard guard;
		const Render::PassScope scope{ "ExponentialHeightFog/Fog" };

		// Unbind first: DS_002 may still hang on the output merger as the
		// depth view of whatever drew last, and we read it now.
		context->OMSetRenderTargets(0, nullptr, nullptr);

		static_cast<void>(_constants.Update(std::addressof(data), sizeof(data)));
		auto* const buffer = _constants.Buffer();

		context->OMSetRenderTargets(1, std::addressof(scene), nullptr);
		const float blendFactor[4]{ 0.0f, 0.0f, 0.0f, 0.0f };
		context->OMSetBlendState(_alpha, blendFactor, 0xFFFFFFFFu);
		context->PSSetShaderResources(0, 1, std::addressof(depth));
		context->PSSetConstantBuffers(1, 1, std::addressof(buffer));
		context->PSSetShader(_shader, nullptr, 0);

		Render::DrawFullscreen();
	}

	bool ExponentialHeightFog::Compile()
	{
		const auto source = Shader::LoadSource(ShaderRoot(), kShaderFile);
		if (!source) {
			const std::filesystem::path only[] = { ShaderRoot() / kShaderFile };
			_watch.Reset(only);
			REX::ERROR("ExponentialHeightFog: {}", source.error());
			return false;
		}
		_watch.Reset(source->files);

		const auto compiled = Shader::CompilePixelShader(source->text, "Fog.hlsl", "main");
		if (!compiled.Succeeded()) {
			REX::ERROR("ExponentialHeightFog: {}", compiled.diagnostics);
			return false;
		}

		auto* const device = Render::GetDevice();
		REX::W32::ID3D11PixelShader* shader = nullptr;
		if (device == nullptr ||
			device->CreatePixelShader(
				compiled.bytecode.data(), compiled.bytecode.size(), nullptr, std::addressof(shader)) < 0) {
			REX::ERROR("ExponentialHeightFog: the fog shader could not be created");
			return false;
		}

		if (_shader != nullptr) {
			_shader->Release();
		}
		_shader = shader;
		static_cast<void>(Render::SetDebugName(
			reinterpret_cast<REX::W32::ID3D11DeviceChild*>(_shader), "FO4CS_PS_ExponentialHeightFog"));
		REX::INFO("ExponentialHeightFog: fog shader compiled");
		return true;
	}

	bool ExponentialHeightFog::EnsureBlendState()
	{
		if (_alpha != nullptr) {
			return true;
		}
		auto* const device = Render::GetDevice();
		if (device == nullptr) {
			return false;
		}

		// dest = src * a + dest * (1 - a) for colour; alpha is left as the
		// target had it. R11G11B10 carries none, but a target that did would
		// keep its own.
		REX::W32::D3D11_BLEND_DESC desc{};
		desc.alphaToCoverageEnable = false;
		desc.independentBlendEnable = false;
		auto& target = desc.renderTarget[0];
		target.blendEnable = true;
		target.srcBlend = REX::W32::D3D11_BLEND_SRC_ALPHA;
		target.destBlend = REX::W32::D3D11_BLEND_INV_SRC_ALPHA;
		target.blendOp = REX::W32::D3D11_BLEND_OP_ADD;
		target.srcBlendAlpha = REX::W32::D3D11_BLEND_ZERO;
		target.destBlendAlpha = REX::W32::D3D11_BLEND_ONE;
		target.blendOpAlpha = REX::W32::D3D11_BLEND_OP_ADD;
		target.renderTargetWriteMask = static_cast<std::uint8_t>(REX::W32::D3D11_COLOR_WRITE_ENABLE_ALL);

		if (device->CreateBlendState(std::addressof(desc), std::addressof(_alpha)) < 0) {
			REX::ERROR("ExponentialHeightFog: the alpha blend state could not be created");
			return false;
		}
		return true;
	}

	void ExponentialHeightFog::ApplyVanillaFog()
	{
		auto* const state = RE::BSGraphics::State::GetSingleton();
		if (state == nullptr) {
			return;
		}

		const auto amount = static_cast<float>(
			std::clamp(Settings::GetDouble("ExponentialHeightFog/vanillaFog"), 0.0, 1.0));
		float& clamp = state->fogState.clamp;

		if (amount >= 1.0f) {
			RestoreVanillaFog();
			return;
		}

		// Whose value is this? Ours from last frame means the engine did not
		// refresh it, and the original stands; anything else is a fresh
		// original. Without this the value halves every frame.
		if (!_clampOverridden || clamp != _clampWritten) {
			_clampOriginal = clamp;
		}
		_clampWritten = _clampOriginal * amount;
		clamp = _clampWritten;
		_clampOverridden = true;
	}

	void ExponentialHeightFog::RestoreVanillaFog() noexcept
	{
		if (!_clampOverridden) {
			return;
		}
		if (auto* const state = RE::BSGraphics::State::GetSingleton(); state != nullptr) {
			// Only if it is still ours: the engine may have moved on, and
			// writing an old original over a new value would be our own bug.
			if (state->fogState.clamp == _clampWritten) {
				state->fogState.clamp = _clampOriginal;
			}
		}
		_clampOverridden = false;
	}
}
```

`#include <cstring>` für `std::memcpy` ergänzen. Ergab Lauf 1 **Ausgang 2**, wandert der Aufruf
`ApplyVanillaFog()` aus `Frame()` an den Anfang von `Draw()` (vor den Abbruchprüfungen, weil der
Wert auch im Innenraum zurückgegeben werden muß, wenn der Regler auf 1 geht), und `Frame()` ruft
nur noch den Wächter. Bei **Ausgang 3** entfallen `ApplyVanillaFog`, `RestoreVanillaFog`, die drei
Felder und der Regler `vanillaFog`; die Spec-Tabelle und `en.json` werden dann mit angepaßt.

-   [ ] **Step 4: Registrieren, Katalog, Bau**

In `src/Feature/FeatureSystem.cpp` nach `ScreenSpaceShadows`:

```cpp
			TheRegistry().Register(std::make_unique<ExponentialHeightFog>());
```

`python tools/extract-i18n.py --write`, dann ohne `--write` grün. Voller Bau, `ctest`,
`verify-plugin.ps1`. Erwartet grün; der Deploy trägt `Fog.hlsl` ins Spiel, weil `package.ps1`
`package/Features/*` staged.

-   [ ] **Step 5: Commit**

```bash
git add src/Features/ExponentialHeightFog.h src/Features/ExponentialHeightFog.cpp src/Feature/FeatureSystem.cpp tests/SettingsSchemaTests.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: exponential height fog behind the opaque scene"
```

---

## Task 13: Paket und Prüfung

**Files:**

-   Modify: `tools/verify-package.ps1`

-   [ ] **Step 1: Fünftes Archiv prüfen**

Neben `$shadows`:

```powershell
$fog = @($archives | Where-Object { $_.Name -like "ExponentialHeightFog-*" })
```

Neben `Check ($shadows.Count -eq 1) …`:

```powershell
Check ($fog.Count -eq 1) "exactly one ExponentialHeightFog archive"
```

Die Bedingung des großen `if` um `-and $fog.Count -eq 1` erweitern, darin
`$fogEntries = @(Get-Entries $fog[0].FullName)`, und nach dem Shadow-Block:

```powershell
    Check (
        $fogEntries.Count -eq 1 -and $fogEntries[0] -eq "Shaders/FO4/ExponentialHeightFog/Fog.hlsl"
    ) "the fog addon carries its shader and nothing else"
    Check (
        -not ($baseEntries -contains "Shaders/FO4/ExponentialHeightFog/Fog.hlsl")
    ) "and the base does not carry it"
```

`$union` um `$fogEntries` erweitern und in der `foreach`-Liste
`@{ Name = "fog addon"; Entries = $fogEntries }` ergänzen.

-   [ ] **Step 2: Paket bauen und prüfen**

`cmake --build --preset FO4 --target package`, dann `pwsh tools/verify-package.ps1`. Erwartet:
fünf Archive, alles grün.

-   [ ] **Step 3: Commit**

```bash
git add tools/verify-package.ps1
git commit -m "build: verify the height fog addon archive"
```

---

## Task 14: Lauf 2 — Abnahme

Kein Code vor dem Lauf. `FrameTrace/enabled` darf an bleiben.

-   [ ] **Step 1: Prüfliste an den Nutzer** (Spec Abschnitt 11, Lauf 2), nummeriert mit „was
        schiefgehen kann":

1. Overlay: Block `Exponential Height Fog` mit Schalter und sieben Reglern, Block `Frame Trace`.
2. Draußen, Blick in die Senke am Fluß: Schalter aus und an. Kein Unterschied: Anker oder Ziel.
3. `height` hoch- und runterschieben. Die Schicht wandert nicht: Höhe oder Strahl.
4. Blick zur Sonne, `sunInscattering` auf 5. Kein Glühen: Sonnenrichtung oder -farbe.
5. `vanillaFog` auf 0, Blick in die Ferne. Distanztrübung bleibt: Ausgang 3.
6. Performance-Tafel: `ExponentialHeightFog/Draw` und `/Fog` mit Zahlen, dann **F11**.
7. Root Cellar: im Log „no sun, standing down".
8. Pip-Boy, Alt-Tab und zurück.
9. Feature im Spiel abschalten, wieder an. Spielnebel muß beim Abschalten zurück sein.
10. `Data/Shaders/FO4/ExponentialHeightFog/Fog.hlsl` speichern, während das Spiel läuft: binnen
    einer Sekunde „shader changed, recompiling". **Diesmal ausführen.**

Dazu der Farbabgleich aus Spec 6.4, wenn Schritt 2 steht: `#define FOG_DEBUG_COLOR 1` in die
Datei im Spielordner, speichern, Horizont mit dem Spielnebel vergleichen, Define wieder entfernen.

-   [ ] **Step 2: Log auswerten**

Keine `[E]`-Zeilen; `fog:`-Zeilen je Sekunde mit plausibler Höhe (Sanctuary ≈ 7900) und `near`
15; der Schnappschuß mit den beiden Passzeilen. Fällt ein Schritt, gilt systematic-debugging:
Ursache vor Korrektur, ein Lauf je Hypothese, zwei Kandidaten nebeneinander ins Log.

---

## Task 15: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: Roadmap**

In `docs/fallout4-port/ROADMAP.md`: Statuszeile oben (F3 abgeschlossen, als Nächstes F4), Tabelle
(F3 **abgeschlossen**), den Zwischenstand aus Task 7 in einen Abschnitt „Aus Teilprojekt F3
bestätigt" überführen, nach dem F2-Abschnitt: die Zahlen (Schnappschuß, Passzeile gegen die
F1-Grundlinie), der Trace-Auszug mit Anker und HDR-Ziel, der Ausgang der Vanilla-Probe, die
Gabelungen der Spec (Farbe, Regler), die Abnahme (zehn Schritte, was ungeprüft blieb), das
Zugeständnis zu den Transparenzen, und der Verweis, daß der volumetrische Nebel bei F6
mitentschieden wird. Der Abschnitt „Wie F+ in F1…Fn zerfallen ist" bekommt einen Satz zu
`FrameTrace` als Werkzeug für die Anker von F4 bis F11.

-   [ ] **Step 2: Commit**

```bash
git add docs/fallout4-port/ROADMAP.md
git commit -m "docs: record what F3 confirmed"
```

-   [ ] **Step 3: Erinnerungen**

`fallout4-engine-facts.md` um den Trace-Befund (Frame-Reihenfolge, HDR-Ziel, `fogState`-Verhalten)
und `fallout4-port-roadmap.md` um den Stand (F3 fertig, F4 als Nächstes, `FrameTrace` als
Werkzeug) ergänzen; eine `fallout4-f3-stand.md` nur, falls etwas offen bleibt.

-   [ ] **Step 4: Branch abschließen**

finishing-a-development-branch: volle Testsuite, Menü, nach Wahl des Nutzers Fast-Forward nach
`dev`, Branch löschen, Push nur auf Ansage.

---

## Reihenfolge und Abhängigkeiten

```
1 Hotkeys ─┐
2 TraceLog ─┼─► 4 FrameTrace ─► 6 Probe ─► 7 Lauf 1 ─► 8 Anker & kSceneHDR ─┐
3 Namen ───┘                                                                  │
5 Phasen ─────────────────────────────────────────────────────────────────────┤
9 Sichtstrahl ──┐                                                             │
10 Wächter ─────┼─► 12 Feature ─► 13 Paket ─► 14 Lauf 2 ─► 15 Abschluß ◄──────┘
11 Shader ──────┘
```

Task 5 kann vor oder nach Lauf 1 liegen; Task 6 braucht ihn nur für `kBeforeComposite`, das es
schon gibt. Tasks 9 bis 11 sind unabhängig von Lauf 1 und können warten, während der Nutzer ihn
macht. Zwei Spielstarts insgesamt, drei bei Ausgang 2 der Probe.
