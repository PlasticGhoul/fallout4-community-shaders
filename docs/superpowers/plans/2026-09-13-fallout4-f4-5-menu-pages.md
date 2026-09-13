# F4.5 — Menü in zwei Spalten: Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Das Overlay zeichnet links eine Liste der Seiten mit den Feature-Schaltern und rechts nur
die gewählte Seite; die Performance-Tabelle wird eine dieser Seiten.

**Architecture:** Ein reines Seitenmodell `Menu::PageList` (Namen hinein, Seiten und Auswahl
heraus, host-getestet) neben dem Panel; `SettingsPanel` zeichnet daraus zwei Spalten in einer
ImGui-Tabelle; `PerformancePanel` gibt Tabelle und Verlauf als Baustein `DrawPerformanceTable`
her, den die Performance-Seite einbettet, und behält den Kompaktmodus in der Ecke.

**Tech Stack:** C++23, MSVC `/W4 /WX`, ImGui 1.92 (dx11/win32-Backend), REX, glaze; Host-Tests
als `main` mit handgeschriebenem `Check`; `tools/extract-i18n.py`.

**Spec:** `docs/superpowers/specs/2026-09-13-fallout4-f4-5-menu-pages-design.md`

## Global Constraints

-   `/W4 /WX` auf unserem Ziel; fremde Warnungen eng unterdrücken, nie `/WX` lockern.
-   Jede sichtbare Zeichenkette über `T("key", "English")`; nach jeder Änderung an Schlüsseln
    `python tools/extract-i18n.py --write`, sonst fällt `python tools/extract-i18n.py`.
-   Kein ImGui in `src/Features/`; kein Feature ändert für F4.5 eine Zeile.
-   `Declare` bleibt die einzige Naht eines Features zum Overlay.
-   Bauen: `cmake --build --preset FO4` (schreibt per Deploy ins Spiel); Tests:
    `ctest --test-dir build/FO4 -C Release --output-on-failure`. In jeder PowerShell vorher
    `$env:VCPKG_ROOT = "C:\vcpkg"` und
    `$cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"`;
    `<cmake>` in den Befehlen der Tasks steht für diesen Pfad.
-   Jeder Host-Test wird nach dem Grünwerden einmal absichtlich gebrochen; vorher benennen, welche
    Prüfung fallen muß; **erst prüfen, daß der Bau geglückt ist** (eine Mutation, die nicht
    übersetzt, läßt das alte Executable stehen). Mutationen mit dem Edit-Werkzeug setzen und
    zurücknehmen, nie `git checkout <datei>`.
-   Conventional Commits, Titel ≤ 50 Zeichen, Body bei 72 umbrochen, Abschluß
    `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>`. pre-commit formatiert; nach
    „files were modified by this hook" erneut `git add` und committen.
-   Quelltext mit dem Write-/Edit-Werkzeug schreiben, nicht über die Shell (Umlaute und
    `\n`-Escapes gehen dort verloren).
-   Branch `port/f4.5-menu-pages`, Basis `dev`.

---

## Dateien

| Datei                                                           | Aufgabe                                                                |
| --------------------------------------------------------------- | ---------------------------------------------------------------------- |
| `src/Menu/PageList.h`, `src/Menu/PageList.cpp` (neu)            | Seitenmodell: feste Seiten, Featureseiten, Auswahl, Block → Seite      |
| `tests/MenuPagesTests.cpp` (neu), `CMakeLists.txt`              | Host-Test des Modells                                                  |
| `src/Menu/PerformancePanel.h`, `.cpp`                           | `DrawPerformanceTable` als Baustein, `DrawPerformanceHud` für die Ecke |
| `src/Menu/SettingsPanel.h`, `.cpp`                              | Zwei Spalten, drei Seitenarten, zweiter Parameter                      |
| `src/Menu/Overlay.cpp`                                          | Meßdaten ans Panel, kein eigenes Performance-Fenster mehr              |
| `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json` | regeneriert                                                            |
| `.claude/CLAUDE.md`                                             | Abschnitt Menu: ein Absatz zum Seitenmodell                            |
| `docs/fallout4-port/ROADMAP.md`                                 | F4.5 abgeschlossen, Abschnitt „Aus Teilprojekt F4.5 bestätigt"         |

Die Plugin-Quellen werden in `CMakeLists.txt` per `GLOB_RECURSE … CONFIGURE_DEPENDS` eingesammelt;
`PageList.cpp` braucht dort keinen Eintrag. Der Test braucht einen.

---

## Task 1: `Menu::PageList` mit Host-Test

**Files:**

-   Create: `src/Menu/PageList.h`, `src/Menu/PageList.cpp`, `tests/MenuPagesTests.cpp`
-   Modify: `CMakeLists.txt` (nach dem Block `MenuGateTests`, vor `SettingsSchemaTests`)

**Interfaces:**

