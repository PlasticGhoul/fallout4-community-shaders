# Teilprojekt F1 — Performance Overlay: Implementierungsplan

> **Für agentische Bearbeiter:** ERFORDERLICHE UNTER-SKILL:
> `superpowers:subagent-driven-development` (empfohlen) oder
> `superpowers:executing-plans`, um diesen Plan Aufgabe für Aufgabe umzusetzen. Die Schritte
> benutzen Kästchen (`- [ ]`) zur Verfolgung.

**Ziel:** Eine Zeitmessung je benanntem Pass in CPU- und GPU-Zeit, ablesbar als HUD im laufenden
Spiel und als Tabelle im Overlay, mit einer Taste, die eine Momentaufnahme ins Log schreibt.

**Architektur:** Der rechnende Teil (Ringpuffer, Perzentile, Namensbuchhaltung) liegt ohne D3D in
`Render::PassStatistics` und ist damit ohne Spiel prüfbar. `Render::Profiler` setzt darauf die
D3D11-Timestamp-Queries, einen Ring aus drei Frame-Plätzen und einen Stapel offener Pässe. Die
Frame-Grenze liegt im `Present`-Hook; eine einzige Klammer in `Features::Registry` misst jedes
Feature. Gezeichnet wird in `src/Menu/` aus einer Ergebnisliste, die die Tafel gereicht bekommt —
kein D3D im Menü, kein ImGui im Renderer.

**Technikstapel:** C++23, MSVC, `/W4 /WX`. CommonLibF4 (REX, F4SE), D3D11 über `REX::W32`, ImGui
1.92.6 mit `dx11-binding` und `win32-binding`. Tests sind eigenständige ausführbare Dateien mit
einem handgeschriebenen `Check()` und `ctest`.

**Spec:** `docs/superpowers/specs/2026-09-05-fallout4-f1-performance-overlay-design.md`

## Globale Randbedingungen

-   **Branch:** `port/f1-performance-overlay`, bereits angelegt. `dev` wird nicht direkt bebaut.
-   **C++23, MSVC, `/W4 /WX`** für unser Ziel. Eine neue Warnung aus einem fremden Header wird eng
    auf unserem Ziel unterdrückt, mit Kommentar, der den Header nennt — niemals durch Lockern von
    `/WX`.
-   **`<d3d11.h>` ist verboten.** D3D-Typen kommen aus `REX/W32/D3D11.h`; der PCH bringt nur `REL`
    und `REX` mit, wer D3D-Typen nennt bindet den Header selbst ein.
-   **`<Windows.h>` ist verboten.** Fehlende USER32- und KERNEL32-Deklarationen gehören nach
    `src/Menu/Win32.h`.
-   **Jede sichtbare Zeichenkette** geht durch `T("key", "English")`, jede Beschriftung durch
    `.Label(…)` / `.Help(…)`. Danach `python tools/extract-i18n.py --write`, und der Lauf **ohne**
    `--write` muss fehlerfrei sein.
-   **Keine Platzhalter, kein TODO, keine Teilimplementierung.** Kommentare erklären das Warum.
-   **Conventional Commits**, Titel höchstens 50 Zeichen, Rumpf auf 72 umgebrochen.
-   **Bauen:** `cmake --build --preset FO4`, einzelne Ziele mit `--target <Ziel>`. Vorher in jeder
    Sitzung `$env:VCPKG_ROOT = "C:\vcpkg"`. `cmake` liegt unter
    `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\`.
-   **Neue Quelldateien** werden vom `GLOB_RECURSE` des Hauptziels erfasst; neue **Testziele** sind
    einzeln in `CMakeLists.txt` einzutragen. Nach beidem einmal neu konfigurieren.
-   **Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen**, mit vorher benannter
    Erwartung — und **vorher wird geprüft, dass der Bau geglückt ist**: eine Mutation, die sich
    nicht übersetzen lässt, hinterlässt das alte Executable, und der Test sieht dann fälschlich
    grün aus.

---

## Aufgabenübersicht

| #   | Aufgabe                                      | Prüfung                          |
| --- | -------------------------------------------- | -------------------------------- |
| 1   | `PassStatistics` — Ringpuffer und Perzentile | Host-Test                        |
| 2   | `PassTable` — Namen, Tiefe, Ausmustern       | Host-Test                        |
| 3   | `KeyLatch` — Taste über Threadgrenze         | Host-Test                        |
| 4   | `Profiler` und `PassScope` — die D3D-Hälfte  | baut unter `/W4 /WX`             |
| 5   | Einhängen in `Present` und in die Registry   | baut, Log zeigt Pässe            |
| 6   | Einstellungsblock `Performance` und Katalog  | `extract-i18n.py` ohne `--write` |
| 7   | `PerformancePanel`, HUD und Tafel            | **Spiellauf 1**                  |
| 8   | Logtaste und Momentaufnahme                  | **Spiellauf 2, Abnahme**         |
| 9   | Befund in die Roadmap, Branch abschließen    | `ctest`, `verify-plugin.ps1`     |

**Zwei Spielläufe, nicht einer.** Lauf 1 kommt so früh wie möglich, weil er die riskanteste
Annahme der Spec prüft: dass ein `NoInputs`-Fenster bei geschlossenem Overlay dem Systemzeiger
nicht in die Quere kommt. Nach dem, was ImGuis Klickposition in E2 gekostet hat, wird das nicht
ans Ende gelegt.

---

### Aufgabe 1: `PassStatistics` — Ringpuffer und Perzentile

**Dateien:**

-   Anlegen: `src/Render/PassStatistics.h`, `src/Render/PassStatistics.cpp`
-   Anlegen: `tests/ProfilerStatsTests.cpp`
-   Ändern: `CMakeLists.txt` (neues Testziel `ProfilerStatsTests`)

**Schnittstellen:**

-   Verbraucht: nichts.
-   Liefert: `Render::PassStatistics` mit `Push(float)`, `Last()`, `Average()`, `Percentile(float)`,
    `Count()`, `Sample(std::size_t)`, `Clear()`. Aufgabe 2 und 4 bauen darauf.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

`tests/ProfilerStatsTests.cpp`:

```cpp
#include "Render/PassStatistics.h"

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

	bool Near(float a_value, float a_expected)
	{
		const auto difference = a_value > a_expected ? a_value - a_expected : a_expected - a_value;
		return difference < 0.001f;
	}
}

