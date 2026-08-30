# Teilprojekt D1 — Feature-Framework, Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eine Basisklasse für Features, eine Registrierung, persistente Einstellungen und
Umschalten im laufenden Spiel — bewiesen an C's Shader-Pipeline als erstem Feature.

**Architecture:** `Features::Feature` hat fünf Methoden. `Features::Registry` besitzt die Features,
hält je Feature einen von drei Zuständen und kennt **keine** Einstellungen — der Wunschzustand
kommt als `EnabledQuery`-Lambda herein, was die gesamte Zustandslogik auf dem Host prüfbar macht.
`Features::Settings` beantwortet diese Abfrage im Spiel aus `REX::FJsonSettingStore`. Ein einziger
Einstieg pro Frame ersetzt C's Sonderverdrahtung im Present-Hook.

**Tech Stack:** C++23, MSVC, CommonLibF4, `REX::FJsonSettingStore` mit glaze 7.2.1 über vcpkg,
CMake 4.2+, handgeschriebene Host-Tests nach dem Muster in `tests/`.

**Spec:** `docs/superpowers/specs/2026-08-30-fallout4-feature-framework-design.md`

## Global Constraints

-   **Runtime:** ausschließlich Fallout 4 AE `1.11.240`.
-   **Compiler-Flags unseres Targets:** `/W4 /WX /permissive- /utf-8 /Zc:preprocessor`. Fremde
    Header, die anschlagen, werden **eng auf unserem Target** unterdrückt, mit einem Kommentar, der
    den Header nennt — nie durch Lockern von `/WX`.
-   **`<d3d11.h>` und `<Windows.h>` bleiben verboten.** Alles kommt aus `REX::W32`.
-   **Trampolin bleibt aus.** `InitInfo::trampoline` und `hook` bleiben auf `false`.
-   **commonlibf4 wird nicht geändert.** Kein Submodul-Commit.
-   **Konventionen:** Tabs, `a_` für Parameter, `_` für Member, anonymer Namensraum für Internes,
    Kommentare begründen statt zu beschreiben. Conventional Commits, Titel maximal 50 Zeichen,
    Rumpf bei 72 umgebrochen. Code und Commits auf Englisch, `docs/` auf Deutsch.
-   **Branch:** `port/d1-feature-framework`. Kein Push ohne Ansage.
-   **Bauen:** `cmake` liegt nicht auf dem PATH, und der Ninja-Preset `FO4-Fast` braucht eine
    Developer-Shell, die wir nicht haben. Es gilt:

    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
    & $cmake --build --preset FO4 --target <Ziel>
    ctest --test-dir build/FO4 -C Release --output-on-failure
    ```

-   **Testdisziplin:** Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen, und der
    erwartete Fehlschlag wird **vorher benannt**. Dabei prüfen, dass die Mutation auch übersetzt.

### Fallstricke in `REX`, die dieser Plan umgeht

Alle drei im Quelltext von commonlib-shared nachgeschlagen, nicht vermutet:

1.  **`FSettingStore::m_fileBase` und `m_fileUser` sind `std::string_view`** (`FSettingStore.h`),
    `Init` nimmt aber `const char*`. Ein temporärer `std::string` ergibt einen hängenden View. Der
    Pfad muss die Lebensdauer des Prozesses haben.
2.  **`TJsonSetting::m_path` ist ebenfalls ein `std::string_view`** (`TJsonSetting.h:48`). Die
    Pfad-Zeichenkette muss die Einstellung überleben, und sie darf nach der Konstruktion nicht mehr
    umziehen — bei einem `std::string` mit Kurzstring-Optimierung würde ein Verschieben den View
    ungültig machen.
3.  **`FJsonSettingStore::Save()` schreibt nach `m_fileBase`, nicht nach `m_fileUser`**
    (`FJsonSettingStore.cpp`). Wir setzen `fileBase` auf die eine Nutzerdatei und lassen `fileUser`
    leer.

Und für später: `JsonSettingLoad`/`Save` sind für `bool`, `double`, `std::uint8/16/32_t`,
`std::int8/16/32_t`, `std::string` und `std::vector` davon instanziiert — **nicht für `float`**.
Wer in F einen Gleitkommawert braucht, nimmt `double`.

---

## File Structure

| Datei                                | Verantwortung                                                |
| ------------------------------------ | ------------------------------------------------------------ |
| `src/Util/FileWatch.h/.cpp`          | aus `src/Shader/ShaderWatcher` umgezogen, Namensraum `Util`  |
| `src/Feature/Feature.h`              | die Basisklasse, sonst nichts                                |
| `src/Feature/FeatureRegistry.h/.cpp` | Besitz, drei Zustände, Reihenfolge, Ausnahmefang             |
| `src/Feature/FeatureSettings.h/.cpp` | Pfad, `REX`-Anbindung, Neuladen, `IsEnabled`                 |
| `src/Feature/FeatureSystem.h/.cpp`   | der eine Einstieg pro Frame, plus Registrierung der Features |
| `src/Features/FrameCounter.h/.cpp`   | das triviale Feature                                         |
| `src/Features/ImagespaceTint.h/.cpp` | C's Pipeline als Feature                                     |
| `tests/FileWatchTests.cpp`           | aus `ShaderWatcherTests` umgezogen                           |
| `tests/FeatureRegistryTests.cpp`     | die gesamte Zustandslogik                                    |
| `tests/FeatureSettingsTests.cpp`     | `REX` plus glaze                                             |
| `CMakeLists.txt`                     | zwei neue Test-Executables, eines umbenannt                  |
| `cmake/CommonLibF4.cmake.in`         | `COMMONLIB_JSON` einschalten                                 |
| `vcpkg.json`                         | `glaze`                                                      |

Entfällt: `src/Shader/ShaderPipeline.h/.cpp` geht in `ImagespaceTint` auf.

---

## Task 1: `Util::FileWatch` — Umzug aus `Shader`

**Files:**

-   Move: `src/Shader/ShaderWatcher.h` → `src/Util/FileWatch.h`
-   Move: `src/Shader/ShaderWatcher.cpp` → `src/Util/FileWatch.cpp`
-   Move: `tests/ShaderWatcherTests.cpp` → `tests/FileWatchTests.cpp`
-   Modify: `src/Shader/ShaderPipeline.cpp`, `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts.
-   Produces: `Util::FileWatch` mit unveränderter Schnittstelle —
    `void Reset(std::span<const std::filesystem::path>)` und `[[nodiscard]] bool Poll()`.
    Aufgabe 2 und Aufgabe 5 benutzen sie.