-   Produces: `Menu::PageKind { kGeneral, kPerformance, kFeature }`, `Menu::Page { kind, name }`,
    `Menu::PageList` mit `SetFeatures(std::span<const std::string_view>)`, `Pages()`,
    `Selected()`, `Select(std::string_view) -> bool`, `PageOf(std::string_view) -> const Page&`,
    Konstanten `PageList::kGeneralName == "General"`, `PageList::kPerformanceName == "Performance"`.

-   [ ] **Step 1: Header schreiben**

`src/Menu/PageList.h`:

```cpp
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Menu
{
	enum class PageKind
	{
		kGeneral,
		kPerformance,
		kFeature
	};

	struct Page
	{
		PageKind kind{ PageKind::kGeneral };
		/// "General", "Performance", or the feature's name. A key, not a
		/// caption: the panel translates the fixed two.
		std::string name;
	};

	/// Which pages the overlay has and which one is open. Knows neither ImGui
	/// nor the registry: it is handed names and answers with names, which is
	/// what lets a host test drive it.
	class PageList
	{
	public:
		static constexpr std::string_view kGeneralName = "General";
		static constexpr std::string_view kPerformanceName = "Performance";

		PageList();

		/// Replaces the feature pages, keeping the fixed two in front and the
		/// order given. The selection survives if its page still exists and
		/// falls back to General otherwise.
		void SetFeatures(std::span<const std::string_view> a_names);

		[[nodiscard]] std::span<const Page> Pages() const noexcept { return _pages; }
		[[nodiscard]] const Page& Selected() const noexcept { return _pages[_selected]; }

		/// Selecting a name that is no page leaves the selection where it is
		/// and returns false. The panel never gets to point at nothing.
		bool Select(std::string_view a_name) noexcept;

		/// The page a settings block belongs to: the Performance block to the
		/// performance page, a feature's block to its page, every other block
		/// to General. The one rule the panel used to hold, now here.
		[[nodiscard]] const Page& PageOf(std::string_view a_block) const noexcept;

	private:
		static constexpr std::size_t kFixedPages = 2;
		static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

		[[nodiscard]] std::size_t IndexOf(std::string_view a_name) const noexcept;

		std::vector<Page> _pages;
		std::size_t _selected{ 0 };
	};
}
```

-   [ ] **Step 2: Test schreiben**

`tests/MenuPagesTests.cpp`:

```cpp
#include "Menu/PageList.h"

#include <array>
#include <cstdio>
#include <string_view>

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
	using Menu::PageKind;
	using Menu::PageList;

	// Fresh: the two fixed pages, General open.
	{
		PageList pages;
		Check(pages.Pages().size() == 2, "a fresh list has two pages");
		Check(pages.Pages()[0].kind == PageKind::kGeneral && pages.Pages()[0].name == "General",
			"the first is General");
		Check(pages.Pages()[1].kind == PageKind::kPerformance && pages.Pages()[1].name == "Performance",
			"the second is Performance");
		Check(pages.Selected().kind == PageKind::kGeneral, "General is open");
	}

	// Features follow the fixed pages, in the order given.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> names{ "A", "B" };
		pages.SetFeatures(names);
		Check(pages.Pages().size() == 4, "two features make four pages");
		Check(pages.Pages()[2].kind == PageKind::kFeature && pages.Pages()[2].name == "A",
			"the third is feature A");
		Check(pages.Pages()[3].name == "B", "the fourth is feature B");
		Check(pages.Selected().name == "General", "adding features leaves General open");
	}

	// Selecting by name, and refusing a name that is no page.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> names{ "A", "B" };
		pages.SetFeatures(names);
		Check(pages.Select("B"), "selecting B succeeds");
		Check(pages.Selected().name == "B", "and B is open");
		Check(!pages.Select("C"), "selecting an unknown name fails");
		Check(pages.Selected().name == "B", "and leaves B open");
		Check(pages.Select("Performance"), "the fixed pages select by their key");
		Check(pages.Selected().kind == PageKind::kPerformance, "and Performance is open");
		Check(pages.Select("General"), "General selects by its key");
		Check(pages.Selected().kind == PageKind::kGeneral, "and General is open");
	}

	// The selection survives a feature list that still holds it, and falls
	// back to General when it does not.
	{
		PageList pages;
		constexpr std::array<std::string_view, 2> two{ "A", "B" };
		constexpr std::array<std::string_view, 1> one{ "A" };
		pages.SetFeatures(two);
		pages.Select("A");
		pages.SetFeatures(two);
		Check(pages.Selected().name == "A", "a selection that still exists survives SetFeatures");
		pages.Select("B");
		pages.SetFeatures(one);
		Check(pages.Selected().kind == PageKind::kGeneral, "a selection that vanished falls back to General");
		Check(pages.Pages().size() == 3, "and the list shrank to three");
	}

	// Which page a settings block belongs to.
	{
		PageList pages;
		constexpr std::array<std::string_view, 1> names{ "A" };
		pages.SetFeatures(names);
		Check(pages.PageOf("Performance").kind == PageKind::kPerformance, "the Performance block is the performance page");
		Check(pages.PageOf("A").kind == PageKind::kFeature && pages.PageOf("A").name == "A", "a feature's block is its page");
		Check(pages.PageOf("Menu").kind == PageKind::kGeneral, "the Menu block is General");
		Check(pages.PageOf("Unknown").kind == PageKind::kGeneral, "an unknown block is General");
	}

	if (g_failures != 0) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("all checks passed\n");
	return 0;
}
```

