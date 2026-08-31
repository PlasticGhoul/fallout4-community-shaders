# Teilprojekt E1 — Overlay und Eingabe, Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ein ImGui-Overlay, das sich per Taste öffnet, mit der Maus bedienen lässt, und das die
Spieleingabe anhält, solange es offen ist.

**Architecture:** `src/Menu/` ist ein eigenes System neben `Features`, kein Feature. `Menu::Gate`
hält den Zustand und kennt weder ImGui noch die Engine — die Eingabesperre kommt als zwei Rückrufe
herein, was den Zustandsautomaten ohne Spiel prüfbar macht. `Menu::Overlay` setzt ImGui auf und
zeichnet. Die Fensterprozedur setzt nur ein Flag; gehandelt wird auf dem Render-Thread.

**Tech Stack:** C++23, MSVC, ImGui 1.92.6 über vcpkg mit `dx11-binding` und `win32-binding`,
CommonLibF4, handgeschriebene Host-Tests nach dem Muster in `tests/`.

**Spec:** `docs/superpowers/specs/2026-08-31-fallout4-overlay-eingabe-design.md`

## Global Constraints

-   **Runtime:** ausschließlich Fallout 4 AE `1.11.240`.
-   **`/W4 /WX /permissive- /utf-8 /Zc:preprocessor`** auf unserem Ziel. Schlägt ein fremder Header
    an, wird er **eng auf unserem Ziel** unterdrückt, mit einem Kommentar, der ihn nennt.
-   **`<d3d11.h>` und `<Windows.h>` bleiben verboten.** Alles kommt aus `REX::W32`; was dort fehlt,
    wird selbst deklariert.
-   **Trampolin bleibt aus.** `InitInfo::trampoline` und `hook` bleiben auf `false`.
-   **commonlibf4 wird nicht geändert.** Kein Submodul-Commit.
-   **Konventionen:** Tabs, `a_` für Parameter, `_` für Member, anonymer Namensraum für Internes,
    Kommentare begründen statt zu beschreiben. Conventional Commits, Titel maximal 50 Zeichen,
    Rumpf bei 72 umgebrochen. Code und Commits auf Englisch, `docs/` auf Deutsch.