Reiner Umzug. Die Logik wird **nicht** angefasst; die sieben Prüfungen aus C ziehen mit und müssen
unverändert grün bleiben. Das ist der Beleg, dass der Umzug nichts kaputtgemacht hat.

-   [ ] **Step 1: Dateien verschieben**

```bash
mkdir -p src/Util
git mv src/Shader/ShaderWatcher.h src/Util/FileWatch.h
git mv src/Shader/ShaderWatcher.cpp src/Util/FileWatch.cpp
git mv tests/ShaderWatcherTests.cpp tests/FileWatchTests.cpp
```

-   [ ] **Step 2: Namensraum und Includes anpassen**

In `src/Util/FileWatch.h` und `.cpp`: `namespace Shader` → `namespace Util`. In `FileWatch.cpp`
den Include auf `#include "Util/FileWatch.h"` ändern. Den Kommentarblock über der Klasse
unverändert lassen — die Begründung, warum abgefragt statt `ReadDirectoryChangesW` benutzt wird,
gilt weiter.

In `tests/FileWatchTests.cpp`: Include auf `"Util/FileWatch.h"`, `Shader::FileWatch` →
`Util::FileWatch`, Verzeichnisname im Test von `fo4cs-filewatch-tests` beibehalten.

In `src/Shader/ShaderPipeline.cpp`: `#include "Shader/ShaderWatcher.h"` →
`#include "Util/FileWatch.h"`, und die beiden Verwendungen `FileWatch` → `Util::FileWatch`.

-   [ ] **Step 3: `CMakeLists.txt` nachziehen**

Den Block `ShaderWatcherTests` umbenennen: Ziel `FileWatchTests`, Quellen
`"${CMAKE_SOURCE_DIR}/tests/FileWatchTests.cpp"` und `"${CMAKE_SOURCE_DIR}/src/Util/FileWatch.cpp"`,
Test `add_test(NAME FileWatch COMMAND FileWatchTests)`.

-   [ ] **Step 4: Bauen und alle Tests laufen lassen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
```

Erwartet: sechs Tests grün, darunter `FileWatch` mit denselben sieben Prüfzeilen wie zuvor.

-   [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: move file watching out of the shader code"
```

---

## Task 2: `Features::Settings` — glaze, Pfad, Persistenz

**Files:**

-   Modify: `vcpkg.json`, `cmake/CommonLibF4.cmake.in`, `CMakeLists.txt`
-   Create: `src/Feature/FeatureSettings.h`, `src/Feature/FeatureSettings.cpp`
-   Test: `tests/FeatureSettingsTests.cpp`

**Interfaces:**

-   Consumes: `Util::FileWatch` (Aufgabe 1).
-   Produces:

    ```cpp
    namespace Features::Settings
    {
        void DeclareFeature(std::string_view a_name, bool a_default);
        void Init(const std::filesystem::path& a_file);
        void Init();
        [[nodiscard]] bool IsEnabled(std::string_view a_name) noexcept;
        bool ReloadIfChanged() noexcept;
        [[nodiscard]] const std::filesystem::path& File() noexcept;
    }
    ```

    Aufgabe 4 ruft `DeclareFeature`, `Init()`, `ReloadIfChanged()` und `IsEnabled`.

-   [ ] **Step 1: Abhängigkeit aufnehmen**

In `vcpkg.json`, im Array `dependencies`, nach dem `spdlog`-Objekt:

```json
"glaze"
```

In `cmake/CommonLibF4.cmake.in`, **vor** dem `add_subdirectory` von `commonlib-shared`:

```cmake
# REX ships a JSON setting store behind this option, which is what subproject
# D1 persists feature settings with. It has to be set before the subdirectory
# is added, or the option's default of OFF wins.
set(COMMONLIB_JSON ON)
```

-   [ ] **Step 2: Header anlegen**

`src/Feature/FeatureSettings.h`:

```cpp
#pragma once

#include <filesystem>
#include <string_view>

namespace Features::Settings
{
	/// Declares "<a_name>.enabled". Must run before Init: REX registers a
	/// setting with its store at construction, and Init is what loads them.
	void DeclareFeature(std::string_view a_name, bool a_default);

	/// Points the store at a_file and loads it. The overload without an
	/// argument resolves <Documents>/My Games/<save folder>/F4SE/<plugin>.json,
	/// the same directory F4SE puts the log in.
	void Init(const std::filesystem::path& a_file);
	void Init();

	/// False for a name that was never declared.
	[[nodiscard]] bool IsEnabled(std::string_view a_name) noexcept;

	/// Reloads when the watched file changed. Returns whether it did, which is
	/// also the signal that refused features deserve another try.
	bool ReloadIfChanged() noexcept;

	[[nodiscard]] const std::filesystem::path& File() noexcept;
}
```

-   [ ] **Step 3: Den fehlschlagenden Test schreiben**

`tests/FeatureSettingsTests.cpp`:

```cpp
#include "Feature/FeatureSettings.h"

#include <cstdio>
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

	void WriteFile(const std::filesystem::path& a_path, std::string_view a_content)
	{
		std::filesystem::create_directories(a_path.parent_path());
		std::ofstream stream{ a_path, std::ios::binary };
		stream.write(a_content.data(), static_cast<std::streamsize>(a_content.size()));
	}
}

int main()
{
	const auto root = std::filesystem::temp_directory_path() / "fo4cs-settings-tests";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);

	// Declared before Init, because REX registers a setting with its store at
	// construction and Init is what walks that registration.
	Features::Settings::DeclareFeature("alpha", false);
	Features::Settings::DeclareFeature("beta", true);
	Features::Settings::DeclareFeature("gamma", true);

	const auto good = root / "good.json";
	WriteFile(good, R"({"alpha":{"enabled":true},"beta":{"enabled":false}})");
	Features::Settings::Init(good);

	Check(Features::Settings::IsEnabled("alpha"), "the file turns a default-off feature on");
	Check(!Features::Settings::IsEnabled("beta"), "the file turns a default-on feature off");
	Check(
		Features::Settings::IsEnabled("gamma"),
		"a feature the file never mentions keeps its default");
	Check(!Features::Settings::IsEnabled("delta"), "an undeclared name reads as off");

	// A broken file must not wipe what is already loaded.
	const auto broken = root / "broken.json";
	WriteFile(broken, "{ this is not json");
	Features::Settings::Init(broken);

	Check(Features::Settings::IsEnabled("alpha"), "a broken file leaves alpha as it was");
	Check(!Features::Settings::IsEnabled("beta"), "a broken file leaves beta as it was");

	// Neither must a missing one.
	Features::Settings::Init(root / "absent.json");

	Check(Features::Settings::IsEnabled("alpha"), "a missing file leaves alpha as it was");

	// The watch reports a change exactly once.
	Features::Settings::Init(good);
	Check(!Features::Settings::ReloadIfChanged(), "no change right after Init");

	const auto stamp = std::filesystem::last_write_time(good);
	std::filesystem::last_write_time(good, stamp + std::chrono::seconds{ 5 });

	Check(Features::Settings::ReloadIfChanged(), "a touched file is reported");
	Check(!Features::Settings::ReloadIfChanged(), "and not reported twice");

	std::filesystem::remove_all(root);
	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 4: Test in `CMakeLists.txt` eintragen**

Nach `add_test(NAME FileWatch COMMAND FileWatchTests)`, nach dem Muster der bestehenden Blöcke:
Ziel `FeatureSettingsTests`, Quellen `tests/FeatureSettingsTests.cpp`,
`src/Feature/FeatureSettings.cpp` und `src/Util/FileWatch.cpp`,
`add_test(NAME FeatureSettings COMMAND FeatureSettingsTests)`.

-   [ ] **Step 5: Bauen und den Fehlschlag sehen**

Erwartet: **Bau schlägt fehl**, `FeatureSettings.cpp` existiert nicht.

-   [ ] **Step 6: Implementierung schreiben**

`src/Feature/FeatureSettings.cpp`:

```cpp
#include "Feature/FeatureSettings.h"

#include "Util/FileWatch.h"

#include <REX/FJsonSettingStore.h>
#include <REX/TJsonSetting.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>

#include <format>
#include <map>
#include <memory>

namespace Features::Settings
{
	namespace
	{
		struct Entry
		{
			// Owned here and never moved after the setting is built: REX keeps
			// only a string_view of it (TJsonSetting.h:48).
			std::string path;
			std::unique_ptr<REX::TJsonSetting<bool>> setting;
		};

		// A node based container on purpose. The settings hold views into the
		// paths above, so the strings must keep their addresses; a vector would
		// invalidate every one of them on the next growth.
		std::map<std::string, Entry, std::less<>>& Entries()
		{
			static std::map<std::string, Entry, std::less<>> entries;
			return entries;
		}

		// Process lifetime, because FSettingStore stores the path it is given
		// as a string_view rather than copying it (FSettingStore.h).
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