-   [ ] **Step 3: Test in CMake eintragen**

In `CMakeLists.txt` direkt nach `add_test(NAME MenuGate COMMAND MenuGateTests)`:

```cmake
    add_executable(
        MenuPagesTests
        "${CMAKE_SOURCE_DIR}/tests/MenuPagesTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Menu/PageList.cpp"
    )

    target_include_directories(
        MenuPagesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(MenuPagesTests PRIVATE cxx_std_23)
    target_precompile_headers(
        MenuPagesTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(MenuPagesTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            MenuPagesTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME MenuPages COMMAND MenuPagesTests)
```

-   [ ] **Step 4: Bauen, Fehlschlag erwarten**

`src/Menu/PageList.cpp` existiert noch nicht. Erwartet: die Konfiguration schlägt fehl mit
„Cannot find source file: …/src/Menu/PageList.cpp". Das ist der rote Zustand dieses Tests.

-   [ ] **Step 5: Implementierung**

`src/Menu/PageList.cpp`:

```cpp
#include "Menu/PageList.h"

namespace Menu
{
	PageList::PageList() :
		_pages{
			Page{ PageKind::kGeneral, std::string{ kGeneralName } },
			Page{ PageKind::kPerformance, std::string{ kPerformanceName } }
		}
	{}

	void PageList::SetFeatures(std::span<const std::string_view> a_names)
	{
		// Copied out first: the resize below may free the string the
		// selection points at.
		const std::string selectedName = _pages[_selected].name;

		_pages.resize(kFixedPages);
		for (const auto name : a_names) {
			_pages.push_back(Page{ PageKind::kFeature, std::string{ name } });
		}

		const auto index = IndexOf(selectedName);
		_selected = index == kNone ? 0 : index;
	}

	bool PageList::Select(std::string_view a_name) noexcept
	{
		const auto index = IndexOf(a_name);
		if (index == kNone) {
			return false;
		}
		_selected = index;
		return true;
	}

	const Page& PageList::PageOf(std::string_view a_block) const noexcept
	{
		// From the second page on: General is where everything else lands,
		// so a block that happened to be called General needs no rule.
		for (std::size_t i = 1; i < _pages.size(); ++i) {
			if (_pages[i].name == a_block) {
				return _pages[i];
			}
		}
		return _pages[0];
	}

	std::size_t PageList::IndexOf(std::string_view a_name) const noexcept
	{
		for (std::size_t i = 0; i < _pages.size(); ++i) {
			if (_pages[i].name == a_name) {
				return i;
			}
		}
		return kNone;
	}
}
```

-   [ ] **Step 6: Bauen und Test laufen lassen**

```pwsh
$env:VCPKG_ROOT = "C:\vcpkg"
& "<cmake>\cmake.exe" --build --preset FO4 --target MenuPagesTests
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release -R MenuPages --output-on-failure
```

Erwartet: `MenuPages ... Passed`, 23 `ok`-Zeilen.

-   [ ] **Step 7: Drei Mutationen, je eine benannte Prüfung muß fallen**

1. In `SetFeatures` die Zeile `_selected = index == kNone ? 0 : index;` durch `_selected = 0;`
   ersetzen → „a selection that still exists survives SetFeatures" fällt.
2. In `Select` das `return false` durch `_selected = 0; return true;` ersetzen (den `if`-Block) →
   „selecting an unknown name fails" und „and leaves B open" fallen.
3. In `PageOf` die Schleife bei `i = 0` beginnen lassen → keine Prüfung fällt (General ist an
   Index 0 und ist zugleich der Rückfall). **Das ist ein Befund über den Test**, nicht über den
   Code: die Regel „General wird nie über den Namen gefunden" ist ungeprüft. Also ergänzen:

    ```cpp
    		Check(pages.PageOf("General").kind == PageKind::kGeneral, "a block called General is General, by the fallback");
    ```

    Diese Prüfung fällt mit Mutation 3 ebenfalls nicht — die Zuordnung ist dieselbe. Sie bleibt
    trotzdem, weil sie das Verhalten festhält; Mutation 3 ist damit als harmlos belegt und der
    Kommentar in `PageOf` ist der Grund für `i = 1`.

