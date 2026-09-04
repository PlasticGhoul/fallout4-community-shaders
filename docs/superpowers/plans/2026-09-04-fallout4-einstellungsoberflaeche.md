# Teilprojekt E2 — Einstellungsoberfläche: Implementierungsplan

> **Für agentische Bearbeiter:** ERFORDERLICHE UNTER-SKILL:
> `superpowers:subagent-driven-development` (empfohlen) oder
> `superpowers:executing-plans`, um diesen Plan Aufgabe für Aufgabe umzusetzen. Die Schritte
> benutzen Kästchen (`- [ ]`) zur Verfolgung.

**Ziel:** Eine Oberfläche im ImGui-Overlay, in der jede Einstellung sichtbar, änderbar und
dauerhaft ist — samt Schrift, Theme und Sprache, in denen das geschieht.

**Architektur:** Einstellungen werden deklarativ beschrieben — jedes Feature meldet in `Declare()`
seine Einstellungen mit Art, Vorgabewert und Beschriftung an. Das Menü zeichnet daraus generisch;
kein Feature bindet ImGui ein. Dieselbe Deklaration liefert die i18n-Schlüssel und die Liste
aller Schlüssel, die in der Einstellungsdatei stehen müssen — womit das Nachrüstungsproblem aus
E1 verschwindet, statt gelöst zu werden.

**Technikstapel:** C++23, MSVC, `/W4 /WX`. CommonLibF4 (REX, F4SE), glaze (über REX), ImGui
1.92.6 mit `dx11-binding` und `win32-binding`. Tests sind eigenständige ausführbare Dateien mit
einem handgeschriebenen `Check()` und `ctest`.

**Spec:** `docs/superpowers/specs/2026-09-04-fallout4-einstellungsoberflaeche-design.md`

## Globale Randbedingungen

-   **C++23, nur MSVC.** Unser Ziel baut mit `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`. Die
    beiden Fremdziele behalten ihre eigenen Optionen und werden nicht angefasst. Emittiert ein
    fremder Header eine neue Warnung, wird sie eng auf unserem Ziel unterdrückt, mit einem
    Kommentar, der den Header nennt — niemals durch Aufweichen von `/WX`.
-   **`<Windows.h>` und `<d3d11.h>` sind verboten.** Was `REX::W32` fehlt, wird in
    `src/Menu/Win32.h` deklariert. `user32.lib` ist bereits `PUBLIC` gelinkt.
-   **Einstellungspfade sind `"Block/Key"`** — genau ein Schrägstrich, keine Hälfte leer. Ein
    JSON-Pointer, kein Punkt-Pfad.
-   **Ganze Zahlen werden als `double` gespeichert.** `REX::TJsonSetting<T>` liest für keinen
    Ganzzahltyp jemals aus der Datei. Nur `bool`, `double` und `std::string` funktionieren.
-   **Conventional Commits.** Titel höchstens 50 Zeichen, Rumpf auf 72 umgebrochen. pre-commit
    lässt clang-format, prettier und gersemi laufen; nach einem Umformatieren erneut `git add`
    und der Commit-Befehl wird wiederholt.
-   **Vollständige Umsetzungen.** Kein `TODO`, kein Platzhaltercode.
-   **Kommentare erklären warum, nicht was.** Deutsch nur in `docs/`; Quelltext, Kommentare und
    Commit-Nachrichten bleiben Englisch.

**Bauen und Testen.** `cmake` liegt nicht auf dem PATH einer normalen Shell. In jeder
PowerShell-Sitzung einmal setzen:

```pwsh
$env:VCPKG_ROOT = "C:\vcpkg"
$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
```

Danach je Aufgabe:

```pwsh
& $cmake --build --preset FO4 --target <Ziel>
ctest --test-dir build/FO4 -C Release --output-on-failure -R <Testname>
```

Der Preset `FO4-Fast` (Ninja) **funktioniert aus einer normalen Shell nicht** — er sucht `cl.exe`
auf dem PATH. Nur `FO4` benutzen.

**Testdisziplin.** Jeder Test wird nach dem Grünwerden absichtlich gebrochen, um zu belegen, dass
er greift. Die erwarteten Fehlschläge stehen im jeweiligen Schritt. Wichtig: eine Mutation, die
sich wegen `/W4 /WX` gar nicht übersetzen lässt, beweist nichts — dann läuft die alte ausführbare
Datei. Immer prüfen, dass der Bau **erfolgreich** war und der Test **danach** fehlschlägt.

---

## Dateistruktur

**Neu:**

| Datei                                                    | Zuständigkeit                                                                                                       |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| `src/Settings/Settings.h`                                | die öffentliche Fläche: `Declare*`, `Handle`, `Entry`, `Get*`, `Set*`, `ForEach*`, `Init`, `Save`, `ConsumeChanged` |
| `src/Settings/Internal.h`                                | `Impl::Record` und die Tabelle, geteilt von `Schema.cpp` und `Store.cpp`. Kennt kein REX                            |
| `src/Settings/Schema.cpp`                                | `Declare*`, `Handle`, `ForEachBlock`, `ForEachEntry`. Ohne REX, ohne Datei                                          |
| `src/Settings/Store.cpp`                                 | REX-Bindung, `Init`, `Get*`, `Set*`, `Save`, Nachrüstung, `ConsumeChanged`                                          |
| `src/I18n/I18n.h`, `src/I18n/I18n.cpp`                   | `T()`, Sprachfund, Umschaltung, Rückfall                                                                            |
| `src/Menu/Theme.h`, `src/Menu/Theme.cpp`                 | der `ImGuiStyle` im Quelltext                                                                                       |
| `src/Menu/Fonts.h`, `src/Menu/Fonts.cpp`                 | das Laden der TTF, die Größe zur Laufzeit                                                                           |
| `src/Menu/SettingsPanel.h`, `src/Menu/SettingsPanel.cpp` | zeichnet das Schema                                                                                                 |
| `tests/SettingsSchemaTests.cpp`                          | Arten, Metadaten, Blockbildung, Einschalter, Pfadprüfung                                                            |
| `tests/SettingsStoreTests.cpp`                           | Laden, Nachrüsten, Schreiben, `ConsumeChanged`                                                                      |
| `tests/I18nTests.cpp`                                    | Sprachfund, Rückfälle, Umschaltung                                                                                  |
| `tools/extract-i18n.py`                                  | erzeugt `en.json` aus dem Quelltext                                                                                 |
| `package/F4SE/Plugins/CommunityShadersFO4/`              | Schriften und Übersetzungen, `Data`-relativ                                                                         |

**Gelöscht:** `src/Feature/FeatureSettings.h`, `src/Feature/FeatureSettings.cpp`,
`tests/FeatureSettingsTests.cpp`.

**Geändert:** `src/Feature/Feature.h`, `src/Feature/FeatureRegistry.{h,cpp}`,
`src/Feature/FeatureSystem.cpp`, `src/Features/FrameCounter.{h,cpp}`,
`src/Features/ImagespaceTint.{h,cpp}`, `src/Menu/MenuSystem.cpp`, `src/Menu/Overlay.{h,cpp}`,
`src/Menu/WindowHook.{h,cpp}`, `src/Menu/MenuGate.{h,cpp}`, `src/Menu/Win32.h`,
`src/Util/FileWatch.{h,cpp}`, `CMakeLists.txt`, `tools/package.ps1`,
`tools/verify-package.ps1`, `.claude/CLAUDE.md`, `docs/fallout4-port/ROADMAP.md`.

`CMakeLists.txt` sammelt die Quelldateien des Plugins mit `file(GLOB_RECURSE ... CONFIGURE_DEPENDS
"src/*.cpp" "src/*.h")` — neue Dateien unter `src/` brauchen **keinen** CMake-Eintrag. Nur neue
**Testziele** müssen von Hand eingetragen werden.

---

### Aufgabe 1: `Util::FileWatch::Rebase`

Der eigene Schreibvorgang darf nicht als fremde Änderung zurückkommen. `Reset` kann das schon,
verlangt aber den Dateisatz erneut; `Rebase` erneuert die Zeitstempel des unveränderten Satzes.

**Dateien:**

-   Ändern: `src/Util/FileWatch.h`, `src/Util/FileWatch.cpp`
-   Test: `tests/FileWatchTests.cpp`

**Schnittstellen:**

-   Nutzt: nichts.
-   Liefert: `void Util::FileWatch::Rebase()` — von Aufgabe 4 (`Settings::Save`) gebraucht.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

An das Ende von `tests/FileWatchTests.cpp`, direkt vor die abschließende Ausgabe von
`g_failures`, einfügen. Die vorhandenen Hilfsfunktionen des Tests (`Check`, das Schreiben von
Dateien) sind dort schon vorhanden; sollte eine fehlen, den vorhandenen Testkopf entsprechend
erweitern.

```cpp
	// Rebase hides a change that already happened, but nothing after it. This
	// is what keeps our own settings write from looking like someone else's.
	{
		const auto file = root / "rebase.json";
		WriteFile(file, "{}");

		Util::FileWatch watch;
		const std::filesystem::path watched[]{ file };
		watch.Reset(watched);

		const auto stamp = std::filesystem::last_write_time(file);
		std::filesystem::last_write_time(file, stamp + std::chrono::seconds{ 5 });

		watch.Rebase();
		Check(!watch.Poll(), "a change before Rebase is not reported");

		std::filesystem::last_write_time(file, stamp + std::chrono::seconds{ 10 });
		Check(watch.Poll(), "but a change after Rebase still is");
	}
```

-   [ ] **Schritt 2: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --build --preset FO4 --target FileWatchTests
```

Erwartet: **Übersetzungsfehler**, `C2039: 'Rebase': is not a member of 'Util::FileWatch'`.

-   [ ] **Schritt 3: Die Deklaration ergänzen**

In `src/Util/FileWatch.h`, hinter `Poll()`:

```cpp
		/// Takes the current timestamps as the new baseline without changing
		/// the watched set. Exists for the case where we are the writer: our
		/// own write must not come back as somebody else's change.
		void Rebase();
```

-   [ ] **Schritt 4: Die Umsetzung schreiben**

In `src/Util/FileWatch.cpp`, hinter `Reset`:

```cpp
	void FileWatch::Rebase()
	{
		for (auto& [path, stamp] : _entries) {
			stamp = TimestampOr(path, stamp);
		}
	}
```

-   [ ] **Schritt 5: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --build --preset FO4 --target FileWatchTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R FileWatch
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 6: Den Test absichtlich brechen**

`Rebase` zu einem leeren Rumpf machen (`{ }`), neu bauen, laufen lassen. Erwartet: **`FAIL  a
change before Rebase is not reported`**, `1 failure(s)`. Danach die Umsetzung wiederherstellen und
noch einmal grün laufen lassen.

-   [ ] **Schritt 7: Commit**

```bash
git add src/Util/FileWatch.h src/Util/FileWatch.cpp tests/FileWatchTests.cpp
git commit -m "feat: let a file watch rebase its own writes"
```

---

### Aufgabe 2: Das Schema

Die Beschreibung der Einstellungen, ohne REX und ohne Datei. Das ist es, was die Oberfläche liest.

**Dateien:**

-   Anlegen: `src/Settings/Settings.h`, `src/Settings/Internal.h`, `src/Settings/Schema.cpp`
-   Anlegen: `tests/SettingsSchemaTests.cpp`
-   Ändern: `CMakeLists.txt`

**Schnittstellen:**

-   Nutzt: nichts.
-   Liefert: den gesamten Inhalt von `src/Settings/Settings.h`, siehe unten. Aufgaben 3, 4, 6, 9
    und 11 bauen darauf.

-   [ ] **Schritt 1: Die öffentliche Fläche anlegen**

`src/Settings/Settings.h`:

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Settings
{
	/// What a setting is, not merely what type it has. The type alone is not
	/// enough: a virtual key code is a double like any other, and neither a
	/// slider nor a number box is the right thing to put in front of it.
	enum class Kind
	{
		kBool,
		kSlider,
		kChoice,
		kKey
	};

	/// A read-only view of one declared setting. Every string_view points into
	/// the schema's own table, which is node based and outlives the process.
	struct Entry
	{
		std::string_view path;
		std::string_view block;
		std::string_view key;
		Kind kind{ Kind::kBool };

		std::string_view labelKey;
		std::string_view labelText;
		std::string_view helpKey;
		std::string_view helpText;

		// kSlider only.
		double min{ 0.0 };
		double max{ 0.0 };

		// kChoice only.
		std::span<const std::string> choices;

		bool defaultBool{ false };
		double defaultNumber{ 0.0 };
		std::string_view defaultChoice;

		/// The feature's own on/off switch, declared through DeclareFeature.
		/// The panel draws it as the checkbox of the heading rather than as a
		/// line among the feature's settings.
		bool isFeatureSwitch{ false };
	};

	/// Returned by every Declare. A view of the record just declared, valid
	/// only for the length of the declaration expression.
	class Handle
	{
	public:
		explicit Handle(void* a_record) noexcept :
			_record(a_record)
		{}

		/// The English default plus its translation key. Both are written out
		/// rather than derived from the path: a derivation would have to be
		/// kept identical in C++ and in tools/extract-i18n.py, and a drift
		/// between the two loses a translation without breaking anything.
		Handle& Label(std::string_view a_key, std::string_view a_text) noexcept;
		Handle& Help(std::string_view a_key, std::string_view a_text) noexcept;

	private:
		// Impl::Record*, or null when the declaration was refused.
		void* _record;
	};

	/// Declares one setting under a two segment path, "Block/Key". The store
	/// addresses its values as JSON pointers with one object per segment, so
	/// anything else would address a top level key with a slash in its name.
	/// A malformed path is refused with a log line rather than accepted; the
	/// returned handle then does nothing.
	Handle DeclareBool(std::string_view a_path, bool a_default);
	Handle DeclareSlider(std::string_view a_path, double a_default, double a_min, double a_max);
	Handle DeclareChoice(
		std::string_view a_path,
		std::string_view a_default,
		std::vector<std::string> a_choices);

	/// A virtual key code. Stored as a double like every whole number, because
	/// REX cannot read an integer setting back from the file.
	Handle DeclareKey(std::string_view a_path, std::uint32_t a_default);

	/// Shorthand for DeclareBool("<a_name>/enabled", a_default), additionally
	/// marked as the block's feature switch.
	Handle DeclareFeature(std::string_view a_name, bool a_default);

	/// Blocks in declaration order, entries within a block in declaration
	/// order. Deterministic, so the written file does not reshuffle itself.
	void ForEachBlock(const std::function<void(std::string_view a_block)>& a_visit);
	void ForEachEntry(
		std::string_view a_block,
		const std::function<void(const Entry& a_entry)>& a_visit);

	/// False, zero, respectively empty, for a path that was never declared.
	[[nodiscard]] bool GetBool(std::string_view a_path) noexcept;
	[[nodiscard]] double GetDouble(std::string_view a_path) noexcept;
	[[nodiscard]] std::uint32_t GetUInt32(std::string_view a_path) noexcept;
	[[nodiscard]] std::string GetString(std::string_view a_path) noexcept;

	/// Writes into the value only. Nothing reaches the disk until Save, and
	/// every Set marks the settings as changed for ConsumeChanged.
	void SetBool(std::string_view a_path, bool a_value) noexcept;
	void SetDouble(std::string_view a_path, double a_value) noexcept;
	void SetUInt32(std::string_view a_path, std::uint32_t a_value) noexcept;
	void SetString(std::string_view a_path, std::string_view a_value) noexcept;

	/// Every declared setting back to its declared default. Marks changed.
	void RestoreDefaults() noexcept;

	/// Points the store at a_file and loads it. The overload without an
	/// argument resolves <Documents>/My Games/<save folder>/F4SE/<plugin>.json,
	/// the same directory F4SE puts the log in.
	///
	/// A file that is not there is written from the declared defaults. A file
	/// that is missing declared keys is extended by them, keeping the values
	/// and the unknown keys it already had.
	void Init(const std::filesystem::path& a_file);
	void Init();

	/// Writes every declared setting, then rebases the watch so that our own
	/// write does not come back as somebody else's change.
	void Save() noexcept;

	/// True when the file changed on disk or something was Set since the last
	/// call. Both are the same occasion to give a refused feature another try.
	[[nodiscard]] bool ConsumeChanged() noexcept;

	/// False for a name that was never declared.
	[[nodiscard]] bool IsEnabled(std::string_view a_name) noexcept;

	[[nodiscard]] const std::filesystem::path& File() noexcept;
}
```