		std::filesystem::path ResolveDefaultFile()
		{
			wchar_t* raw = nullptr;
			const auto result = REX::W32::SHGetKnownFolderPath(
				REX::W32::FOLDERID_Documents,
				REX::W32::KF_FLAG_DEFAULT,
				nullptr,
				std::addressof(raw));

			std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(
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
	}

	void DeclareFeature(std::string_view a_name, bool a_default)
	{
		auto& entry = Entries()[std::string{ a_name }];
		if (entry.setting != nullptr) {
			return;  // Declared twice; the first declaration wins.
		}

		entry.path = std::format("{}.enabled", a_name);
		entry.setting = std::make_unique<REX::TJsonSetting<bool>>(entry.path, a_default);
	}

	void Init(const std::filesystem::path& a_file)
	{
		CurrentFile() = a_file;
		StoredPath() = a_file.string();

		auto* const store = REX::FJsonSettingStore::GetSingleton();

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

	bool IsEnabled(std::string_view a_name) noexcept
	{
		const auto it = Entries().find(a_name);
		if (it == Entries().end() || it->second.setting == nullptr) {
			return false;
		}
		return it->second.setting->GetValue();
	}

	bool ReloadIfChanged() noexcept
	{
		if (!Watch().Poll()) {
			return false;
		}

		REX::INFO("settings changed, reloading");
		REX::FJsonSettingStore::GetSingleton()->Load();
		return true;
	}

	const std::filesystem::path& File() noexcept
	{
		return CurrentFile();
	}
}
```

-   [ ] **Step 7: Bauen und Test grün sehen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4 --target FeatureSettingsTests
& "build\FO4\Release\FeatureSettingsTests.exe"
```

Erwartet: neun Prüfzeilen `ok`, `0 failure(s)`.

Schlägt der vcpkg-Schritt fehl, ist `glaze` in der Baseline `dddca6fa87f177e0678e2545c4b4636a44aa05bd`
nachzuschlagen — Version 7.2.1 ist dort vorhanden.

-   [ ] **Step 8: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `a broken file leaves alpha as it was` und
`a broken file leaves beta as it was` schlagen fehl; die übrigen bleiben grün.

Mutation in `ReloadIfChanged` hilft hier nicht — der Bruch muss beim Laden ansetzen. Stattdessen in
`Init(const std::filesystem::path&)` vor `store->Load()` einfügen:

```cpp
		for (auto& [name, entry] : Entries()) {
			entry.setting->SetValue(false);
		}
```

Das setzt alles zurück, bevor geladen wird — womit eine kaputte Datei die alten Werte eben doch
verliert. Übersetzt sauber. Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: persist feature settings as json"
```

---

## Task 3: `Features::Feature` und `Features::Registry`

**Files:**

-   Create: `src/Feature/Feature.h`, `src/Feature/FeatureRegistry.h`,
    `src/Feature/FeatureRegistry.cpp`
-   Test: `tests/FeatureRegistryTests.cpp`
-   Modify: `CMakeLists.txt`

**Interfaces:**

-   Consumes: nichts. Die Registrierung kennt bewusst weder Einstellungen noch Engine.
-   Produces: `Features::Feature`, `Features::State`, `Features::EnabledQuery`,
    `Features::Registry`, `Features::TheRegistry()`. Aufgabe 4 registriert darin, Aufgabe 5 liefert
    ein Feature.

-   [ ] **Step 1: Die Basisklasse anlegen**

`src/Feature/Feature.h`:

```cpp
#pragma once

#include <string_view>

namespace Features
{
	class Feature
	{
	public:
		virtual ~Feature() = default;

		/// Stable key, and the settings path prefix. Changing it after a user
		/// has a settings file silently resets their choice, so it must not
		/// change.
		[[nodiscard]] virtual std::string_view Name() const = 0;

		/// Acquires what the feature needs. Returning false refuses the enable:
		/// the registry logs it once and does not try again until the settings
		/// change.
		///
		/// It must therefore fail only on things that would fail again. Waiting
		/// for the engine to be ready is the business of Frame, not of Setup.
		[[nodiscard]] virtual bool Setup() = 0;

		/// Once per Present, only while running.
		virtual void Frame() {}

		/// Releases everything Setup acquired, and leaves the engine as it was
		/// found. Must be callable after a failed Setup, because a Setup that
		/// gave up halfway still has something to give back.
		virtual void Shutdown() = 0;
	};
}
```

-   [ ] **Step 2: Den fehlschlagenden Test schreiben**

`tests/FeatureRegistryTests.cpp`:

```cpp
#include "Feature/FeatureRegistry.h"

#include <cstdio>
#include <set>
#include <stdexcept>
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

	// Records the order in which the fakes are set up and shut down, so the
	// reverse teardown can be checked rather than assumed.
	std::vector<std::string> g_order;

	class FakeFeature : public Features::Feature
	{
	public:
		FakeFeature(std::string a_name, bool a_setupSucceeds, bool a_frameThrows) :
			_name(std::move(a_name)),
			_setupSucceeds(a_setupSucceeds),
			_frameThrows(a_frameThrows)
		{}

		[[nodiscard]] std::string_view Name() const override { return _name; }

		[[nodiscard]] bool Setup() override
		{
			++setups;
			g_order.push_back("setup:" + _name);
			return _setupSucceeds;
		}

		void Frame() override
		{
			++frames;
			if (_frameThrows) {
				throw std::runtime_error("deliberate");
			}
		}

		void Shutdown() override
		{
			++shutdowns;
			g_order.push_back("shutdown:" + _name);
		}

		int setups = 0;
		int frames = 0;
		int shutdowns = 0;

	private:
		std::string _name;
		bool _setupSucceeds;
		bool _frameThrows;
	};

	Features::EnabledQuery Only(std::set<std::string> a_enabled)
	{
		return [enabled = std::move(a_enabled)](std::string_view a_name) {
			return enabled.contains(std::string{ a_name });
		};
	}
}

int main()
{
	// A feature that is on runs; one that is off does not.
	{
		Features::Registry registry;
		auto* const alpha = new FakeFeature{ "alpha", true, false };
		auto* const beta = new FakeFeature{ "beta", true, false };
		registry.Register(std::unique_ptr<Features::Feature>{ alpha });
		registry.Register(std::unique_ptr<Features::Feature>{ beta });

		registry.Tick(Only({ "alpha" }));

		Check(alpha->setups == 1, "an enabled feature is set up");
		Check(alpha->frames == 1, "and gets a frame");
		Check(beta->setups == 0, "a disabled feature is not set up");
		Check(beta->frames == 0, "and gets no frame");
		Check(
			registry.StateOf("alpha") == Features::State::kRunning,
			"the enabled one reports running");
		Check(registry.StateOf("beta") == Features::State::kOff, "the disabled one reports off");

		// Toggling one must leave the other alone.
		registry.Tick(Only({ "beta" }));

		Check(alpha->shutdowns == 1, "turning a feature off shuts it down");
		Check(beta->setups == 1, "turning the other on sets it up");
		Check(alpha->frames == 1, "the stopped one gets no further frames");
	}

	// Teardown runs in reverse registration order.
	{
		g_order.clear();
		Features::Registry registry;
		registry.Register(std::make_unique<FakeFeature>("first", true, false));
		registry.Register(std::make_unique<FakeFeature>("second", true, false));

		registry.Tick(Only({ "first", "second" }));
		registry.Tick(Only({}));

		const std::vector<std::string> expected{
			"setup:first", "setup:second", "shutdown:second", "shutdown:first"
		};
		Check(g_order == expected, "setup runs in order and teardown in reverse");
	}

	// A refused feature is not retried every frame.
	{
		Features::Registry registry;
		auto* const broken = new FakeFeature{ "broken", false, false };
		registry.Register(std::unique_ptr<Features::Feature>{ broken });

		registry.Tick(Only({ "broken" }));
		registry.Tick(Only({ "broken" }));
		registry.Tick(Only({ "broken" }));

		Check(broken->setups == 1, "a refused feature is set up only once");
		Check(broken->frames == 0, "and never gets a frame");
		Check(
			registry.StateOf("broken") == Features::State::kRefused,
			"and reports as refused");
		Check(broken->shutdowns == 1, "a failed setup is still shut down");

		// A settings change is what earns it another go.
		registry.ClearRefusals();
		registry.Tick(Only({ "broken" }));

		Check(broken->setups == 2, "clearing refusals lets it try again");
	}

	// A feature that throws from Frame is shut down, not left running.
	{
		Features::Registry registry;
		auto* const thrower = new FakeFeature{ "thrower", true, true };
		auto* const bystander = new FakeFeature{ "bystander", true, false };
		registry.Register(std::unique_ptr<Features::Feature>{ thrower });
		registry.Register(std::unique_ptr<Features::Feature>{ bystander });

		registry.Tick(Only({ "thrower", "bystander" }));

		Check(thrower->shutdowns == 1, "a throwing feature is shut down");
		Check(
			registry.StateOf("thrower") == Features::State::kRefused,
			"and reports as refused");
		Check(bystander->frames == 1, "a throwing feature does not stop its neighbour");

		registry.Tick(Only({ "thrower", "bystander" }));

		Check(thrower->frames == 1, "and gets no further frames");
		Check(bystander->frames == 2, "while the neighbour keeps running");
	}

	std::printf("%d failure(s)\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Step 3: Test in `CMakeLists.txt` eintragen**

Ziel `FeatureRegistryTests`, Quellen `tests/FeatureRegistryTests.cpp` und
`src/Feature/FeatureRegistry.cpp`, `add_test(NAME FeatureRegistry COMMAND FeatureRegistryTests)`.

-   [ ] **Step 4: Bauen und den Fehlschlag sehen**

Erwartet: **Bau schlägt fehl**, `FeatureRegistry.h` existiert nicht.

-   [ ] **Step 5: Header schreiben**

`src/Feature/FeatureRegistry.h`:

```cpp
#pragma once

#include "Feature/Feature.h"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Features
{
	enum class State
	{
		kOff,
		kRunning,

		/// Enabled, but Setup said no or Frame threw. Held apart from kOff so
		/// that a broken feature is not retried every single frame, which would
		/// fill the log in seconds.
		kRefused
	};

	/// Answers whether a feature should be running. The registry takes the
	/// desired state through this rather than reading settings itself, which is
	/// what makes the whole state machine testable without a game.
	using EnabledQuery = std::function<bool(std::string_view a_name)>;

	class Registry
	{
	public:
		void Register(std::unique_ptr<Feature> a_feature);

		/// Brings every feature into the state the query asks for, then frames
		/// the running ones. Teardown runs before setup, and in reverse
		/// registration order.
		void Tick(const EnabledQuery& a_query) noexcept;

		/// Lets refused features try again. Called when the settings changed:
		/// that is the moment a refusal deserves reconsidering.
		void ClearRefusals() noexcept;

		[[nodiscard]] State StateOf(std::string_view a_name) const noexcept;
		[[nodiscard]] std::size_t Count() const noexcept;

	private:
		struct Entry
		{
			std::unique_ptr<Feature> feature;
			State state{ State::kOff };
		};

		void SetupEntry(Entry& a_entry) noexcept;
		void FrameEntry(Entry& a_entry) noexcept;
		void ShutdownEntry(Entry& a_entry) noexcept;

		std::vector<Entry> _entries;
	};

	/// The one the game uses. Tests build their own.
	[[nodiscard]] Registry& TheRegistry() noexcept;
}
```

-   [ ] **Step 6: Implementierung schreiben**

`src/Feature/FeatureRegistry.cpp`:

```cpp
#include "Feature/FeatureRegistry.h"

#include <exception>

namespace Features
{
	namespace
	{
		// Every call into a feature goes through here. An exception escaping
		// into our Present hook would land in the engine, and what Fallout 4
		// does with that is not worth finding out. Costs nothing until it
		// throws.
		template <class F>
		bool Guarded(std::string_view a_name, const char* a_what, F&& a_call) noexcept
		{
			try {
				a_call();
				return true;
			} catch (const std::exception& e) {
				REX::ERROR("{}: {} threw: {}", a_name, a_what, e.what());
			} catch (...) {
				REX::ERROR("{}: {} threw an unknown exception", a_name, a_what);
			}
			return false;
		}
	}

	void Registry::Register(std::unique_ptr<Feature> a_feature)
	{
		if (a_feature == nullptr) {
			return;
		}

		REX::INFO("registered feature {}", a_feature->Name());
		_entries.emplace_back(Entry{ std::move(a_feature), State::kOff });
	}

	void Registry::Tick(const EnabledQuery& a_query) noexcept
	{
		// Teardown first, and in reverse: a frame never has both the old and
		// the new owner of a resource alive at once.
		for (auto it = _entries.rbegin(); it != _entries.rend(); ++it) {
			if (it->state == State::kRunning && !a_query(it->feature->Name())) {
				ShutdownEntry(*it);
			}
		}

		for (auto& entry : _entries) {
			if (entry.state == State::kOff && a_query(entry.feature->Name())) {
				SetupEntry(entry);
			}
		}

		for (auto& entry : _entries) {
			if (entry.state == State::kRunning) {
				FrameEntry(entry);
			}
		}
	}

	void Registry::ClearRefusals() noexcept
	{
		for (auto& entry : _entries) {
			if (entry.state == State::kRefused) {
				entry.state = State::kOff;
			}
		}
	}

	State Registry::StateOf(std::string_view a_name) const noexcept
	{
		for (const auto& entry : _entries) {
			if (entry.feature->Name() == a_name) {
				return entry.state;
			}
		}
		return State::kOff;
	}

	std::size_t Registry::Count() const noexcept
	{
		return _entries.size();
	}

	void Registry::SetupEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();

		bool accepted = false;
		const bool survived = Guarded(name, "Setup", [&] { accepted = a_entry.feature->Setup(); });

		if (survived && accepted) {
			a_entry.state = State::kRunning;
			REX::INFO("{}: running", name);
			return;
		}

		// Even a Setup that gave up halfway may hold something, so it still
		// gets its Shutdown before being written off.
		ShutdownEntry(a_entry);
		a_entry.state = State::kRefused;
		REX::ERROR("{}: refused", name);
	}

	void Registry::FrameEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();

		if (Guarded(name, "Frame", [&] { a_entry.feature->Frame(); })) {
			return;
		}

		ShutdownEntry(a_entry);
		a_entry.state = State::kRefused;
		REX::ERROR("{}: refused after throwing from Frame", name);
	}

	void Registry::ShutdownEntry(Entry& a_entry) noexcept
	{
		const auto name = a_entry.feature->Name();
		static_cast<void>(Guarded(name, "Shutdown", [&] { a_entry.feature->Shutdown(); }));

		// Set regardless: a Shutdown that threw leaves nothing better to do
		// than to stop calling into the feature.
		a_entry.state = State::kOff;
		REX::INFO("{}: off", name);
	}

	Registry& TheRegistry() noexcept
	{
		static Registry registry;
		return registry;
	}
}
```

-   [ ] **Step 7: Bauen und Test grün sehen**

```pwsh
& $cmake --build --preset FO4 --target FeatureRegistryTests
& "build\FO4\Release\FeatureRegistryTests.exe"
```

Erwartet: 18 Prüfzeilen `ok`, `0 failure(s)`.

-   [ ] **Step 8: Test absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `setup runs in order and teardown in reverse` schlägt
fehl, alle übrigen bleiben grün.

Mutation in `Registry::Tick`: die Rückwärtsschleife durch eine Vorwärtsschleife ersetzen.

```cpp
		for (auto& entry : _entries) {
			if (entry.state == State::kRunning && !a_query(entry.feature->Name())) {
				ShutdownEntry(entry);
			}
		}
```

Übersetzt sauber. Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: add the feature base class and registry"
```

---

## Task 4: Verdrahtung und `FrameCounter`

**Files:**

-   Create: `src/Feature/FeatureSystem.h`, `src/Feature/FeatureSystem.cpp`,
    `src/Features/FrameCounter.h`, `src/Features/FrameCounter.cpp`
-   Modify: `src/Render/SwapChainHook.cpp`, `src/XSEPlugin.cpp`

**Interfaces:**

-   Consumes: `Features::TheRegistry` (3), `Features::Settings` (2).
-   Produces: `void Features::StartSystem() noexcept` und `void Features::TickSystem() noexcept`.
    Aufgabe 5 registriert ihr Feature in `StartSystem`.

-   [ ] **Step 1: `FrameCounter` schreiben**

`src/Features/FrameCounter.h`:

```cpp
#pragma once

#include "Feature/Feature.h"

#include <cstdint>

namespace Features
{
	/// Counts frames while it runs, and says so when it starts and stops.
	///
	/// It exists to make independence visible: toggling it must not disturb
	/// whatever else is running, and its counter starting from zero again is
	/// the evidence that Shutdown actually ran.
	class FrameCounter : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "FrameCounter"; }
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		static constexpr std::uint64_t kReportInterval = 600;

		std::uint64_t _frames{ 0 };
	};
}
```

`src/Features/FrameCounter.cpp`:

```cpp
#include "Features/FrameCounter.h"

namespace Features
{
	bool FrameCounter::Setup()
	{
		_frames = 0;
		REX::INFO("FrameCounter: starting from zero");
		return true;
	}