Jede Mutation: Test-Ziel bauen, **prüfen, daß der Bau geglückt ist**, Test laufen lassen, Fall
sehen, Mutation mit Edit zurücknehmen, Test wieder grün.

-   [ ] **Step 8: Commit**

```bash
git add src/Menu/PageList.h src/Menu/PageList.cpp tests/MenuPagesTests.cpp CMakeLists.txt
git commit -m "feat: a page list for the overlay, with a host test"
```

---

## Task 2: Die Performance-Tabelle als Baustein

**Files:**

-   Modify: `src/Menu/PerformancePanel.h`, `src/Menu/PerformancePanel.cpp` (der Block
    `DrawPerformancePanel`, heute Zeilen 212–242)

**Interfaces:**

-   Produces: `void Menu::DrawPerformanceTable(const PerformanceContext&)` — zeichnet in das
    aktuelle Fenster; `DrawPerformancePanel(ctx, Detail)` bleibt in diesem Task bestehen und
    ruft für `kFull` den Baustein in seinem eigenen Fenster. Verhalten unverändert.

-   [ ] **Step 1: Deklaration ergänzen**

In `src/Menu/PerformancePanel.h` vor `bool DrawPerformancePanel(...)`:

```cpp
	/// The table and the frame history, drawn into whatever window is
	/// current. The performance page of the overlay embeds it; the compact
	/// display in the corner does not.
	void DrawPerformanceTable(const PerformanceContext& a_context);
```

-   [ ] **Step 2: Körper verschieben**

In `src/Menu/PerformancePanel.cpp` `DrawPerformancePanel` ersetzen durch:

```cpp
	void DrawPerformanceTable(const PerformanceContext& a_context)
	{
		RefreshIfDue(a_context);

		if (!a_context.measuring) {
			ImGui::TextUnformatted(T("performance.paused", "Measurement is off"));
			return;
		}

		// Said here rather than in a help text nobody opens: the frame figure is
		// wall time between two presents and carries a vsync wait with it, while
		// a pass of our own is exact.
		ImGui::TextUnformatted(
			T("performance.frame_note", "Frame is wall time between presents; passes are exact."));
		ImGui::Separator();

		DrawTable();
		DrawHistory(a_context);
	}

	bool DrawPerformancePanel(const PerformanceContext& a_context, Detail a_detail)
	{
		if (a_detail == Detail::kCompact) {
			RefreshIfDue(a_context);
			return DrawCompact(a_context);
		}

		if (!ImGui::Begin(T("performance.title", "Performance"))) {
			ImGui::End();
			return false;
		}
		DrawPerformanceTable(a_context);
		ImGui::End();
		return true;
	}
```

-   [ ] **Step 3: Bauen**

`cmake --build --preset FO4 --target CommunityShadersFO4`. Erwartet: keine Warnung, `staged to …`.

-   [ ] **Step 4: Commit**

```bash
git add src/Menu/PerformancePanel.h src/Menu/PerformancePanel.cpp
git commit -m "refactor: the performance table as a building block"
```

---

## Task 3: Das Panel in zwei Spalten

**Files:**

-   Modify: `src/Menu/SettingsPanel.h` (Signatur), `src/Menu/SettingsPanel.cpp` (ab `StateText`,
    heute Zeile 152, bis Dateiende), `src/Menu/Overlay.cpp` (Zeilen 191–192)
-   Modify: `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json` (regeneriert)

**Interfaces:**

-   Consumes: `Menu::PageList` (Task 1), `Menu::DrawPerformanceTable` (Task 2),
    `Features::TheRegistry().ForEach(visit(name, state))`, `Settings::ForEachBlock`,
    `Settings::ForEachEntry`, `Fonts::Heading()`.
-   Produces: `bool Menu::DrawSettingsPanel(const PanelContext&, const PerformanceContext&)`.

-   [ ] **Step 1: Signatur**

`src/Menu/SettingsPanel.h`: `#include "Menu/PerformancePanel.h"` zu den Includes; die Deklaration
wird

```cpp
	/// One ImGui window, drawn from the settings schema and the feature
	/// registry: a list of pages on the left, the open page on the right. The
	/// performance figures come in because one of the pages shows them.
	/// Returns whether the player asked to close it - acted on by the caller,
	/// so that the button and the toggle key take the same path through the
	/// gate.
	[[nodiscard]] bool DrawSettingsPanel(
		const PanelContext& a_context,
		const PerformanceContext& a_performance);
```

-   [ ] **Step 2: Includes im Panel**

In `src/Menu/SettingsPanel.cpp` `#include "Menu/PageList.h"` und `#include <optional>` ergänzen;
`#include "Menu/PerformancePanel.h"` kommt über den Header.

-   [ ] **Step 3: Den Teil ab `StateText` ersetzen**