-   **Branch:** `port/e-menue`. Kein Push ohne Ansage.
-   **Bauen und Prüfen:**

    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
    & $cmake --build --preset FO4
    ctest --test-dir build/FO4 -C Release --output-on-failure
    ```

-   **Testdisziplin:** Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen, und der
    erwartete Fehlschlag wird **vorher benannt**.
-   **Ein Spielstart, nicht sechs.** Aufgabe 3 und 4 enden am grünen Bau; alles im Spiel steht
    gesammelt in Aufgabe 5.

### Was in `REX::W32` fehlt und selbst deklariert wird

`CallWindowProcW` und `SetWindowLongPtrW`. `user32.lib` linkt `commonlib-shared` bereits `PUBLIC`,
es fehlen also nur die Prototypen. Vorhanden und benutzbar sind `GWLP_WNDPROC`, die `WM_`- und
`VK_`-Konstanten, der Typ `WNDPROC`, `GetKeyState` und `SetWindowLongPtrA`.

---

## File Structure

| Datei                                | Verantwortung                                               |
| ------------------------------------ | ----------------------------------------------------------- |
| `src/Feature/FeatureSettings.h/.cpp` | erweitert um freie Pfade und `uint32`                       |
| `src/Menu/MenuGate.h/.cpp`           | der Zustandsautomat, ohne ImGui und ohne Engine             |
| `src/Menu/Win32.h`                   | die zwei Prototypen, die `REX::W32` fehlen                  |
| `src/Menu/Overlay.h/.cpp`            | ImGui aufsetzen, Renderziel binden, zeichnen                |
| `src/Menu/WindowHook.h/.cpp`         | Fensterprozedur verketten                                   |
| `src/Menu/InputLayer.h/.cpp`         | `BSInputEnableManager`-Schicht holen und wieder loslassen   |
| `src/Menu/MenuSystem.h/.cpp`         | der eine Einstieg pro Frame, hält Gate und Overlay zusammen |
| `tests/MenuGateTests.cpp`            | der gesamte Zustandsautomat                                 |
| `tests/FeatureSettingsTests.cpp`     | erweitert um `uint32` und die Gruppierung                   |
| `CMakeLists.txt`                     | ein neues Test-Executable, `imgui` verlinkt                 |
| `vcpkg.json`                         | `imgui` mit zwei Features                                   |

---

## Task 1: `Features::Settings` für freie Pfade

**Files:**

-   Modify: `src/Feature/FeatureSettings.h`, `src/Feature/FeatureSettings.cpp`,
    `tests/FeatureSettingsTests.cpp`

**Interfaces:**

-   Consumes: nichts Neues.
-   Produces:

    ```cpp
    namespace Features::Settings
    {
        void DeclareBool(std::string_view a_path, bool a_default);
        [[nodiscard]] bool GetBool(std::string_view a_path) noexcept;
        void DeclareUInt32(std::string_view a_path, std::uint32_t a_default);
        [[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;
    }
    ```

    Aufgabe 5 meldet damit `Menu/toggleKey` an. `DeclareFeature` und `IsEnabled` bleiben.

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

In `tests/FeatureSettingsTests.cpp`, **vor** dem Block „The watch reports a change exactly once",
einfügen:

```cpp
	// A setting that is not a feature switch, and not a bool.
	Features::Settings::DeclareUInt32("Menu/toggleKey", 35);
	Features::Settings::DeclareBool("Menu/verbose", true);

	// Two segments are the contract; anything else is refused rather than
	// silently addressing a top level key.
	Features::Settings::DeclareUInt32("noSlash", 7);
	Features::Settings::DeclareUInt32("too/many/segments", 8);

	const auto mixed = root / "mixed.json";
	Features::Settings::Init(mixed);

	Check(std::filesystem::exists(mixed), "a mixed default file is written");

	const auto mixedText = ReadFile(mixed);
	Check(
		Contains(mixedText, R"("Menu":{"toggleKey":35,"verbose":true})"),
		"and groups both Menu settings into one block");
	Check(
		Contains(mixedText, R"("alpha":{"enabled":false})"),
		"and still writes the feature blocks");
	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 35, "a uint32 reads back");
	Check(Features::Settings::GetBool("Menu/verbose"), "and so does a bool");
	Check(Features::Settings::GetUInt32("noSlash") == 0, "a path without a slash was refused");
	Check(
		Features::Settings::GetUInt32("too/many/segments") == 0,
		"and so was one with too many");

	WriteFile(mixed, R"({"Menu":{"toggleKey":112,"verbose":false}})");
	Features::Settings::Init(mixed);

	Check(Features::Settings::GetUInt32("Menu/toggleKey") == 112, "the file overrides the uint32");
	Check(!Features::Settings::GetBool("Menu/verbose"), "and the bool");
```

-   [ ] **Step 2: Bauen und den Fehlschlag sehen**

```pwsh
& $cmake --build --preset FO4 --target FeatureSettingsTests
```

Erwartet: **Bau schlägt fehl**, `DeclareUInt32`, `DeclareBool`, `GetUInt32` und `GetBool` sind
nicht deklariert.

-   [ ] **Step 3: Den Header erweitern**

In `src/Feature/FeatureSettings.h`, vor `DeclareFeature`:

```cpp
	/// Declares one setting under a two segment path, "Block/Key". The store
	/// addresses its values as JSON pointers with one object per segment, so
	/// anything else would address a top level key with a slash in its name.
	/// A malformed path is refused with a log line rather than accepted.
	void DeclareBool(std::string_view a_path, bool a_default);
	void DeclareUInt32(std::string_view a_path, std::uint32_t a_default);

	/// False, respectively zero, for a path that was never declared.
	[[nodiscard]] bool GetBool(std::string_view a_path) noexcept;
	[[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;
```

Der Kommentar über `DeclareFeature` bekommt einen Zusatz:

```cpp
	/// Shorthand for DeclareBool("<a_name>/enabled", a_default).
```

-   [ ] **Step 4: Die Implementierung umbauen**

In `src/Feature/FeatureSettings.cpp` den Block von `struct Entry` bis einschließlich
`WriteDefaultFile` durch Folgendes ersetzen. `StoredPath`, `CurrentFile`, `Watch` und
`ResolveDefaultFile` bleiben unverändert.

```cpp
		template <class T>
		struct Entry
		{
			// Owned here and never moved after the setting is built: REX keeps
			// only a string_view of it (TJsonSetting.h:48).
			std::string path;

			// Kept apart from the setting's own default, because loading from
			// the base file overwrites that one (TJsonSetting::Load). The value
			// that was declared is what a generated file has to contain.
			T declaredDefault{};

			std::unique_ptr<REX::TJsonSetting<T>> setting;
		};

		// Node based containers on purpose. The settings hold views into the
		// paths above, so the strings must keep their addresses; a vector would
		// invalidate every one of them on the next growth.
		template <class T>
		std::map<std::string, Entry<T>, std::less<>>& Entries()
		{
			static std::map<std::string, Entry<T>, std::less<>> entries;
			return entries;
		}

		// "Block/Key" and nothing else. One slash, neither half empty.
		bool SplitPath(std::string_view a_path, std::string_view& a_block, std::string_view& a_key)
		{
			const auto slash = a_path.find('/');
			if (slash == std::string_view::npos || slash == 0 || slash + 1 >= a_path.size()) {
				return false;
			}
			if (a_path.find('/', slash + 1) != std::string_view::npos) {
				return false;
			}

			a_block = a_path.substr(0, slash);
			a_key = a_path.substr(slash + 1);
			return true;
		}

		template <class T>
		void Declare(std::string_view a_path, T a_default)
		{
			std::string_view block;
			std::string_view key;
			if (!SplitPath(a_path, block, key)) {
				REX::ERROR("setting path {} is not <Block>/<Key>, ignored", a_path);
				return;
			}

			auto& entry = Entries<T>()[std::string{ a_path }];
			if (entry.setting != nullptr) {
				return;  // Declared twice; the first declaration wins.
			}

			// A JSON pointer, not a dotted path: REX prepends a slash and hands
			// the result to glz::get, which walks one object per segment.
			entry.path = std::string{ a_path };
			entry.declaredDefault = a_default;
			entry.setting = std::make_unique<REX::TJsonSetting<T>>(entry.path, a_default);
		}

		template <class T>
		T Get(std::string_view a_path, T a_fallback) noexcept
		{
			const auto it = Entries<T>().find(a_path);
			if (it == Entries<T>().end() || it->second.setting == nullptr) {
				return a_fallback;
			}
			return it->second.setting->GetValue();
		}

		// Collected from every kind of setting, grouped by the first path
		// segment, so that two settings in one block share one JSON object.
		std::map<std::string, std::vector<std::pair<std::string, std::string>>> DefaultsByBlock()
		{
			std::map<std::string, std::vector<std::pair<std::string, std::string>>> blocks;

			const auto add = [&blocks](std::string_view a_path, std::string a_literal) {
				std::string_view block;
				std::string_view key;
				if (SplitPath(a_path, block, key)) {
					blocks[std::string{ block }].emplace_back(std::string{ key }, std::move(a_literal));
				}
			};

			for (const auto& [path, entry] : Entries<bool>()) {
				add(path, entry.declaredDefault ? "true" : "false");
			}
			for (const auto& [path, entry] : Entries<std::uint32_t>()) {
				add(path, std::format("{}", entry.declaredDefault));
			}

			return blocks;
		}

		// REX saves through glz::set, and glz::set only assigns to keys that are
		// already in the document (seek_op<generic_json>). A settings file that
		// does not exist yet can therefore never be produced by saving, so the
		// first one is written here, from the declared defaults.
		void WriteDefaultFile(const std::filesystem::path& a_file)
		{
			const auto blocks = DefaultsByBlock();

			std::string text = "{\n";
			for (auto block = blocks.begin(); block != blocks.end(); ++block) {
				text += std::format("    \"{}\": {{\n", block->first);
				for (auto it = block->second.begin(); it != block->second.end(); ++it) {
					text += std::format(
						"        \"{}\": {}{}\n",
						it->first,
						it->second,
						std::next(it) == block->second.end() ? "" : ",");
				}
				text += std::format("    }}{}\n", std::next(block) == blocks.end() ? "" : ",");
			}
			text += "}\n";

			std::error_code ec;
			if (const auto parent = a_file.parent_path(); !parent.empty()) {
				std::filesystem::create_directories(parent, ec);
			}

			std::ofstream stream{ a_file, std::ios::binary };
			if (!stream) {
				REX::ERROR(
					"could not write {}, settings stay at defaults",
					a_file.generic_string());
				return;
			}

			stream.write(text.data(), static_cast<std::streamsize>(text.size()));
			REX::INFO("wrote a settings file with the defaults to {}", a_file.generic_string());
		}
```

Und die öffentlichen Funktionen:

```cpp
	void DeclareBool(std::string_view a_path, bool a_default)
	{
		Declare<bool>(a_path, a_default);
	}

	void DeclareUInt32(std::string_view a_path, std::uint32_t a_default)
	{
		Declare<std::uint32_t>(a_path, a_default);
	}

	bool GetBool(std::string_view a_path) noexcept
	{
		return Get<bool>(a_path, false);
	}

	std::uint32_t GetUInt32(std::string_view a_path) noexcept
	{
		return Get<std::uint32_t>(a_path, 0);
	}

	void DeclareFeature(std::string_view a_name, bool a_default)
	{
		DeclareBool(std::format("{}/enabled", a_name), a_default);
	}

	bool IsEnabled(std::string_view a_name) noexcept
	{
		return GetBool(std::format("{}/enabled", a_name));
	}
```

`<vector>` und `<utility>` kommen zu den Includes.

**Achtung bei `IsEnabled`:** Es baut jetzt je Aufruf eine `std::string`, und es wird je Feature und
Frame gerufen. Bei zwei Features ist das nichts; wenn F daraus zwanzig macht, gehört es gemessen.

-   [ ] **Step 5: Bauen und Test grün sehen**

```pwsh
& $cmake --build --preset FO4 --target FeatureSettingsTests
& "build\FO4\Release\FeatureSettingsTests.exe"
```

Erwartet: 22 Prüfzeilen `ok`, `0 failure(s)`.

-   [ ] **Step 6: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `a path without a slash was refused` und
`and so was one with too many` schlagen fehl, alle übrigen bleiben grün.

Mutation in `SplitPath`: die Mehrsegment-Prüfung streichen und einen fehlenden Schrägstrich
durchlassen, statt beide Hälften uninitialisiert zu lassen — der Beleg soll die Ablehnung treffen,
nicht das Verhalten danach:

```cpp
			const auto slash = a_path.find('/');
			if (slash == std::string_view::npos) {
				a_block = a_path;
				a_key = a_path;
				return true;
			}
```

Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 7: Alle Tests laufen lassen und committen**

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: acht Tests grün.

```bash
git add -A
git commit -m "feat: let settings live outside feature switches"
```

---

## Task 2: `Menu::Gate`

**Files:**

-   Create: `src/Menu/MenuGate.h`, `src/Menu/MenuGate.cpp`
-   Test: `tests/MenuGateTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts. Der Automat kennt weder ImGui noch die Engine.
-   Produces: `Menu::Gate`. Aufgabe 4 gibt ihm die echten Rückrufe, Aufgabe 5 die Taste.

-   [ ] **Step 1: Den fehlschlagenden Test schreiben**

`tests/MenuGateTests.cpp`:

```cpp
#include "Menu/MenuGate.h"

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

	struct Counters
	{
		int suppressed = 0;
		int restored = 0;
		bool suppressSucceeds = true;
	};

	// Returned as a prvalue on purpose: Gate holds a std::atomic and is
	// therefore neither copyable nor movable. Guaranteed elision is what makes
	// this compile.
	Menu::Gate MakeGate(Counters& a_counters)
	{
		return Menu::Gate{
			[&a_counters] {
				++a_counters.suppressed;
				return a_counters.suppressSucceeds;
			},
			[&a_counters] { ++a_counters.restored; }
		};
	}
}

int main()
{
	// A press opens, and the game's input is taken away once.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(!gate.IsOpen(), "a fresh gate is closed");

		gate.RequestToggle();
		Check(gate.Tick(), "a request opens it on the next tick");
		Check(counters.suppressed == 1, "and takes the game's input away once");
		Check(counters.restored == 0, "and gives nothing back yet");

		gate.RequestToggle();
		Check(!gate.Tick(), "a second request closes it");
		Check(counters.restored == 1, "and gives the input back once");
		Check(counters.suppressed == 1, "without asking to suppress again");
	}

	// A tick without a request changes nothing.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(!gate.Tick(), "a tick without a request leaves it closed");
		Check(counters.suppressed == 0, "and asks for nothing");
	}

	// Key repeat must not queue up transitions.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.RequestToggle();
		gate.RequestToggle();
		gate.RequestToggle();

		Check(gate.Tick(), "three requests before one tick open it");
		Check(counters.suppressed == 1, "and count as one transition");
		Check(!gate.Tick(), "and the next tick does not close it again");
	}

	// A failed suppression still opens: an overlay without an input layer is
	// worth more than no overlay.
	{
		Counters counters;
		counters.suppressSucceeds = false;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.RequestToggle();
		Check(gate.Tick(), "the overlay opens even when suppression failed");

		gate.RequestToggle();
		Check(!gate.Tick(), "and closes again");
		Check(counters.restored == 0, "without giving back what it never took");
	}

	// The key is the configured one, and nothing else.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		Check(gate.IsToggleKey(35), "the configured key is recognised");
		Check(!gate.IsToggleKey(36), "another key is not");

		gate.SetToggleKey(112);
		Check(gate.IsToggleKey(112), "and the key can be changed");
		Check(!gate.IsToggleKey(35), "which retires the old one");
	}

	// A key of zero disables the toggle rather than matching every key.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(0);

		Check(!gate.IsToggleKey(0), "a toggle key of zero matches nothing");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 2: Test in `CMakeLists.txt` eintragen**

Nach `add_test(NAME FeatureRegistry COMMAND FeatureRegistryTests)`, nach dem Muster der
bestehenden Blöcke: Ziel `MenuGateTests`, Quellen `tests/MenuGateTests.cpp` und
`src/Menu/MenuGate.cpp`, `add_test(NAME MenuGate COMMAND MenuGateTests)`.

-   [ ] **Step 3: Bauen und den Fehlschlag sehen**

Erwartet: **Konfiguration schlägt fehl**, `src/Menu/MenuGate.cpp` existiert nicht.

-   [ ] **Step 4: Den Header schreiben**

`src/Menu/MenuGate.h`:

```cpp
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace Menu
{
	/// Owns whether the overlay is open, and turns a pending key press into a
	/// transition.
	///
	/// It knows neither ImGui nor the engine: the input layer arrives as two
	/// callbacks. That is what makes the whole state machine testable without a
	/// game, and it is the reason for the seam - not tidiness.
	class Gate
	{
	public:
		/// Takes the game's input away. Returns whether it worked; a false
		/// still opens the overlay, because an overlay without an input layer
		/// is worth more than no overlay at all.
		using Suppress = std::function<bool()>;

		/// Gives it back. Called only when the matching Suppress succeeded.
		using Restore = std::function<void()>;

		Gate(Suppress a_suppress, Restore a_restore);

		void SetToggleKey(std::uint32_t a_key) noexcept;

		/// A key of zero matches nothing, so a settings file that names no key
		/// disables the overlay rather than opening it on every keystroke.
		[[nodiscard]] bool IsToggleKey(std::uint32_t a_key) const noexcept;

		/// Called from the window procedure, on the window thread. Records the
		/// wish and returns; key repeat therefore costs one transition, not one
		/// per repeat.
		void RequestToggle() noexcept;

		/// Called once per frame on the render thread, which is where the
		/// engine calls belong. Carries out a pending wish and returns whether
		/// the overlay is open.
		bool Tick() noexcept;

		[[nodiscard]] bool IsOpen() const noexcept { return _open; }

	private:
		Suppress _suppress;
		Restore _restore;

		std::atomic<bool> _toggleWanted{ false };
		bool _open{ false };
		bool _suppressing{ false };
		std::uint32_t _toggleKey{ 0 };
	};
}
```

-   [ ] **Step 5: Die Implementierung schreiben**

`src/Menu/MenuGate.cpp`:

```cpp
#include "Menu/MenuGate.h"

namespace Menu
{
	Gate::Gate(Suppress a_suppress, Restore a_restore) :
		_suppress(std::move(a_suppress)),
		_restore(std::move(a_restore))
	{}

	void Gate::SetToggleKey(std::uint32_t a_key) noexcept
	{
		_toggleKey = a_key;
	}

	bool Gate::IsToggleKey(std::uint32_t a_key) const noexcept
	{
		return _toggleKey != 0 && a_key == _toggleKey;
	}

	void Gate::RequestToggle() noexcept
	{
		_toggleWanted.store(true, std::memory_order_release);
	}

	bool Gate::Tick() noexcept
	{
		if (!_toggleWanted.exchange(false, std::memory_order_acq_rel)) {
			return _open;
		}

		if (_open) {
			// Only give back what was actually taken. A suppression that failed
			// left the engine untouched, and restoring it would be a second
			// call into something that never worked.
			if (_suppressing && _restore) {
				_restore();
			}
			_suppressing = false;
			_open = false;
			return false;
		}

		_suppressing = _suppress && _suppress();
		_open = true;
		return true;
	}
}
```

-   [ ] **Step 6: Bauen und Test grün sehen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4 --target MenuGateTests
& "build\FO4\Release\MenuGateTests.exe"
```

Erwartet: 20 Prüfzeilen `ok`, `0 failure(s)`.

-   [ ] **Step 7: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `and count as one transition` schlägt fehl, sowie
`and the next tick does not close it again`. Alle übrigen bleiben grün.

Mutation in `Tick`: das `exchange` durch ein `load` ersetzen, sodass der Wunsch stehen bleibt.

```cpp
		if (!_toggleWanted.load(std::memory_order_acquire)) {
			return _open;
		}
```

Übersetzt sauber. Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "feat: add the overlay's state machine"
```

---

## Task 3: ImGui aufsetzen und zeichnen

**Files:**

-   Modify: `vcpkg.json`, `CMakeLists.txt`, `src/Render/SwapChainHook.cpp`, `src/XSEPlugin.cpp`
-   Create: `src/Menu/Overlay.h`, `src/Menu/Overlay.cpp`, `src/Menu/MenuSystem.h`,
    `src/Menu/MenuSystem.cpp`

**Interfaces:**

-   Consumes: `Menu::Gate` (2), `Features::Settings::DeclareUInt32`/`GetUInt32` (1),
    `Render::GetDevice`/`GetContext`/`GetSwapChain` (aus B1).
-   Produces: `void Menu::StartSystem() noexcept` und `void Menu::TickSystem() noexcept`.
    Aufgabe 4 hängt die Fensterprozedur an dieselben Stellen.

In dieser Aufgabe wird die Taste **abgefragt** statt über eine Fensterprozedur zu kommen, damit die
Aufgabe für sich lauffähig ist. Aufgabe 4 ersetzt die Abfragestelle; `Gate::RequestToggle` bleibt.

-   [ ] **Step 1: Abhängigkeit aufnehmen**

In `vcpkg.json`, im Array `dependencies`, nach `"glaze"`:

```json
{
    "name": "imgui",
    "default-features": false,
    "features": ["dx11-binding", "win32-binding"]
}
```

In `CMakeLists.txt`, nach dem `commonlibf4`-Block:

```cmake
find_package(imgui CONFIG REQUIRED)
```

und beim Plugin-Ziel:

```cmake
target_link_libraries(${PROJECT_NAME} PRIVATE CommonLibF4::CommonLibF4 imgui::imgui)
```

-   [ ] **Step 2: Den Overlay-Header schreiben**

`src/Menu/Overlay.h`:

```cpp
#pragma once

#include <cstdint>

namespace Menu
{
	/// Sets ImGui up against the live device on first use, and draws one frame
	/// when asked to.
	///
	/// Everything here runs on the render thread, for the same reason
	/// subproject C kept its D3D calls there: it is the one thread we can be
	/// sure about.
	class Overlay
	{
	public:
		/// Idempotent. Returns whether ImGui is ready; a failure is logged once
		/// and never retried, because a setup that failed will fail again.
		[[nodiscard]] bool EnsureReady() noexcept;

		/// One ImGui frame. Draws nothing but the frame itself when a_visible
		/// is false, so that ImGui keeps its input state consistent.
		void Draw(bool a_visible, std::uint64_t a_frame) noexcept;

		[[nodiscard]] void* Window() const noexcept { return _window; }

	private:
		[[nodiscard]] bool BindBackBuffer() noexcept;

		void* _window{ nullptr };
		void* _renderTarget{ nullptr };
		std::uint32_t _width{ 0 };
		std::uint32_t _height{ 0 };
		bool _ready{ false };
		bool _refused{ false };
	};
}
```

**Warum `void*` statt der D3D-Typen:** Der Header wird von `MenuSystem.cpp` eingebunden, und
`imgui_impl_dx11.h` bringt eigene Vorwärtsdeklarationen mit. Die beiden Typwelten treffen nur in
`Overlay.cpp` aufeinander, und dort genau einmal.

-   [ ] **Step 3: Die Implementierung schreiben**

`src/Menu/Overlay.cpp`:

```cpp
#include "Menu/Overlay.h"

#include "Render/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace Menu
{
	namespace
	{
		// The two type worlds meet here and nowhere else. REX::W32 and the
		// Windows SDK declare the same COM objects with different names; the
		// object behind the pointer is one and the same.
		template <class T, class U>
		T* Reinterpret(U* a_pointer) noexcept
		{
			return reinterpret_cast<T*>(a_pointer);
		}
	}

	bool Overlay::EnsureReady() noexcept
	{
		if (_ready) {
			return true;
		}
		if (_refused) {
			return false;
		}

		auto* const device = Render::GetDevice();
		auto* const context = Render::GetContext();
		auto* const swapChain = Render::GetSwapChain();

		if (device == nullptr || context == nullptr || swapChain == nullptr) {
			REX::ERROR("no renderer, the overlay stays off");
			_refused = true;
			return false;
		}

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) < 0 || desc.outputWindow == nullptr) {
			REX::ERROR("no output window, the overlay stays off");
			_refused = true;
			return false;
		}

		_window = desc.outputWindow;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		auto& io = ImGui::GetIO();

		// Our own cursor rather than the system one: the game hides and warps
		// the system cursor for its own purposes, and fighting it over that is
		// a fight with no end.
		io.MouseDrawCursor = true;

		// No imgui.ini. The game's working directory is not ours to write in,
		// and the overlay has no layout worth remembering yet.
		io.IniFilename = nullptr;

		if (!ImGui_ImplWin32_Init(_window) ||
			!ImGui_ImplDX11_Init(
				Reinterpret<ID3D11Device>(device),
				Reinterpret<ID3D11DeviceContext>(context))) {
			REX::ERROR("ImGui refused the device, the overlay stays off");
			ImGui::DestroyContext();
			_refused = true;
			return false;
		}

		REX::INFO(
			"overlay ready, window {}, ImGui {}",
			_window,
			ImGui::GetVersion());

		_ready = true;
		return true;
	}

	bool Overlay::BindBackBuffer() noexcept
	{
		auto* const swapChain = Render::GetSwapChain();
		auto* const context = Render::GetContext();
		if (swapChain == nullptr || context == nullptr) {
			return false;
		}

		REX::W32::DXGI_SWAP_CHAIN_DESC desc{};
		if (swapChain->GetDesc(std::addressof(desc)) < 0) {
			return false;
		}

		// Held across frames, but thrown away when the buffer changed size: a
		// resolution change or a switch to fullscreen leaves the old view
		// pointing at a buffer that no longer exists.
		const auto width = desc.bufferDesc.width;
		const auto height = desc.bufferDesc.height;
		if (_renderTarget != nullptr && (width != _width || height != _height)) {
			Reinterpret<REX::W32::ID3D11RenderTargetView>(_renderTarget)->Release();
			_renderTarget = nullptr;
		}

		if (_renderTarget == nullptr) {
			REX::W32::ID3D11Texture2D* backBuffer = nullptr;
			if (swapChain->GetBuffer(0, REX::W32::IID_ID3D11Texture2D, reinterpret_cast<void**>(std::addressof(backBuffer))) < 0 ||
				backBuffer == nullptr) {
				return false;
			}

			REX::W32::ID3D11RenderTargetView* view = nullptr;
			const auto hr = Render::GetDevice()->CreateRenderTargetView(
				backBuffer,
				nullptr,
				std::addressof(view));
			backBuffer->Release();

			if (hr < 0 || view == nullptr) {
				return false;
			}

			_renderTarget = view;
			_width = width;
			_height = height;
		}

		auto* view = Reinterpret<REX::W32::ID3D11RenderTargetView>(_renderTarget);

		// ImGui draws into whatever is bound, and what that is at Present time
		// is nobody's promise. Bind the back buffer ourselves.
		context->OMSetRenderTargets(1, std::addressof(view), nullptr);
		return true;
	}

	void Overlay::Draw(bool a_visible, std::uint64_t a_frame) noexcept
	{
		if (!_ready) {
			return;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (a_visible) {
			ImGui::SetNextWindowSize(ImVec2{ 380.0f, 0.0f }, ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Community Shaders")) {
				ImGui::Text("Frame %llu", static_cast<unsigned long long>(a_frame));
				ImGui::Separator();
				ImGui::TextUnformatted("The feature list arrives with subproject E2.");
			}
			ImGui::End();
		}

		ImGui::Render();

		if (a_visible && BindBackBuffer()) {
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		}
	}
}
```

**Der `ImGui::Render()` läuft auch unsichtbar**, damit ImGui seinen Eingabezustand fortschreibt und
beim Öffnen nicht mit einer Bildnummer aus der Vergangenheit anfängt.

-   [ ] **Step 4: `MenuSystem` schreiben**

`src/Menu/MenuSystem.h`:

```cpp
#pragma once

namespace Menu
{
	/// Declares the overlay's settings. Called once, from kGameDataReady,
	/// before Features::StartSystem loads the file.
	void StartSystem() noexcept;

	/// The one entry per frame, from Present, on the render thread.
	void TickSystem() noexcept;
}
```

`src/Menu/MenuSystem.cpp`:

```cpp
#include "Menu/MenuSystem.h"

#include "Feature/FeatureSettings.h"
#include "Menu/MenuGate.h"
#include "Menu/Overlay.h"
#include "Render/SwapChainHook.h"

#include <REX/W32/USER32.h>

namespace Menu
{
	namespace
	{
		constexpr auto kToggleKeyPath = "Menu/toggleKey"sv;

		// VK_END. Unbound in Fallout 4 and common among its plugins.
		constexpr std::uint32_t kDefaultToggleKey = 0x23;

		Gate& TheGate()
		{
			// The input layer arrives in task 4; until then the gate opens the
			// overlay without taking anything away, which is exactly what its
			// failed-suppression path already does.
			static Gate gate{ [] { return false; }, [] {} };
			return gate;
		}

		Overlay& TheOverlay()
		{
			static Overlay overlay;
			return overlay;
		}

		// Polled here rather than taken from a window procedure, because there
		// is none yet. Task 4 replaces this and keeps the gate.
		void PollToggleKey()
		{
			static bool wasDown = false;

			const auto key = Features::Settings::GetUInt32(kToggleKeyPath);
			if (key == 0) {
				return;
			}

			const bool isDown = (REX::W32::GetKeyState(static_cast<std::int32_t>(key)) & 0x8000) != 0;
			if (isDown && !wasDown) {
				TheGate().RequestToggle();
			}
			wasDown = isDown;
		}
	}

	void StartSystem() noexcept
	{
		Features::Settings::DeclareUInt32(kToggleKeyPath, kDefaultToggleKey);
	}

	void TickSystem() noexcept
	{
		if (!TheOverlay().EnsureReady()) {
			return;
		}

		TheGate().SetToggleKey(Features::Settings::GetUInt32(kToggleKeyPath));
		PollToggleKey();

		TheOverlay().Draw(TheGate().Tick(), Render::FrameCount());
	}
}
```

-   [ ] **Step 5: Verdrahten**

In `src/XSEPlugin.cpp`, im `kGameDataReady`-Zweig, **vor** `Features::StartSystem()`:

```cpp
			// Before the features, because their StartSystem is what loads the
			// settings file, and a setting has to be declared before that.
			Menu::StartSystem();
```

samt `#include "Menu/MenuSystem.h"`.

In `src/Render/SwapChainHook.cpp`, direkt **nach** `Features::TickSystem();`:

```cpp
			// After the features: the overlay belongs on top of whatever they
			// drew.
			Menu::TickSystem();
```

samt Include.

-   [ ] **Step 6: Bauen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4
```

Erwartet: übersetzt ohne Warnung. Schlägt `imgui_impl_dx11.h` mit unbekannten D3D-Typen fehl, ist
die erste Annahme aus Abschnitt 8 der Spec widerlegt — dann bekommt `Overlay.cpp` eine
Übersetzungseinheit ohne `REX::W32`, und die Zeiger reisen als `void*` hinein.

-   [ ] **Step 7: Alle Host-Tests laufen lassen und committen**

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: neun Tests grün, Plugin-Prüfung ohne Beanstandung.

```bash
git add -A
git commit -m "feat: draw an imgui overlay from present"
```

---

## Task 4: Fensterprozedur und Eingabeschicht

**Files:**

-   Create: `src/Menu/Win32.h`, `src/Menu/WindowHook.h`, `src/Menu/WindowHook.cpp`,
    `src/Menu/InputLayer.h`, `src/Menu/InputLayer.cpp`
-   Modify: `src/Menu/MenuSystem.cpp`

**Interfaces:**

-   Consumes: `Menu::Gate` (2), `Menu::Overlay::Window` (3).
-   Produces: `Menu::InstallWindowHook`, `Menu::InputLayer`.

-   [ ] **Step 1: Die fehlenden Prototypen deklarieren**

`src/Menu/Win32.h`:

```cpp
#pragma once

#include <REX/W32/USER32.h>

// REX::W32 declares SetWindowLongPtrA but not the W form, and no
// CallWindowProc at all. user32.lib is already linked PUBLIC by
// commonlib-shared, so only the prototypes are missing.
//
// The W forms on purpose: using the A form on a Unicode window switches its
// message translation to ANSI, which subproject E2 would notice the moment it
// puts a text field on screen.
namespace Menu::Win32
{
	extern "C"
	{
		std::intptr_t __stdcall SetWindowLongPtrW(
			REX::W32::HWND a_wnd,
			std::int32_t a_index,
			std::intptr_t a_newPtr) noexcept;

		std::intptr_t __stdcall CallWindowProcW(
			REX::W32::WNDPROC a_prev,
			REX::W32::HWND a_wnd,
			std::uint32_t a_msg,
			std::uintptr_t a_wParam,
			std::intptr_t a_lParam) noexcept;
	}
}
```

-   [ ] **Step 2: Die Eingabeschicht schreiben**

`src/Menu/InputLayer.h`:

```cpp
#pragma once

namespace Menu
{
	/// Holds the engine's own input enable layer, the one its menus use.
	///
	/// This is the first call into an engine function in the whole port -
	/// everything before it only read memory. The three address library ids
	/// behind it were measured present for 1.11.240; whether they point at the
	/// functions we take them for is what the acceptance run decides.
	class InputLayer
	{
	public:
		/// Takes the game's input away. Returns whether it worked.
		[[nodiscard]] bool Suppress() noexcept;

		/// Gives it back and releases the layer.
		void Restore() noexcept;

	private:
		void* _layer{ nullptr };
	};
}
```

`src/Menu/InputLayer.cpp`:

```cpp
#include "Menu/InputLayer.h"

#include <RE/B/BSInputEnableManager.h>
#include <RE/B/BSInputEnableLayer.h>

namespace Menu
{
	bool InputLayer::Suppress() noexcept
	{
		if (_layer != nullptr) {
			return true;
		}

		auto* const manager = RE::BSInputEnableManager::GetSingleton();
		if (manager == nullptr) {
			REX::ERROR("no input enable manager, the overlay opens without one");
			return false;
		}

		RE::BSTSmartPointer<RE::BSInputEnableLayer> layer;
		if (!manager->AllocateNewLayer(layer, "CommunityShadersFO4") || !layer) {
			REX::ERROR("could not allocate an input layer, the overlay opens without one");
			return false;
		}

		// kAll is -1: movement, looking, fighting, VATS and the rest, all at
		// once. A menu that leaves any of them on is a menu you fight with.
		manager->EnableUserEvent(
			layer->layerID,
			RE::UserEvents::USER_EVENT_FLAG::kAll,
			false,
			RE::UserEvents::SENDER_ID::kMenu);

		layer->IncRef();
		_layer = layer.get();

		REX::INFO("input layer {} acquired", layer->layerID);
		return true;
	}

	void InputLayer::Restore() noexcept
	{
		if (_layer == nullptr) {
			return;
		}

		auto* const layer = static_cast<RE::BSInputEnableLayer*>(_layer);
		auto* const manager = RE::BSInputEnableManager::GetSingleton();

		if (manager != nullptr) {
			manager->EnableUserEvent(
				layer->layerID,
				RE::UserEvents::USER_EVENT_FLAG::kAll,
				true,
				RE::UserEvents::SENDER_ID::kMenu);
		}

		static_cast<void>(layer->DecRef());
		_layer = nullptr;

		REX::INFO("input layer released");
	}
}
```

-   [ ] **Step 3: Die Fensterprozedur schreiben**

`src/Menu/WindowHook.h`:

```cpp
#pragma once

#include <functional>

namespace Menu
{
	/// Chains our procedure in front of the window's own. a_wantsToggle is
	/// asked whether a key is the toggle key, a_onToggle records the wish, and
	/// a_isOpen decides whether input goes to ImGui or to the game.
	///
	/// Safe to call once; further calls are ignored.
	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept;
}
```

`src/Menu/WindowHook.cpp`:

```cpp
#include "Menu/WindowHook.h"

#include "Menu/Win32.h"

#include <imgui.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam);

namespace Menu
{
	namespace
	{
		REX::W32::WNDPROC g_original = nullptr;
		std::function<bool(std::uint32_t)> g_wantsToggle;
		std::function<void()> g_onToggle;
		std::function<bool()> g_isOpen;
		bool g_installed = false;

		bool IsInputMessage(std::uint32_t a_msg) noexcept
		{
			// Keyboard and mouse, the ranges the game and other plugins care
			// about. Everything else - painting, focus, sizing - must reach the
			// window or the game misbehaves in ways that have nothing to do
			// with us.
			return (a_msg >= 0x0100 && a_msg <= 0x0109) ||  // WM_KEYFIRST..WM_UNICHAR
			       (a_msg >= 0x0200 && a_msg <= 0x020E);    // WM_MOUSEFIRST..WM_MOUSEHWHEEL
		}

		std::intptr_t WindowProc(
			REX::W32::HWND a_wnd,
			std::uint32_t a_msg,
			std::uintptr_t a_wParam,
			std::intptr_t a_lParam)
		{
			if (a_msg == REX::W32::WM_KEYDOWN && g_wantsToggle &&
				g_wantsToggle(static_cast<std::uint32_t>(a_wParam))) {
				g_onToggle();
				return 0;
			}

			if (g_isOpen && g_isOpen()) {
				if (ImGui_ImplWin32_WndProcHandler(
						reinterpret_cast<HWND>(a_wnd),
						a_msg,
						a_wParam,
						a_lParam)) {
					return 1;
				}

				// The game does not read its input from messages, but other
				// plugins and overlays do. A message meant for us should not
				// also take effect somewhere else.
				if (IsInputMessage(a_msg)) {
					return 0;
				}
			}

			return Win32::CallWindowProcW(g_original, a_wnd, a_msg, a_wParam, a_lParam);
		}
	}

	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept
	{
		if (g_installed || a_window == nullptr) {
			return;
		}

		g_wantsToggle = std::move(a_wantsToggle);
		g_onToggle = std::move(a_onToggle);
		g_isOpen = std::move(a_isOpen);

		const auto previous = Win32::SetWindowLongPtrW(
			static_cast<REX::W32::HWND>(a_window),
			REX::W32::GWLP_WNDPROC,
			reinterpret_cast<std::intptr_t>(&WindowProc));

		if (previous == 0) {
			REX::ERROR("could not chain the window procedure, the overlay stays off");
			return;
		}

		g_original = reinterpret_cast<REX::W32::WNDPROC>(previous);
		g_installed = true;

		REX::INFO("window procedure chained, original at {}", reinterpret_cast<void*>(previous));
	}
}
```

-   [ ] **Step 4: `MenuSystem` umstellen**

In `src/Menu/MenuSystem.cpp`: `PollToggleKey` und sein Aufruf entfallen. `TheGate()` bekommt die
echten Rückrufe, und nach `EnsureReady` wird die Prozedur verkettet.

```cpp
		InputLayer& TheInputLayer()
		{
			static InputLayer layer;
			return layer;
		}

		Gate& TheGate()
		{
			static Gate gate{
				[] { return TheInputLayer().Suppress(); },
				[] { TheInputLayer().Restore(); }
			};
			return gate;
		}
```

```cpp
	void TickSystem() noexcept
	{
		if (!TheOverlay().EnsureReady()) {
			return;
		}

		TheGate().SetToggleKey(Features::Settings::GetUInt32(kToggleKeyPath));

		// Installed here rather than at kGameDataReady: the window handle comes
		// from the swap chain, which EnsureReady is what reads.
		InstallWindowHook(
			TheOverlay().Window(),
			[](std::uint32_t a_key) { return TheGate().IsToggleKey(a_key); },
			[] { TheGate().RequestToggle(); },
			[] { return TheGate().IsOpen(); });

		TheOverlay().Draw(TheGate().Tick(), Render::FrameCount());
	}
```

Die Includes für `Menu/InputLayer.h` und `Menu/WindowHook.h` kommen dazu, der für
`REX/W32/USER32.h` entfällt.

-   [ ] **Step 5: Bauen**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: übersetzt ohne Warnung, neun Tests grün, Plugin-Prüfung ohne Beanstandung.

Zieht `imgui.h` in `WindowHook.cpp` Windows-Typen herein, die mit `REX::W32` kollidieren, wird die
Deklaration von `ImGui_ImplWin32_WndProcHandler` mit eigenen Typen wiederholt statt `imgui.h`
einzubinden — die Signatur ist `LRESULT(HWND, UINT, WPARAM, LPARAM)`, also
`std::intptr_t(void*, std::uint32_t, std::uintptr_t, std::intptr_t)`.

-   [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: take input for the overlay and off the game"
```

---

## Task 5: Abnahme im Spiel

**Files:** keine Änderung erwartet.

Diese Aufgabe braucht den Nutzer. Ein Spielstart deckt alles ab. Vorher bauen und deployen, damit
die Installation dem Stand entspricht.

-   [ ] **Step 1: Start und Grundzustand**

Spiel über `f4se_loader.exe`, `coc SanctuaryExt`.

Erwartet im Log: `overlay ready, window …, ImGui 1.92.6` und
`window procedure chained, original at …`. In der Einstellungsdatei steht ein Block `Menu` mit
`toggleKey: 35`.

-   [ ] **Step 2: Öffnen und schließen** — Abnahmekriterien 1 und 2

**Ende** drücken. Erwartet: ein Fenster „Community Shaders" mit einer laufenden Bildnummer, ein
gezeichneter Mauszeiger. Das Fenster lässt sich mit der Maus verschieben. **Ende** schließt es.

-   [ ] **Step 3: Die Spieleingabe steht** — Abnahmekriterium 3, der Prüfstein

Overlay öffnen, dann W, A, S, D drücken, die Maus bewegen und links klicken.

Erwartet: Der Spieler bewegt sich **nicht**, die Kamera dreht sich **nicht**, es wird **nicht**
geschossen. Im Log steht `input layer <n> acquired`. Nach dem Schließen (`input layer released`)
funktioniert alles wieder.

Bleibt die Eingabe aktiv, ist die Identität der drei IDs widerlegt — vorhanden waren sie, aber
nicht das, wofür wir sie hielten. Dann ist das ein Befund, kein Weiterbasteln.

-   [ ] **Step 4: Der Stich bleibt** — Abnahmekriterium 4

Bei offenem Overlay auf das Bild dahinter achten: der Rotstich aus C muss unverändert da sein.

-   [ ] **Step 5: Alt-Tab** — Abnahmekriterium 5

Mit offenem Overlay hinaus und zurück. Erwartet: Das Overlay ist weiterhin da und bedienbar, kein
Absturz, keine schwarze Fläche.

-   [ ] **Step 6: Die Taste ist konfigurierbar** — Abnahmekriterium 6

Spiel beenden, in der Einstellungsdatei `Menu/toggleKey` auf `112` setzen (F1), starten.

Erwartet: **F1** öffnet, **Ende** tut nichts.

-   [ ] **Step 7: Befunde notieren**

Alles Beobachtete roh festhalten, bevor etwas geändert wird. Rohmaterial für Aufgabe 6.

Kein Commit in dieser Aufgabe.

---

## Task 6: Dokumente und Abschluss

**Files:**

-   Modify: `docs/fallout4-port/ROADMAP.md`, `.claude/CLAUDE.md`

-   [ ] **Step 1: Roadmap nachziehen**

-   Zeile E in **E1** (abgeschlossen, Abnahme „Overlay im Spiel bedienbar, Spieleingabe steht") und
    **E2** (offen, Abnahme „Einstellungen im Overlay ändern, sie überleben einen Neustart") teilen,
    mit einem Satz zur Begründung der Teilung.
-   Abschnitt „Aus Teilprojekt E1 bestätigt" nach dem Muster der vorherigen anlegen, mit den
    gemessenen Werten aus Aufgabe 5 — insbesondere, ob die drei Adressbibliotheks-IDs auf die
    erwarteten Funktionen zeigen.
-   Die Fallstricke aufnehmen: was `REX::W32` an USER32 fehlt, und dass eine vorhandene ID nur
    Auflösbarkeit belegt, nicht Identität.

-   [ ] **Step 2: `CLAUDE.md` nachziehen**

-   Ein Abschnitt „Menu": wo es lebt, warum es kein Feature ist, dass die Fensterprozedur nur ein
    Flag setzt und der Render-Thread handelt, und dass `src/Menu/Win32.h` die zwei fehlenden
    Prototypen führt.
-   Der Abschnitt „Features" bekommt einen Hinweis auf die erweiterten Einstellungen
    (`DeclareBool`/`DeclareUInt32` mit Zwei-Segment-Pfad).
-   Die i18n-Zeile in „Temporarily moot" wandert von `Subproject E` auf `Subproject E2`.

-   [ ] **Step 3: Voller Lauf**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
& $cmake --build --preset FO4 --target package
pwsh tools/verify-package.ps1
```

Erwartet: neun Tests grün, beide Prüfskripte ohne Beanstandung.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: record subproject e1 acceptance"
```

-   [ ] **Step 5: Abschluss**

`superpowers:finishing-a-development-branch` aufrufen. Erwartete Wahl nach bisherigem Muster: lokal
nach `dev` mergen (Fast-Forward), Feature-Branch löschen, Push auf Ansage.

---

## Self-Review

**Spec-Abdeckung**

| Spec-Abschnitt                           | Aufgabe                                                               |
| ---------------------------------------- | --------------------------------------------------------------------- |
| 4.1 `Menu::System`                       | 3 (Step 4)                                                            |
| 4.2 Das Renderziel                       | 3 (Step 3, `BindBackBuffer`)                                          |
| 4.3 Die Fensterprozedur                  | 4 (Step 1 und 3)                                                      |
| 4.4 Die Eingabeschicht                   | 4 (Step 2)                                                            |
| 4.5 Was gezeichnet wird                  | 3 (Step 3, `Draw`)                                                    |
| 4.6 Erweiterung von `Features::Settings` | 1                                                                     |
| 5 Zustände und Ablauf                    | 2 (Gate), 3 (Zeitpunkte)                                              |
| 6 Fehlerbehandlung, alle sechs Fälle     | 3 (Renderer, ImGui, Ziel, Taste `0`), 4 (Prozedur, Manager `nullptr`) |
| 7.1 Host-Tests                           | 1, 2                                                                  |
| 7.3 Abnahmekriterien 1–7                 | 5, plus Kriterium 7 in 1–4                                            |
| 8 Annahmen, alle fünf                    | 3 (Step 6), 4 (Step 5), 5 (Step 3 und 5)                              |
| 9 Übergabe                               | 6                                                                     |

**Bewusste Abweichungen von der Spec**

-   Die Spec nennt `Menu::Tick`; der Plan macht daraus `Menu::TickSystem` und `Menu::StartSystem`,
    passend zu `Features::StartSystem`/`TickSystem`. Zwei Systeme, die dasselbe tun, sollen auch
    gleich heißen.
-   Aufgabe 3 fragt die Taste ab, statt sie aus der Fensterprozedur zu bekommen, damit sie für sich
    lauffähig ist. Aufgabe 4 ersetzt die Abfragestelle; `Gate::RequestToggle` bleibt unverändert.
    Verworfen wird dabei eine Funktion von zwölf Zeilen — der Preis dafür, dass Aufgabe 3 ein
    prüfbares Ergebnis hat statt eines unsichtbaren.

**Typkonsistenz geprüft:** `Menu::Gate` nimmt `std::function<bool()>` und `std::function<void()>`;
`MenuSystem` reicht Lambdas herein, die auf `InputLayer::Suppress`/`Restore` zeigen — Rückgabetypen
`bool` und `void` passen. `InstallWindowHook` nimmt `std::function<bool(std::uint32_t)>`,
`std::function<void()>` und `std::function<bool()>`; die drei Lambdas in `TickSystem` entsprechen
`Gate::IsToggleKey`, `Gate::RequestToggle` und `Gate::IsOpen`. `Overlay::Window()` gibt `void*`,
`InstallWindowHook` nimmt `void*`. `Features::Settings::GetUInt32` gibt `std::uint32_t`,
`Gate::SetToggleKey` nimmt `std::uint32_t`.

**Platzhalter:** keine. Jeder Codeschritt enthält den Code, jeder Prüfschritt das erwartete
Ergebnis, jeder Mutationsschritt den vorher benannten Fehlschlag.