	void FrameCounter::Frame()
	{
		++_frames;

		// Roughly every ten seconds at 60 fps: enough to show it is alive
		// without filling the log.
		if (_frames % kReportInterval == 0) {
			REX::INFO("FrameCounter: {} frames", _frames);
		}
	}

	void FrameCounter::Shutdown()
	{
		REX::INFO("FrameCounter: stopping after {} frames", _frames);
		_frames = 0;
	}
}
```

-   [ ] **Step 2: `FeatureSystem` schreiben**

`src/Feature/FeatureSystem.h`:

```cpp
#pragma once

namespace Features
{
	/// Registers the features, declares their settings and loads them. Called
	/// once, from kGameDataReady.
	void StartSystem() noexcept;

	/// The one entry per frame, from Present, on the render thread.
	void TickSystem() noexcept;
}
```

`src/Feature/FeatureSystem.cpp`:

```cpp
#include "Feature/FeatureSystem.h"

#include "Feature/FeatureRegistry.h"
#include "Feature/FeatureSettings.h"
#include "Features/FrameCounter.h"

namespace Features
{
	namespace
	{
		// Registration order is also teardown order, reversed. Keep the cheap
		// and self-contained ones first.
		void RegisterAll()
		{
			TheRegistry().Register(std::make_unique<FrameCounter>());
		}
	}