Alles von `const char* StateText(...)` bis zum Ende der Datei wird durch folgendes ersetzt
(`DrawFooter` ist unverändert übernommen, `FeatureNames`, `DrawGeneral`, `DrawFeature`,
`DrawFeatures` entfallen):

```cpp
		const char* StateText(Features::State a_state)
		{
			switch (a_state) {
			case Features::State::kRunning:
				return T("menu.state.running", "running");
			case Features::State::kRefused:
				// The only place a player learns that their tick did nothing.
				// Without it the refusal exists only in the log.
				return T("menu.state.refused", "refused");
			default:
				return T("menu.state.off", "off");
			}
		}

		struct FeatureRow
		{
			std::string name;
			Features::State state{ Features::State::kOff };
		};

		std::vector<FeatureRow> FeatureRows()
		{
			std::vector<FeatureRow> rows;
			Features::TheRegistry().ForEach(
				[&rows](std::string_view a_name, Features::State a_state) {
					rows.push_back(FeatureRow{ std::string{ a_name }, a_state });
				});
			return rows;
		}

		Features::State StateOf(const std::vector<FeatureRow>& a_rows, std::string_view a_name)
		{
			for (const auto& row : a_rows) {
				if (row.name == a_name) {
					return row.state;
				}
			}
			return Features::State::kOff;
		}

		/// A feature's block split the way the panel draws it: the switch goes
		/// into the list on the left, everything else onto the page.
		struct FeatureEntries
		{
			std::optional<Settings::Entry> switchEntry;
			std::vector<Settings::Entry> rest;
		};

		FeatureEntries CollectEntries(std::string_view a_block)
		{
			FeatureEntries entries;
			Settings::ForEachEntry(a_block, [&entries](const Settings::Entry& a_entry) {
				if (a_entry.isFeatureSwitch) {
					entries.switchEntry = a_entry;
				} else {
					entries.rest.push_back(a_entry);
				}
			});
			return entries;
		}

		const char* PageTitle(const Page& a_page)
		{
			switch (a_page.kind) {
			case PageKind::kGeneral:
				return T("menu.page.general", "General");
			case PageKind::kPerformance:
				return T("performance.title", "Performance");
			default:
				return a_page.name.c_str();
			}
		}

		void DrawHeading(const char* a_text)
		{
			ImGui::PushFont(Fonts::Heading(), 0.0f);
			ImGui::TextUnformatted(a_text);
			ImGui::PopFont();
		}

		void DrawFixedRow(PageList& a_pages, const Page& a_page)
		{
			const bool selected = a_pages.Selected().name == a_page.name;
			if (ImGui::Selectable(PageTitle(a_page), selected)) {
				a_pages.Select(a_page.name);
			}
		}

		void DrawFeatureRow(PageList& a_pages, const Page& a_page, Features::State a_state)
		{
			// One ID per feature, so that the label-less checkboxes of two rows
			// do not collapse into one widget.
			ImGui::PushID(a_page.name.c_str());

			const auto entries = CollectEntries(a_page.name);
			if (entries.switchEntry.has_value()) {
				// The switch itself, with no caption: the caption is the row.
				// Its help is not a tooltip here; it is the page's description.
				DrawBool(*entries.switchEntry, "##enabled");
			} else {
				// A feature that never declared its switch cannot be turned on
				// at all. An empty place keeps the names aligned.
				ImGui::Dummy(ImVec2{ ImGui::GetFrameHeight(), ImGui::GetFrameHeight() });
			}
			ImGui::SameLine();

			const char* const state = StateText(a_state);
			const float stateWidth = ImGui::CalcTextSize(state).x;
			const float rowWidth = ImGui::GetContentRegionAvail().x;
			const bool selected = a_pages.Selected().name == a_page.name;

			if (!entries.switchEntry.has_value()) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			}
			if (ImGui::Selectable(
					a_page.name.c_str(),
					selected,
					ImGuiSelectableFlags_None,
					ImVec2{ rowWidth - stateWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f })) {
				a_pages.Select(a_page.name);
			}
			if (!entries.switchEntry.has_value()) {
				ImGui::PopStyleColor();
			}

			ImGui::SameLine();
			ImGui::TextDisabled("%s", state);

			ImGui::PopID();
		}

		void DrawPageList(PageList& a_pages, const std::vector<FeatureRow>& a_rows)
		{
			ImGui::SeparatorText(T("menu.section.general", "General"));
			for (const auto& page : a_pages.Pages()) {
				if (page.kind != PageKind::kFeature) {
					DrawFixedRow(a_pages, page);
				}
			}

			ImGui::SeparatorText(T("menu.section.features", "Features"));
			for (const auto& page : a_pages.Pages()) {
				if (page.kind == PageKind::kFeature) {
					DrawFeatureRow(a_pages, page, StateOf(a_rows, page.name));
				}
			}
		}

		/// Every block that is neither a feature nor Performance. Today that is
		/// Menu alone; the point is that no feature has to register itself as
		/// having a surface - the lists are matched by name.
		void DrawGeneralPage(const PanelContext& a_context, const PageList& a_pages)
		{
			DrawHeading(T("menu.page.general", "General"));
			ImGui::Separator();
			Settings::ForEachBlock([&](std::string_view a_block) {
				if (a_pages.PageOf(a_block).kind != PageKind::kGeneral) {
					return;
				}
				Settings::ForEachEntry(a_block, [&](const Settings::Entry& a_entry) {
					DrawEntry(a_entry, a_context);
				});
			});
		}

		void DrawPerformancePage(const PanelContext& a_context, const PerformanceContext& a_performance)
		{
			DrawHeading(T("performance.title", "Performance"));
			ImGui::Separator();
			DrawPerformanceTable(a_performance);
			ImGui::Separator();
			Settings::ForEachEntry(PageList::kPerformanceName, [&](const Settings::Entry& a_entry) {
				DrawEntry(a_entry, a_context);
			});
		}

		void DrawFeaturePage(const PanelContext& a_context, const Page& a_page, Features::State a_state)
		{
			DrawHeading(a_page.name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", StateText(a_state));

			// The description is the help of the switch, which every feature
			// declares already. Nothing new to declare for a page.
			const auto entries = CollectEntries(a_page.name);
			if (!entries.switchEntry.has_value()) {
				ImGui::TextWrapped("%s",
					T("menu.no_switch", "This feature declares no switch and cannot be turned on."));
			} else if (!entries.switchEntry->helpText.empty()) {
				ImGui::TextWrapped("%s",
					Translate(entries.switchEntry->helpKey, entries.switchEntry->helpText));
			}
			ImGui::Separator();

			for (const auto& entry : entries.rest) {
				DrawEntry(entry, a_context);
			}
		}

		void DrawPage(
			const PanelContext& a_context,
			const PerformanceContext& a_performance,
			const PageList& a_pages,
			const std::vector<FeatureRow>& a_rows)
		{
			const auto& page = a_pages.Selected();
			switch (page.kind) {
			case PageKind::kGeneral:
				DrawGeneralPage(a_context, a_pages);
				break;
			case PageKind::kPerformance:
				DrawPerformancePage(a_context, a_performance);
				break;
			case PageKind::kFeature:
				DrawFeaturePage(a_context, page, StateOf(a_rows, page.name));
				break;
			}
		}

		void DrawFooter(bool& a_closeWanted)
		{
			if (ImGui::Button(T("menu.restore_defaults", "Restore defaults"))) {
				ImGui::OpenPopup("confirm-restore");
			}
			if (ImGui::BeginPopupModal(
					"confirm-restore",
					nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
				// Asked, because a change is written the moment it is made:
				// there is no "just do not save" to fall back on.
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
			ImGui::SameLine();
			if (ImGui::Button(T("menu.close", "Close"))) {
				a_closeWanted = true;
			}
		}

		/// State of the panel for the session, like the Skyrim menu's expansion
		/// states: which page is open. Not a setting, not written anywhere.
		PageList& ThePages()
		{
			static PageList pages;
			return pages;
		}
	}

	bool DrawSettingsPanel(const PanelContext& a_context, const PerformanceContext& a_performance)
	{
		bool closeWanted = false;

		// Sized to the screen on first use of a session, then left alone. ImGui
		// keeps no ini for us (io.IniFilename is null), so this is what "first
		// use" means here.
		const auto display = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSize(ImVec2{ display.x * 0.6f, display.y * 0.7f }, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowPos(
			ImVec2{ display.x * 0.5f, display.y * 0.5f }, ImGuiCond_FirstUseEver, ImVec2{ 0.5f, 0.5f });

		if (ImGui::Begin(T("menu.title", "Community Shaders"))) {
			ImGui::PushFont(Fonts::Heading(), 0.0f);
			ImGui::TextUnformatted(Plugin::NAME.data());
			ImGui::PopFont();
			ImGui::SameLine();
			// BUILD_DESCRIBE rather than the version triple: it is what answers
			// "which build is this" in a bug report, and cmake/Plugin.h.in
			// declares NAME, VERSION and this, and nothing else.
			ImGui::TextDisabled("%s", Plugin::BUILD_DESCRIBE.data());
			ImGui::Text(
				"%s %llu",
				T("menu.frame", "Frame"),
				static_cast<unsigned long long>(a_context.frame));
			ImGui::Separator();

			// The registry is constant, but the model is not to know that:
			// handed the names every frame, it keeps its selection by name.
			const auto rows = FeatureRows();
			std::vector<std::string_view> names;
			names.reserve(rows.size());
			for (const auto& row : rows) {
				names.emplace_back(row.name);
			}
			auto& pages = ThePages();
			pages.SetFeatures(names);

			// Everything above the footer is the two columns, and the footer
			// does not scroll: with forty features from F+ the buttons must not
			// walk off the bottom of the window.
			const auto footer =
				ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
			if (ImGui::BeginChild("body", ImVec2{ 0.0f, -footer })) {
				constexpr auto flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
				                       ImGuiTableFlags_SizingStretchProp;
				if (ImGui::BeginTable("columns", 2, flags)) {
					ImGui::TableSetupColumn("##pages", ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGui::TableSetupColumn("##page", ImGuiTableColumnFlags_WidthStretch, 3.0f);
					ImGui::TableNextRow();

					// Each column is a child that scrolls for itself, the list
					// as long as the features and the page as long as its
					// settings.
					ImGui::TableNextColumn();
					if (ImGui::BeginChild("pages", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y })) {
						DrawPageList(pages, rows);
					}
					ImGui::EndChild();

					ImGui::TableNextColumn();
					if (ImGui::BeginChild("page", ImVec2{ 0.0f, ImGui::GetContentRegionAvail().y })) {
						DrawPage(a_context, a_performance, pages, rows);
					}
					ImGui::EndChild();

					ImGui::EndTable();
				}
			}
			ImGui::EndChild();

			ImGui::Separator();
			DrawFooter(closeWanted);
		}
		ImGui::End();

		return closeWanted;
	}
}
```