-   [ ] **Schritt 2: Die geteilte Tabelle anlegen**

`src/Settings/Internal.h`. Beachte, dass hier **kein** REX-Header eingebunden wird — das ist der
Punkt der Teilung.

```cpp
#pragma once

#include "Settings/Settings.h"

#include <map>
#include <vector>

namespace Settings::Impl
{
	/// One declared setting. Owns every string the public Entry views into,
	/// which is why the table below is node based: a vector would move these
	/// on its next growth and invalidate every view, and REX additionally
	/// keeps the path as a string_view of its own.
	struct Record
	{
		std::string path;
		std::string block;
		std::string key;
		Kind kind{ Kind::kBool };

		std::string labelKey;
		std::string labelText;
		std::string helpKey;
		std::string helpText;

		double min{ 0.0 };
		double max{ 0.0 };
		std::vector<std::string> choices;

		bool defaultBool{ false };
		double defaultNumber{ 0.0 };
		std::string defaultChoice;

		bool isFeatureSwitch{ false };

		/// Declaration order, so that both the panel and the written file are
		/// deterministic without depending on how the paths happen to sort.
		std::size_t ordinal{ 0 };
	};

	/// Keyed by path. Node based on purpose, see Record.
	std::map<std::string, Record, std::less<>>& Records();

	/// Null for a path that was never declared.
	Record* Find(std::string_view a_path) noexcept;

	/// The public view of a record.
	Entry ViewOf(const Record& a_record) noexcept;
}
```

-   [ ] **Schritt 3: Den fehlschlagenden Test schreiben**

`tests/SettingsSchemaTests.cpp`:

```cpp
#include "Settings/Settings.h"

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
}

int main()
{
	Settings::DeclareFeature("Alpha", true)
		.Label("feature.alpha.name", "Alpha");
	Settings::DeclareBool("Alpha/verbose", false)
		.Label("setting.alpha.verbose", "Verbose")
		.Help("setting.alpha.verbose.help", "Writes more to the log.");
	Settings::DeclareSlider("Menu/fontSize", 18.0, 12.0, 32.0)
		.Label("setting.menu.font_size", "Font size");
	Settings::DeclareKey("Menu/toggleKey", 0x23)
		.Label("setting.menu.toggle_key", "Toggle key");
	Settings::DeclareChoice("Menu/language", "en", { "en", "de" })
		.Label("setting.menu.language", "Language");

	// Two segments are the contract; anything else is refused rather than
	// silently addressing a top level key with a slash in its name.
	Settings::DeclareBool("noSlash", true);
	Settings::DeclareBool("too/many/segments", true);
	Settings::DeclareBool("/leadingEmpty", true);
	Settings::DeclareBool("trailingEmpty/", true);

	// Blocks come back in declaration order, not sorted.
	{
		std::vector<std::string> blocks;
		Settings::ForEachBlock([&blocks](std::string_view a_block) {
			blocks.emplace_back(a_block);
		});

		Check(blocks.size() == 2, "two blocks were declared");
		Check(blocks.size() == 2 && blocks[0] == "Alpha", "Alpha came first");
		Check(blocks.size() == 2 && blocks[1] == "Menu", "Menu came second");
	}

	// A malformed path never becomes a block.
	{
		bool sawJunk = false;
		Settings::ForEachBlock([&sawJunk](std::string_view a_block) {
			if (a_block == "noSlash" || a_block == "too" || a_block.empty() ||
				a_block == "trailingEmpty") {
				sawJunk = true;
			}
		});
		Check(!sawJunk, "no malformed path became a block");
	}

	// The feature switch is marked, and it is the only one in its block.
	{
		int entries = 0;
		int switches = 0;
		Settings::ForEachEntry("Alpha", [&](const Settings::Entry& a_entry) {
			++entries;
			if (a_entry.isFeatureSwitch) {
				++switches;
				Check(a_entry.key == "enabled", "the switch is the enabled key");
				Check(a_entry.defaultBool, "and carries its declared default");
			}
		});
		Check(entries == 2, "Alpha has two entries");
		Check(switches == 1, "exactly one of them is the feature switch");
	}

	// Kinds and metadata survive the declaration.
	{
		Settings::ForEachEntry("Menu", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "fontSize") {
				Check(a_entry.kind == Settings::Kind::kSlider, "fontSize is a slider");
				Check(a_entry.min == 12.0 && a_entry.max == 32.0, "with its declared bounds");
				Check(a_entry.defaultNumber == 18.0, "and its declared default");
				Check(a_entry.labelText == "Font size", "and its English label");
				Check(a_entry.labelKey == "setting.menu.font_size", "and its translation key");
			} else if (a_entry.key == "toggleKey") {
				Check(a_entry.kind == Settings::Kind::kKey, "toggleKey is a key");
				Check(a_entry.defaultNumber == 35.0, "stored as a double");
			} else if (a_entry.key == "language") {
				Check(a_entry.kind == Settings::Kind::kChoice, "language is a choice");
				Check(a_entry.choices.size() == 2, "with two choices");
				Check(a_entry.defaultChoice == "en", "and its declared default");
			}
		});
	}

	// Help is optional and empty when it was never given.
	{
		Settings::ForEachEntry("Alpha", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "verbose") {
				Check(a_entry.helpText == "Writes more to the log.", "help is carried through");
			} else if (a_entry.key == "enabled") {
				Check(a_entry.helpText.empty(), "and is empty when never given");
			}
		});
	}

	// The first declaration of a path wins; a second is ignored rather than
	// overwriting a label somebody else already set.
	{
		Settings::DeclareBool("Alpha/verbose", true).Label("other.key", "Other");

		Settings::ForEachEntry("Alpha", [](const Settings::Entry& a_entry) {
			if (a_entry.key == "verbose") {
				Check(!a_entry.defaultBool, "a second declaration does not change the default");
				Check(a_entry.labelText == "Verbose", "nor the label");
			}
		});
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Schritt 4: Das Testziel eintragen**

In `CMakeLists.txt`, im Block `if(FO4CS_BUILD_TESTS)`, hinter dem letzten vorhandenen Testziel:

```cmake
    add_executable(
        SettingsSchemaTests
        "${CMAKE_SOURCE_DIR}/tests/SettingsSchemaTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Schema.cpp"
    )

    target_include_directories(
        SettingsSchemaTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(SettingsSchemaTests PRIVATE cxx_std_23)
    target_precompile_headers(
        SettingsSchemaTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(SettingsSchemaTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            SettingsSchemaTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME SettingsSchema COMMAND SettingsSchemaTests)
```

Nur `Schema.cpp` wird gelinkt, nicht `Store.cpp`. Der Linker verlangt nur, was der Test auch
ruft — `Get*` und `Init` kommen darin nicht vor.

-   [ ] **Schritt 5: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --preset FO4
& $cmake --build --preset FO4 --target SettingsSchemaTests
```

Erwartet: **Linkerfehler**, `LNK2019: unresolved external symbol` für `Settings::DeclareBool` und
Geschwister — `Schema.cpp` gibt es noch nicht. Der erneute `--preset FO4`-Lauf ist nötig, weil
`CMakeLists.txt` geändert wurde.

-   [ ] **Schritt 6: Das Schema umsetzen**

`src/Settings/Schema.cpp`:

```cpp
#include "Settings/Internal.h"

#include <utility>

namespace Settings
{
	namespace
	{
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

		// Returns null when the path was refused, which is what makes the
		// handle a no-op rather than a crash.
		Impl::Record* Declare(std::string_view a_path, Kind a_kind)
		{
			std::string_view block;
			std::string_view key;
			if (!SplitPath(a_path, block, key)) {
				REX::ERROR("setting path {} is not <Block>/<Key>, ignored", a_path);
				return nullptr;
			}

			auto& records = Impl::Records();
			const auto [it, inserted] = records.try_emplace(std::string{ a_path });
			if (!inserted) {
				return nullptr;  // Declared twice; the first declaration wins.
			}

			auto& record = it->second;
			record.path = it->first;
			record.block = std::string{ block };
			record.key = std::string{ key };
			record.kind = a_kind;
			record.ordinal = records.size() - 1;
			return std::addressof(record);
		}
	}

	Handle& Handle::Label(std::string_view a_key, std::string_view a_text) noexcept
	{
		if (auto* const record = static_cast<Impl::Record*>(_record)) {
			record->labelKey = std::string{ a_key };
			record->labelText = std::string{ a_text };
		}
		return *this;
	}

	Handle& Handle::Help(std::string_view a_key, std::string_view a_text) noexcept
	{
		if (auto* const record = static_cast<Impl::Record*>(_record)) {
			record->helpKey = std::string{ a_key };
			record->helpText = std::string{ a_text };
		}
		return *this;
	}

	Handle DeclareBool(std::string_view a_path, bool a_default)
	{
		auto* const record = Declare(a_path, Kind::kBool);
		if (record != nullptr) {
			record->defaultBool = a_default;
		}
		return Handle{ record };
	}

	Handle DeclareSlider(std::string_view a_path, double a_default, double a_min, double a_max)
	{
		auto* const record = Declare(a_path, Kind::kSlider);
		if (record != nullptr) {
			record->defaultNumber = a_default;
			record->min = a_min;
			record->max = a_max;
		}
		return Handle{ record };
	}

	Handle DeclareChoice(
		std::string_view a_path,
		std::string_view a_default,
		std::vector<std::string> a_choices)
	{
		auto* const record = Declare(a_path, Kind::kChoice);
		if (record != nullptr) {
			record->defaultChoice = std::string{ a_default };
			record->choices = std::move(a_choices);
		}
		return Handle{ record };
	}

	Handle DeclareKey(std::string_view a_path, std::uint32_t a_default)
	{
		auto* const record = Declare(a_path, Kind::kKey);
		if (record != nullptr) {
			record->defaultNumber = static_cast<double>(a_default);
		}
		return Handle{ record };
	}

	Handle DeclareFeature(std::string_view a_name, bool a_default)
	{
		auto handle = DeclareBool(std::format("{}/enabled", a_name), a_default);

		// Marked here rather than through the handle: it is a property of the
		// declaration, not something a caller should be able to set.
		if (auto* const record = Impl::Find(std::format("{}/enabled", a_name))) {
			record->isFeatureSwitch = true;
		}
		return handle;
	}

	void ForEachBlock(const std::function<void(std::string_view)>& a_visit)
	{
		// Declaration order, taken from the ordinal of each block's first
		// entry. The table itself is sorted by path, which would put a block
		// declared later ahead of one declared earlier.
		std::vector<std::pair<std::size_t, std::string_view>> blocks;
		for (const auto& [path, record] : Impl::Records()) {
			const auto known = std::find_if(
				blocks.begin(),
				blocks.end(),
				[&record](const auto& a_pair) { return a_pair.second == record.block; });
			if (known == blocks.end()) {
				blocks.emplace_back(record.ordinal, record.block);
			} else if (record.ordinal < known->first) {
				known->first = record.ordinal;
			}
		}

		std::sort(blocks.begin(), blocks.end());
		for (const auto& [ordinal, block] : blocks) {
			a_visit(block);
		}
	}

	void ForEachEntry(std::string_view a_block, const std::function<void(const Entry&)>& a_visit)
	{
		std::vector<const Impl::Record*> entries;
		for (const auto& [path, record] : Impl::Records()) {
			if (record.block == a_block) {
				entries.push_back(std::addressof(record));
			}
		}

		std::sort(entries.begin(), entries.end(), [](const auto* a_lhs, const auto* a_rhs) {
			return a_lhs->ordinal < a_rhs->ordinal;
		});

		for (const auto* const record : entries) {
			a_visit(Impl::ViewOf(*record));
		}
	}

	namespace Impl
	{
		std::map<std::string, Record, std::less<>>& Records()
		{
			static std::map<std::string, Record, std::less<>> records;
			return records;
		}

		Record* Find(std::string_view a_path) noexcept
		{
			const auto it = Records().find(a_path);
			return it == Records().end() ? nullptr : std::addressof(it->second);
		}

		Entry ViewOf(const Record& a_record) noexcept
		{
			Entry entry;
			entry.path = a_record.path;
			entry.block = a_record.block;
			entry.key = a_record.key;
			entry.kind = a_record.kind;
			entry.labelKey = a_record.labelKey;
			entry.labelText = a_record.labelText;
			entry.helpKey = a_record.helpKey;
			entry.helpText = a_record.helpText;
			entry.min = a_record.min;
			entry.max = a_record.max;
			entry.choices = a_record.choices;
			entry.defaultBool = a_record.defaultBool;
			entry.defaultNumber = a_record.defaultNumber;
			entry.defaultChoice = a_record.defaultChoice;
			entry.isFeatureSwitch = a_record.isFeatureSwitch;
			return entry;
		}
	}
}
```

`<algorithm>`, `<format>` und `<memory>` kommen aus dem PCH; sollte der Übersetzer eines davon
vermissen, wird es in `Internal.h` ergänzt, nicht in der `.cpp`.

-   [ ] **Schritt 7: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --build --preset FO4 --target SettingsSchemaTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R SettingsSchema
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 8: Den Test absichtlich brechen**

In `SplitPath` die Prüfung auf den zweiten Schrägstrich entfernen (die drei Zeilen mit
`a_path.find('/', slash + 1)`). Neu bauen — das übersetzt sauber — und laufen lassen. Erwartet:
**`FAIL  no malformed path became a block`**, weil `too/many/segments` nun zum Block `too` wird.
Danach wiederherstellen und grün laufen lassen.

-   [ ] **Schritt 9: Commit**

```bash
git add src/Settings/Settings.h src/Settings/Internal.h src/Settings/Schema.cpp \
        tests/SettingsSchemaTests.cpp CMakeLists.txt
git commit -m "feat: describe settings instead of just naming them"
```

---

### Aufgabe 3: Der Speicher, Leseweg

REX-Bindung, Laden, Schreiben der ersten Fassung, Nachrüsten fehlender Schlüssel, `Get*`.

**Dateien:**

-   Anlegen: `src/Settings/Store.cpp`
-   Anlegen: `tests/SettingsStoreTests.cpp`
-   Ändern: `CMakeLists.txt`

**Schnittstellen:**

-   Nutzt: `Settings::Kind`, `Settings::Entry`, `Impl::Records`, `Impl::Find` (Aufgabe 2);
    `Util::FileWatch::Rebase` (Aufgabe 1).
-   Liefert: `Settings::Init`, `Settings::GetBool/GetDouble/GetUInt32/GetString`,
    `Settings::IsEnabled`, `Settings::File`. `Set*`, `Save`, `RestoreDefaults` und
    `ConsumeChanged` folgen in Aufgabe 4.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

`tests/SettingsStoreTests.cpp`:

```cpp
#include "Settings/Settings.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}

	std::string ReadFile(const std::filesystem::path& a_path)
	{
		std::ifstream stream{ a_path, std::ios::binary };
		std::ostringstream text;
		text << stream.rdbuf();
		return text.str();
	}

	// Whitespace-insensitive: the exact indentation is not what these tests
	// are about.
	bool Contains(const std::string& a_haystack, std::string_view a_needle)
	{
		std::string stripped;
		stripped.reserve(a_haystack.size());
		for (const char c : a_haystack) {
			if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
				stripped.push_back(c);
			}
		}
		return stripped.find(a_needle) != std::string::npos;
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-store-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	// Declared before Init: REX registers a setting with its store at
	// construction, and Init is what walks that registration.
	Settings::DeclareFeature("Alpha", false);
	Settings::DeclareFeature("Beta", true);
	Settings::DeclareSlider("Menu/fontSize", 18.0, 12.0, 32.0);
	Settings::DeclareKey("Menu/toggleKey", 0x23);
	Settings::DeclareChoice("Menu/language", "en", { "en", "de" });

	// A file that is not there is written from the declared defaults, because
	// REX saves through glz::set, which only overwrites keys that exist.
	const auto fresh = root / "fresh.json";
	Settings::Init(fresh);

	Check(std::filesystem::exists(fresh), "a missing file is written");

	{
		const auto text = ReadFile(fresh);
		Check(Contains(text, R"("Alpha":{"enabled":false})"), "with a feature block");
		Check(Contains(text, R"("toggleKey":35)"), "with the key code as a number");
		Check(Contains(text, R"("language":"en")"), "and the choice as a string");
	}

	Check(!Settings::IsEnabled("Alpha"), "the declared default is what got loaded");
	Check(Settings::IsEnabled("Beta"), "for both features");
	Check(Settings::GetUInt32("Menu/toggleKey") == 35, "and the key reads back");
	Check(Settings::GetDouble("Menu/fontSize") == 18.0, "and the slider");
	Check(Settings::GetString("Menu/language") == "en", "and the choice");

	// An existing file wins over the defaults.
	const auto existing = root / "existing.json";
	WriteFile(existing, R"({
		"Alpha": { "enabled": true },
		"Beta": { "enabled": false },
		"Menu": { "fontSize": 24, "toggleKey": 112, "language": "de" }
	})");
	Settings::Init(existing);

	Check(Settings::IsEnabled("Alpha"), "the file turns a default-off feature on");
	Check(!Settings::IsEnabled("Beta"), "and a default-on feature off");
	Check(Settings::GetUInt32("Menu/toggleKey") == 112, "the file overrides the key");
	Check(Settings::GetDouble("Menu/fontSize") == 24.0, "and the slider");
	Check(Settings::GetString("Menu/language") == "de", "and the choice");

	// The gap E1 recorded: an incomplete file is extended, and nothing that was
	// already in it is lost.
	const auto partial = root / "partial.json";
	WriteFile(partial, R"({
		"Alpha": { "enabled": true },
		"Menu": { "toggleKey": 112, "somebodyElsesKey": 7 }
	})");
	Settings::Init(partial);

	{
		const auto text = ReadFile(partial);
		Check(Contains(text, R"("enabled":true)"), "an existing value survives the extension");
		Check(Contains(text, R"("toggleKey":112)"), "and so does an existing number");
		Check(Contains(text, R"("somebodyElsesKey":7)"), "and an unknown key is left alone");
		Check(Contains(text, R"("fontSize":18)"), "a missing key appears with its default");
		Check(Contains(text, R"("language":"en")"), "including a missing choice");
		Check(Contains(text, R"("Beta":{"enabled":true})"), "and a whole missing block");
	}

	Check(Settings::GetUInt32("Menu/toggleKey") == 112, "the loaded value is the file's");
	Check(Settings::GetDouble("Menu/fontSize") == 18.0, "and the added one is the default");

	// A complete file is not rewritten. Its timestamp is the evidence.
	{
		const auto complete = root / "complete.json";
		Settings::Init(complete);
		const auto first = std::filesystem::last_write_time(complete);

		std::filesystem::last_write_time(complete, first - std::chrono::seconds{ 30 });
		const auto marked = std::filesystem::last_write_time(complete);

		Settings::Init(complete);
		Check(
			std::filesystem::last_write_time(complete) == marked,
			"a complete file is left untouched on the next start");
	}

	// A broken file must not wipe what is already loaded.
	Settings::Init(existing);
	const auto broken = root / "broken.json";
	WriteFile(broken, "{ this is not json");
	Settings::Init(broken);

	Check(Settings::IsEnabled("Alpha"), "a broken file leaves the values as they were");

	// Whole numbers are stored as doubles, so a hand edited file is not
	// confined to the range of a uint32.
	const auto odd = root / "odd.json";
	WriteFile(odd, R"({"Menu":{"toggleKey":-5}})");
	Settings::Init(odd);
	Check(Settings::GetUInt32("Menu/toggleKey") == 0, "a negative key clamps to zero");

	WriteFile(odd, R"({"Menu":{"toggleKey":1e30}})");
	Settings::Init(odd);
	Check(
		Settings::GetUInt32("Menu/toggleKey") == 4294967295u,
		"and one past the top clamps to the maximum");

	Check(!Settings::IsEnabled("Undeclared"), "an undeclared name reads as off");
	Check(Settings::GetDouble("no/such/path") == 0.0, "and an undeclared path as zero");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Schritt 2: Das Testziel eintragen**

In `CMakeLists.txt`, hinter dem Block aus Aufgabe 2:

```cmake
    add_executable(
        SettingsStoreTests
        "${CMAKE_SOURCE_DIR}/tests/SettingsStoreTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Schema.cpp"
        "${CMAKE_SOURCE_DIR}/src/Settings/Store.cpp"
        "${CMAKE_SOURCE_DIR}/src/Util/FileWatch.cpp"
    )

    target_include_directories(
        SettingsStoreTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(SettingsStoreTests PRIVATE cxx_std_23)
    target_precompile_headers(
        SettingsStoreTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(SettingsStoreTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            SettingsStoreTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME SettingsStore COMMAND SettingsStoreTests)
```

-   [ ] **Schritt 3: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --preset FO4
& $cmake --build --preset FO4 --target SettingsStoreTests
```

Erwartet: **Fehler**, `Cannot find source file: .../src/Settings/Store.cpp`.

-   [ ] **Schritt 4: Den Speicher umsetzen**

`src/Settings/Store.cpp`:

```cpp
#include "Settings/Internal.h"

#include "Util/FileWatch.h"

#include <REX/FJsonSettingStore.h>
#include <REX/TJsonSetting.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include <glaze/glaze.hpp>

#include <limits>

namespace Settings
{
	namespace
	{
		// One REX setting per record. Exactly one pointer is non-null, chosen
		// by the record's kind: only bool, double and std::string can read a
		// file at all, so a key code and a slider are both doubles.
		struct Bound
		{
			std::unique_ptr<REX::TJsonSetting<bool>> asBool;
			std::unique_ptr<REX::TJsonSetting<double>> asNumber;
			std::unique_ptr<REX::TJsonSetting<std::string>> asString;
		};

		std::map<std::string, Bound, std::less<>>& Bindings()
		{
			static std::map<std::string, Bound, std::less<>> bindings;
			return bindings;
		}

		Bound* FindBinding(std::string_view a_path) noexcept
		{
			const auto it = Bindings().find(a_path);
			return it == Bindings().end() ? nullptr : std::addressof(it->second);
		}

		// Process lifetime, because FSettingStore keeps the path it is given as
		// a string_view rather than copying it (FSettingStore.h).
		std::string& StoredPath()
		{
			static std::string path;
			return path;
		}

		std::filesystem::path& CurrentFile()
		{
			static std::filesystem::path file;
			return file;
		}

		Util::FileWatch& Watch()
		{
			static Util::FileWatch watch;
			return watch;
		}

		// REX reads through glz::get<T>, which matches the variant alternative
		// with std::same_as (glaze/core/seek.hpp:271), and glz::generic holds a
		// JSON number only ever as a double (glaze/json/generic.hpp:68). An
		// integer setting therefore never matches and value_or hands back the
		// default without a word. Hence: store double, clamp on the way out.
		std::uint32_t NarrowToUInt32(double a_value) noexcept
		{
			constexpr auto max = static_cast<double>(std::numeric_limits<std::uint32_t>::max());

			if (!(a_value >= 0.0)) {  // Also catches NaN, which fails every comparison.
				return 0;
			}
			if (a_value >= max) {
				return std::numeric_limits<std::uint32_t>::max();
			}
			return static_cast<std::uint32_t>(a_value);
		}

		// Same shape REX writes with, so a file we produce and a file REX
		// produces are indistinguishable.
		struct SaveOpts : glz::opts
		{
			static constexpr bool prettify = true;
			std::uint8_t indentation_width = 4;
		};

		// glz::generic uses ordered_small_map, so objects keep insertion order:
		// an existing file keeps the order it had and new keys land at the end
		// of their block. Nothing reshuffles on a write.
		glz::generic& ObjectAt(glz::generic& a_parent, const std::string& a_key)
		{
			auto& child = a_parent[a_key];
			if (!child.is_object()) {
				child = glz::generic::object_t{};
			}
			return child;
		}

		void PutValue(glz::generic& a_block, const Impl::Record& a_record)
		{
			auto* const bound = FindBinding(a_record.path);

			switch (a_record.kind) {
			case Kind::kBool:
				a_block[a_record.key] =
					bound && bound->asBool ? bound->asBool->GetValue() : a_record.defaultBool;
				break;
			case Kind::kSlider:
			case Kind::kKey:
				a_block[a_record.key] =
					bound && bound->asNumber ? bound->asNumber->GetValue() : a_record.defaultNumber;
				break;
			case Kind::kChoice:
				a_block[a_record.key] =
					bound && bound->asString ? bound->asString->GetValue() : a_record.defaultChoice;
				break;
			}
		}

		// Reads the file first and writes every declared key onto what came
		// back. Two things follow, and both are the point: unknown keys survive
		// because we build on the parsed tree, and missing keys appear because
		// glz::generic::operator[] inserts. glz::set could do neither - it
		// bails out at value.find(key) == end() (glaze/core/seek.hpp:238).
		bool WriteFile(const std::filesystem::path& a_file)
		{
			glz::generic root{};
			if (std::filesystem::exists(a_file)) {
				(void)glz::read_file_json(root, a_file.string(), std::string{});
			}
			if (!root.is_object()) {
				root = glz::generic::object_t{};
			}

			for (const auto& [path, record] : Impl::Records()) {
				PutValue(ObjectAt(root, record.block), record);
			}

			std::error_code ec;
			if (const auto parent = a_file.parent_path(); !parent.empty()) {
				std::filesystem::create_directories(parent, ec);
			}

			if (glz::write_file_json<SaveOpts{}>(root, a_file.string(), std::string{})) {
				REX::ERROR("could not write {}", a_file.generic_string());
				return false;
			}
			return true;
		}

		// True when at least one declared key is absent from the file. That is
		// the only occasion to rewrite it on startup: rewriting unconditionally
		// would churn the timestamp on every launch and risk losing the file to
		// a crash mid-write.
		bool IsIncomplete(const std::filesystem::path& a_file)
		{
			glz::generic root{};
			if (glz::read_file_json(root, a_file.string(), std::string{})) {
				return false;  // Unreadable: reported by Load, not repaired here.
			}
			if (!root.is_object()) {
				return true;
			}

			for (const auto& [path, record] : Impl::Records()) {
				if (!root.contains(record.block)) {
					return true;
				}

				const auto& block = root[record.block];
				if (!block.is_object() || !block.contains(record.key)) {
					return true;
				}
			}
			return false;
		}

		std::filesystem::path ResolveDefaultFile()
		{
			wchar_t* raw = nullptr;
			const auto result = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT,
				nullptr,
				std::addressof(raw));

			const std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(
				raw,
				REX::W32::CoTaskMemFree);

			if (!owned || result != 0) {
				REX::ERROR("could not resolve the documents folder, settings stay at defaults");
				return {};
			}

			// Deliberately the same directory F4SE puts the log in: user
			// writable, and outside the Data tree that Vortex manages.
			std::filesystem::path path = owned.get();
			path /= std::format(
				"My Games/{}/F4SE/{}.json",
				F4SE::GetSaveFolderName(),
				F4SE::GetPluginName());
			return path;
		}

		// One REX setting per record, built from the declared default. Rebuilt
		// on every Init so that a second Init against another file starts from
		// the declared defaults rather than from what the first file held.
		void Bind()
		{
			Bindings().clear();

			for (auto& [path, record] : Impl::Records()) {
				auto& bound = Bindings()[path];

				switch (record.kind) {
				case Kind::kBool:
					bound.asBool = std::make_unique<REX::TJsonSetting<bool>>(
						record.path,
						record.defaultBool);
					break;
				case Kind::kSlider:
				case Kind::kKey:
					bound.asNumber = std::make_unique<REX::TJsonSetting<double>>(
						record.path,
						record.defaultNumber);
					break;
				case Kind::kChoice:
					bound.asString = std::make_unique<REX::TJsonSetting<std::string>>(
						record.path,
						record.defaultChoice);
					break;
				}
			}
		}
	}

	bool GetBool(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asBool ? bound->asBool->GetValue() : false;
	}

	double GetDouble(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asNumber ? bound->asNumber->GetValue() : 0.0;
	}

	std::uint32_t GetUInt32(std::string_view a_path) noexcept
	{
		return NarrowToUInt32(GetDouble(a_path));
	}

	std::string GetString(std::string_view a_path) noexcept
	{
		const auto* const bound = FindBinding(a_path);
		return bound && bound->asString ? bound->asString->GetValue() : std::string{};
	}

	bool IsEnabled(std::string_view a_name) noexcept
	{
		return GetBool(std::format("{}/enabled", a_name));
	}

	void Init(const std::filesystem::path& a_file)
	{
		CurrentFile() = a_file;

		// REX's store outlives a rebind, so its registration list has to be
		// emptied or a second Init would leave the first set of settings in it,
		// pointing at freed records.
		auto* const store = REX::FJsonSettingStore::GetSingleton();
		store->GetSettings().clear();

		Bind();

		if (!std::filesystem::exists(a_file)) {
			(void)WriteFile(a_file);
		} else if (IsIncomplete(a_file)) {
			REX::INFO("{} is missing declared keys, extending it", a_file.generic_string());
			(void)WriteFile(a_file);
		}

		StoredPath() = a_file.string();

		// fileUser stays empty: Save() writes to fileBase, so pointing fileBase
		// anywhere but the user's own file would overwrite shipped defaults.
		store->Init(StoredPath().c_str(), "");
		store->Load();

		const std::filesystem::path watched[]{ a_file };
		Watch().Reset(watched);

		REX::INFO("settings loaded from {}", a_file.generic_string());
	}

	void Init()
	{
		const auto file = ResolveDefaultFile();
		if (file.empty()) {
			return;
		}
		Init(file);
	}

	const std::filesystem::path& File() noexcept
	{
		return CurrentFile();
	}
}
```

**Achtung, eine Unbekannte:** `REX::FSettingStore` hält seine Registrierung in einem
`protected std::vector<ISetting*> m_settings` und bietet dafür **keinen** öffentlichen Zugriff.
`store->GetSettings()` gibt es womöglich nicht. Prüfe zuerst
`extern/CommonLibF4/lib/commonlib-shared/include/REX/FSettingStore.h` und `ISettingStore.h`. Gibt
es keinen Weg, die Liste zu leeren, dann `Bind()` **nicht** neu binden, sondern die Bindungen
beim ersten Aufruf anlegen und bei weiteren nur die Werte auf die deklarierten Vorgaben
zurücksetzen (`SetValue(default)`) — REX registriert nur im Konstruktor, ein einmal registriertes
Setting bleibt also gültig. Das ist der einzige Punkt dieser Aufgabe, der im Quelltext
nachzusehen ist, bevor er geschrieben wird.

-   [ ] **Schritt 5: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --preset FO4
& $cmake --build --preset FO4 --target SettingsStoreTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R SettingsStore
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 6: Den Test absichtlich brechen**

In `Init` den Zweig `else if (IsIncomplete(a_file))` samt Rumpf entfernen. Neu bauen, laufen
lassen. Erwartet: **`FAIL  a missing key appears with its default`**, **`FAIL  including a missing
choice`** und **`FAIL  and a whole missing block`**, `3 failure(s)`. Danach wiederherstellen.

-   [ ] **Schritt 7: Commit**

```bash
git add src/Settings/Store.cpp tests/SettingsStoreTests.cpp CMakeLists.txt
git commit -m "feat: extend a settings file instead of ignoring it"
```

---

### Aufgabe 4: Der Speicher, Schreibweg

`Set*`, `Save`, `RestoreDefaults`, `ConsumeChanged`.

**Dateien:**

-   Ändern: `src/Settings/Store.cpp`
-   Ändern: `tests/SettingsStoreTests.cpp`

**Schnittstellen:**

-   Nutzt: alles aus Aufgabe 3.
-   Liefert: `Settings::SetBool/SetDouble/SetUInt32/SetString`, `Settings::Save`,
    `Settings::RestoreDefaults`, `Settings::ConsumeChanged`. Aufgaben 6, 9 und 11 brauchen sie.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

In `tests/SettingsStoreTests.cpp` vor der abschließenden `remove_all`-Zeile einfügen:

```cpp
	// The round trip the acceptance criterion asks for: change it, write it,
	// read it back from a fresh load.
	{
		const auto trip = root / "trip.json";
		Settings::Init(trip);

		Settings::SetBool("Alpha/enabled", true);
		Settings::SetDouble("Menu/fontSize", 24.0);
		Settings::SetUInt32("Menu/toggleKey", 112);
		Settings::SetString("Menu/language", "de");

		Check(Settings::IsEnabled("Alpha"), "a Set is visible to Get immediately");
		Check(Settings::GetUInt32("Menu/toggleKey") == 112, "for a key too");

		Settings::Save();
		Settings::Init(trip);

		Check(Settings::IsEnabled("Alpha"), "and survives a reload");
		Check(Settings::GetDouble("Menu/fontSize") == 24.0, "including the slider");
		Check(Settings::GetUInt32("Menu/toggleKey") == 112, "and the key");
		Check(Settings::GetString("Menu/language") == "de", "and the choice");
	}

	// Our own write must not come back as somebody else's change, or every
	// click in the overlay would tear down and set up every feature.
	{
		const auto quiet = root / "quiet.json";
		Settings::Init(quiet);
		Check(!Settings::ConsumeChanged(), "nothing changed right after Init");

		Settings::SetBool("Alpha/enabled", true);
		Check(Settings::ConsumeChanged(), "a Set is a change");
		Check(!Settings::ConsumeChanged(), "and is reported exactly once");

		Settings::Save();
		Check(!Settings::ConsumeChanged(), "our own write is not a change");

		const auto stamp = std::filesystem::last_write_time(quiet);
		std::filesystem::last_write_time(quiet, stamp + std::chrono::seconds{ 5 });
		Check(Settings::ConsumeChanged(), "but somebody else's write still is");
	}

	// Unknown keys survive a Save, not just an Init.
	{
		const auto foreign = root / "foreign.json";
		WriteFile(foreign, R"({"Menu":{"somebodyElsesKey":7}})");
		Settings::Init(foreign);

		Settings::SetDouble("Menu/fontSize", 20.0);
		Settings::Save();

		const auto text = ReadFile(foreign);
		Check(Contains(text, R"("somebodyElsesKey":7)"), "an unknown key survives a save");
		Check(Contains(text, R"("fontSize":20)"), "next to the value we wrote");
	}

	// The way back.
	{
		const auto reset = root / "reset.json";
		Settings::Init(reset);

		Settings::SetBool("Alpha/enabled", true);
		Settings::SetUInt32("Menu/toggleKey", 112);
		Settings::RestoreDefaults();

		Check(!Settings::IsEnabled("Alpha"), "restoring puts a bool back");
		Check(Settings::GetUInt32("Menu/toggleKey") == 35, "and a key");
		Check(Settings::GetString("Menu/language") == "en", "and a choice");
		Check(Settings::ConsumeChanged(), "and counts as a change");
	}
```

-   [ ] **Schritt 2: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --build --preset FO4 --target SettingsStoreTests
```

Erwartet: **Linkerfehler**, `LNK2019: unresolved external symbol` für `Settings::SetBool`,
`Settings::Save`, `Settings::RestoreDefaults` und `Settings::ConsumeChanged`.

-   [ ] **Schritt 3: Den Schreibweg umsetzen**

In `src/Settings/Store.cpp`, im anonymen Namensraum neben `Watch()`:

```cpp
		// Set on every Set and every RestoreDefaults. Read and cleared by
		// ConsumeChanged, which is also what asks the watch.
		bool& Dirty()
		{
			static bool dirty = false;
			return dirty;
		}
```

Und die öffentlichen Funktionen, hinter `IsEnabled`:

```cpp
	void SetBool(std::string_view a_path, bool a_value) noexcept
	{
		auto* const bound = FindBinding(a_path);
		if (bound == nullptr || bound->asBool == nullptr) {
			return;
		}
		bound->asBool->SetValue(a_value);
		Dirty() = true;
	}

	void SetDouble(std::string_view a_path, double a_value) noexcept
	{
		auto* const bound = FindBinding(a_path);
		if (bound == nullptr || bound->asNumber == nullptr) {
			return;
		}
		bound->asNumber->SetValue(a_value);
		Dirty() = true;
	}

	void SetUInt32(std::string_view a_path, std::uint32_t a_value) noexcept
	{
		SetDouble(a_path, static_cast<double>(a_value));
	}

	void SetString(std::string_view a_path, std::string_view a_value) noexcept
	{
		auto* const bound = FindBinding(a_path);
		if (bound == nullptr || bound->asString == nullptr) {
			return;
		}
		bound->asString->SetValue(std::string{ a_value });
		Dirty() = true;
	}

	void RestoreDefaults() noexcept
	{
		for (const auto& [path, record] : Impl::Records()) {
			switch (record.kind) {
			case Kind::kBool:
				SetBool(path, record.defaultBool);
				break;
			case Kind::kSlider:
			case Kind::kKey:
				SetDouble(path, record.defaultNumber);
				break;
			case Kind::kChoice:
				SetString(path, record.defaultChoice);
				break;
			}
		}
	}

	void Save() noexcept
	{
		const auto& file = CurrentFile();
		if (file.empty()) {
			return;
		}

		if (!WriteFile(file)) {
			// One line, not one per widget: a settings file that cannot be
			// written cannot be written on the next click either, and a slider
			// would otherwise fill the log by itself.
			static bool reported = false;
			if (!reported) {
				REX::ERROR(
					"{} could not be written; changes will not survive a restart",
					file.generic_string());
				reported = true;
			}
			return;
		}

		// The write we just did is ours. Without this the next poll would read
		// it as somebody else's change and set up every feature again.
		Watch().Rebase();
	}

	bool ConsumeChanged() noexcept
	{
		const bool fromFile = Watch().Poll();
		if (fromFile) {
			REX::INFO("settings changed, reloading");
			REX::FJsonSettingStore::GetSingleton()->Load();
		}

		const bool fromUs = std::exchange(Dirty(), false);
		return fromFile || fromUs;
	}
```

-   [ ] **Schritt 4: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --build --preset FO4 --target SettingsStoreTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R SettingsStore
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 5: Den Test absichtlich brechen**

In `Save()` den Aufruf `Watch().Rebase();` entfernen. Neu bauen, laufen lassen. Erwartet:
**`FAIL  our own write is not a change`**, `1 failure(s)`. Danach wiederherstellen.

-   [ ] **Schritt 6: Commit**

```bash
git add src/Settings/Store.cpp tests/SettingsStoreTests.cpp
git commit -m "feat: let settings be written back"
```

---

### Aufgabe 5: `Feature/FeatureSettings` außer Dienst stellen

Ein Umzug ohne Verhaltensänderung. Er steht hier und nicht früher, damit der neue Speicher schon
vollständig ist, wenn der alte verschwindet.

**Dateien:**

-   Löschen: `src/Feature/FeatureSettings.h`, `src/Feature/FeatureSettings.cpp`,
    `tests/FeatureSettingsTests.cpp`
-   Ändern: `src/Feature/FeatureSystem.cpp`, `src/Menu/MenuSystem.cpp`, `CMakeLists.txt`

**Schnittstellen:**

-   Nutzt: alles aus den Aufgaben 2 bis 4.
-   Liefert: einen Baum, in dem `Features::Settings` nicht mehr vorkommt.

-   [ ] **Schritt 1: Die Aufrufer finden**

```bash
grep -rn "FeatureSettings\|Features::Settings" src tests CMakeLists.txt
```

Erwartet: `src/Feature/FeatureSystem.cpp`, `src/Menu/MenuSystem.cpp`,
`tests/FeatureSettingsTests.cpp` und drei Stellen in `CMakeLists.txt`.

-   [ ] **Schritt 2: `FeatureSystem.cpp` umstellen**

`#include "Feature/FeatureSettings.h"` wird zu `#include "Settings/Settings.h"`. In
`StartSystem` und `TickSystem` `Features::Settings::` durch `Settings::` ersetzen und
`ReloadIfChanged()` durch `ConsumeChanged()`. Die beiden `DeclareFeature`-Zeilen bleiben vorerst
stehen; Aufgabe 6 verschiebt sie in die Features.

-   [ ] **Schritt 3: `MenuSystem.cpp` umstellen**

`#include "Feature/FeatureSettings.h"` wird zu `#include "Settings/Settings.h"`. In
`StartSystem` wird aus

```cpp
		Features::Settings::DeclareUInt32(kToggleKeyPath, kDefaultToggleKey);
```

die neue Art:

```cpp
		Settings::DeclareKey(kToggleKeyPath, kDefaultToggleKey)
			.Label("setting.menu.toggle_key", "Toggle key")
			.Help("setting.menu.toggle_key.help", "Opens and closes this overlay.");
```

In `TickSystem` wird `Features::Settings::GetUInt32(kToggleKeyPath)` zu
`Settings::GetUInt32(kToggleKeyPath)`.

-   [ ] **Schritt 4: Die alten Dateien und ihr Testziel entfernen**

```bash
git rm src/Feature/FeatureSettings.h src/Feature/FeatureSettings.cpp tests/FeatureSettingsTests.cpp
```

In `CMakeLists.txt` den gesamten `FeatureSettingsTests`-Block entfernen — von `add_executable(`
bis einschließlich `add_test(NAME FeatureSettings COMMAND FeatureSettingsTests)`.

-   [ ] **Schritt 5: Alles bauen und alle Tests laufen lassen**

```pwsh
& $cmake --preset FO4
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: der Bau geht durch, und alle Tests sind grün. `FeatureSettings` taucht in der Testliste
nicht mehr auf, `SettingsSchema` und `SettingsStore` dafür schon.

-   [ ] **Schritt 6: Nachsehen, dass nichts übrig ist**

```bash
grep -rn "FeatureSettings\|Features::Settings" src tests CMakeLists.txt
```

Erwartet: **keine Ausgabe**.

-   [ ] **Schritt 7: Commit**

```bash
git add -A
git commit -m "refactor: give settings a system of their own"
```

---

### Aufgabe 6: `Feature::Declare` und `Registry::ForEach`

**Dateien:**

-   Ändern: `src/Feature/Feature.h`, `src/Feature/FeatureRegistry.h`,
    `src/Feature/FeatureRegistry.cpp`, `src/Feature/FeatureSystem.cpp`,
    `src/Features/FrameCounter.h`, `src/Features/FrameCounter.cpp`,
    `src/Features/ImagespaceTint.h`, `src/Features/ImagespaceTint.cpp`
-   Test: `tests/FeatureRegistryTests.cpp`

**Schnittstellen:**

-   Nutzt: `Settings::DeclareFeature` (Aufgabe 2).
-   Liefert: `virtual void Features::Feature::Declare()` und
    `void Features::Registry::ForEach(const std::function<void(std::string_view, State)>&) const noexcept`.
    Aufgabe 11 braucht `ForEach`.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

An das Ende von `tests/FeatureRegistryTests.cpp`, vor die abschließende Ausgabe. Die vorhandene
Test-Feature-Klasse der Datei wird um einen Zähler erweitert; falls sie `Declare` noch nicht
kennt, dort ergänzen:

```cpp
	// Declare runs once, when the feature is registered, and before anything
	// asks whether it should be running.
	{
		Features::Registry registry;

		auto feature = std::make_unique<CountingFeature>("solo");
		auto* const raw = feature.get();
		registry.Register(std::move(feature));

		Check(raw->declared == 1, "Register declares the feature exactly once");
		Check(raw->setups == 0, "and does not set it up");

		registry.Tick([](std::string_view) { return true; });
		Check(raw->declared == 1, "and a tick does not declare it again");
	}

	// ForEach hands out names and states, in registration order.
	{
		Features::Registry registry;
		registry.Register(std::make_unique<CountingFeature>("first"));
		registry.Register(std::make_unique<CountingFeature>("second"));

		registry.Tick([](std::string_view a_name) { return a_name == "first"; });

		std::vector<std::string> names;
		std::vector<Features::State> states;
		registry.ForEach([&](std::string_view a_name, Features::State a_state) {
			names.emplace_back(a_name);
			states.push_back(a_state);
		});

		Check(names.size() == 2, "ForEach visits every feature");
		Check(names.size() == 2 && names[0] == "first", "in registration order");
		Check(names.size() == 2 && names[1] == "second", "both of them");
		Check(states.size() == 2 && states[0] == Features::State::kRunning, "the running one");
		Check(states.size() == 2 && states[1] == Features::State::kOff, "and the off one");
	}
```

`CountingFeature` ist die Testklasse der Datei; sie bekommt `int declared = 0;` und

```cpp
		void Declare() override { ++declared; }
```

-   [ ] **Schritt 2: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --build --preset FO4 --target FeatureRegistryTests
```

Erwartet: **Übersetzungsfehler**, `C3668: 'CountingFeature::Declare': method with override
specifier 'override' did not override any base class methods`, und `C2039: 'ForEach': is not a
member of 'Features::Registry'`.

-   [ ] **Schritt 3: Die Basisklasse erweitern**

In `src/Feature/Feature.h`, zwischen `Name()` und `Setup()`:

```cpp
		/// Declares the feature's settings, including its own enable switch.
		/// Called by the registry when the feature is registered, which is
		/// before Settings::Init - a REX setting registers with its store at
		/// construction, and Init is what walks that registration.
		///
		/// Declaring is not setting up: this runs whether or not the feature
		/// will ever be enabled, and must touch nothing but Settings.
		virtual void Declare() {}
```

-   [ ] **Schritt 4: Die Registry erweitern**

In `src/Feature/FeatureRegistry.h`, hinter `ClearRefusals`:

```cpp
		/// Every feature, in registration order, with the state it is in.
		/// Name and state only: the menu needs no more, and the registry gives
		/// away nothing it owns.
		void ForEach(
			const std::function<void(std::string_view a_name, State a_state)>& a_visit)
			const noexcept;
```

In `src/Feature/FeatureRegistry.cpp`, in `Register` vor dem `emplace_back`:

```cpp
		// Declared before it goes into the table: a feature that throws out of
		// Declare has no business being registered, and the guard reports it
		// the same way as every other call into a feature.
		static_cast<void>(
			Guarded(a_feature->Name(), "Declare", [&] { a_feature->Declare(); }));
```

Und hinter `Count`:

```cpp
	void Registry::ForEach(
		const std::function<void(std::string_view, State)>& a_visit) const noexcept
	{
		for (const auto& entry : _entries) {
			a_visit(entry.feature->Name(), entry.state);
		}
	}
```

-   [ ] **Schritt 5: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --build --preset FO4 --target FeatureRegistryTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R FeatureRegistry
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 6: Die Deklarationen in die Features verschieben**

In `src/Features/FrameCounter.h`, hinter `Name()`:

```cpp
		void Declare() override;
```

In `src/Features/FrameCounter.cpp`:

```cpp
	void FrameCounter::Declare()
	{
		Settings::DeclareFeature("FrameCounter", false)
			.Label("feature.frame_counter.name", "Frame Counter")
			.Help(
				"feature.frame_counter.help",
				"Counts rendered frames and reports the count to the log.");
	}
```

Dazu `#include "Settings/Settings.h"`.

Dasselbe für `src/Features/ImagespaceTint.{h,cpp}`:

```cpp
	void ImagespaceTint::Declare()
	{
		Settings::DeclareFeature("ImagespaceTint", true)
			.Label("feature.imagespace_tint.name", "Imagespace Tint")
			.Help(
				"feature.imagespace_tint.help",
				"Replaces an imagespace pixel shader with one of our own.");
	}
```

In `src/Feature/FeatureSystem.cpp` fallen die beiden Zeilen

```cpp
		Settings::DeclareFeature("FrameCounter", false);
		Settings::DeclareFeature("ImagespaceTint", true);
```

ersatzlos weg, samt des Kommentars darüber, der auf sie zeigt. `RegisterAll()` erledigt das jetzt.
Der Kommentar in `XSEPlugin.cpp` über die Reihenfolge bleibt richtig und wird nicht angefasst.

-   [ ] **Schritt 7: Alles bauen und alle Tests laufen lassen**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: alles grün.

-   [ ] **Schritt 8: Den Test absichtlich brechen**

In `Registry::Register` den `Declare`-Aufruf entfernen. Neu bauen, laufen lassen. Erwartet:
**`FAIL  Register declares the feature exactly once`**, `1 failure(s)`. Danach wiederherstellen.

-   [ ] **Schritt 9: Commit**

```bash
git add -A
git commit -m "feat: let a feature declare its own settings"
```

---

### Aufgabe 7: Der i18n-Motor

**Dateien:**

-   Anlegen: `src/I18n/I18n.h`, `src/I18n/I18n.cpp`, `tests/I18nTests.cpp`
-   Ändern: `CMakeLists.txt`

**Schnittstellen:**

-   Nutzt: nichts. Bewusst unabhängig von `Settings`, damit der Test keine Einstellungsdatei
    braucht.
-   Liefert: `const char* T(std::string_view a_key, const char* a_default)`,
    `I18n::Init(dir)`, `I18n::SetLocale`, `I18n::AvailableLocales`, `I18n::CurrentLocale`.
    Aufgaben 11 und 12 bauen darauf.

Die Vorlage steht unter `git show skyrim-base:src/I18n/I18n.cpp` (413 Zeilen). Übernommen werden
`Init`, `Get`, `SetLocale`, `AvailableLocales`, `DiscoverLocales` und `LoadLocaleInto`.
**Nicht** übernommen: `Format` und `SubstitutePlaceholders` (kein Formatierer, siehe Spec) und
`DetectSystemLocale` (rund 70 Zeilen Windows-`LANGID`-Abbildung; die Sprache ist bei uns eine
Einstellung). `nlohmann/json` wird durch glaze ersetzt.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

`tests/I18nTests.cpp`:

```cpp
#include "I18n/I18n.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

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

	bool Same(const char* a_lhs, const char* a_rhs)
	{
		return a_lhs != nullptr && a_rhs != nullptr && std::string_view{ a_lhs } == a_rhs;
	}

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-i18n-tests";
	std::filesystem::remove_all(root);

	// No directory at all: English, and nothing falls over.
	{
		I18n::GetSingleton()->Init(root / "missing");
		Check(I18n::GetSingleton()->CurrentLocale() == "en", "a missing directory means English");
		Check(Same(T("a.key", "Fallback"), "Fallback"), "and the inline default is what shows");
	}

	const auto dir = root / "Translations";
	WriteFile(dir / "en.json", R"({
		"_meta": { "language": "English", "locale": "en" },
		"menu.close": "Close",
		"menu.only_in_english": "English only"
	})");
	WriteFile(dir / "de.json", R"({
		"_meta": { "language": "Deutsch", "locale": "de" },
		"menu.close": "Schließen"
	})");
	WriteFile(dir / "broken.json", "{ not json at all");

	I18n::GetSingleton()->Init(dir);

	// Discovery, and the broken file does not take the others with it.
	{
		const auto locales = I18n::GetSingleton()->AvailableLocales();
		bool hasEn = false;
		bool hasDe = false;
		bool hasBroken = false;
		for (const auto& [code, name] : locales) {
			if (code == "en") {
				hasEn = true;
				Check(name == "English", "the display name comes from _meta");
			}
			if (code == "de") {
				hasDe = true;
			}
			if (code == "broken") {
				hasBroken = true;
			}
		}
		Check(hasEn && hasDe, "both good locales are discovered");
		Check(!hasBroken, "and a malformed file is not offered");
	}

	Check(Same(T("menu.close", "Close"), "Close"), "English reads from en.json");

	I18n::GetSingleton()->SetLocale("de");
	Check(I18n::GetSingleton()->CurrentLocale() == "de", "the locale switches");
	Check(Same(T("menu.close", "Close"), "Schließen"), "and the text switches with it");
	Check(
		Same(T("menu.only_in_english", "Ignored"), "English only"),
		"a key missing in German falls back to English, not to the inline default");
	Check(
		Same(T("menu.nowhere", "Inline"), "Inline"),
		"a key missing everywhere falls back to the inline default");
	Check(Same(T("menu.nothing", nullptr), "menu.nothing"), "and to the key as a last resort");

	// Switching to a locale that does not exist leaves the current one alone.
	I18n::GetSingleton()->SetLocale("xx");
	Check(I18n::GetSingleton()->CurrentLocale() == "de", "an unknown locale is refused");

	// The pointer from a previous Get stays valid until the locale changes,
	// which is what lets the panel hand it straight to ImGui.
	{
		const char* first = T("menu.close", "Close");
		const char* second = T("menu.close", "Close");
		Check(first == second, "the same key returns the same pointer");
	}

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Schritt 2: Den Kopf anlegen**

`src/I18n/I18n.h`:

```cpp
#pragma once

#include <deque>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

/// Flat-JSON translations, one file per locale, loaded from a directory.
///
/// Every string handed out is owned here and stays valid until the next
/// SetLocale, which is what makes it safe to pass straight to ImGui.
class I18n
{
public:
	static I18n* GetSingleton()
	{
		static I18n singleton;
		return std::addressof(singleton);
	}

	/// Discovers the locales in a_directory and loads English as the fallback.
	/// A directory that is not there leaves English with an empty table, which
	/// still works: every lookup then yields its inline default.
	void Init(const std::filesystem::path& a_directory);

	/// Lookup order: current locale, then English, then a_default, then the
	/// key itself. a_default may be null.
	[[nodiscard]] const char* Get(std::string_view a_key, const char* a_default) const;

	[[nodiscard]] std::string CurrentLocale() const;

	/// Ignored, with a log line, for a locale that was never discovered.
	void SetLocale(std::string_view a_locale);

	/// Code and display name, English first and the rest by display name.
	[[nodiscard]] std::vector<std::pair<std::string, std::string>> AvailableLocales() const;

private:
	I18n() = default;

	void DiscoverLocales(const std::filesystem::path& a_directory);
	bool LoadLocaleInto(std::string_view a_locale, std::unordered_map<std::string, std::string>& a_target) const;

	std::filesystem::path _directory;
	std::string _current{ "en" };

	std::unordered_map<std::string, std::string> _strings;   // Current locale.
	std::unordered_map<std::string, std::string> _fallback;  // Always en.json.

	/// Inline defaults handed out as const char*. A deque because it never
	/// invalidates a pointer on push_back, which a vector would.
	mutable std::deque<std::string> _inlineCache;
	mutable std::unordered_map<std::string, const char*> _inlineIndex;

	mutable std::shared_mutex _mutex;

	std::vector<std::pair<std::string, std::string>> _available;
};

/// The call every user-visible string goes through. The second argument is
/// both the fallback and the source tools/extract-i18n.py reads to generate
/// en.json, which is why it must always be a string literal.
[[nodiscard]] inline const char* T(std::string_view a_key, const char* a_default)
{
	return I18n::GetSingleton()->Get(a_key, a_default);
}
```

-   [ ] **Schritt 3: Den Motor umsetzen**

`src/I18n/I18n.cpp`. Die Vorlage aus `skyrim-base` liefert Aufbau und Sperrstrategie; die
JSON-Hälfte wird auf glaze umgeschrieben. Eine Übersetzungsdatei ist flach — jeder Wert eine
Zeichenkette, außer dem Block `_meta`, der übersprungen wird:

```cpp
	bool I18n::LoadLocaleInto(
		std::string_view a_locale,
		std::unordered_map<std::string, std::string>& a_target) const
	{
		const auto file = _directory / std::format("{}.json", a_locale);

		glz::generic root{};
		if (glz::read_file_json(root, file.string(), std::string{}) || !root.is_object()) {
			REX::ERROR("translation file {} could not be read, skipped", file.generic_string());
			return false;
		}

		for (const auto& [key, value] : root.get_object()) {
			// The metadata block is not a translation, and its nested object
			// would not be a string anyway.
			if (key == "_meta") {
				continue;
			}
			if (const auto* const text = value.get_if<std::string>()) {
				a_target.insert_or_assign(key, *text);
			}
		}
		return true;
	}
```

`Get` hält die Sperrstrategie der Vorlage: schneller Weg unter `shared_lock` über `_strings`,
dann `_fallback`, dann der schon zwischengespeicherte Inline-Wert; erst wenn der Inline-Wert neu
ist, wird auf eine `unique_lock` hochgestuft und in `_inlineCache` abgelegt. Beim Hochstufen
**erneut prüfen**, ob ein anderer Faden ihn inzwischen eingetragen hat.

`DiscoverLocales` läuft über `*.json` im Verzeichnis, liest je Datei `_meta.language` für den
Anzeigenamen und **überspringt eine Datei, die sich nicht parsen lässt** — sie darf nicht in
`_available` landen. Sortiert wird mit `en` zuerst, dann nach Anzeigename.

`SetLocale` prüft gegen `_available` und lehnt Unbekanntes mit einer Logzeile ab. Beim Wechsel
werden `_strings`, `_inlineCache` und `_inlineIndex` unter `unique_lock` geleert und neu befüllt;
für `en` bleibt `_strings` leer, weil `_fallback` dieselbe Tabelle ist.

-   [ ] **Schritt 4: Das Testziel eintragen**

```cmake
    add_executable(
        I18nTests
        "${CMAKE_SOURCE_DIR}/tests/I18nTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/I18n/I18n.cpp"
    )

    target_include_directories(
        I18nTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(I18nTests PRIVATE cxx_std_23)
    target_precompile_headers(I18nTests PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h")
    target_link_libraries(I18nTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            I18nTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME I18n COMMAND I18nTests)
```

-   [ ] **Schritt 5: Bauen, laufen lassen, grün sehen**

```pwsh
& $cmake --preset FO4
& $cmake --build --preset FO4 --target I18nTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R I18n
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`. Sollte `Schließen` als Vergleich scheitern, ist es
eine Kodierungsfrage: die Testdatei ist UTF-8 und `/utf-8` steht in den Optionen — dann liegt es
daran, wie die Datei geschrieben wurde, nicht am Motor.

-   [ ] **Schritt 6: Den Test absichtlich brechen**

In `Get` den Rückfall auf `_fallback` überspringen (den mittleren Zweig entfernen). Neu bauen,
laufen lassen. Erwartet: **`FAIL  a key missing in German falls back to English, not to the
inline default`**, `1 failure(s)`. Danach wiederherstellen.

-   [ ] **Schritt 7: Commit**

```bash
git add src/I18n/I18n.h src/I18n/I18n.cpp tests/I18nTests.cpp CMakeLists.txt
git commit -m "feat: translate what the overlay says"
```

---

### Aufgabe 8: Die Paketierung

Muss vor der Schrift kommen: geladen werden kann nur, was ausgeliefert wurde, und `package.ps1`
ist zugleich der Deploy-Schritt.

**Dateien:**

-   Anlegen: `package/F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-Regular.ttf`,
    `…/IBMPlexSans-SemiBold.ttf`, `…/Translations/en.json`
-   Ändern: `tools/package.ps1`, `tools/verify-package.ps1`

**Schnittstellen:**

-   Nutzt: nichts.
-   Liefert: `Data/F4SE/Plugins/CommunityShadersFO4/{Fonts,Translations}/` in der
    Spielinstallation und im Basisarchiv. Aufgaben 9, 11 und 12 legen dort ab.

-   [ ] **Schritt 1: Den Baum anlegen**

```bash
mkdir -p package/F4SE/Plugins/CommunityShadersFO4/Fonts
mkdir -p package/F4SE/Plugins/CommunityShadersFO4/Translations
cp "package/Interface/CommunityShaders/Fonts/IBMPlexSans/IBMPlexSans-Regular.ttf" \
   package/F4SE/Plugins/CommunityShadersFO4/Fonts/
cp "package/Interface/CommunityShaders/Fonts/IBMPlexSans/IBMPlexSans-SemiBold.ttf" \
   package/F4SE/Plugins/CommunityShadersFO4/Fonts/
cp "package/Interface/CommunityShaders/Fonts/IBMPlexSans/OFL.txt" \
   package/F4SE/Plugins/CommunityShadersFO4/Fonts/
```

Die Lizenzdatei muss mit: IBM Plex steht unter der SIL Open Font License, die verlangt, dass sie
die Schrift begleitet.

Eine erste Übersetzungsdatei, damit der Sprachfund etwas zu finden hat. Aufgabe 12 erzeugt sie neu
aus dem Quelltext; hier steht nur, was es bis dahin gibt:

`package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json`

```json
{
    "_meta": {
        "language": "English",
        "locale": "en",
        "auto_generated": true,
        "generator": "tools/extract-i18n.py",
        "note": "DO NOT EDIT MANUALLY. Run: python tools/extract-i18n.py --write"
    }
}
```

-   [ ] **Schritt 2: `package.ps1` erweitern**

In `tools/package.ps1`, in `New-BaseTree`, hinter dem `$shaders`-Block:

```powershell
    # Data the plugin reads at runtime: fonts and translations. Fallout 4 loads
    # from Data/F4SE/Plugins, not from SKSE - package/SKSE is inherited Skyrim
    # content and ships nothing.
    $runtime = Join-Path $root "package/F4SE"
    if (Test-Path $runtime) {
        Copy-Tree $runtime (Join-Path $To "F4SE")
    }
```

Der Kopiervorgang läuft **vor** oder **nach** dem Ablegen der DLL — beides geht, `Copy-Tree`
mischt in ein vorhandenes Verzeichnis und die Dateinamen überschneiden sich nicht.

Den Kommentar im `.DESCRIPTION`-Kopf mitziehen:

```
    package/ is never copied wholesale. Only package/Shaders/FO4,
    package/F4SE and package/Features/<Name> travel; package/Interface and
    package/SKSE are inherited Skyrim content and must not ship.
```

-   [ ] **Schritt 3: `verify-package.ps1` erweitern**

Die vorhandenen Erwartungen an den Basisbaum um die drei Pfade ergänzen:

```
F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-Regular.ttf
F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-SemiBold.ttf
F4SE/Plugins/CommunityShadersFO4/Translations/en.json
```

Zusätzlich prüfen, dass **nichts** aus `Interface/` oder `SKSE/` im Baum liegt — der Fehler, den
diese Aufgabe gerade beseitigt, soll nicht zurückkehren können. Die vorhandene Prüfstruktur der
Datei wird dafür übernommen, nicht eine neue erfunden.

-   [ ] **Schritt 4: Paketieren und prüfen**

```pwsh
& $cmake --build --preset FO4 --target package
pwsh tools/verify-package.ps1
```

Erwartet: die Prüfung ist grün und nennt die drei neuen Pfade.

-   [ ] **Schritt 5: Die Prüfung absichtlich brechen**

Den `$runtime`-Block in `package.ps1` auskommentieren, neu paketieren, prüfen. Erwartet: die
Prüfung schlägt fehl und benennt die drei fehlenden Pfade. Danach wiederherstellen.

-   [ ] **Schritt 6: Ins Spiel ausliefern und nachsehen**

```pwsh
& $cmake --build --preset FO4
```

Danach prüfen, dass
`F:\SteamLibrary\steamapps\common\Fallout 4\Data\F4SE\Plugins\CommunityShadersFO4\Fonts\`
die beiden TTF enthält.

-   [ ] **Schritt 7: Commit**

```bash
git add package/F4SE tools/package.ps1 tools/verify-package.ps1
git commit -m "feat: ship what the overlay reads at runtime"
```

---

### Aufgabe 9: Schrift und Theme

**Dateien:**

-   Anlegen: `src/Menu/Fonts.h`, `src/Menu/Fonts.cpp`, `src/Menu/Theme.h`, `src/Menu/Theme.cpp`
-   Ändern: `src/Menu/Overlay.cpp`, `src/Menu/MenuSystem.cpp`

**Schnittstellen:**

-   Nutzt: `Settings::DeclareSlider`, `Settings::GetDouble` (Aufgaben 2 und 3).
-   Liefert: `Menu::Fonts::Load()`, `Menu::Fonts::Body()`, `Menu::Fonts::Heading()`,
    `Menu::ApplyTheme(float a_fontSize)`. Aufgabe 11 benutzt `Heading()`.

Der entscheidende Punkt aus der Vorerkundung: ImGui 1.92 lädt Glyphen **bei Bedarf** und skaliert
zur Laufzeit, weil das dx11-Backend `ImGuiBackendFlags_RendererHasTextures` meldet. Es sind also
**keine** Glyphenbereiche zu deklarieren, und ein Größenwechsel baut den Atlas nicht neu.

-   [ ] **Schritt 1: Die Schrift laden**

`src/Menu/Fonts.h`:

```cpp
#pragma once

struct ImFont;

namespace Menu::Fonts
{
	/// Loads the shipped family once. Safe to call every frame; only the first
	/// call does anything. A missing file is not fatal - ImGui's built-in font
	/// stays, and the overlay is ugly but usable.
	void Load() noexcept;

	/// Null until Load succeeded, which is ImGui's way of saying "the default".
	[[nodiscard]] ImFont* Body() noexcept;
	[[nodiscard]] ImFont* Heading() noexcept;
}
```

`src/Menu/Fonts.cpp` lädt aus `Data/F4SE/Plugins/CommunityShadersFO4/Fonts/`. Der Pfad wird
relativ zum Arbeitsverzeichnis des Spiels gebildet — Fallout 4 läuft in seinem Installationsordner,
`Data/…` ist von dort aus richtig:

```cpp
		constexpr auto kDirectory = "Data/F4SE/Plugins/CommunityShadersFO4/Fonts"sv;

		ImFont* LoadOne(std::string_view a_file)
		{
			const auto path = std::format("{}/{}", kDirectory, a_file);
			if (!std::filesystem::exists(path)) {
				REX::ERROR("font {} is missing, the built-in font stays", path);
				return nullptr;
			}

			// Size zero: 1.92 sizes a font when it is pushed, not when it is
			// added, so there is nothing to commit to here.
			return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f);
		}
```

-   [ ] **Schritt 2: Das Theme schreiben**

`src/Menu/Theme.h`:

```cpp
#pragma once

namespace Menu
{
	/// Applies our style to the current ImGui context, scaled to a_fontSize.
	/// Idempotent, and cheap enough to call whenever the size changed.
	void ApplyTheme(float a_fontSize) noexcept;
}
```

`src/Menu/Theme.cpp` setzt einen dunklen Stil. Zwei Dinge sind keine Geschmacksfrage:

```cpp
		// Opaque, not translucent. A see-through panel over Boston at night is
		// unreadable, and the overlay is a settings window, not an effect.
		colors[ImGuiCol_WindowBg] = ImVec4{ 0.09f, 0.09f, 0.10f, 0.98f };
```

und

```cpp
		// Spacing has to grow with the text, or a larger font sits in padding
		// meant for a smaller one.
		style.ScaleAllSizes(a_fontSize / kReferenceFontSize);
```

mit `constexpr float kReferenceFontSize = 18.0f;` — derselbe Wert wie der Vorgabewert der
Einstellung, weil die Skalierung bei der Vorgabe genau 1 ergeben soll.

-   [ ] **Schritt 3: Die Einstellung deklarieren**

In `src/Menu/MenuSystem.cpp`, in `StartSystem`, neben der Umschalttaste:

```cpp
		Settings::DeclareSlider(kFontSizePath, 18.0, 12.0, 32.0)
			.Label("setting.menu.font_size", "Font size")
			.Help("setting.menu.font_size.help", "Size of the text in this overlay.");
```

mit `constexpr auto kFontSizePath = "Menu/fontSize"sv;` bei den anderen Pfadkonstanten.

-   [ ] **Schritt 4: Overlay anschließen**

In `Overlay::EnsureReady`, nach `ImGui::CreateContext()` und vor den Backend-Aufrufen,
`Menu::Fonts::Load()`.

In `Overlay::Draw`, nach `ImGui::NewFrame()` und vor dem Zeichnen:

```cpp
		// Read every frame rather than cached: there is no change notification,
		// so whoever caches a setting has to refresh it themselves.
		const auto fontSize = static_cast<float>(Settings::GetDouble("Menu/fontSize"));

		static float appliedSize = 0.0f;
		if (fontSize != appliedSize) {
			ApplyTheme(fontSize);
			appliedSize = fontSize;
		}

		ImGui::PushFont(Fonts::Body(), fontSize);
```

und ein `ImGui::PopFont();` vor `ImGui::Render()`. **Nicht** die einargumentige Form von
`PushFont` benutzen — 1.92 hat sie entfernt, gerade damit dieser Umstieg auffällt.

-   [ ] **Schritt 5: Bauen und im Spiel ansehen**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Spiel über `f4se_loader.exe` starten, Overlay mit `Ende` öffnen. Erwartet: die Schrift ist IBM
Plex Sans und lesbar groß, das Fenster ist deckend. Im Log steht keine Zeile über eine fehlende
Schrift.

-   [ ] **Schritt 6: Den Ausfallweg prüfen**

Eine der beiden TTF im Spielordner umbenennen, Spiel starten. Erwartet: das Overlay öffnet, zeigt
ImGuis eingebaute Schrift, und im Log steht **eine** Zeile mit dem vollständigen Pfad. Danach
zurückbenennen.

-   [ ] **Schritt 7: Commit**

```bash
git add src/Menu/Fonts.h src/Menu/Fonts.cpp src/Menu/Theme.h src/Menu/Theme.cpp \
        src/Menu/Overlay.cpp src/Menu/MenuSystem.cpp
git commit -m "feat: give the overlay a face"
```

---

### Aufgabe 10: Tastenaufnahme

**Dateien:**

-   Ändern: `src/Menu/Win32.h`, `src/Menu/WindowHook.h`, `src/Menu/WindowHook.cpp`,
    `src/Menu/MenuGate.h`, `src/Menu/MenuGate.cpp`, `src/Menu/MenuSystem.cpp`
-   Test: `tests/MenuGateTests.cpp`

**Schnittstellen:**

-   Nutzt: nichts.
-   Liefert: `Menu::Gate::ArmCapture()`, `Menu::Gate::IsCapturing()`,
    `Menu::Gate::OfferKey(std::uint32_t)`, `Menu::Gate::TakeCapturedKey()`. Aufgabe 11 zeichnet
    damit das Bedienelement.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

An das Ende von `tests/MenuGateTests.cpp`, vor die Ausgabe von `g_failures`:

```cpp
	// Capture beats the toggle key. Without this the toggle key could never be
	// rebound onto itself: the press would close the overlay instead of being
	// taken.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.RequestToggle();
		Check(gate.Tick(), "the overlay is open");

		Check(!gate.IsCapturing(), "nothing is being captured yet");
		Check(!gate.OfferKey(112), "and a key is not taken");

		gate.ArmCapture();
		Check(gate.IsCapturing(), "arming starts a capture");
		Check(gate.OfferKey(35), "the toggle key itself is taken, not acted on");
		Check(!gate.IsCapturing(), "and the capture disarms itself");

		Check(gate.TakeCapturedKey() == 35, "the captured key comes back once");
		Check(gate.TakeCapturedKey() == 0, "and only once");

		Check(gate.Tick(), "and the overlay never closed");
	}

	// An armed capture that is cancelled takes nothing.
	{
		Counters counters;
		auto gate = MakeGate(counters);
		gate.SetToggleKey(35);

		gate.ArmCapture();
		gate.CancelCapture();
		Check(!gate.IsCapturing(), "a cancelled capture is disarmed");
		Check(!gate.OfferKey(112), "and takes nothing afterwards");
		Check(gate.TakeCapturedKey() == 0, "and has nothing to hand over");
	}
```

-   [ ] **Schritt 2: Den Test laufen lassen und den Fehlschlag sehen**

```pwsh
& $cmake --build --preset FO4 --target MenuGateTests
```

Erwartet: **Übersetzungsfehler**, `C2039: 'ArmCapture': is not a member of 'Menu::Gate'`.

-   [ ] **Schritt 3: Das Tor erweitern**

In `src/Menu/MenuGate.h`, neben den vorhandenen atomaren Feldern:

```cpp
		/// Starts taking the next key press instead of acting on it. Set from
		/// the render thread, read and cleared on the window thread - the same
		/// split the toggle already uses.
		void ArmCapture() noexcept;
		void CancelCapture() noexcept;
		[[nodiscard]] bool IsCapturing() const noexcept;

		/// Offered every key the window procedure sees, before the toggle is
		/// considered. Returns whether the key was taken, in which case the
		/// caller must not act on it.
		[[nodiscard]] bool OfferKey(std::uint32_t a_key) noexcept;

		/// The captured key, once. Zero when there is none.
		[[nodiscard]] std::uint32_t TakeCapturedKey() noexcept;

	private:
		std::atomic<bool> _capturing{ false };
		std::atomic<std::uint32_t> _captured{ 0 };
```

Und in `src/Menu/MenuGate.cpp`:

```cpp
	void Gate::ArmCapture() noexcept
	{
		_capturing.store(true);
	}

	void Gate::CancelCapture() noexcept
	{
		_capturing.store(false);
		_captured.store(0);
	}

	bool Gate::IsCapturing() const noexcept
	{
		return _capturing.load();
	}

	bool Gate::OfferKey(std::uint32_t a_key) noexcept
	{
		// exchange rather than load-then-store: two presses in one frame must
		// not both be taken, and this runs on the window thread.
		if (!_capturing.exchange(false)) {
			return false;
		}

		_captured.store(a_key);
		return true;
	}

	std::uint32_t Gate::TakeCapturedKey() noexcept
	{
		return _captured.exchange(0);
	}
```

-   [ ] **Schritt 4: Den Test laufen lassen und grün sehen**

```pwsh
& $cmake --build --preset FO4 --target MenuGateTests
ctest --test-dir build/FO4 -C Release --output-on-failure -R MenuGate
```

Erwartet: alle Zeilen `ok`, `0 failure(s)`.

-   [ ] **Schritt 5: Den Fensterhaken erweitern**

`InstallWindowHook` bekommt einen vierten Rückruf. In `src/Menu/WindowHook.h`:

```cpp
	/// a_offerKey is asked about every key press before a_wantsToggle, and
	/// returns whether it took the key. That order is the whole point: asked
	/// the other way round, the toggle key could never be rebound onto itself.
	void InstallWindowHook(
		void* a_window,
		std::function<bool(std::uint32_t)> a_offerKey,
		std::function<bool(std::uint32_t)> a_wantsToggle,
		std::function<void()> a_onToggle,
		std::function<bool()> a_isOpen) noexcept;
```

In `WindowProc`, **vor** der vorhandenen Umschalttastenprüfung:

```cpp
			if (a_msg == Win32::WM_KEYDOWN && g_offerKey &&
				g_offerKey(static_cast<std::uint32_t>(a_wParam))) {
				return 0;
			}
```

-   [ ] **Schritt 6: `MenuSystem` verdrahten**

Der `InstallWindowHook`-Aufruf bekommt den neuen Rückruf als zweites Argument:

```cpp
			[](std::uint32_t a_key) { return TheGate().OfferKey(a_key); },
```

-   [ ] **Schritt 7: Den Tastennamen anzeigen können**

`REX::W32` hat `GetKeyNameTextW`, aber dessen Parameter ist ein `lParam` mit dem Scancode in den
Bits 16 bis 23 — der aus dem virtuellen Tastencode erst zu gewinnen ist. `MapVirtualKeyW` fehlt
in `REX::W32`. In `src/Menu/Win32.h`, im `extern "C"`-Block:

```cpp
		// Needed to turn a virtual key code into the scan code that
		// GetKeyNameTextW wants in bits 16..23 of its parameter. REX::W32 has
		// GetKeyNameTextW but not this.
		std::uint32_t __stdcall MapVirtualKeyW(std::uint32_t a_code, std::uint32_t a_mapType) noexcept;
```

mit `inline constexpr std::uint32_t MAPVK_VK_TO_VSC = 0u;` daneben.

-   [ ] **Schritt 8: Bauen und alle Tests laufen lassen**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: alles grün.

-   [ ] **Schritt 9: Den Test absichtlich brechen**

In `Gate::OfferKey` das `exchange(false)` durch `load()` ersetzen. Neu bauen, laufen lassen.
Erwartet: **`FAIL  and the capture disarms itself`**, `1 failure(s)`. Danach wiederherstellen.

-   [ ] **Schritt 10: Commit**

```bash
git add src/Menu/MenuGate.h src/Menu/MenuGate.cpp src/Menu/WindowHook.h \
        src/Menu/WindowHook.cpp src/Menu/MenuSystem.cpp src/Menu/Win32.h \
        tests/MenuGateTests.cpp
git commit -m "feat: take a key press instead of acting on it"
```

---

### Aufgabe 11: Das Panel

**Dateien:**

-   Anlegen: `src/Menu/SettingsPanel.h`, `src/Menu/SettingsPanel.cpp`
-   Ändern: `src/Menu/Overlay.h`, `src/Menu/Overlay.cpp`, `src/Menu/MenuSystem.cpp`

**Schnittstellen:**

-   Nutzt: `Settings::ForEachBlock`, `ForEachEntry`, `Get*`, `Set*`, `Save`, `RestoreDefaults`
    (Aufgaben 2 bis 4); `Features::TheRegistry().ForEach` (Aufgabe 6); `T()` (Aufgabe 7);
    `Menu::Fonts::Heading()` (Aufgabe 9); `Gate::ArmCapture`, `IsCapturing`, `TakeCapturedKey`
    (Aufgabe 10).
-   Liefert: `bool Menu::DrawSettingsPanel(const PanelContext&)` — liefert zurück, ob der
    Schließen-Knopf gedrückt wurde.

-   [ ] **Schritt 1: Die Sprachauswahl deklarieren**

In `src/Menu/MenuSystem.cpp`, in `StartSystem`, **vor** den anderen Deklarationen, weil der
i18n-Motor die Liste der Sprachen liefern muss, bevor sie deklariert wird:

```cpp
		// The catalogue lives next to the fonts, under Data. Init before the
		// declaration: the list of locales is what the choice is made of.
		I18n::GetSingleton()->Init("Data/F4SE/Plugins/CommunityShadersFO4/Translations");

		std::vector<std::string> locales;
		for (const auto& [code, name] : I18n::GetSingleton()->AvailableLocales()) {
			locales.push_back(code);
		}
		if (locales.empty()) {
			locales.emplace_back("en");
		}

		Settings::DeclareChoice(kLanguagePath, "en", std::move(locales))
			.Label("setting.menu.language", "Language");
```

In `TickSystem` die Sprache anwenden, wenn sie sich geändert hat:

```cpp
		if (const auto language = Settings::GetString(kLanguagePath);
			language != I18n::GetSingleton()->CurrentLocale()) {
			I18n::GetSingleton()->SetLocale(language);
		}
```

-   [ ] **Schritt 2: Das Panel schreiben**

`src/Menu/SettingsPanel.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>

namespace Menu
{
	/// What the panel needs from its surroundings, handed in rather than
	/// reached for: the panel draws, and knows neither the gate nor the frame
	/// counter.
	struct PanelContext
	{
		std::uint64_t frame{ 0 };

		/// Starts taking the next key press. Drawn as a button that then says
		/// so until a key arrives.
		std::function<void()> armCapture;
		std::function<bool()> isCapturing;
	};

	/// One ImGui window. Returns whether the player asked to close it.
	[[nodiscard]] bool DrawSettingsPanel(const PanelContext& a_context);
}
```

`src/Menu/SettingsPanel.cpp` — der Kern ist das Zeichnen eines Eintrags nach seiner Art. Jedes
Bedienelement folgt derselben Form: zeichnen, bei Änderung `Set*`, und bei
`IsItemDeactivatedAfterEdit()` speichern.

```cpp
		// The one rule that makes a slider write once instead of sixty times a
		// second: change the value while dragging, write when the drag ends.
		// A checkbox deactivates in the same frame it changes, so one branch
		// serves both.
		void Commit()
		{
			if (ImGui::IsItemDeactivatedAfterEdit()) {
				Settings::Save();
			}
		}

		void DrawEntry(const Settings::Entry& a_entry, const PanelContext& a_context)
		{
			const char* const label = T(a_entry.labelKey, a_entry.labelText.data());
			ImGui::PushID(a_entry.path.data(), a_entry.path.data() + a_entry.path.size());

			switch (a_entry.kind) {
			case Settings::Kind::kBool:
				{
					bool value = Settings::GetBool(a_entry.path);
					if (ImGui::Checkbox(label, std::addressof(value))) {
						Settings::SetBool(a_entry.path, value);
					}
					Commit();
					break;
				}
			case Settings::Kind::kSlider:
				{
					auto value = static_cast<float>(Settings::GetDouble(a_entry.path));
					if (ImGui::SliderFloat(
							label,
							std::addressof(value),
							static_cast<float>(a_entry.min),
							static_cast<float>(a_entry.max))) {
						Settings::SetDouble(a_entry.path, value);
					}
					Commit();
					break;
				}
			case Settings::Kind::kChoice:
				DrawChoice(a_entry, label);
				break;
			case Settings::Kind::kKey:
				DrawKey(a_entry, label, a_context);
				break;
			}

			if (!a_entry.helpText.empty() && ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", T(a_entry.helpKey, a_entry.helpText.data()));
			}

			ImGui::PopID();
		}
```

**Vorsicht bei `a_entry.labelText.data()`:** `Entry` führt `string_view`s in eine Tabelle, deren
Zeichenketten `std::string` sind — also nullterminiert. `data()` ist hier zulässig. Sollte sich
das ändern, muss `T()` eine `string_view` nehmen.

`DrawKey` zeichnet einen Knopf mit dem Namen der Taste (über `MapVirtualKeyW` und
`GetKeyNameTextW`), oder `Press a key…` solange `a_context.isCapturing()` gilt. Ein Klick ruft
`a_context.armCapture()`. Der aufgenommene Tastencode wird **nicht** hier abgeholt, sondern in
`MenuSystem::TickSystem` — dort, wo das Tor liegt:

```cpp
		if (const auto captured = TheGate().TakeCapturedKey(); captured != 0) {
			Settings::SetUInt32(kToggleKeyPath, captured);
			Settings::Save();
		}
```

Der Aufbau des Fensters:

```cpp
	bool DrawSettingsPanel(const PanelContext& a_context)
	{
		bool closeWanted = false;

		ImGui::SetNextWindowSize(ImVec2{ 520.0f, 480.0f }, ImGuiCond_FirstUseEver);
		if (ImGui::Begin(T("menu.title", "Community Shaders"))) {
			ImGui::PushFont(Fonts::Heading(), 0.0f);
			ImGui::TextUnformatted(Plugin::NAME.data());
			ImGui::PopFont();
			ImGui::SameLine();
			// BUILD_DESCRIBE, not the version triple: it is a string_view that
			// certainly exists, and "v0.1.0-3-gabcdef1-dirty" answers "which
			// build is this" in a way a bug report can be read against.
			// cmake/Plugin.h.in declares NAME, VERSION (a REL::Version) and
			// BUILD_DESCRIBE, and nothing else.
			ImGui::TextDisabled("%s", Plugin::BUILD_DESCRIBE.data());
			ImGui::Text("%s %llu", T("menu.frame", "Frame"), static_cast<unsigned long long>(a_context.frame));

			ImGui::Separator();

			// Everything below scrolls, and the footer below it does not: with
			// forty features from F+ the buttons must not walk off the bottom.
			const auto footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
			if (ImGui::BeginChild("body", ImVec2{ 0.0f, -footer })) {
				DrawGeneral(a_context);
				DrawFeatures(a_context);
			}
			ImGui::EndChild();

			ImGui::Separator();
			DrawFooter(closeWanted);
		}
		ImGui::End();

		return closeWanted;
	}
```

`DrawGeneral` läuft mit `ForEachBlock` und überspringt jeden Block, zu dem ein Feature gleichen
Namens registriert ist. `DrawFeatures` läuft mit `Features::TheRegistry().ForEach`, zeichnet je
Feature eine Checkbox aus dem Einschalter, den Zustand rechts daneben, und darunter eingerückt
die übrigen Einträge des Blocks. Welcher Eintrag der Einschalter ist, sagt `isFeatureSwitch` —
er wird aus der Liste herausgezogen und nicht ein zweites Mal gezeichnet.

Der Zustandstext:

```cpp
		const char* StateText(Features::State a_state)
		{
			switch (a_state) {
			case Features::State::kRunning:
				return T("menu.state.running", "running");
			case Features::State::kRefused:
				// The only place a player learns that the tick did nothing.
				// Without it the refusal exists only in the log.
				return T("menu.state.refused", "refused");
			default:
				return T("menu.state.off", "off");
			}
		}
```

`DrawFooter` zeichnet `Restore defaults` mit einer Rückfrage — sofort gespeichert heißt, es gibt
kein „einfach nicht speichern" — und `Close`, das `closeWanted` setzt:

```cpp
			if (ImGui::Button(T("menu.restore_defaults", "Restore defaults"))) {
				ImGui::OpenPopup("confirm-restore");
			}

			if (ImGui::BeginPopupModal("confirm-restore", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
				ImGui::TextUnformatted(
					T("menu.restore_confirm", "Put every setting back to its default?"));
				ImGui::Separator();
				if (ImGui::Button(T("menu.yes", "Yes"))) {
					Settings::RestoreDefaults();
					Settings::Save();
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(T("menu.no", "No"))) {
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
```

-   [ ] **Schritt 3: Overlay umstellen**

In `src/Menu/Overlay.cpp` wird der Platzhalterblock in `Draw` — von `ImGui::SetNextWindowSize`
bis `ImGui::End()`, samt der Zeile `"The feature list arrives with subproject E2."` — durch

```cpp
			closeWanted = DrawSettingsPanel(a_panel);
```

ersetzt. `Overlay::Draw` bekommt dafür einen weiteren Parameter `const PanelContext& a_panel`;
`Overlay.h` wird entsprechend geändert, und `MenuSystem::TickSystem` baut den Kontext:

```cpp
		PanelContext panel;
		panel.frame = Render::FrameCount();
		panel.armCapture = [] { TheGate().ArmCapture(); };
		panel.isCapturing = [] { return TheGate().IsCapturing(); };
```

-   [ ] **Schritt 4: Bauen und alle Tests laufen lassen**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: alles grün. Die Host-Tests kennen das Panel nicht — es hängt an ImGui und ist damit
Sache der Prüfung im Spiel.

-   [ ] **Schritt 5: Im Spiel prüfen**

Vor dem Start die vorhandene `CommunityShadersFO4.json` **stehen lassen** — dass sie ergänzt
wird, ist Abnahmekriterium 3 und soll hier zum ersten Mal am echten Fall greifen. Spiel starten,
Overlay öffnen.

Erwartet, der Reihe nach:

1. Im Log eine Zeile `is missing declared keys, extending it`.
2. Das Panel zeigt „General" mit Sprache, Schriftgröße und Umschalttaste, und darunter beide
   Features mit ihrem Zustand.
3. Das Häkchen an `FrameCounter` schaltet es ein; im Log erscheint `FrameCounter: running`.
4. Der Schriftgrößenregler wirkt beim Ziehen.
5. Ein Klick auf die Umschalttaste sagt „Press a key…", der nächste Anschlag wird übernommen und
   schließt das Overlay **nicht**.
6. Nach dem Beenden steht in `CommunityShadersFO4.json`, was eingestellt wurde.
7. Nach einem Neustart ist alles noch da — auch die neue Umschalttaste.
8. Im Log steht **kein** `settings changed, reloading` nach einer Änderung im Overlay.

-   [ ] **Schritt 6: Commit**

```bash
git add src/Menu/SettingsPanel.h src/Menu/SettingsPanel.cpp src/Menu/Overlay.h \
        src/Menu/Overlay.cpp src/Menu/MenuSystem.cpp
git commit -m "feat: put every setting on the screen"
```

---

### Aufgabe 12: Das Extraktionsskript und der Bestand

**Dateien:**

-   Anlegen: `tools/extract-i18n.py`
-   Ändern: `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json`
-   Anlegen: `package/F4SE/Plugins/CommunityShadersFO4/Translations/de.json`

**Schnittstellen:**

-   Nutzt: die `T()`-, `.Label()`- und `.Help()`-Stellen aus allen vorherigen Aufgaben.
-   Liefert: `en.json`, erzeugt aus dem Quelltext.

Die Vorlage steht unter `git show skyrim-base:tools/extract-i18n.py`. Unsere Fassung hat es
leichter, weil alle drei Formen dieselbe Gestalt haben — Schlüssel, dann englischer Text:

```
T("key", "English")
.Label("key", "English")
.Help("key", "English")
```

-   [ ] **Schritt 1: Das Skript schreiben**

`tools/extract-i18n.py`, mit einem regulären Ausdruck über `src/**/*.cpp` und `src/**/*.h`:

```python
PATTERN = re.compile(
    r'(?:\bT|\.Label|\.Help)\s*\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)'
)
```

Die drei Formen in einem Ausdruck, weil sie dieselbe Gestalt haben — genau dafür sind die
Schlüssel ausgeschrieben statt aus dem Pfad abgeleitet.

Aufrufe: ohne Argument wird geprüft und die Abweichung berichtet, mit `--write` wird `en.json`
neu geschrieben. Der `_meta`-Block bleibt erhalten. Die Schlüssel werden sortiert, damit die
Datei nicht bei jedem Lauf anders aussieht.

Ein doppelter Schlüssel mit **verschiedenen** englischen Texten ist ein Fehler und muss das
Skript mit einem von null verschiedenen Rückgabewert beenden — sonst gewinnt zufällig, was zuletzt
gefunden wurde.

-   [ ] **Schritt 2: `en.json` erzeugen**

```pwsh
python tools/extract-i18n.py --write
```

Erwartet: die Datei enthält jeden Schlüssel aus den Aufgaben 5 bis 11 —
`setting.menu.toggle_key`, `setting.menu.font_size`, `setting.menu.language`,
`feature.frame_counter.name`, `feature.imagespace_tint.name`, `menu.title`, `menu.frame`,
`menu.state.running`, `menu.state.refused`, `menu.state.off`, `menu.restore_defaults`,
`menu.restore_confirm`, `menu.yes`, `menu.no`, `menu.close` und die zugehörigen `.help`.

-   [ ] **Schritt 3: Prüfen, dass das Skript Abweichungen findet**

Eine Beschriftung im Quelltext ändern (etwa `"Font size"` zu `"Text size"`), dann

```pwsh
python tools/extract-i18n.py
```

Erwartet: das Skript meldet die Abweichung und endet mit einem von null verschiedenen
Rückgabewert. Danach zurückändern.

-   [ ] **Schritt 4: `de.json` von Hand schreiben**

Jeder Schlüssel aus `en.json`, mit deutscher Übersetzung, plus der `_meta`-Block:

```json
{
    "_meta": { "language": "Deutsch", "locale": "de" },
    "menu.title": "Community Shaders",
    "menu.close": "Schließen",
    "setting.menu.font_size": "Schriftgröße"
}
```

Die Datei ist **nicht** auto-generiert und trägt deshalb kein `auto_generated` im `_meta`-Block.

-   [ ] **Schritt 5: Im Spiel prüfen**

Bauen, starten, Overlay öffnen, Sprache auf `de` stellen. Erwartet: die Beschriftungen wechseln
**ohne** Neustart. Ein Schlüssel, den `de.json` nicht führt, zeigt seinen englischen Text — nicht
den Schlüssel und nicht nichts.

-   [ ] **Schritt 6: Commit**

```bash
git add tools/extract-i18n.py package/F4SE/Plugins/CommunityShadersFO4/Translations
git commit -m "feat: generate the english catalogue from source"
```

---

### Aufgabe 13: Die Abnahme festhalten

**Dateien:**

-   Ändern: `docs/fallout4-port/ROADMAP.md`, `.claude/CLAUDE.md`

-   [ ] **Schritt 1: Alle sieben Abnahmekriterien durchgehen**

Die Liste aus Abschnitt 7.3 der Spec, im Spiel, in einem Durchgang. Jedes Kriterium wird mit dem
Beleg notiert — Logzeile, Dateiinhalt oder Beobachtung. Ein Kriterium, das nicht geprüft werden
konnte, wird **als ungeprüft** vermerkt, nicht stillschweigend abgehakt.

-   [ ] **Schritt 2: Den Roadmap-Abschnitt schreiben**

In `docs/fallout4-port/ROADMAP.md` die Zeile für E2 in der Zerlegungstabelle auf
**abgeschlossen** setzen und hinter dem E1-Abschnitt einen Abschnitt „Aus Teilprojekt E2
bestätigt" ergänzen, in der Form der vorhandenen: eine Tabelle mit den gemessenen Werten, danach
die Befunde, die spätere Teilprojekte kennen sollten, danach die bewussten Abweichungen von Spec
und Plan.

Mindestens hineingehören:

-   Dass `glz::generic` `ordered_small_map` benutzt und Objekte damit ihre Einfügereihenfolge
    behalten — glaze bietet `generic_sorted` ausdrücklich als sortierte Alternative an. Die in
    Spec-Abschnitt 8 aufgeworfene Frage nach der Blockreihenfolge ist damit beantwortet.
-   Dass ImGui 1.92 Glyphen bei Bedarf lädt und Schriftgrößen zur Laufzeit skaliert, weil das
    dx11-Backend `ImGuiBackendFlags_RendererHasTextures` meldet — also keine Glyphenbereiche.
-   Was die Prüfung an `REX::FSettingStore` in Aufgabe 3 Schritt 4 ergeben hat: ob sich die
    Registrierung leeren lässt, und wie ein zweites `Init` gelöst wurde.
-   Die Antwort auf die D2-Frage: `package/Interface` und `package/SKSE` liefern nichts aus und
    werden nicht gelöscht.

-   [ ] **Schritt 3: `CLAUDE.md` nachziehen**

-   Im Abschnitt „Features": `Declare` als fünfte Methode ergänzen, und dass die Deklaration jetzt
    beim Feature liegt statt in `RegisterAll`.
-   Der Abschnitt über die Einstellungen: `src/Settings/` statt `src/Feature/FeatureSettings`,
    die vier Arten, und **der Hinweis, dass eine bestehende Datei nicht mehr ergänzt werden muss —
    das erledigt E2**. Der Absatz „An existing file is never extended. … Subproject E2 owns this."
    wird durch die Beschreibung des jetzigen Verhaltens ersetzt.
-   Im Abschnitt „Packaging": `package/F4SE` als drittes ausgeliefertes Verzeichnis, und dass
    `package/Interface` und `package/SKSE` bewusst nichts ausliefern.
-   In der Tabelle „Temporarily moot": die Zeile zu i18n entfernen, weil E2 sie eingelöst hat.
    Themes und Schriften ebenfalls, soweit die Zeile sie nennt.
-   Ein Abschnitt „Menu" um das Panel und die Tastenaufnahme ergänzen.

-   [ ] **Schritt 4: Commit**

```bash
git add docs/fallout4-port/ROADMAP.md .claude/CLAUDE.md
git commit -m "docs: record subproject e2 acceptance"
```

-   [ ] **Schritt 5: Den Zweig abschließen**

Mit der Skill `superpowers:finishing-a-development-branch`. Nach dem bisherigen Muster: lokal nach
`dev` mergen, Zweig löschen, und **nur auf ausdrückliche Ansage** pushen.

---

## Was dieser Plan bewusst offen lässt

-   **Ob `REX::FSettingStore` seine Registrierung leeren lässt.** In Aufgabe 3, Schritt 4 benannt,
    samt Ausweichweg. Es ist die einzige Stelle, an der der Plan eine Bibliotheksfrage an die
    Umsetzung weitergibt, statt sie beantwortet zu haben — `m_settings` ist `protected` und der
    Zugriff darauf ist in den Kopfdateien nachzusehen.
-   **Ob IBM Plex Sans die deutschen Umlaute führt.** Zu erwarten, aber nicht nachgesehen. Fällt
    in Aufgabe 12, Schritt 5 auf, sobald `Schriftgröße` auf dem Schirm steht.
-   **Ob die Fensterprozedur jede Taste sieht, die ein Spieler belegen will.** E1 hat belegt, dass
    `WM_KEYDOWN` ankommt; ob das auch für Tasten gilt, die das Spiel selbst greift, ist nicht
    gemessen. Fällt in Aufgabe 11, Schritt 5 auf.