int main()
{
	{
		Render::PassStatistics stats;
		Check(stats.Count() == 0, "a fresh history is empty");
		Check(stats.Average() == 0.0f, "an empty history averages zero");
		Check(stats.Percentile(50.0f) == 0.0f, "an empty history has no median");
	}

	{
		Render::PassStatistics stats;
		stats.Push(2.0f);
		Check(stats.Count() == 1, "one sample counts as one");
		Check(Near(stats.Last(), 2.0f), "the last sample is the one pushed");
		Check(Near(stats.Average(), 2.0f), "one sample averages itself");
		Check(Near(stats.Percentile(0.0f), 2.0f), "p0 of one sample is that sample");
		Check(Near(stats.Percentile(100.0f), 2.0f), "p100 of one sample is that sample");
	}

	{
		Render::PassStatistics stats;
		for (int i = 1; i <= 4; ++i) {
			stats.Push(static_cast<float>(i));
		}
		Check(Near(stats.Average(), 2.5f), "four samples average correctly");
		Check(Near(stats.Percentile(0.0f), 1.0f), "p0 is the smallest sample");
		Check(Near(stats.Percentile(100.0f), 4.0f), "p100 is the largest sample");

		// Rank 0.5 * 3 = 1.5, so halfway between the second and third sample.
		Check(Near(stats.Percentile(50.0f), 2.5f), "the median interpolates between neighbours");
	}

	{
		// The samples arrive unsorted; a percentile has to sort them.
		Render::PassStatistics stats;
		stats.Push(9.0f);
		stats.Push(1.0f);
		stats.Push(5.0f);
		Check(Near(stats.Percentile(0.0f), 1.0f), "p0 finds the smallest whatever the order");
		Check(Near(stats.Percentile(100.0f), 9.0f), "p100 finds the largest whatever the order");
	}

	{
		// One more than the buffer holds: the oldest has to fall out, and the
		// order the plot reads has to survive the wrap.
		Render::PassStatistics stats;
		for (std::size_t i = 0; i < Render::PassStatistics::kHistorySize + 1; ++i) {
			stats.Push(static_cast<float>(i));
		}
		Check(stats.Count() == Render::PassStatistics::kHistorySize, "the buffer stops at its size");
		Check(Near(stats.Sample(0), 1.0f), "the oldest sample fell out");
		Check(
			Near(stats.Sample(Render::PassStatistics::kHistorySize - 1),
				static_cast<float>(Render::PassStatistics::kHistorySize)),
			"the newest sample is last");
		Check(Near(stats.Last(), static_cast<float>(Render::PassStatistics::kHistorySize)),
			"the last sample is the newest");
	}

	{
		Render::PassStatistics stats;
		stats.Push(1.0f);
		stats.Clear();
		Check(stats.Count() == 0, "clearing empties the history");
		Check(stats.Last() == 0.0f, "clearing forgets the last sample");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Schritt 2: Testziel eintragen**

In `CMakeLists.txt`, direkt vor der schließenden `endif()` des Testblocks, nach dem Muster von
`ShaderLayoutTests`:

```cmake
    add_executable(
        ProfilerStatsTests
        "${CMAKE_SOURCE_DIR}/tests/ProfilerStatsTests.cpp"
        "${CMAKE_SOURCE_DIR}/src/Render/PassStatistics.cpp"
    )

    target_include_directories(
        ProfilerStatsTests
        PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/include"
    )
    target_compile_features(ProfilerStatsTests PRIVATE cxx_std_23)
    target_precompile_headers(
        ProfilerStatsTests
        PRIVATE "${CMAKE_SOURCE_DIR}/include/PCH.h"
    )
    target_link_libraries(ProfilerStatsTests PRIVATE CommonLibF4::CommonLibF4)

    if(MSVC)
        target_compile_options(
            ProfilerStatsTests
            PRIVATE /W4 /WX /permissive- /utf-8 /Zc:preprocessor
        )
    endif()

    add_test(NAME ProfilerStats COMMAND ProfilerStatsTests)
```

-   [ ] **Schritt 3: Bauen und den Fehlschlag sehen**

```
cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
cmake --build --preset FO4 --target ProfilerStatsTests
```

Erwartet: **Übersetzungsfehler**, `Render/PassStatistics.h` existiert nicht.

-   [ ] **Schritt 4: Die Kopfdatei schreiben**

`src/Render/PassStatistics.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>

namespace Render
{
	/// A pass's timings over the recent past, as a ring of samples.
	///
	/// Deliberately free of D3D: this is the half of the profiler that
	/// computes, and computing is the half that can be tested without a game.
	class PassStatistics
	{
	public:
		/// Three hundred samples is about 1.7 seconds at 180 fps and five at
		/// 60. Long enough for a p99 to mean something, short enough that
		/// walking into a new cell shows up rather than being averaged away.
		static constexpr std::size_t kHistorySize = 300;

		void Push(float a_ms) noexcept;
		void Clear() noexcept;

		[[nodiscard]] float Last() const noexcept { return _last; }
		[[nodiscard]] std::size_t Count() const noexcept { return _count; }

		[[nodiscard]] float Average() const noexcept;

		/// Interpolated, with a_p in [0, 100]. Zero when there is nothing to
		/// take a percentile of.
		[[nodiscard]] float Percentile(float a_p) const noexcept;

		/// Oldest first, which is the order a plot wants. Zero past the end.
		[[nodiscard]] float Sample(std::size_t a_index) const noexcept;

	private:
		std::array<float, kHistorySize> _history{};
		std::size_t _head{ 0 };
		std::size_t _count{ 0 };
		float _last{ 0.0f };
	};
}
```

-   [ ] **Schritt 5: Die Umsetzung schreiben**

`src/Render/PassStatistics.cpp`:

```cpp
#include "Render/PassStatistics.h"

#include <algorithm>
#include <cmath>

namespace Render
{
	void PassStatistics::Push(float a_ms) noexcept
	{
		_history[_head] = a_ms;
		_head = (_head + 1) % kHistorySize;
		if (_count < kHistorySize) {
			++_count;
		}

		_last = a_ms;
	}

	void PassStatistics::Clear() noexcept
	{
		_head = 0;
		_count = 0;
		_last = 0.0f;
	}

	float PassStatistics::Average() const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		float total = 0.0f;
		for (std::size_t i = 0; i < _count; ++i) {
			total += Sample(i);
		}

		return total / static_cast<float>(_count);
	}

	float PassStatistics::Percentile(float a_p) const noexcept
	{
		if (_count == 0) {
			return 0.0f;
		}

		// Sorted on demand rather than kept sorted: this runs once per pass per
		// drawn frame, over at most three hundred floats, and a second ordered
		// structure would have to be kept right on every push.
		std::array<float, kHistorySize> sorted{};
		for (std::size_t i = 0; i < _count; ++i) {
			sorted[i] = Sample(i);
		}
		std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(_count));

		const float clamped = std::clamp(a_p, 0.0f, 100.0f);
		const float rank = clamped / 100.0f * static_cast<float>(_count - 1);
		const auto lower = static_cast<std::size_t>(std::floor(rank));
		const auto upper = std::min(lower + 1, _count - 1);
		const float fraction = rank - static_cast<float>(lower);

		return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
	}

	float PassStatistics::Sample(std::size_t a_index) const noexcept
	{
		if (a_index >= _count) {
			return 0.0f;
		}

		// _head points at the next slot to write, so the oldest sample sits
		// _count places behind it.
		const std::size_t oldest = (_head + kHistorySize - _count) % kHistorySize;
		return _history[(oldest + a_index) % kHistorySize];
	}
}
```

-   [ ] **Schritt 6: Bauen und laufen lassen**

```
cmake --build --preset FO4 --target ProfilerStatsTests
build\FO4\Release\ProfilerStatsTests.exe
```

Erwartet: **alle Prüfungen grün**, keine Warnung.

-   [ ] **Schritt 7: Den Test absichtlich brechen**

Zwei Mutationen, jeweils bauen, prüfen dass der Bau geglückt ist, laufen lassen, zurücknehmen:

1. In `Sample`, `oldest` durch `0` ersetzen. **Erwartet:** „the oldest sample fell out" und
   „the newest sample is last" fallen; die Prüfungen ohne Umlauf bleiben grün.
2. In `Percentile`, `fraction` durch `0.0f` ersetzen. **Erwartet:** nur „the median interpolates
   between neighbours" fällt.

-   [ ] **Schritt 8: Committen**

```
git add src/Render/PassStatistics.h src/Render/PassStatistics.cpp tests/ProfilerStatsTests.cpp CMakeLists.txt
git commit -m "feat: keep a pass's recent timings"
```

---

### Aufgabe 2: `PassTable` — Namen, Tiefe, Ausmustern

**Dateien:**

-   Ändern: `src/Render/PassStatistics.h`, `src/Render/PassStatistics.cpp`
-   Ändern: `tests/ProfilerStatsTests.cpp`

**Schnittstellen:**

-   Verbraucht: `Render::PassStatistics` aus Aufgabe 1.
-   Liefert: `Render::PassResult` und `Render::PassTable` mit `Find(std::string_view) -> std::size_t`,
    `kNoPass`, `Sample(std::size_t, std::uint32_t, float, float, std::uint64_t)`,
    `Retire(std::uint64_t)`, `Results() -> std::span<const PassResult>`, `Clear()`. Aufgabe 4 und 7
    bauen darauf.

**Warum die Tabelle Namen statt Indizes nach außen gibt:** Der Profiler merkt sich in einem
Frame-Platz, welcher Pass gemessen wurde, und liest ihn drei Frames später aus. Läuft dazwischen
ein Ausmustern, verschieben sich Indizes — der Wert landete dann auf dem falschen Pass. Deshalb
speichert ein Frame-Platz den **Namen**, und der Index wird erst beim Einsammeln bestimmt.

-   [ ] **Schritt 1: Den fehlschlagenden Test ergänzen**

In `tests/ProfilerStatsTests.cpp`, vor der Schlusszeile, einfügen:

```cpp
	{
		Render::PassTable table;
		const auto frame = table.Find("Frame");
		const auto overlay = table.Find("Overlay");

		Check(frame != Render::PassTable::kNoPass, "a new name gets a slot");
		Check(frame != overlay, "two names get two slots");
		Check(table.Find("Frame") == frame, "a known name gets its own slot back");

		table.Sample(frame, 0, 5.0f, 3.0f, 1);
		table.Sample(overlay, 1, 0.2f, 0.1f, 1);

		const auto results = table.Results();
		Check(results.size() == 2, "both passes are reported");
		Check(results[0].name == "Frame", "the first pass seen is reported first");
		Check(results[0].depth == 0 && results[1].depth == 1, "the depth is carried through");
		Check(Near(results[0].gpuMs, 5.0f), "the gpu time is carried through");
		Check(Near(results[1].cpuMs, 0.1f), "the cpu time is carried through");
	}

	{
		// The case that costs an afternoon when it is wrong: a pass drops out,
		// and the pass behind it must still find its own history.
		Render::PassTable table;
		const auto first = table.Find("First");
		const auto second = table.Find("Second");
		table.Sample(first, 0, 1.0f, 1.0f, 1);
		table.Sample(second, 0, 2.0f, 2.0f, 1);

		// Only the second keeps reporting.
		for (std::uint64_t frame = 2; frame <= Render::PassTable::kRetireFrames + 2; ++frame) {
			table.Sample(table.Find("Second"), 0, 2.0f, 2.0f, frame);
			table.Retire(frame);
		}

		const auto results = table.Results();
		Check(results.size() == 1, "a pass that stopped reporting is retired");
		Check(results.size() == 1 && results[0].name == "Second", "the right pass survived");
		Check(Near(results[0].gpuMs, 2.0f), "the survivor kept its own history");

		const auto again = table.Find("Second");
		Check(again != Render::PassTable::kNoPass, "the survivor is still addressable");
		Check(Near(table.Results()[0].avgMs, 2.0f), "and still averages its own samples");
	}

	{
		Render::PassTable table;
		for (std::size_t i = 0; i < Render::PassTable::kMaxPasses; ++i) {
			const auto name = std::string{ "pass" } + std::to_string(i);
			Check(table.Find(name) != Render::PassTable::kNoPass || i == 0, "every slot up to the cap is handed out");
		}

		Check(table.Find("one too many") == Render::PassTable::kNoPass, "the cap refuses the next name");
		Check(table.Find("pass0") != Render::PassTable::kNoPass, "a known name still works at the cap");
	}
```

Dafür oben `#include <string>` ergänzen.

-   [ ] **Schritt 2: Bauen und den Fehlschlag sehen**

```
cmake --build --preset FO4 --target ProfilerStatsTests
```

Erwartet: **Übersetzungsfehler**, `Render::PassTable` ist unbekannt.

-   [ ] **Schritt 3: Die Erweiterung der Kopfdatei schreiben**

An `src/Render/PassStatistics.h` anhängen, innerhalb von `namespace Render`, dazu
`#include <cstdint>`, `#include <span>`, `#include <string>`, `#include <string_view>`,
`#include <unordered_map>`, `#include <vector>`:

```cpp
	/// What one pass looked like in the frame just collected.
	struct PassResult
	{
		std::string name;
		std::uint32_t depth{ 0 };

		float gpuMs{ 0.0f };
		float cpuMs{ 0.0f };
		float avgMs{ 0.0f };
		float p95Ms{ 0.0f };
		float p99Ms{ 0.0f };

		/// The GPU history, for the plot. Owned by the table, valid until the
		/// next Retire.
		const PassStatistics* history{ nullptr };
	};

	/// Every pass the profiler has seen lately, in the order it first saw them.
	///
	/// Order matters: passes open in the same order every frame, so first-seen
	/// order is tree order, and the table reads as the tree it is.
	class PassTable
	{
	public:
		static constexpr std::size_t kMaxPasses = 128;
		static constexpr std::size_t kNoPass = static_cast<std::size_t>(-1);

		/// A pass that has not been sampled for this many frames leaves the
		/// table, so a switched-off feature stops showing its last number.
		static constexpr std::uint64_t kRetireFrames = 60;

		/// The slot for a name, creating it if there is room. kNoPass once the
		/// cap is reached, which is refused once and logged by the caller.
		[[nodiscard]] std::size_t Find(std::string_view a_name) noexcept;

		void Sample(
			std::size_t a_index,
			std::uint32_t a_depth,
			float a_gpuMs,
			float a_cpuMs,
			std::uint64_t a_frame) noexcept;

		void Retire(std::uint64_t a_frame) noexcept;
		void Clear() noexcept;

		[[nodiscard]] std::span<const PassResult> Results() const noexcept { return _results; }

	private:
		struct Entry
		{
			std::string name;
			std::uint32_t depth{ 0 };
			PassStatistics gpu;
			PassStatistics cpu;
			std::uint64_t lastFrame{ 0 };
		};

		/// Repoints the name index at the current positions. Erasing shifts
		/// everything behind the hole, so the map has to be rebuilt rather than
		/// patched.
		void RebuildIndex() noexcept;
		void RebuildResults() noexcept;

		std::vector<Entry> _entries;
		std::unordered_map<std::string, std::size_t> _index;
		std::vector<PassResult> _results;
	};
```

-   [ ] **Schritt 4: Die Umsetzung schreiben**

An `src/Render/PassStatistics.cpp` anhängen:

```cpp
	std::size_t PassTable::Find(std::string_view a_name) noexcept
	{
		if (const auto found = _index.find(std::string{ a_name }); found != _index.end()) {
			return found->second;
		}

		if (_entries.size() >= kMaxPasses) {
			return kNoPass;
		}

		_entries.push_back(Entry{ std::string{ a_name } });
		const auto index = _entries.size() - 1;
		_index.emplace(_entries.back().name, index);
		return index;
	}

	void PassTable::Sample(
		std::size_t a_index,
		std::uint32_t a_depth,
		float a_gpuMs,
		float a_cpuMs,
		std::uint64_t a_frame) noexcept
	{
		if (a_index >= _entries.size()) {
			return;
		}

		auto& entry = _entries[a_index];
		entry.depth = a_depth;
		entry.gpu.Push(a_gpuMs);
		entry.cpu.Push(a_cpuMs);
		entry.lastFrame = a_frame;

		RebuildResults();
	}

	void PassTable::Retire(std::uint64_t a_frame) noexcept
	{
		const auto before = _entries.size();
		std::erase_if(_entries, [&](const Entry& a_entry) {
			return a_frame > a_entry.lastFrame &&
			       a_frame - a_entry.lastFrame > kRetireFrames;
		});

		if (_entries.size() == before) {
			return;
		}

		RebuildIndex();
		RebuildResults();
	}

	void PassTable::Clear() noexcept
	{
		_entries.clear();
		_index.clear();
		_results.clear();
	}

	void PassTable::RebuildIndex() noexcept
	{
		_index.clear();
		for (std::size_t i = 0; i < _entries.size(); ++i) {
			_index.emplace(_entries[i].name, i);
		}
	}

	void PassTable::RebuildResults() noexcept
	{
		_results.clear();
		_results.reserve(_entries.size());

		for (const auto& entry : _entries) {
			_results.push_back(PassResult{
				entry.name,
				entry.depth,
				entry.gpu.Last(),
				entry.cpu.Last(),
				entry.gpu.Average(),
				entry.gpu.Percentile(95.0f),
				entry.gpu.Percentile(99.0f),
				std::addressof(entry.gpu) });
		}
	}
```

-   [ ] **Schritt 5: Bauen und laufen lassen**

```
cmake --build --preset FO4 --target ProfilerStatsTests
build\FO4\Release\ProfilerStatsTests.exe
```

Erwartet: **alle Prüfungen grün**.

-   [ ] **Schritt 6: Den Test absichtlich brechen**

1. In `Retire` den Aufruf `RebuildIndex()` entfernen. **Erwartet:** „the survivor kept its own
   history" oder „and still averages its own samples" fällt — das ist genau der Fehler, für den
   die Vorlage einen `RebuildTimerIndex` hat.
2. In `Find` die Prüfung gegen `kMaxPasses` entfernen. **Erwartet:** „the cap refuses the next
   name" fällt.

-   [ ] **Schritt 7: Committen**

```
git add src/Render/PassStatistics.h src/Render/PassStatistics.cpp tests/ProfilerStatsTests.cpp
git commit -m "feat: keep every pass the profiler has seen"
```

---

### Aufgabe 3: `KeyLatch` — eine Taste über die Threadgrenze

**Dateien:**

-   Anlegen: `src/Menu/KeyLatch.h`, `src/Menu/KeyLatch.cpp`
-   Anlegen: `tests/KeyLatchTests.cpp`
-   Ändern: `CMakeLists.txt` (Testziel `KeyLatchTests`)

**Schnittstellen:**

-   Verbraucht: nichts.
-   Liefert: `Menu::KeyLatch` mit `SetKey(std::uint32_t)`, `Key()`, `Offer(std::uint32_t)`,
    `Take() -> bool`. Aufgabe 8 benutzt es.

-   [ ] **Schritt 1: Den fehlschlagenden Test schreiben**

`tests/KeyLatchTests.cpp`:

```cpp
#include "Menu/KeyLatch.h"

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
	{
		Menu::KeyLatch latch;
		Check(!latch.Take(), "a fresh latch has nothing to take");
	}

	{
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x7A);
		Check(latch.Take(), "the bound key latches");
		Check(!latch.Take(), "and is taken only once");
	}

	{
		// Key repeat sends the same press over and over. One press, one action.
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x7A);
		latch.Offer(0x7A);
		latch.Offer(0x7A);
		Check(latch.Take(), "repeats latch");
		Check(!latch.Take(), "repeats collapse into one");
	}

	{
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x70);
		Check(!latch.Take(), "another key does not latch");
	}

	{
		// A settings file naming no key must not fire on every keystroke.
		Menu::KeyLatch latch;
		latch.SetKey(0);
		latch.Offer(0);
		Check(!latch.Take(), "key zero matches nothing");
		latch.Offer(0x7A);
		Check(!latch.Take(), "an unbound latch takes nothing");
	}

	{
		// The key is read from the settings every frame, so it can change while
		// a press is pending. The press was legitimate when it happened.
		Menu::KeyLatch latch;
		latch.SetKey(0x7A);
		latch.Offer(0x7A);
		latch.SetKey(0x70);
		Check(latch.Take(), "a pending press survives a rebind");
	}

	std::printf("\n%s\n", g_failures == 0 ? "all checks passed" : "checks failed");
	return g_failures == 0 ? 0 : 1;
}
```

-   [ ] **Schritt 2: Testziel eintragen**

Wie in Aufgabe 1, mit `KeyLatchTests`, `tests/KeyLatchTests.cpp`, `src/Menu/KeyLatch.cpp` und
`add_test(NAME KeyLatch COMMAND KeyLatchTests)`.

-   [ ] **Schritt 3: Bauen und den Fehlschlag sehen**

```
cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
cmake --build --preset FO4 --target KeyLatchTests
```

Erwartet: **Übersetzungsfehler**, `Menu/KeyLatch.h` existiert nicht.

-   [ ] **Schritt 4: Die Umsetzung schreiben**

`src/Menu/KeyLatch.h`:

```cpp
#pragma once

#include <atomic>
#include <cstdint>

namespace Menu
{
	/// One key press, seen on the window thread and acted on once by the render
	/// thread.
	///
	/// The small sibling of Gate: same split across the two threads, without
	/// the state machine, because a hotkey that does something has nothing to
	/// open or close. Gate stays about the overlay.
	class KeyLatch
	{
	public:
		/// From the render thread, where the settings are read.
		void SetKey(std::uint32_t a_key) noexcept;
		[[nodiscard]] std::uint32_t Key() const noexcept;

		/// From the window thread, for every key press that nothing above took.
		/// A key of zero matches nothing, so a settings file naming no key is
		/// silent rather than triggering on every keystroke.
		void Offer(std::uint32_t a_key) noexcept;

		/// From the render thread. True once per press.
		[[nodiscard]] bool Take() noexcept;

	private:
		std::atomic<std::uint32_t> _key{ 0 };
		std::atomic<bool> _pending{ false };
	};
}
```

`src/Menu/KeyLatch.cpp`:

```cpp
#include "Menu/KeyLatch.h"

namespace Menu
{
	void KeyLatch::SetKey(std::uint32_t a_key) noexcept
	{
		_key.store(a_key, std::memory_order_relaxed);
	}

	std::uint32_t KeyLatch::Key() const noexcept
	{
		return _key.load(std::memory_order_relaxed);
	}

	void KeyLatch::Offer(std::uint32_t a_key) noexcept
	{
		const auto wanted = _key.load(std::memory_order_relaxed);
		if (wanted == 0 || a_key != wanted) {
			return;
		}

		_pending.store(true, std::memory_order_relaxed);
	}

	bool KeyLatch::Take() noexcept
	{
		return _pending.exchange(false, std::memory_order_relaxed);
	}
}
```

-   [ ] **Schritt 5: Bauen und laufen lassen**

```
cmake --build --preset FO4 --target KeyLatchTests
build\FO4\Release\KeyLatchTests.exe
```

Erwartet: **alle Prüfungen grün**.

-   [ ] **Schritt 6: Den Test absichtlich brechen**

1. In `Take` `exchange` durch `load` ersetzen. **Erwartet:** „and is taken only once" und
   „repeats collapse into one" fallen.
2. In `Offer` die Prüfung `wanted == 0` entfernen. **Erwartet:** „key zero matches nothing" fällt.

-   [ ] **Schritt 7: Committen**

```
git add src/Menu/KeyLatch.h src/Menu/KeyLatch.cpp tests/KeyLatchTests.cpp CMakeLists.txt
git commit -m "feat: carry one key press across the threads"
```

---

### Aufgabe 4: `Profiler` und `PassScope` — die D3D-Hälfte

**Dateien:**

-   Anlegen: `src/Render/Profiler.h`, `src/Render/Profiler.cpp`
-   Ändern: `src/Render/Markers.h`, `src/Render/Markers.cpp` (`Push`/`Pop` freilegen,
    `MarkerScope` darauf umstellen)

**Schnittstellen:**

-   Verbraucht: `Render::PassTable`, `Render::PassResult` (Aufgabe 2); `Render::GetDevice()`,
    `Render::GetContext()` (`src/Render/Renderer.h`); `Render::MarkerScope` (`src/Render/Markers.h`).
-   Liefert: `Render::Profiler::GetSingleton()`, `Initialize()`, `Release()`, `BeginFrame()`,
    `EndFrame()`, `Collect()`, `BeginPass(std::string_view)`, `EndPass()`,
    `Results() -> std::span<const PassResult>`, `IsMeasuring() -> bool`, `LogSnapshot()`,
    `FrameGpuMs()`, `FrameCpuMs()`, `DiscardedFrames()`. Dazu `Render::PassScope`.

**Diese Aufgabe hat keinen Host-Test.** Sie hängt an einem echten Gerät; geprüft wird sie durch
den Bau unter `/W4 /WX` und ab Aufgabe 5 im Log. Das steht so in der Spec, Abschnitt 7.2.

-   [ ] **Schritt 1: Die Kopfdatei schreiben**

`src/Render/Profiler.h`:

```cpp
#pragma once

#include "Render/PassStatistics.h"

#include <REX/W32/D3D11.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Render
{
	/// CPU and GPU time per named pass, through D3D11 timestamp queries.
	///
	/// A frame is Present to Present, because that is the one point per frame
	/// where we hold control. The frame figure is therefore wall time and
	/// includes a vsync wait if one is pending; a pass of our own is exact,
	/// because its timestamps sit immediately around our own calls.
	class Profiler
	{
	public:
		/// Three frames in flight is what a GPU is usually behind by. Asking
		/// for a result sooner would mean waiting for it.
		static constexpr std::size_t kFrameLatency = 3;

		/// A slot still not ready after twice around the ring is dropped rather
		/// than waited for.
		static constexpr std::uint64_t kSlotPatience = 2 * kFrameLatency;

		[[nodiscard]] static Profiler& GetSingleton() noexcept;

		/// Idempotent. A failure is logged once and never retried: creating a
		/// query that failed once will fail again.
		bool Initialize() noexcept;
		void Release() noexcept;

		/// Whether queries are being issued at all - false while refused, and
		/// false while the setting is off.
		[[nodiscard]] bool IsMeasuring() const noexcept { return _measuring; }
		void SetMeasuring(bool a_measuring) noexcept;

		void BeginFrame() noexcept;
		void EndFrame() noexcept;

		/// Reads back whatever is ready, without ever blocking the render
		/// thread, and moves the histories forward.
		void Collect() noexcept;

		void BeginPass(std::string_view a_name) noexcept;
		void EndPass() noexcept;

		[[nodiscard]] std::span<const PassResult> Results() const noexcept
		{
			return _passes.Results();
		}

		[[nodiscard]] float FrameGpuMs() const noexcept { return _frameGpuMs; }
		[[nodiscard]] float FrameCpuMs() const noexcept { return _frameCpuMs; }
		[[nodiscard]] std::uint64_t DiscardedFrames() const noexcept { return _discarded; }

		/// Writes the current histories to the log, in the format that goes
		/// into the roadmap.
		void LogSnapshot() const noexcept;

	private:
		struct Timing
		{
			std::string name;
			std::uint32_t depth{ 0 };
			REX::W32::ID3D11Query* begin{ nullptr };
			REX::W32::ID3D11Query* end{ nullptr };
			std::int64_t cpuBegin{ 0 };
			float cpuMs{ 0.0f };
		};

		struct Slot
		{
			REX::W32::ID3D11Query* disjoint{ nullptr };

			// The name rather than the index: a retire between issuing and
			// collecting shifts indices, and the sample would land on another
			// pass.
			std::vector<Timing> timings;
			std::uint64_t frame{ 0 };
			bool inFlight{ false };
		};

		[[nodiscard]] bool CreateSlot(Slot& a_slot) noexcept;
		void ReleaseSlot(Slot& a_slot) noexcept;
		[[nodiscard]] REX::W32::ID3D11Query* AcquireTimestamp() noexcept;
		void CollectSlot(Slot& a_slot) noexcept;

		std::array<Slot, kFrameLatency> _slots{};
		std::vector<REX::W32::ID3D11Query*> _spare;

		PassTable _passes;
		std::vector<std::size_t> _open;

		std::uint64_t _frame{ 0 };
		std::uint64_t _discarded{ 0 };
		float _frameGpuMs{ 0.0f };
		float _frameCpuMs{ 0.0f };
		double _ticksToMs{ 0.0 };

		std::size_t _writeSlot{ 0 };
		bool _ready{ false };
		bool _refused{ false };
		bool _measuring{ true };
		bool _frameOpen{ false };
	};

	/// Opens a pass on construction and closes it on destruction, and opens a
	/// RenderDoc marker of the same name alongside - so a capture and the
	/// overlay call the same thing by the same name, without anyone keeping two
	/// lists in step.
	class PassScope
	{
	public:
		explicit PassScope(std::string_view a_name) noexcept;
		~PassScope() noexcept;

		PassScope(const PassScope&) = delete;
		PassScope& operator=(const PassScope&) = delete;

	private:
		// Not a MarkerScope member: that one takes a wchar_t* and this takes a
		// string_view, so the widened name has to outlive it.
		std::wstring _wide;
		bool _open{ false };
	};
}
```

-   [ ] **Schritt 2: Aufbau und Abbau schreiben**

`src/Render/Profiler.cpp`, erster Teil:

```cpp
#include "Render/Profiler.h"

#include "Render/Markers.h"
#include "Render/Renderer.h"

#include <REX/W32/KERNEL32.h>

#include <format>

namespace Render
{
	namespace
	{
		[[nodiscard]] std::int64_t Now() noexcept
		{
			std::int64_t counter = 0;
			static_cast<void>(REX::W32::QueryPerformanceCounter(std::addressof(counter)));
			return counter;
		}

		// A line every 600 frames rather than one per frame. A profiler that
		// floods the log is worse than one that says nothing.
		constexpr std::uint64_t kComplaintInterval = 600;
	}

	Profiler& Profiler::GetSingleton() noexcept
	{
		static Profiler profiler;
		return profiler;
	}

	bool Profiler::Initialize() noexcept
	{
		if (_ready) {
			return true;
		}

		// A refusal is final. Creating a query that failed once will fail
		// again, and retrying every frame would only fill the log.
		if (_refused) {
			return false;
		}

		if (GetDevice() == nullptr || GetContext() == nullptr) {
			_refused = true;
			REX::ERROR("profiler: no device or context, not measuring");
			return false;
		}

		std::int64_t frequency = 0;
		if (!REX::W32::QueryPerformanceFrequency(std::addressof(frequency)) || frequency == 0) {
			_refused = true;
			REX::ERROR("profiler: no performance counter, not measuring");
			return false;
		}

		_ticksToMs = 1000.0 / static_cast<double>(frequency);

		for (auto& slot : _slots) {
			if (!CreateSlot(slot)) {
				_refused = true;
				REX::ERROR("profiler: could not create a disjoint query, not measuring");
				Release();
				return false;
			}
		}

		_ready = true;
		REX::INFO("profiler ready, {} frames in flight", kFrameLatency);
		return true;
	}

	bool Profiler::CreateSlot(Slot& a_slot) noexcept
	{
		REX::W32::D3D11_QUERY_DESC desc{};
		desc.query = REX::W32::D3D11_QUERY_TIMESTAMP_DISJOINT;
		desc.miscFlags = 0;

		return GetDevice()->CreateQuery(std::addressof(desc), std::addressof(a_slot.disjoint)) >= 0 &&
		       a_slot.disjoint != nullptr;
	}

	REX::W32::ID3D11Query* Profiler::AcquireTimestamp() noexcept
	{
		if (!_spare.empty()) {
			auto* const reused = _spare.back();
			_spare.pop_back();
			return reused;
		}

		REX::W32::D3D11_QUERY_DESC desc{};
		desc.query = REX::W32::D3D11_QUERY_TIMESTAMP;
		desc.miscFlags = 0;

		REX::W32::ID3D11Query* query = nullptr;
		if (GetDevice()->CreateQuery(std::addressof(desc), std::addressof(query)) < 0) {
			return nullptr;
		}

		return query;
	}

	void Profiler::ReleaseSlot(Slot& a_slot) noexcept
	{
		for (auto& timing : a_slot.timings) {
			if (timing.begin != nullptr) {
				timing.begin->Release();
			}
			if (timing.end != nullptr) {
				timing.end->Release();
			}
		}

		a_slot.timings.clear();
		a_slot.inFlight = false;

		if (a_slot.disjoint != nullptr) {
			a_slot.disjoint->Release();
			a_slot.disjoint = nullptr;
		}
	}

	void Profiler::Release() noexcept
	{
		for (auto& slot : _slots) {
			ReleaseSlot(slot);
		}

		for (auto* const query : _spare) {
			query->Release();
		}

		_spare.clear();
		_passes.Clear();
		_open.clear();
		_ready = false;
		_frameOpen = false;
	}

	void Profiler::SetMeasuring(bool a_measuring) noexcept
	{
		if (a_measuring == _measuring) {
			return;
		}

		_measuring = a_measuring;

		// Samples from before a pause next to samples from after it would
		// average into a number that never happened.
		_passes.Clear();
		_frameGpuMs = 0.0f;
		_frameCpuMs = 0.0f;
	}
}
```

-   [ ] **Schritt 3: Frame und Pässe schreiben**

Im selben Namensraum anhängen:

```cpp
	void Profiler::BeginFrame() noexcept
	{
		if (!_measuring || !Initialize()) {
			return;
		}

		++_frame;

		auto& slot = _slots[_writeSlot];
		if (slot.inFlight) {
			// Still not back after twice around the ring. Waiting for it would
			// stall the render thread, which is the one thing this must never
			// do.
			++_discarded;
			if (_frame % kComplaintInterval == 0) {
				REX::WARN("profiler: dropping slots that never came back, {} so far", _discarded);
			}

			for (auto& timing : slot.timings) {
				_spare.push_back(timing.begin);
				_spare.push_back(timing.end);
			}

			slot.timings.clear();
			slot.inFlight = false;
		}

		slot.frame = _frame;
		GetContext()->Begin(slot.disjoint);

		_frameOpen = true;
		BeginPass("Frame");
	}

	void Profiler::EndFrame() noexcept
	{
		if (!_frameOpen) {
			return;
		}

		// Whatever a feature left open is closed here rather than carried into
		// the next frame, where its timestamps would belong to another slot.
		while (!_open.empty()) {
			EndPass();
		}

		auto& slot = _slots[_writeSlot];
		GetContext()->End(slot.disjoint);
		slot.inFlight = true;

		_writeSlot = (_writeSlot + 1) % kFrameLatency;
		_frameOpen = false;
	}

	void Profiler::BeginPass(std::string_view a_name) noexcept
	{
		if (!_measuring || !_ready || !_frameOpen) {
			return;
		}

		auto& slot = _slots[_writeSlot];

		Timing timing;
		timing.name = std::string{ a_name };
		timing.depth = static_cast<std::uint32_t>(_open.size());
		timing.begin = AcquireTimestamp();
		timing.end = AcquireTimestamp();
		timing.cpuBegin = Now();

		if (timing.begin == nullptr || timing.end == nullptr) {
			if (timing.begin != nullptr) {
				_spare.push_back(timing.begin);
			}
			return;
		}

		// A timestamp query is issued with End alone; Begin belongs to the
		// range queries and asserts in the debug layer here.
		GetContext()->End(timing.begin);

		slot.timings.push_back(std::move(timing));
		_open.push_back(slot.timings.size() - 1);
	}

	void Profiler::EndPass() noexcept
	{
		if (_open.empty()) {
			return;
		}

		auto& slot = _slots[_writeSlot];
		auto& timing = slot.timings[_open.back()];
		_open.pop_back();

		GetContext()->End(timing.end);
		timing.cpuMs = static_cast<float>(static_cast<double>(Now() - timing.cpuBegin) * _ticksToMs);
	}
```

-   [ ] **Schritt 4: Einsammeln und Momentaufnahme schreiben**

```cpp
	void Profiler::Collect() noexcept
	{
		if (!_ready || !_measuring) {
			return;
		}

		for (auto& slot : _slots) {
			if (slot.inFlight) {
				CollectSlot(slot);
			}
		}

		_passes.Retire(_frame);
	}

	void Profiler::CollectSlot(Slot& a_slot) noexcept
	{
		auto* const context = GetContext();

		REX::W32::D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
		if (context->GetData(
				a_slot.disjoint,
				std::addressof(disjoint),
				sizeof(disjoint),
				REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) != 0) {
			return;  // S_FALSE: not ready. Asked again next frame.
		}

		const bool usable = !disjoint.disjoint && disjoint.frequency != 0;
		if (!usable) {
			// The GPU clock moved during this frame. Every number in it would
			// be a lie, so none of them is kept.
			++_discarded;
		}

		for (auto& timing : a_slot.timings) {
			std::uint64_t begin = 0;
			std::uint64_t end = 0;

			const bool read =
				context->GetData(timing.begin, std::addressof(begin), sizeof(begin),
					REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0 &&
				context->GetData(timing.end, std::addressof(end), sizeof(end),
					REX::W32::D3D11_ASYNC_GETDATA_DONOTFLUSH) == 0;

			if (usable && read && end >= begin) {
				const auto gpuMs = static_cast<float>(
					static_cast<double>(end - begin) * 1000.0 /
					static_cast<double>(disjoint.frequency));

				// Found by name, not by a remembered index: a retire between
				// issuing and collecting shifts every index behind the hole.
				if (const auto index = _passes.Find(timing.name); index != PassTable::kNoPass) {
					_passes.Sample(index, timing.depth, gpuMs, timing.cpuMs, a_slot.frame);
				} else if (_frame % kComplaintInterval == 0) {
					REX::WARN("profiler: no room left for the pass named {}", timing.name);
				}

				if (timing.depth == 0) {
					_frameGpuMs = gpuMs;
					_frameCpuMs = timing.cpuMs;
				}
			}

			_spare.push_back(timing.begin);
			_spare.push_back(timing.end);
		}

		a_slot.timings.clear();
		a_slot.inFlight = false;
	}

	void Profiler::LogSnapshot() const noexcept
	{
		const auto results = _passes.Results();
		if (results.empty()) {
			REX::INFO("=== performance snapshot: nothing measured ===");
			return;
		}

		const auto samples = results.front().history != nullptr ? results.front().history->Count() : 0;
		REX::INFO("=== performance snapshot over {} frames ===", samples);
		REX::INFO("  {:<22} {:>7} {:>8} {:>8} {:>8}", "pass", "ms", "avg", "p95", "p99");

		for (const auto& result : results) {
			const std::string indented =
				std::string(static_cast<std::size_t>(result.depth) * 2, ' ') + result.name;

			REX::INFO("  {:<22} {:>7.2f} {:>8.2f} {:>8.2f} {:>8.2f}",
				indented, result.gpuMs, result.avgMs, result.p95Ms, result.p99Ms);
		}

		const float fps = _frameGpuMs > 0.0f ? 1000.0f / _frameGpuMs : 0.0f;
		REX::INFO("  {:.1f} fps, cpu {:.2f} ms, gpu {:.2f} ms, {} frame(s) discarded",
			fps, _frameCpuMs, _frameGpuMs, _discarded);
	}

	PassScope::PassScope(std::string_view a_name) noexcept
	{
		Profiler::GetSingleton().BeginPass(a_name);
		_open = true;

		_wide.assign(a_name.begin(), a_name.end());
		Markers::Push(_wide.c_str());
	}

	PassScope::~PassScope() noexcept
	{
		if (!_open) {
			return;
		}

		Markers::Pop();
		Profiler::GetSingleton().EndPass();
	}
```

`Markers::Push` und `Markers::Pop` sind zwei neue freie Funktionen neben `MarkerScope` in
`src/Render/Markers.{h,cpp}` — dieselben zwei Aufrufe, die `MarkerScope` heute im Konstruktor und
Destruktor macht, nur ohne die RAII-Hülle, weil `PassScope` seine eigene ist. `MarkerScope` wird
darauf umgestellt und behält seine Schnittstelle.

**`_wide.assign(a_name.begin(), a_name.end())` reicht**, weil alle Passnamen aus unserem eigenen
Quelltext stammen und ASCII sind. Ein Name aus einer Übersetzungsdatei käme hier nie an.

-   [ ] **Schritt 5: Bauen**

```
cmake --build --preset FO4
```

Erwartet: **kein Fehler, keine Warnung**. `/W4 /WX` — eine Warnung ist ein Fehler. Erwartbare
Stolpersteine: eine ungenutzte Variable in einem `if constexpr`-losen Zweig, und Umwandlungen
zwischen `std::size_t` und `std::uint32_t`, die ausdrücklich zu schreiben sind.

-   [ ] **Schritt 6: Committen**

```
git add src/Render/Profiler.h src/Render/Profiler.cpp src/Render/Markers.h src/Render/Markers.cpp
git commit -m "feat: time named passes on the gpu and the cpu"
```

---

### Aufgabe 5: Einhängen in `Present` und in die Registry

**Dateien:**

-   Ändern: `src/Render/SwapChainHook.cpp` (die `Present`-Funktion, ab Zeile 32)
-   Ändern: `src/Feature/FeatureRegistry.cpp:123` (`FrameEntry`)

**Schnittstellen:**

-   Verbraucht: `Render::Profiler`, `Render::PassScope` (Aufgabe 4).
-   Liefert: eine gefüllte Ergebnisliste, die Aufgabe 7 zeichnet.

-   [ ] **Schritt 1: Die Frame-Grenze setzen**

In `src/Render/SwapChainHook.cpp`, in `Present`, **in dieser Reihenfolge** — sie ist der Entwurf,
nicht Geschmack:

```cpp
	auto& profiler = Profiler::GetSingleton();
	profiler.SetMeasuring(Settings::GetBool("Performance/measure"));

	// The slot opened at the last Present is complete only now.
	profiler.EndFrame();
	profiler.Collect();
	profiler.BeginFrame();

	{
		const PassScope pass{ "Features" };
		Features::TickSystem();
	}

	{
		const PassScope pass{ "Overlay" };
		Menu::TickSystem();
	}
```

`src/Render/SwapChainHook.cpp` bindet dafür `"Settings/Settings.h"` und `"Render/Profiler.h"`
ein. `Profiler::Initialize()` wird **nicht** von Hand gerufen: `BeginFrame` holt es beim ersten
Durchlauf selbst nach, weil das Gerät erst steht, wenn der Hook hängt.

-   [ ] **Schritt 2: Die eine Klammer in der Registry**

`src/Feature/FeatureRegistry.cpp`, in `FrameEntry`, aus

```cpp
		if (Guarded(name, "Frame", [&] { a_entry.feature->Frame(); })) {
```

wird

```cpp
		// The one place every feature is measured from. A feature contributes
		// nothing for this - and gets nested passes of its own for free from F2
		// on, because the profiler keeps a stack.
		if (Guarded(name, "Frame", [&] {
				const Render::PassScope pass{ name };
				a_entry.feature->Frame();
			})) {
```

Nur `Frame` wird geklammert, nicht `Setup` und nicht `Shutdown`: die laufen nicht je Frame, und
eine Historie über Einzelereignisse sagt nichts.

-   [ ] **Schritt 3: Bauen, deployen, einmal starten**

```
cmake --build --preset FO4
```

Dieser Lauf braucht **kein** Spielfenster im eigentlichen Sinn: es genügt, das Spiel über
`f4se_loader.exe` zu starten, einen Spielstand zu laden, zehn Sekunden zu warten und zu beenden.

Im Log (`C:\Users\minni\Documents\My Games\Fallout4\F4SE\CommunityShadersFO4.log`) erwartet:

-   eine Zeile, dass der Profiler bereit ist, **oder** eine, die benennt, woran er gescheitert ist;
-   **kein** Absturz;
-   **keine** Flut ratenbegrenzter Verwurfzeilen.

-   [ ] **Schritt 4: Committen**

```
git add src/Render/SwapChainHook.cpp src/Feature/FeatureRegistry.cpp
git commit -m "feat: measure the frame and every feature in it"
```

---

### Aufgabe 6: Einstellungsblock `Performance` und Katalog

**Dateien:**

-   Ändern: `src/Menu/MenuSystem.cpp` (Konstanten oben, `StartSystem` ab Zeile 58)
-   Ändern: `package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json` (erzeugt)

**Schnittstellen:**

-   Verbraucht: `Settings::DeclareBool`, `DeclareChoice`, `DeclareKey`.
-   Liefert: die Pfade `Performance/measure`, `Performance/hud`, `Performance/corner`,
    `Performance/logKey`. Aufgabe 7 und 8 lesen sie.

-   [ ] **Schritt 1: Die Pfade als Konstanten anlegen**

Im anonymen Namensraum von `src/Menu/MenuSystem.cpp`, neben `kToggleKeyPath`:

```cpp
		constexpr auto kMeasurePath = "Performance/measure"sv;
		constexpr auto kHudPath = "Performance/hud"sv;
		constexpr auto kCornerPath = "Performance/corner"sv;
		constexpr auto kLogKeyPath = "Performance/logKey"sv;

		// VK_F11. Fallout 4 takes F5 and F9 for quick save and load, and Steam
		// sits on F12.
		constexpr std::uint32_t kDefaultLogKey = 0x7A;
```

-   [ ] **Schritt 2: Deklarieren**

Am Ende von `StartSystem`, nach der Deklaration von `kToggleKeyPath`:

```cpp
		Settings::DeclareBool(kMeasurePath, true)
			.Label("setting.performance.measure", "Measure performance")
			.Help(
				"setting.performance.measure.help",
				"Times each pass on the CPU and the GPU. Off issues no queries at all.");

		Settings::DeclareBool(kHudPath, true)
			.Label("setting.performance.hud", "Show while playing")
			.Help(
				"setting.performance.hud.help",
				"Draws a small display while this overlay is closed.");

		Settings::DeclareChoice(
			kCornerPath, "top-right",
			std::vector<std::string>{ "top-left", "top-right", "bottom-left", "bottom-right" })
			.Label("setting.performance.corner", "Corner")
			.Help("setting.performance.corner.help", "Where the small display sits.");

		Settings::DeclareKey(kLogKeyPath, kDefaultLogKey)
			.Label("setting.performance.log_key", "Write to log")
			.Help(
				"setting.performance.log_key.help",
				"Writes the current numbers to the plugin log.");
```

-   [ ] **Schritt 3: Katalog erzeugen und prüfen**

```
python tools/extract-i18n.py --write
python tools/extract-i18n.py
```

Erwartet: der erste Lauf schreibt eine gewachsene Zahl von Schlüsseln, der zweite meldet „is up to
date" und endet mit 0.

-   [ ] **Schritt 4: Bauen und einmal starten**

Der Block erscheint im Overlay als allgemeine Einstellung, weil es kein Feature namens
`Performance` gibt. Erwartet: vier Einträge, der Choice als Auswahlfeld, der Key mit
Aufnahmeknopf. Die Datei `CommunityShadersFO4.json` bekommt beim Start den Block ergänzt, **ohne**
die vorhandenen Werte zu verlieren.

-   [ ] **Schritt 5: Committen**

```
git add src/Menu/MenuSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: declare the performance settings"
```

---

### Aufgabe 7: `PerformancePanel`, HUD und Tafel

**Dateien:**

-   Anlegen: `src/Menu/PerformancePanel.h`, `src/Menu/PerformancePanel.cpp`
-   Ändern: `src/Menu/Overlay.h`, `src/Menu/Overlay.cpp` (`Draw`, ab Zeile 144, Ausgabebedingung
    Zeile 191)
-   Ändern: `src/Menu/MenuSystem.cpp` (`TickSystem`, der `Draw`-Aufruf am Ende)

**Schnittstellen:**

-   Verbraucht: `Render::PassResult` (Aufgabe 2), `Render::Profiler` (Aufgabe 4), die Pfade aus
    Aufgabe 6.
-   Liefert: `Menu::PerformanceContext` mit `std::span<const Render::PassResult> passes`,
    `float frameGpuMs`, `float frameCpuMs`, `bool measuring`, `bool hud`, `int corner`; dazu
    `Menu::DrawPerformancePanel(const PerformanceContext&, Detail)` mit
    `Detail::kCompact` / `Detail::kFull`, das zurückgibt, ob etwas gezeichnet wurde.

-   [ ] **Schritt 1: Die Kopfdatei schreiben**

`src/Menu/PerformancePanel.h`:

```cpp
#pragma once

#include "Render/PassStatistics.h"

#include <span>

namespace Menu
{
	/// What the panel needs from around it, handed in rather than reached for.
	/// The panel draws; it reads no setting and knows no D3D.
	struct PerformanceContext
	{
		std::span<const Render::PassResult> passes;

		float frameGpuMs{ 0.0f };
		float frameCpuMs{ 0.0f };

		bool measuring{ false };
		bool hud{ false };

		/// 0 top left, 1 top right, 2 bottom left, 3 bottom right.
		int corner{ 1 };
	};

	enum class Detail
	{
		/// While the overlay is closed: four numbers, no decoration, no input.
		kCompact,

		/// While the overlay is open: the whole table with its history.
		kFull
	};

	/// Returns whether anything was drawn, which is what tells the overlay
	/// whether it has draw data worth handing to the backend.
	bool DrawPerformancePanel(const PerformanceContext& a_context, Detail a_detail);
}
```

-   [ ] **Schritt 2: Die Umsetzung schreiben**

`src/Menu/PerformancePanel.cpp`:

```cpp
#include "Menu/PerformancePanel.h"

#include "I18n/I18n.h"

#include <imgui.h>

#include <format>
#include <string>

namespace Menu
{
	namespace
	{
		constexpr float kMargin = 10.0f;

		void PlaceInCorner(int a_corner)
		{
			const auto* const viewport = ImGui::GetMainViewport();
			const auto work = viewport->WorkPos;
			const auto size = viewport->WorkSize;

			const bool right = a_corner == 1 || a_corner == 3;
			const bool bottom = a_corner == 2 || a_corner == 3;

			const ImVec2 position{
				work.x + (right ? size.x - kMargin : kMargin),
				work.y + (bottom ? size.y - kMargin : kMargin)
			};

			const ImVec2 pivot{ right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f };

			ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
			ImGui::SetNextWindowBgAlpha(0.55f);
		}

		void DrawTable(const PerformanceContext& a_context)
		{
			constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
			if (!ImGui::BeginTable("passes", 5, flags)) {
				return;
			}

			ImGui::TableSetupColumn(T("performance.pass", "Pass"), ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn(T("performance.ms", "ms"));
			ImGui::TableSetupColumn(T("performance.avg", "avg"));
			ImGui::TableSetupColumn(T("performance.p95", "p95"));
			ImGui::TableSetupColumn(T("performance.p99", "p99"));
			ImGui::TableHeadersRow();

			for (const auto& pass : a_context.passes) {
				ImGui::TableNextRow();
				ImGui::TableNextColumn();

				// Indented rather than a tree: the shape is fixed per frame and
				// nothing here is worth collapsing.
				const std::string indented =
					std::string(static_cast<std::size_t>(pass.depth) * 4, ' ') + pass.name;
				ImGui::TextUnformatted(indented.c_str());

				for (const float value : { pass.gpuMs, pass.avgMs, pass.p95Ms, pass.p99Ms }) {
					ImGui::TableNextColumn();
					ImGui::Text("%.2f", static_cast<double>(value));
				}
			}

			ImGui::EndTable();
		}

		void DrawHistory(const PerformanceContext& a_context)
		{
			if (a_context.passes.empty() || a_context.passes.front().history == nullptr) {
				return;
			}

			const auto& history = *a_context.passes.front().history;
			const auto count = history.Count();
			if (count == 0) {
				return;
			}

			std::vector<float> ordered;
			ordered.reserve(count);
			for (std::size_t i = 0; i < count; ++i) {
				ordered.push_back(history.Sample(i));
			}

			ImGui::PlotLines(
				"##frame",
				ordered.data(),
				static_cast<int>(ordered.size()),
				0,
				T("performance.frame_history", "Frame time"),
				0.0f,
				FLT_MAX,
				ImVec2{ 0.0f, 60.0f });
		}
	}

	bool DrawPerformancePanel(const PerformanceContext& a_context, Detail a_detail)
	{
		if (a_detail == Detail::kCompact) {
			PlaceInCorner(a_context.corner);

			constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
			                       ImGuiWindowFlags_AlwaysAutoResize |
			                       ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			                       ImGuiWindowFlags_NoSavedSettings;

			if (!ImGui::Begin("##performance_hud", nullptr, flags)) {
				ImGui::End();
				return false;
			}

			if (!a_context.measuring) {
				// Not frozen numbers: a display that keeps showing the last
				// value it had is worse than one that says it stopped.
				ImGui::TextUnformatted(T("performance.paused", "Measurement is off"));
			} else {
				const float fps =
					a_context.frameGpuMs > 0.0f ? 1000.0f / a_context.frameGpuMs : 0.0f;

				ImGui::Text("%.0f fps   %.2f ms", static_cast<double>(fps),
					static_cast<double>(a_context.frameGpuMs));
				ImGui::Text("cpu %.2f   gpu %.2f", static_cast<double>(a_context.frameCpuMs),
					static_cast<double>(a_context.frameGpuMs));
			}

			ImGui::End();
			return true;
		}

		if (!ImGui::Begin(T("performance.title", "Performance"))) {
			ImGui::End();
			return false;
		}

		if (!a_context.measuring) {
			ImGui::TextUnformatted(T("performance.paused", "Measurement is off"));
			ImGui::End();
			return true;
		}

		// Said here rather than in a help text nobody opens: the frame figure
		// is wall time between two Presents and carries a vsync wait with it,
		// while a pass of our own is exact.
		ImGui::TextUnformatted(
			T("performance.frame_note", "Frame is wall time between presents; passes are exact."));
		ImGui::Separator();

		DrawTable(a_context);
		DrawHistory(a_context);

		ImGui::End();
		return true;
	}
}
```

`<vector>` und `<cfloat>` sind dafür einzubinden.

-   [ ] **Schritt 3: `Overlay::Draw` erweitern**

Vierter Parameter `const PerformanceContext& a_performance`. Innerhalb, nach dem
`PushFont`-Abschnitt:

```cpp
		bool drewHud = false;
		if (a_visible) {
			closeWanted = DrawSettingsPanel(a_panel);
			DrawPerformancePanel(a_performance, Detail::kFull);
		} else if (a_performance.hud) {
			drewHud = DrawPerformancePanel(a_performance, Detail::kCompact);
		}
```

und die Ausgabebedingung in Zeile 191 von

```cpp
		if (a_visible && BindBackBuffer()) {
```

zu

```cpp
		// Also when only the small display was drawn: without this the draw
		// data is built every frame and thrown away, which is what happened
		// before F1 because nothing outside the overlay drew anything.
		if ((a_visible || drewHud) && BindBackBuffer()) {
```

**Unverändert bleibt:** `MousePointer` wird weiterhin nur bei sichtbarem Overlay eingesetzt, und
`AddMousePosEvent` bleibt an `a_visible` gebunden. Das HUD nimmt keine Eingabe und darf an der in
E2 teuer erkauften Regelung nichts ändern.

-   [ ] **Schritt 4: Den Kontext füllen**

In `Menu::TickSystem`, neben dem `PanelContext`:

```cpp
		const auto& profiler = Render::Profiler::GetSingleton();

		PerformanceContext performance;
		performance.passes = profiler.Results();
		performance.frameGpuMs = profiler.FrameGpuMs();
		performance.frameCpuMs = profiler.FrameCpuMs();
		performance.measuring = profiler.IsMeasuring();
		performance.hud = Settings::GetBool(kHudPath);
		performance.corner = CornerFromSetting(Settings::GetString(kCornerPath));
```

`CornerFromSetting` bildet die vier Zeichenketten auf 0..3 ab und liefert bei allem anderen 1
(`top-right`) — eine von Hand verstellte Datei darf das Overlay nicht zerlegen.

-   [ ] **Schritt 5: Bauen und Spiellauf 1**

Dies ist der Lauf, der die riskanteste Annahme prüft. **Reihenfolge einhalten:**

1. Spiel starten, Spielstand laden. **Erwartet:** das HUD steht in der rechten oberen Ecke und
   zeigt Zahlen.
2. **Ohne das Overlay zu öffnen:** herumlaufen, umsehen, schießen. **Erwartet:** die Maus verhält
   sich wie immer, die Kamera dreht normal, nichts springt. **Geht das schief, ist Annahme 3 der
   Spec falsch** — dann Befund notieren und hier anhalten, nicht weiterbauen.
3. Overlay öffnen. **Erwartet:** das HUD verschwindet, die Tafel erscheint mit `Frame`,
   eingerücktem `Features`, je Feature einer Zeile, und `Overlay`. Der Verlauf läuft.
4. Im Overlay klicken, ein Häkchen setzen, einen Abschnitt zuklappen. **Erwartet:** alles
   reagiert an der Stelle, an der der Zeiger gezeichnet ist — die E2-Regelung steht noch.
5. `ImagespaceTint` einschalten. **Erwartet:** eine Zeile kommt hinzu. Ausschalten: sie
   verschwindet nach etwa einer Sekunde.
6. `Performance/measure` ausschalten. **Erwartet:** beide Anzeigen sagen, dass nicht gemessen
   wird, statt eingefrorene Zahlen zu zeigen.
7. Ecke umstellen. **Erwartet:** das HUD springt sofort.

-   [ ] **Schritt 6: Committen**

```
git add src/Menu/PerformancePanel.h src/Menu/PerformancePanel.cpp src/Menu/Overlay.h src/Menu/Overlay.cpp src/Menu/MenuSystem.cpp package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
git commit -m "feat: put the numbers on the screen"
```

---

### Aufgabe 8: Logtaste und Momentaufnahme

**Dateien:**

-   Ändern: `src/Menu/WindowHook.h`, `src/Menu/WindowHook.cpp` (sechster Rückruf)
-   Ändern: `src/Menu/MenuSystem.cpp` (`TickSystem`, `InstallWindowHook`)
-   Ändern: `src/Render/Profiler.cpp` (`LogSnapshot`, falls in Aufgabe 4 nur angelegt)

**Schnittstellen:**

-   Verbraucht: `Menu::KeyLatch` (Aufgabe 3), `Profiler::LogSnapshot` (Aufgabe 4).
-   Liefert: nichts, worauf eine spätere Aufgabe baut.

-   [ ] **Schritt 1: Der sechste Rückruf**

`InstallWindowHook` bekommt `std::function<void(std::uint32_t)> a_onKey`, gerufen für **jeden**
Tastendruck, den weder die Aufnahme noch die Overlay-Taste genommen hat. Die Reihenfolge in der
Fensterprozedur lautet damit: **Aufnahme, Overlay-Taste, `a_onKey`.**

Die Aufnahme zuerst, weil sonst keine Taste auf sich selbst umbelegbar wäre — die Regel aus E2.
`a_onKey` zuletzt, damit eine versehentlich doppelt belegte Taste das Overlay öffnet, statt beides
zu tun.

-   [ ] **Schritt 2: Verdrahten**

In `Menu::TickSystem`, vor `InstallWindowHook`, ein Dateistatisches `KeyLatch` wie die anderen
Singletons oben in der Datei. Je Tick `TheLogLatch().SetKey(Settings::GetUInt32(kLogKeyPath))`,
und nach dem `Gate().Tick()`:

```cpp
		if (TheLogLatch().Take()) {
			Render::Profiler::GetSingleton().LogSnapshot();
		}
```

-   [ ] **Schritt 3: Bauen und Spiellauf 2 — die Abnahme**

1. Spiel starten, laden, eine Minute normal spielen.
2. `F11` drücken. Beenden.
3. Im Log erwartet: der Block aus Abschnitt 4.7 der Spec, mit eingerückten Passzeilen, Bildrate,
   CPU, GPU und der Zahl verworfener Frames.
4. Alle sieben Punkte aus Aufgabe 7, Schritt 5, noch einmal — diesmal als Abnahme.

-   [ ] **Schritt 4: Committen**

```
git add src/Menu/WindowHook.h src/Menu/WindowHook.cpp src/Menu/MenuSystem.cpp src/Render/Profiler.cpp
git commit -m "fix: write the numbers down on a key"
```

---

### Aufgabe 9: Befund festhalten und Branch abschließen

**Dateien:**

-   Ändern: `docs/fallout4-port/ROADMAP.md`

-   [ ] **Schritt 1: Vollständig prüfen**

```
cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
python tools/extract-i18n.py
```

Erwartet: 16 von 16 Tests grün, Plugin in Ordnung, Katalog aktuell.

-   [ ] **Schritt 2: Abschnitt „Aus Teilprojekt F1 bestätigt" schreiben**

Mit der Momentaufnahme aus dem Abnahmelauf als erste echte Zahlen des Ports, und mit dem Befund zu
jeder der fünf Annahmen aus Abschnitt 8 der Spec — bestätigt oder widerlegt, keine stillschweigend
abgehakt. Die Zeile F1 in der Zerlegungstabelle auf **abgeschlossen** setzen.

-   [ ] **Schritt 3: Committen und übergeben**

```
git add docs/fallout4-port/ROADMAP.md
git commit -m "docs: record subproject f1 acceptance"
```

Danach `superpowers:finishing-a-development-branch`: Fast-Forward-Merge nach `dev`, Branch löschen,
Push **nur** auf ausdrückliche Ansage.

---

## Was diesen Plan scheitern lassen würde

-   **Annahme 3 der Spec bricht** — ein `NoInputs`-Fenster stört den Systemzeiger doch. Deshalb
    steht Spiellauf 1 in Aufgabe 7 und nicht am Ende. Ausweg wäre, das HUD nur zu zeichnen, während
    das Overlay offen ist, und den Rest von F1 unverändert zu lassen; das kostet die halbe
    Nützlichkeit, nicht das Teilprojekt.
-   **Timestamp-Queries stehen nicht zur Verfügung** — dann bleibt die CPU-Hälfte, und die
    GPU-Spalten entfallen. Das wäre ein Befund für die Roadmap und eine Änderung an der Tafel, nicht
    an der Architektur.
-   **Die Frame-Zahl wird überinterpretiert.** Sie ist Wandzeit inklusive Vsync-Wartezeit. Der
    Erklärtext in der Tafel muss das sagen, sonst führt die erste Abnahme von F2 zu einer falschen
    Schlussfolgerung.