-   [ ] **Step 4: Overlay verdrahten**

In `src/Menu/Overlay.cpp` die beiden Zeilen

```cpp
			closeWanted = DrawSettingsPanel(a_panel);
			static_cast<void>(DrawPerformancePanel(a_performance, Detail::kFull));
```

ersetzen durch

```cpp
			closeWanted = DrawSettingsPanel(a_panel, a_performance);
```

-   [ ] **Step 5: i18n regenerieren, bauen, Tests**

```pwsh
python tools/extract-i18n.py --write
& "<cmake>\cmake.exe" --build --preset FO4
& "<cmake>\ctest.exe" --test-dir build/FO4 -C Release --output-on-failure
python tools/extract-i18n.py
```

Erwartet: Bau ohne Warnung; 24 Tests grün; `en.json` um `menu.page.general`,
`menu.section.general`, `menu.section.features`, `menu.no_switch` gewachsen und
`menu.general`, `menu.features` weg (deren Aufrufer sind entfallen); die Prüfung ohne `--write`
meldet „up to date".

-   [ ] **Step 6: Commit**

```bash
git add src/Menu/SettingsPanel.h src/Menu/SettingsPanel.cpp src/Menu/Overlay.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: the overlay in two columns, a page per entry"
```

---

## Task 4: Das eigene Performance-Fenster entfernen