	void StartSystem() noexcept
	{
		RegisterAll();

		// Declared before Init, because a REX setting registers with its store
		// at construction and Init is what walks that registration.
		Settings::DeclareFeature("FrameCounter", false);

		Settings::Init();

		REX::INFO("{} features registered", TheRegistry().Count());
	}

	void TickSystem() noexcept
	{
		// A settings change is also the moment a refused feature deserves
		// another try, so the two belong together.
		if (Settings::ReloadIfChanged()) {
			TheRegistry().ClearRefusals();
		}

		TheRegistry().Tick([](std::string_view a_name) { return Settings::IsEnabled(a_name); });
	}
}
```

-   [ ] **Step 3: In den Present-Hook einhängen**

In `src/Render/SwapChainHook.cpp`: `#include "Shader/ShaderPipeline.h"` durch
`#include "Feature/FeatureSystem.h"` ersetzen, und den Aufruf `Shader::TickPipeline();` durch
`Features::TickSystem();`.

In `src/XSEPlugin.cpp`: entsprechend `Shader::StartPipeline();` durch `Features::StartSystem();`
ersetzen, samt Include.

-   [ ] **Step 4: Bauen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4
```

`ShaderPipeline` wird an dieser Stelle **noch** übersetzt, aber nicht mehr gerufen. Aufgabe 5 löst
ihn auf. Erwartet: übersetzt ohne Warnung.

-   [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: give features one entry point per frame"
```

---

## Task 5: `ImagespaceTint` — C's Pipeline wird ein Feature

**Files:**

-   Create: `src/Features/ImagespaceTint.h`, `src/Features/ImagespaceTint.cpp`
-   Delete: `src/Shader/ShaderPipeline.h`, `src/Shader/ShaderPipeline.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp`

**Interfaces:**

-   Consumes: `Features::Feature` (3), `Shader::LoadSource`, `Shader::CompilePixelShader`,
    `Shader::RunImagespaceCatalog`, `Shader::PixelShaderOverride`, `Util::FileWatch` — alle aus C
    beziehungsweise Aufgabe 1.
-   Produces: `Features::ImagespaceTint`.

-   [ ] **Step 1: Header anlegen**

`src/Features/ImagespaceTint.h`:

```cpp
#pragma once

#include "Feature/Feature.h"
#include "Shader/ShaderOverride.h"
#include "Util/FileWatch.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Features
{
	/// Subproject C's shader pipeline, as a feature.
	///
	/// It is here rather than in a dummy because it owns real state: a created
	/// ID3D11PixelShader and a pointer written into engine memory. A feature
	/// that owns nothing cannot show whether Shutdown is honest.
	class ImagespaceTint : public Feature
	{
	public:
		[[nodiscard]] std::string_view Name() const override { return "ImagespaceTint"; }
		[[nodiscard]] bool Setup() override;
		void Frame() override;
		void Shutdown() override;

	private:
		void WatcherLoop();
		void CompileAndPublish();
		void TryCatalog();

		Shader::PixelShaderOverride _override;
		Util::FileWatch _watch;

		std::thread _watcher;
		std::mutex _mutex;
		std::vector<std::uint8_t> _pending;
		std::atomic<bool> _hasPending{ false };
		std::atomic<bool> _stop{ false };

		std::uint64_t _frames{ 0 };
		bool _adopted{ false };
	};
}
```

-   [ ] **Step 2: Implementierung schreiben**

`src/Features/ImagespaceTint.cpp` übernimmt den Inhalt von `ShaderPipeline.cpp` mit vier
Änderungen. Die freien Funktionen und Dateiglobalen werden Member; `ShaderRoot`, `ChoosePass` und
die Konstanten bleiben im anonymen Namensraum.

**Änderung 1 — `Setup` scheitert nicht am Warten.** Der Katalog wandert nach `Frame`:

```cpp
	bool ImagespaceTint::Setup()
	{
		_stop.store(false, std::memory_order_release);
		_frames = 0;
		_adopted = false;

		_watcher = std::thread{ [this] { WatcherLoop(); } };

		// Deliberately not the catalog. The engine fills its technique maps
		// only once a world is loaded, so a Setup that insisted on finding a
		// pass would refuse itself in the main menu and stay refused until the
		// user touched the settings file.
		return true;
	}
```

**Änderung 2 — `Frame` holt den Katalog nach**, im selben Rhythmus wie in C:

```cpp
	void ImagespaceTint::Frame()
	{
		++_frames;

		if (!_adopted && _frames % kCatalogInterval == 0) {
			TryCatalog();
		}

		if (_hasPending.exchange(false, std::memory_order_acq_rel)) {
			std::vector<std::uint8_t> bytecode;
			{
				const std::scoped_lock lock{ _mutex };
				bytecode = std::move(_pending);
				_pending.clear();
			}
			static_cast<void>(_override.Install(bytecode, kDebugName));
		}

		_override.Guard();
	}
```

**Änderung 3 — `Shutdown` baut wirklich ab.** Das ist der Kern der ganzen Aufgabe:

```cpp
	void ImagespaceTint::Shutdown()
	{
		_stop.store(true, std::memory_order_release);
		if (_watcher.joinable()) {
			_watcher.join();
		}

		// Restore has existed since subproject C and has never been called.
		// This is the first time the pointer goes back.
		_override.Restore();

		{
			const std::scoped_lock lock{ _mutex };
			_pending.clear();
		}
		_hasPending.store(false, std::memory_order_release);
		_adopted = false;
	}
```

**Änderung 4 — der Watcher-Thread muss zügig aufhören.** Er schlief in C 500 ms am Stück; ein
`join` würde beim Umschalten also bis zu einer halben Sekunde hängen. Stattdessen in Scheiben:

```cpp
	void ImagespaceTint::WatcherLoop()
	{
		constexpr auto kSlice = std::chrono::milliseconds{ 50 };
		constexpr int kSlicesPerPoll = 10;  // 500 ms between polls

		bool loadedOnce = false;
		int slices = 0;

		while (!_stop.load(std::memory_order_acquire)) {
			if (++slices >= kSlicesPerPoll) {
				slices = 0;
				if (!loadedOnce) {
					CompileAndPublish();
					loadedOnce = true;
				} else if (_watch.Poll()) {
					REX::INFO("{} changed, recompiling", kShaderFile);
					CompileAndPublish();
				}
			}

			// Sliced so that a toggle does not wait out a full poll interval.
			std::this_thread::sleep_for(kSlice);
		}
	}
```

`TryCatalog` ist C's `RunCatalogOnce`, mit `_override.Adopt(chosen->slot)` und `_adopted = true`
statt der Dateiglobalen. Der `g_armed`-Mechanismus aus C entfällt: der Thread läuft nur, solange
das Feature läuft.

-   [ ] **Step 3: Alte Pipeline entfernen und Feature registrieren**

```bash
git rm src/Shader/ShaderPipeline.h src/Shader/ShaderPipeline.cpp
```

In `src/Feature/FeatureSystem.cpp`: `#include "Features/ImagespaceTint.h"` ergänzen, in
`RegisterAll()` `TheRegistry().Register(std::make_unique<ImagespaceTint>());` **nach** dem
`FrameCounter` einfügen — Registrierungsreihenfolge ist Aufbaureihenfolge, und der Tint soll zuerst
abgebaut werden. Dazu `Settings::DeclareFeature("ImagespaceTint", true);`.

-   [ ] **Step 4: Bauen und alle Tests laufen lassen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: acht Tests grün (`Runtime`, `VTablePatch`, `FormatNames`, `ShaderSource`,
`ShaderCompiler`, `FileWatch`, `FeatureSettings`, `FeatureRegistry`), Plugin-Prüfung ohne
Beanstandung.

-   [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: turn the shader pipeline into a feature"
```

---

## Task 6: Abnahme im Spiel

**Files:** keine Änderung erwartet.

Diese Aufgabe braucht den Nutzer. Anweisungen konkret halten: welche Datei, welcher Knopf, was
danach passieren soll.

-   [ ] **Step 1: Erster Start, Grundzustand**

Spiel über `f4se_loader.exe`, `coc SanctuaryExt`, kurz stehen bleiben, beenden.

Erwartet im Log: `registered feature FrameCounter`, `registered feature ImagespaceTint`,
`2 features registered`, `settings loaded from …CommunityShadersFO4.json`, und
`ImagespaceTint: running`. Der `FrameCounter` bleibt aus, weil sein Standardwert `false` ist.

Die Einstellungsdatei entsteht beim ersten `Save()`. Existiert sie noch nicht, ist das kein Fehler
— die Standardwerte gelten. Falls sie fehlt, wird sie für den nächsten Schritt von Hand angelegt:

```json
{
    "ImagespaceTint": { "enabled": true },
    "FrameCounter": { "enabled": false }
}
```

-   [ ] **Step 2: Tint im laufenden Spiel abschalten**

Spiel läuft, Farbstich sichtbar. In der JSON `ImagespaceTint.enabled` auf `false`, speichern,
Alt-Tab.

Erwartet binnen einer Sekunde: der Stich **verschwindet**, im Log `settings changed, reloading` und
`ImagespaceTint: off`. Das prüft `PixelShaderOverride::Restore()` zum ersten Mal überhaupt.

Wieder auf `true`, Alt-Tab: der Stich kommt zurück, im Log `ImagespaceTint: running`.

-   [ ] **Step 3: Unabhängigkeit**

Bei laufendem Tint `FrameCounter.enabled` auf `true`, Alt-Tab.

Erwartet: `FrameCounter: starting from zero`, danach alle rund zehn Sekunden eine Zeile — und der
Farbstich bleibt **unberührt**. Wieder auf `false`: `FrameCounter: stopping after N frames`, Stich
weiterhin unberührt.

Dann umgekehrt: bei laufendem `FrameCounter` den Tint umschalten. Seine Zeilen müssen ohne
Unterbrechung weiterlaufen, und beim nächsten Einschalten muss er wieder bei null beginnen.

-   [ ] **Step 4: Kaputte Einstellungsdatei**

Eine schließende Klammer entfernen, speichern, Alt-Tab. Erwartet: beide Features behalten ihren
Zustand, das Spiel läuft, eine Logzeile. Danach reparieren.

-   [ ] **Step 5: Befunde notieren**

Alles Beobachtete roh festhalten, bevor etwas geändert wird. Rohmaterial für Aufgabe 7.

Kein Commit in dieser Aufgabe.

---

## Task 7: Dokumente und Abschluss

**Files:**

-   Modify: `docs/fallout4-port/ROADMAP.md`, `.claude/CLAUDE.md`

-   [ ] **Step 1: Roadmap nachziehen**

-   Zeile D in der Zerlegungstabelle durch zwei Zeilen ersetzen: **D1 Feature-Framework**
    (abgeschlossen, Abnahme „zwei Features unabhängig an-/abschaltbar") und **D2 Paketierung**
    (offen, Abnahme „ausgeliefertes Archiv installiert sich in ein sauberes Spiel"), mit einem
    Satz, dass D2 hinter F rutscht.
-   Abschnitt „Aus Teilprojekt D1 bestätigt" nach dem Muster der vorherigen anlegen, mit den
    gemessenen Werten aus Aufgabe 6.
-   Die drei `REX`-Fallstricke aus den Global Constraints dieses Plans in die Fallstrick-Liste
    aufnehmen, plus den fehlenden `float`.

-   [ ] **Step 2: `CLAUDE.md` nachziehen**

In der Tabelle „Temporarily moot":

-   Zeile „Packaging, AIO archives, `dist/`, feature `.ini` version audit" → `Subproject D2`.
-   Zeile „Feature framework, `Feature` base class, release stages, `CORE` markers" aufteilen: das
    Framework kehrt mit D1 zurück und ist damit **nicht mehr moot**; Release-Stufen und
    `CORE`-Marker bleiben moot, weil D1 sie bewusst nicht mitnimmt.

-   [ ] **Step 3: Voller Lauf**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: acht Tests grün, Plugin-Prüfung ohne Beanstandung.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: record subproject d1 acceptance"
```

-   [ ] **Step 5: Abschluss**

`superpowers:finishing-a-development-branch` aufrufen. Erwartete Wahl nach bisherigem Muster:
lokal nach `dev` mergen (Fast-Forward), Feature-Branch löschen, Push auf Ansage.

---

## Self-Review

**Spec-Abdeckung**

| Spec-Abschnitt                        | Aufgabe                               |
| ------------------------------------- | ------------------------------------- |
| 4.1 Basisklasse                       | 3                                     |
| 4.2 `FeatureRegistry`                 | 3                                     |
| 4.3 `FeatureSettings`                 | 2                                     |
| 4.4 `Util::FileWatch`                 | 1                                     |
| 4.5 `ImagespaceTint`                  | 5                                     |
| 4.6 Verdrahtung im Present-Hook       | 4                                     |
| 4.7 `FrameCounter`                    | 4                                     |
| 5 Zustände und Ablauf                 | 3 (Tick), 4 (Zeitpunkte)              |
| 6 Einstellungen ohne Benachrichtigung | 2 (kein Zwischenspeicher im Entwurf)  |
| 7 Fehlerbehandlung, alle fünf Fälle   | 3 (`Guarded`), 2 (Datei fehlt/kaputt) |
| 8.1 Host-Tests                        | 1, 2, 3                               |
| 8.3 Abnahmekriterien 1–5              | 6                                     |
| 10 Übergabe                           | 7                                     |
| Neue Abhängigkeit `glaze`             | 2                                     |

**Abweichungen von der Spec, bewusst und begründet**

-   Die Spec nennt in 4.6 einen Typ `FeatureSystem` mit einer Methode `Tick`. Der Plan macht daraus
    zwei freie Funktionen `StartSystem`/`TickSystem` — es gibt genau eine Instanz, und ein Typ mit
    einer Methode und ohne Zustand verdient keine Klasse.
-   Der Watcher-Thread schläft in Scheiben von 50 ms statt 500 ms am Stück. Ohne das würde
    `Shutdown` beim Umschalten bis zu eine halbe Sekunde blockieren, und zwar auf dem Render-Thread.
    Das steht so nicht in der Spec, folgt aber aus ihrer Entscheidung, im laufenden Spiel zu
    schalten.

**Typkonsistenz geprüft:** `Features::EnabledQuery` (3) ist
`std::function<bool(std::string_view)>` und passt auf die Lambda in `TickSystem` (4) und auf `Only`
im Test (3). `Util::FileWatch::Reset` (1) nimmt `std::span<const std::filesystem::path>` und wird
in `Settings::Init` (2) mit einem Array-`std::span` bedient. `Shader::PixelShaderOverride` (aus C)
bietet `Adopt`, `Install`, `Restore`, `Guard` — alle vier werden in `ImagespaceTint` (5) benutzt,
`Restore` zum ersten Mal.

**Platzhalter:** keine. Jeder Codeschritt enthält den Code, jeder Prüfschritt das erwartete
Ergebnis, jeder Mutationsschritt den vorher benannten Fehlschlag.