**Files:**

-   Modify: `src/Menu/PerformancePanel.h`, `src/Menu/PerformancePanel.cpp`, `src/Menu/Overlay.cpp`
    (Zeile mit `Detail::kCompact`)

**Interfaces:**

-   Produces: `bool Menu::DrawPerformanceHud(const PerformanceContext&)`; `Detail` und
    `DrawPerformancePanel` entfallen.

-   [ ] **Step 1: Header**

In `src/Menu/PerformancePanel.h` das `enum class Detail` und `DrawPerformancePanel` streichen,
statt dessen:

```cpp
	/// The small display in the corner while the overlay is closed: four
	/// numbers, no decoration, no input. Returns whether anything was drawn,
	/// which is what tells the overlay whether it has draw data worth handing
	/// to the backend.
	bool DrawPerformanceHud(const PerformanceContext& a_context);
```

-   [ ] **Step 2: Implementierung**

In `src/Menu/PerformancePanel.cpp` `DrawPerformancePanel` ersetzen durch

```cpp
	bool DrawPerformanceHud(const PerformanceContext& a_context)
	{
		RefreshIfDue(a_context);
		return DrawCompact(a_context);
	}
```

-   [ ] **Step 3: Overlay**

In `src/Menu/Overlay.cpp`:

```cpp
			drewHud = DrawPerformanceHud(a_performance);
```

-   [ ] **Step 4: Bauen, Tests, i18n**

Wie Task 3 Step 5. `performance.title` bleibt in `en.json`, weil die Seite ihn benutzt.

-   [ ] **Step 5: Commit**

```bash
git add src/Menu/PerformancePanel.h src/Menu/PerformancePanel.cpp src/Menu/Overlay.cpp
git commit -m "refactor: the performance window gives way to the page"
```

---

## Task 5: CLAUDE.md

**Files:**

-   Modify: `.claude/CLAUDE.md`, Abschnitt „Menu", der Absatz, der mit „`Menu::SettingsPanel`
    draws the whole overlay" beginnt.

-   [ ] **Step 1: Absatz ergänzen**

Nach dem Satz „A feature therefore gets a surface by declaring settings, and nothing else — no
ImGui in `src/Features/`, ever." einfügen:

```markdown
The overlay is two columns since F4.5: a list of pages on the left — General, Performance, then
one row per feature with its switch and state — and the open page on the right. Which pages exist
and which is open is `Menu::PageList` (`src/Menu/PageList.h`), a pure model with a host test; the
panel only draws it. A block belongs to the page of the same name, `Performance` to the
performance page, everything else to General. The description at the top of a feature's page is
the `Help` of its `DeclareFeature`, so write that help as a sentence about the effect.
`DrawPerformanceTable` is the table as a building block; the compact display in the corner is
`DrawPerformanceHud`.
```

-   [ ] **Step 2: Commit**

```bash
git add .claude/CLAUDE.md
git commit -m "docs: the overlay's page model in CLAUDE.md"
```

---

## Task 6: Abnahmelauf

Kein Code vor dem Lauf. Bauen mit `cmake --build --preset FO4` (deployt), dann Prüfliste an den
Nutzer, nummeriert, mit „was schiefgehen kann":

1. Overlay öffnen (Ende): großes Fenster mittig, zwei Spalten, links „General" mit General und
   Performance, darunter „Features" mit sieben Zeilen, je Checkbox, Name, Zustand rechts. Klein
   oder einspaltig: Fenstergröße oder Tabelle greift nicht. Die Spalten enden vor der Fußzeile:
   sonst füllt der Kindbereich die Zelle nicht (dann `GetContentRegionAvail` in der Zelle prüfen).
2. Trennlinie ziehen, Fenster verschieben und skalieren: die Spalten folgen.
3. Jede Featurezeile anklicken: rechts Name groß, Zustand daneben, die Beschreibung aus der
   Hilfe des Schalters, dann die Regler. Leere Seite: `PageOf` oder Block falsch.
4. Cloud Shadows aus der Liste ausschalten: Zustand wechselt auf off, Schatten weg; wieder an.
   Keine Wirkung: der Schalter schreibt nicht in den Store.
5. General: Sprache wechseln und zurück, Schriftgröße schieben, Toggle-Taste auf Ende neu
   belegen. Aufnahme landet im falschen Feld: `armCapture`-Pfad.
6. Performance: Tabelle mit allen Passzeilen und Verlauf, darunter die vier Einstellungen; die
   Zahlen wie vor F4.5. Tabelle fehlt: Baustein nicht gerufen.
7. Overlay schließen: HUD in der Ecke wie vorher; „Show while playing" aus und an.
8. Restore defaults mit Ja: Seite bleibt offen, Werte springen; danach Close.
9. F11: Schnappschuß im Log.

Log auswerten: keine `[W]`- und `[E]`-Zeilen, Schnappschuß mit `Overlay`-Zeile unter 0,1 ms.
Fällt ein Schritt: systematic-debugging, ein Lauf je Hypothese.

---

## Task 7: Roadmap, Erinnerungen, Abschluß

-   [ ] **Step 1: Roadmap**

In `docs/fallout4-port/ROADMAP.md`: Statuszeile (F4.5 abgeschlossen, als Nächstes F5), Tabelle
(F4.5 **abgeschlossen**), neuer Abschnitt „Aus Teilprojekt F4.5 bestätigt" nach dem F4-Abschnitt:
was übernommen wurde und was nicht, `PageList` und seine Regel, die Abnahme mit dem, was
ungeprüft blieb, die `Overlay`-Zeile des Schnappschusses, und was F5 aufwärts davon hat (die
Beschreibung ist die Hilfe des Schalters).

-   [ ] **Step 2: Commit**

```bash
git add docs/fallout4-port/ROADMAP.md
git commit -m "docs: record what F4.5 confirmed"
```

-   [ ] **Step 3: Erinnerungen**

`fallout4-port-roadmap.md`: F4.5 fertig, F5 als Nächstes, `PageList` als Andockpunkt für
Kategorien und Suche. `fallout4-engine-facts.md`: nur, falls der Lauf etwas über ImGui in Tabellen
ergab. Keine eigene Stand-Notiz, falls nichts offen bleibt.

-   [ ] **Step 4: Branch abschließen**

finishing-a-development-branch: volle Testsuite, Paket (`--target package`, dann
`verify-package.ps1`), `verify-plugin.ps1`, `extract-i18n.py`; Menü per AskUserQuestion; nach
Wahl des Nutzers Fast-Forward nach `dev`, Branch löschen, Push nur auf Ansage.

---

## Reihenfolge

```
1 PageList ─┐
            ├─► 3 Panel ─► 4 Fenster weg ─► 5 CLAUDE.md ─► 6 Lauf ─► 7 Abschluß
2 Baustein ─┘
```

Tasks 1 und 2 hängen nicht voneinander ab. Ein Spielstart.
