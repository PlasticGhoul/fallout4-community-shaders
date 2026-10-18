# F5.5c Neural Rendering — Umsetzungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** DLSS 5 Neural Rendering (NGX-Feature 18) läuft auf der D3D12-Brücke über dem HUD-losen Bild, HUD und Overlay bleiben unberührt, mit und ohne Frame Generation.

**Architecture:** Der Abgriff (Tiefe, Bewegung, HUD-los, Kamera) wandert aus `FrameGeneration` in einen geteilten Baustein `Bridge::Capture`. In der nativen Insel kommt `NeuralRenderer` dazu: NGX auf dem D3D12-Gerät, Feature, Auswertung, ein Compute-Pass, der NR-Bild und Endbild zusammensetzt, und Zeitstempel. `Presenter::Present` ruft ihn vor der Kopie in die Swapchain. Das Feature `NeuralRendering` deklariert Einstellungen, entscheidet je Frame und reicht die Werte an die Brücke.

**Tech Stack:** C++23/MSVC, D3D11 + D3D12, NVIDIA NGX (DLSS SDK `extern/DLSS`, `nvsdk_ngx_d.lib`), `nvsdk_ngx_defs_dlssnr.h` aus Remix' Paket, HLSL cs_5_0, CMake, PowerShell-Paketierung.

**Spec:** `docs/superpowers/specs/2026-10-18-fallout4-f5-5c-neural-rendering-design.md`

## Beim Planen geklärt (gegenüber der Spec)

-   **R1 entfällt**: Das Log von F5.5b nennt Format 28 (`R8G8B8A8_UNORM`) für den Backbuffer, also auch für die Slot-Texturen — dieselbe Kodierung wie in der Probe.
-   **R3 hat eine Vorlage**: DLSS SR übergibt dieselben UV-Vektoren mit `MVScale` = Vektorauflösung, Vorzeichen + (`src/Features/DLSS.cpp:44-49`, im Lauf vom 2026-10-04 bestätigt). NR übernimmt das.
-   Der Shader `CopyDepth.hlsl` bleibt im FG-Addon; der geteilte Abgriff lädt ihn von dort, was die Voraussetzung „FG-Addon" ohnehin deckt.

## Global Constraints

-   Nur Fallout 4 AE 1.11.240; `/W4 /WX` auf unserem Target; keine TODO/Platzhalter.
-   `<Windows.h>`, D3D12, Streamline und NGX-D3D12 nur in `src/Bridge/Native/` (ohne PCH); Austausch nur über `src/Bridge/Bridge.h` mit einfachen Typen; Logging dort über `Bridge::Log`, nie REX.
-   NGX-Projekt-ID `99023707-327d-45dc-ba45-0c47e8445542`, Engine-Typ `NVSDK_NGX_ENGINE_TYPE_CUSTOM`, Engine-Version `Fallout 4 1.11.240`.
-   NR-Feature-ID `NVSDK_NGX_Feature_DLSSNR` (= Reserved18) aus `nvsdk_ngx_defs_dlssnr.h`; nur dieser Header aus dem Paket kommt in den Include-Pfad.
-   Paket: `https://d1oqksizpb0e3d.cloudfront.net/rtx-remix-ngx_sdk_dlnr@2.7z`, SHA256 `4bb8651b3ac2180f23df1f39422c489810a13d445a94dc836c876c18c93ecefe`.
-   Preset: `fast` → `NVSDK_NGX_DLSSNR_Hint_Render_Preset_B` (2), `original` → `_A` (1). Style `1`/`2`/`3` → 0/1/2. `MaskBlendMode` = `NVSDK_NGX_DLSSNR_Mask_Blend_Mode_Min` (2) bei Figurenerkennung, sonst `_None` (0).
-   Vorgaben: intensity 1,0; structure 0,7; tone 0,3; skin 0,5; characters an; model `fast`; style `1`; view `off`. Slider 0–1.
-   `MVecScaleX/Y` = Breite/Höhe der Bewegungsvektoren (UV → Pixel, Vorzeichen +, wie `kMotionScaleX/Y` in `src/Features/DLSS.cpp:48`). `DepthInverted` 0.
-   Backbuffer des Spiels ist `R8G8B8A8_UNORM` (Format 28, Log F5.5b) — R1 der Spec entfällt; Formate trotzdem aus der Slot-Textur übernehmen, nicht hart kodieren.
-   Alle sichtbaren Texte über `T()`/`.Label`/`.Help`; danach `python tools/extract-i18n.py --write`.
-   NR ist bei offenem Overlay **an**; aus bei Ladebildschirm, Pip-Boy, `inMenuMode`, TAA aus.
-   NR setzt das FrameGeneration-Addon voraus (Streamline). Abnahme: Frame-Zeit steigt höchstens um 1,5 × D3D12-Zeit von NR + Zusammensetzen.
-   Conventional Commits, Titel ≤ 50 Zeichen, Body umbrochen bei 72, Schluss `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
-   Jeder Host-Test wird nach dem Grünwerden absichtlich gebrochen (Fehlschlag vorher benennen, Bau geglückt prüfen) und mit dem Edit-Werkzeug zurückgenommen.

## Review Focus

1. **NR aus muss bitgenau F5.5b sein**: wenn NR nicht rechnet, kopiert `Present` exakt wie bisher das Endbild; kein Zwischenschritt über die Zieltextur. (Task 6, Prüfschritt im Lauf 1.)
2. **Größenänderung/Formatwechsel** während NR läuft: Feature und Texturen werden neu angelegt, alte erst nach dem Release-Fence freigegeben. (Task 5/6; `ResizeBuffers` ist im Spiel nie gelaufen — Code-Review prüft die Reihenfolge.)
3. **Modellwechsel im laufenden Spiel** legt genau einmal neu an, nicht jeden Frame (Vergleich mit dem zuletzt angelegten Preset). (Task 5.)
4. **Kein HUD-loses Bild in diesem Frame** (Ladebildschirm-Übergang, Abgriff abgelehnt): dann kein NR, Endbild unverändert — nie altes HUD-loses Bild eines früheren Frames. (Task 6.)
5. **FG an, NR aus und umgekehrt** im selben Lauf: der Abgriff läuft, sobald einer will; FG-Verhalten unverändert. (Task 4, Lauf 1.)

---

## Dateien

| Datei                                                                                                                                | Aufgabe                                                        |
| ------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| `src/Bridge/StartupSwitch.{h,cpp}`                                                                                                   | zwei Schalter (Task 1)                                         |
| `src/Bridge/NeuralDecision.{h,cpp}`                                                                                                  | rein: Einstellungen → NGX-Werte, Sperrgrund (Task 2)           |
| `cmake/DlssNr.cmake`, `.gitignore`, `CMakeLists.txt`                                                                                 | Bezug der DLL und des Headers (Task 3)                         |
| `package/Features/NeuralRendering/EXTERNAL`, `tools/verify-package.ps1`                                                              | Addon (Task 3)                                                 |
| `src/Bridge/Capture.{h,cpp}`                                                                                                         | geteilter Abgriff, einziger `Upscale::Hooks::Tap` (Task 4)     |
| `src/Features/FrameGeneration.{h,cpp}`                                                                                               | Abgriff entfernt, nutzt `Capture` (Task 4)                     |
| `src/Bridge/Native/NeuralRenderer.{h,cpp}`                                                                                           | NGX, Feature, Auswertung, Zusammensetzen, Zeitstempel (Task 5) |
| `package/Features/NeuralRendering/Shaders/FO4/NeuralRendering/Composite.hlsl`                                                        | Compute-Shader (Task 5)                                        |
| `src/Bridge/Bridge.h`, `src/Bridge/Native/DeviceHook.cpp`, `src/Bridge/Native/Presenter.{h,cpp}`, `src/Bridge/Native/Streamline.cpp` | Anbindung (Task 6)                                             |
| `src/Features/NeuralRendering.{h,cpp}`, `src/Feature/FeatureSystem.cpp`, `package/F4SE/Plugins/CommunityShadersFO4/i18n/en.json`     | Feature (Task 7)                                               |
| `docs/fallout4-port/ROADMAP.md`, `.claude/CLAUDE.md`                                                                                 | Abschluss (Task 9)                                             |

---

### Task 1: Startschalter für zwei Features

**Files:**

-   Modify: `src/Bridge/StartupSwitch.h`, `src/Bridge/StartupSwitch.cpp`
-   Modify: `src/Bridge/Native/Streamline.cpp:82` (Meldung)
-   Test: `tests/StartupSwitchTests.cpp`

**Interfaces:**

-   Produces: unverändert `StartupSwitch ParseStartupSwitch(std::string_view)` / `ReadStartupSwitch(path)`; `on` = `FrameGeneration/enabled` **oder** `NeuralRendering/enabled` ist `true`.

Regel für `reason`: ungültiges JSON → `"the settings file is not valid JSON"` wie bisher. Sonst je Block der bisherige Satz (`"the settings file has no X block"`, `"X/enabled is missing"`, `"X/enabled is not true or false"`, `"X/enabled is false"`), FG zuerst, verbunden mit `"; "`. Leer, sobald einer an ist.

-   [ ] **Step 1: Tests anpassen und ergänzen**

```cpp
Check(ParseStartupSwitch(R"({"NeuralRendering":{"enabled":true}})").on, "NR alone switches the bridge on");
Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":true}})").on, "either is enough");
Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":true},"NeuralRendering":{"enabled":1.0}})").on, "a broken NR value does not stop FG");
const auto bothOff = ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":false}})");
Check(!bothOff.on && bothOff.reason == "FrameGeneration/enabled is false; NeuralRendering/enabled is false", "both off, both named");
Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false}})").reason ==
	"FrameGeneration/enabled is false; the settings file has no NeuralRendering block", "a file from before F5.5c");
Check(ParseStartupSwitch(R"({"FrameGeneration":{"enabled":false},"NeuralRendering":{"enabled":"yes"}})").reason ==
	"FrameGeneration/enabled is false; NeuralRendering/enabled is not true or false", "NR value not a boolean");
```

Die bestehenden Erwartungen an `reason` im Aus-Fall bekommen den NR-Teil angehängt (`"; the settings file has no NeuralRendering block"`); die Fälle selbst bleiben.

-   [ ] **Step 2: Bauen, Test läuft rot**

Run: `cmake --build --preset FO4 --target StartupSwitchTests` dann `ctest --test-dir build/FO4 -C Release -R StartupSwitch --output-on-failure`
Expected: Build OK, FAIL bei „NR alone switches the bridge on" und den angepassten Gründen.

-   [ ] **Step 3: Umsetzen**

In `StartupSwitch.cpp` eine Hilfsfunktion `std::optional<std::string> ReadSwitch(const glz::generic& root, std::string_view block)` (leer = an, sonst Grund), zweimal gerufen. Kommentar im Header nennt beide Schalter. In `Streamline.cpp:82` die Meldung zu `"{} is missing - is the FrameGeneration archive installed? Frame Generation and Neural Rendering both need it"`.

-   [ ] **Step 4: Test grün, dann absichtlich brechen**

Mutation: `||` → `&&` in der Verknüpfung. Erwartet: FAIL bei „NR alone…" und „either is enough". Bau prüfen, laufen lassen, zurücknehmen, grün.

-   [ ] **Step 5: Commit** — `feat: bridge switch for neural rendering too`

---

### Task 2: Reine Entscheidungen für NR

**Files:**

-   Create: `src/Bridge/NeuralDecision.h`, `src/Bridge/NeuralDecision.cpp`
-   Test: `tests/NeuralDecisionTests.cpp`; Testziel `NeuralDecisionTests` in `CMakeLists.txt` nach dem Muster von `StartupSwitchTests` (ohne CommonLib, falls der Header keine braucht)

**Interfaces:**

-   Produces (Namespace `Bridge`, nur Standardtypen, in `Bridge.h` von Task 6 wiederverwendet):

```cpp
enum class NeuralView : std::uint32_t { kOff = 0, kSplit = 1, kMask = 2 };

struct NeuralOptions
{
	std::uint32_t preset{ 2 };        // NVSDK_NGX_DLSSNR_Hint_Render_Preset_B
	std::uint32_t style{ 0 };
	float intensity{ 1.0f };
	float structure{ 0.7f };
	float tone{ 0.3f };
	float skin{ 0.5f };
	bool autoMask{ true };
	std::uint32_t maskBlendMode{ 2 }; // _Min with autoMask, _None (0) without
	NeuralView view{ NeuralView::kOff };
};

struct NeuralSettings
{
	std::string_view model;   // "fast" | "original"
	std::string_view style;   // "1" | "2" | "3"
	std::string_view view;    // "off" | "split" | "mask"
	double intensity, structure, tone, skin;
	bool characters;
};
[[nodiscard]] NeuralOptions MakeNeuralOptions(const NeuralSettings& a_settings) noexcept;

struct NeuralFacts
{
	bool loadingScreen, pipboy, menuMode, overlayOpen, taaOn;
};
/// nullptr when NR may run this frame; the overlay never blocks it.
[[nodiscard]] const char* NeuralBlocker(const NeuralFacts& a_facts) noexcept;
```

Unbekannte Zeichenketten → Vorgabe; Zahlen auf 0–1 geklemmt, NaN → Vorgabe. Sperrgründe in dieser Reihenfolge: `"loading screen"`, `"Pip-Boy"`, `"a game menu is open"`, `"the game's TAA is off, so it writes no motion vectors"`.

-   [ ] **Step 1: Test schreiben**

```cpp
auto o = Bridge::MakeNeuralOptions({ "fast", "1", "off", 1.0, 0.7, 0.3, 0.5, true });
Check(o.preset == 2 && o.style == 0 && o.maskBlendMode == 2 && o.view == Bridge::NeuralView::kOff, "defaults map to B, style 0, Min");
Check(Bridge::MakeNeuralOptions({ "original", "3", "mask", 1, 1, 1, 1, false }).preset == 1, "original is preset A");
Check(Bridge::MakeNeuralOptions({ "original", "3", "mask", 1, 1, 1, 1, false }).style == 2, "style 3 is 2");
Check(Bridge::MakeNeuralOptions({ "fast", "1", "off", 1, 1, 1, 1, false }).maskBlendMode == 0, "no characters, no blend");
Check(Bridge::MakeNeuralOptions({ "huh", "9", "??", 2.0, -1.0, std::nan(""), 0.5, true }).preset == 2, "unknown model falls back");
auto c = Bridge::MakeNeuralOptions({ "huh", "9", "??", 2.0, -1.0, std::nan(""), 0.5, true });
Check(c.style == 0 && c.view == Bridge::NeuralView::kOff && c.intensity == 1.0f && c.structure == 0.0f && c.tone == 0.3f, "clamped and defaulted");
Check(Bridge::MakeNeuralOptions({ "fast", "2", "split", 0, 0, 0, 0, true }).view == Bridge::NeuralView::kSplit, "split");
Check(Bridge::NeuralBlocker({ false, false, false, true, true }) == nullptr, "the open overlay does not block");
Check(std::string_view{ Bridge::NeuralBlocker({ true, true, true, false, false }) } == "loading screen", "loading first");
Check(std::string_view{ Bridge::NeuralBlocker({ false, true, true, false, true }) } == "Pip-Boy", "then Pip-Boy");
Check(std::string_view{ Bridge::NeuralBlocker({ false, false, true, false, true }) } == "a game menu is open", "then menus");
Check(Bridge::NeuralBlocker({ false, false, false, false, false }) != nullptr, "no TAA, no motion vectors");
```

-   [ ] **Step 2: Rot** — Build des Testziels scheitert an fehlendem Header; nach leerem Gerüst FAIL.
-   [ ] **Step 3: Umsetzen** in `NeuralDecision.cpp`.
-   [ ] **Step 4: Grün, dann brechen** — Mutation: Overlay-Prüfung einbauen (`if (a_facts.overlayOpen) return "overlay";`). Erwartet: FAIL „the open overlay does not block". Zurücknehmen.
-   [ ] **Step 5: Commit** — `feat: neural rendering options and blocker`

---

### Task 3: DLL und Header beziehen, Addon packen

**Files:**

-   Create: `cmake/DlssNr.cmake`, `package/Features/NeuralRendering/EXTERNAL`
-   Modify: `CMakeLists.txt` (nach `include(.../Streamline.cmake)`; Include-Pfad), `.gitignore` (`extern/DlssNr/`), `tools/verify-package.ps1`

**Interfaces:**

-   Produces: `DLSSNR_DIR` = `${CMAKE_SOURCE_DIR}/extern/DlssNr` mit `include/nvsdk_ngx_defs_dlssnr.h`, `lib/Windows_x86_64/rel/nvngx_dlssnr.dll`, `LICENSE.txt`, `VERSION` (Inhalt `dlnr@2`). Include-Pfad `${DLSSNR_DIR}/include` **nach** `${DLSS_SDK_DIR}/include`.

-   [ ] **Step 1: `cmake/DlssNr.cmake`** nach dem Muster von `cmake/Streamline.cmake` (Stempel, Hash-Prüfung, Download mit `EXPECTED_HASH`, `file(ARCHIVE_EXTRACT … PATTERNS "include/nvsdk_ngx_defs_dlssnr.h" "lib/Windows_x86_64/rel/nvngx_dlssnr.dll" "LICENSE.txt")`), Meldung „Downloading DLSS Neural Rendering (368 MB, once)". Kopfkommentar: Herkunft ist Remix' Build, kein SDK (R4).
-   [ ] **Step 2: EXTERNAL**

```
extern/DlssNr/lib/Windows_x86_64/rel/nvngx_dlssnr.dll => F4SE/Plugins/CommunityShadersFO4/NeuralRendering/nvngx_dlssnr.dll
extern/DlssNr/LICENSE.txt => F4SE/Plugins/CommunityShadersFO4/NeuralRendering/nvngx_dlssnr.license.txt
```

-   [ ] **Step 3: `verify-package.ps1`** — im Staging-Block die zwei Dateien und `Shaders/FO4/NeuralRendering/Composite.hlsl` prüfen; bei den Archiven `$neural = NeuralRendering-*` genau eins, Inhalt genau diese drei, kein anderes Archiv außer All-in-one trägt `F4SE/Plugins/CommunityShadersFO4/NeuralRendering/*`, `$neuralEntries` in die Vereinigung und in die Schleife der Markerprüfungen.
-   [ ] **Step 4: Konfigurieren** — `cmake -S . --preset FO4`. Expected: einmal Download, danach `extern/DlssNr` mit genau drei Dateien plus `VERSION`; zweiter Lauf lädt nichts.
-   [ ] **Step 5: Absichtlich brechen** — eine Ziffer der SHA256 ändern, `extern/DlssNr/VERSION` löschen, konfigurieren. Erwartet: Archiv wird wegen falschem Hash verworfen und der Download scheitert mit Hash-Fehler. Zurücknehmen, neu konfigurieren.
-   [ ] **Step 6: Commit** — `build: fetch DLSS neural rendering runtime` (Paketprüfung läuft erst in Task 7, wenn der Shader liegt.)

---

### Task 4: Abgriff aus FrameGeneration herauslösen

**Files:**

-   Create: `src/Bridge/Capture.h`, `src/Bridge/Capture.cpp` (außerhalb der Insel, mit PCH, REX erlaubt)
-   Modify: `src/Features/FrameGeneration.h`, `src/Features/FrameGeneration.cpp`

**Interfaces:**

-   Produces:

```cpp
namespace Bridge
{
	enum class Consumer : std::uint32_t { kFrameGeneration = 1, kNeuralRendering = 2 };

	/// The only Upscale::Hooks::Tap: tells the bridge where rendering starts,
	/// copies depth and motion into the slot, builds the camera, copies the
	/// picture before the HUD - in every frame some consumer wants it.
	class Capture final : public Upscale::Hooks::Tap
	{
	public:
		/// From a consumer's Setup: compiles CopyDepth on first use, sets the tap. False with the reason logged.
		[[nodiscard]] bool Acquire(Consumer a_consumer) noexcept;
		/// From Shutdown; the last one out removes the tap and frees the shader.
		void Release(Consumer a_consumer) noexcept;
		/// From Frame, every frame: whether this consumer needs the next frame's inputs.
		void Want(Consumer a_consumer, bool a_want) noexcept;
		/// Once a second from whoever logs; resets the counters.
		void LogSecond() noexcept;
		// Tap overrides: AfterTemporalData, AfterEffectRange, AfterDynamicViewportDefault
	};
	[[nodiscard]] Capture& TheCapture() noexcept;
}
```

Umzug 1:1 aus `FrameGeneration.cpp`: `CopyDepthConstants`, `CompileShader` (Datei bleibt `FrameGeneration/CopyDepth.hlsl` im FG-Addon — NR setzt es voraus), `CaptureDepthAndMotion`, `TakeCameraCut`, `SubmitCamera`, `AfterDynamicViewportDefault`, die Zähler. `_generating` wird „irgendein Abnehmer will"; Reset wird gesetzt beim Übergang von niemand zu jemand (vorher FGs Übergang aus→an). `AfterTemporalData` ruft `Bridge::OnRenderStart()` wie bisher unbedingt. Pass-Namen werden `Capture/DepthMotion` und `Capture/Hudless`. Die Logzeile „frame generation: …" verliert die Abgriffzähler; `Capture::LogSecond` schreibt `capture: {} depth+motion / {} hud-less, refused {} depth+motion / {} camera / {} hud-less` und wird aus FG und NR gerufen, aber höchstens einmal je 180 Frames (eigener Zähler auf `Render::FrameCount()`).

FG: `Setup` ruft `TheCapture().Acquire(Consumer::kFrameGeneration)` statt Shader und Tap; `Frame` ruft `Want(kFrameGeneration, generate)`; `Shutdown` `Release`. Alles andere an FG bleibt.

-   [ ] **Step 1: Umzug** wie beschrieben.
-   [ ] **Step 2: Bauen** — `cmake --build --preset FO4`. Expected: 0 Warnungen, alle bestehenden `ctest` grün, `pwsh tools/verify-plugin.ps1` grün.
-   [ ] **Step 3: Gegenlesen** gegen `git show HEAD:src/Features/FrameGeneration.cpp`: jede Zeile des alten Abgriffs findet sich in `Capture.cpp` wieder (Diff mit `git diff --stat` und Blick auf die Bedingung `a_enabled || !_generating`).
-   [ ] **Step 4: Commit** — `refactor: share the bridge capture` (Beleg im Spiel: Lauf 1, Schritt 2.)

---

### Task 5: NeuralRenderer in der Insel

**Files:**

-   Create: `src/Bridge/Native/NeuralRenderer.h`, `src/Bridge/Native/NeuralRenderer.cpp`
-   Create: `package/Features/NeuralRendering/Shaders/FO4/NeuralRendering/Composite.hlsl`

**Interfaces:**

-   Consumes: `Bridge::NeuralOptions`, `Bridge::NeuralView` (Task 2), `Native::TheBackend()`, `Bridge::Log`.
-   Produces (Namespace `Bridge::Native`):

```cpp
struct NeuralFrame
{
	ID3D12Resource* final;    // slot, COMMON
	ID3D12Resource* hudless;  // slot, COMMON
	ID3D12Resource* depth;    // slot, COMMON, R32_FLOAT
	ID3D12Resource* motion;   // slot, COMMON, R16G16_FLOAT
	UINT width, height;              // display
	UINT motionWidth, motionHeight;  // depth and motion
	bool reset;
	std::size_t slot;                // ring index, for descriptors and timestamps
};

class NeuralRenderer
{
public:
	/// NGX on the bridge's D3D12 device and the composite pipeline. Once per
	/// process; NGX stays up until the end. Empty string on success.
	[[nodiscard]] std::string Start(const std::filesystem::path& a_dllDirectory, std::span<const std::uint8_t> a_composite) noexcept;
	[[nodiscard]] bool Started() const noexcept;
	void SetOptions(const NeuralOptions& a_options, bool a_wanted) noexcept;
	[[nodiscard]] bool Wanted() const noexcept;
	/// Records evaluate + composite into a_list; a_composed then holds the
	/// picture to present (COPY_SOURCE). False: nothing recorded, present the final picture.
	[[nodiscard]] bool Record(ID3D12GraphicsCommandList* a_list, const NeuralFrame& a_frame, std::uint64_t a_doneValue) noexcept;
	[[nodiscard]] ID3D12Resource* Composed() const noexcept;
	/// The NR output, for the DLSS-G hud-less tag, and the state it is left in.
	[[nodiscard]] ID3D12Resource* Output() const noexcept;
	/// Drops feature and textures; freed once the release fence passes a_doneValue.
	void Retire(std::uint64_t a_doneValue) noexcept;
	/// Frees what the GPU is past. Every Present.
	void Collect(std::uint64_t a_completedValue) noexcept;
	[[nodiscard]] double GpuMilliseconds() const noexcept;  // mean over the last 180 Records
};
[[nodiscard]] NeuralRenderer& TheNeuralRenderer() noexcept;
```

Festlegungen:

-   **Start**: `NVSDK_NGX_D3D12_Init_with_ProjectID` auf `TheBackend().device` (nicht Streamlines Proxy), `PathListInfo` = `a_dllDirectory`, App-Datenpfad = `a_dllDirectory` (wie F5.5a), kein Logging-Callback (wie F5.5a). Danach Capability-Parameter; Ablehnungsgründe wörtlich: `"NGX did not start on D3D12 (0x%08X)"`, `"DLSS Neural Rendering is not available on this GPU (FeatureInitResult 0x%08X)"`, `"DLSS Neural Rendering needs driver %u.%u or newer"`, `"nvngx_dlssnr.dll not found in <dir>"` (Datei vor Init prüfen). Root-Signatur: 4 Root-Konstanten bei b0, Tabelle t0–t2 + u0; PSO aus `a_composite`; Deskriptor-Heap 4 × `SlotRing::kSlots`. Zeitstempel-Heap 2 × Slots, Readback-Puffer.
-   **Anlegen** (in `Record`, wenn kein Feature, Größe oder Preset anders): alte Objekte `Retire`; Ausgabe- und Zieltextur `width×height`, Format = Format von `hudless` (`GetDesc`), `ALLOW_UNORDERED_ACCESS`; `DLSSNR.Width/Height`, `DLSSNR.Hint.Render.Preset`, `NVSDK_NGX_D3D12_CreateFeature` auf einer **eigenen** Liste mit eigenem Allocator, ausgeführt auf `TheBackend().queue`, per Fence abgewartet (~0,4 s). Logzeile mit Größe, Preset, VRAM aus `DLSSNRGetStatsCallback`.
-   **Auswertung**: Barrieren `COMMON → NON_PIXEL_SHADER_RESOURCE` für final/hudless/depth/motion; Parameter wie Spec 4.3 (`SetD3d12Resource`, Subrects, `MVecScaleX = motionWidth`, `MVecScaleY = motionHeight`, `Reset`, `Enabled` 1, Regler aus `NeuralOptions`); `NVSDK_NGX_D3D12_EvaluateFeature_C`. Fehlschlag: einmal je Ergebniscode loggen, `false`.
-   **Zusammensetzen**: Ausgabe `UNORDERED_ACCESS → NON_PIXEL_SHADER_RESOURCE`, `composed` in `UNORDERED_ACCESS`; Dispatch `ceil(w/8) × ceil(h/8)`; Root-Konstanten `{ width, height, view, 0 }`; danach `composed → COPY_SOURCE`, Slot-Texturen zurück nach `COMMON`. Die Ausgabe **bleibt** in `NON_PIXEL_SHADER_RESOURCE` (so wird sie für DLSS-G getaggt) und geht zu Beginn des nächsten `Record` zurück nach `UNORDERED_ACCESS`; `composed` ebenso von `COPY_SOURCE` zurück. Zeitstempel vor Evaluate und nach Dispatch, `ResolveQueryData` in den Slot-Bereich; ausgelesen beim nächsten `Record` desselben Slots.
-   `Retire`/`Collect`: Liste `{ value, ComPtr… , NVSDK_NGX_Handle* }`; freigeben, wenn `completed >= value`.

Shader `Composite.hlsl` (cs_5_0, `[numthreads(8,8,1)]`):

```hlsl
Texture2D<float4> Final : register(t0);
Texture2D<float4> Hudless : register(t1);
Texture2D<float4> Neural : register(t2);
RWTexture2D<float4> Composed : register(u0);
cbuffer Composite : register(b0) { uint2 Size; uint View; uint Pad; };
// View 0: neural where final == hudless, else final.
// View 1: left half final, right half as view 0.
// View 2: as view 0, kept pixels lerp(final, float4(1,0,1,1), 0.5).
```

Vergleich exakt (`any(f != h)`), Spec 4.4.

-   [ ] **Step 1: Shader schreiben.** Übersetzt wird er erst in Task 7 (`Setup` meldet Fehler wörtlich).
-   [ ] **Step 2: `NeuralRenderer` umsetzen.**
-   [ ] **Step 3: Bauen** — `cmake --build --preset FO4`, 0 Warnungen (die Klasse ist noch ungenutzt, `TheNeuralRenderer` aber definiert).
-   [ ] **Step 4: Commit** — `feat: neural renderer on the D3D12 bridge`

---

### Task 6: Anbindung an Present und Bridge.h

**Files:**

-   Modify: `src/Bridge/Bridge.h`, `src/Bridge/Native/DeviceHook.cpp` (Implementierung der neuen Aufrufe), `src/Bridge/Native/Presenter.h`, `src/Bridge/Native/Presenter.cpp`
-   Modify: `CMakeLists.txt`: Include-Pfad `${DLSSNR_DIR}/include` (falls nicht schon in Task 3)

**Interfaces:**

-   Consumes: Task 2 (`NeuralOptions`), Task 5 (`TheNeuralRenderer()`).
-   Produces in `Bridge.h` (`#include "Bridge/NeuralDecision.h"`, `<span>`):

```cpp
/// NGX on the bridge's D3D12 device; empty on success, else why not.
[[nodiscard]] std::string StartNeuralRendering(const std::filesystem::path& a_dllDirectory, std::span<const std::uint8_t> a_composite) noexcept;
/// Applied at the next Present. a_wanted false: present as without NR.
void SetNeuralRendering(const NeuralOptions& a_options, bool a_wanted) noexcept;
```

`Status` bekommt `bool neural{ false }; double neuralGpuMs{ 0.0 };`.

Ablauf in `Presenter::Present` (Spec 4.5): nach `list->Reset` und vor der Kopie `TheNeuralRenderer().Collect(_release.fence12->GetCompletedValue())`. Wenn `Wanted()` und `_inputsThisFrame && _hudlessThisFrame`: `Record(list, frame, _release.value + 1)`; bei `true` `CopyResource(target, Composed())` statt `slot.final`; FGs `split` wird dann übersprungen (NR hat eigene Ansichten). Sonst exakt der bisherige Weg (Review Focus 1). `Tag`: wenn NR in diesem Frame aufgezeichnet hat, `hudless`-Tag auf `Output()` im Zustand `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` statt auf `slot.hudless`. `ResizeBuffers` und `Destroy` rufen nach erfolgreichem `WaitForQueueIdle` `TheNeuralRenderer().Retire(0)` und `Collect(UINT64_MAX)`; das Feature wird beim nächsten `Record` in der neuen Größe angelegt. `LogSecond` hängt `neural {on|off} {:.2f} ms` an.

-   [ ] **Step 1: Umsetzen** wie beschrieben.
-   [ ] **Step 2: Bauen** — 0 Warnungen, alle `ctest` grün, `verify-plugin.ps1` grün.
-   [ ] **Step 3: Review-Fokus 1 und 4 gegenlesen**: der Zweig ohne NR ist zeichengleich mit dem alten (`git diff` des Present-Blocks), und `Record` wird nur mit `_hudlessThisFrame` des laufenden Frames gerufen.
-   [ ] **Step 4: Commit** — `feat: present through neural rendering`

---

### Task 7: Feature NeuralRendering

**Files:**

-   Create: `src/Features/NeuralRendering.h`, `src/Features/NeuralRendering.cpp`
-   Modify: `src/Feature/FeatureSystem.cpp` (nach `FrameGeneration` registrieren, Kommentar: liest denselben Abgriff, wird vor FG abgebaut), `package/F4SE/Plugins/CommunityShadersFO4/i18n/en.json` (generiert)

**Interfaces:**

-   Consumes: `Bridge::TheCapture()` (Task 4), `Bridge::StartNeuralRendering`, `Bridge::SetNeuralRendering`, `Bridge::MakeNeuralOptions`, `Bridge::NeuralBlocker` (Task 2/6).

Festlegungen:

-   `Declare`: Pfade und Vorgaben wie Spec 6.1 / Global Constraints; Choice-Listen `{"fast","original"}`, `{"1","2","3"}`, `{"off","split","mask"}`. Schlüssel `feature.neuralrendering.<key>` und `<key>_help`. Help des Features (englisch, wörtlich): „NVIDIA DLSS Neural Rendering (DLSS 5) re-renders the finished picture with a neural network for more lifelike light, materials and contrast; the HUD stays untouched. Needs an RTX 50 GPU, the Frame Generation archive and borderless windowed mode. Switching it on takes effect at the next game start unless Frame Generation was already on. Changing the model stalls the picture once. Uses NVIDIA DLSS technology."
-   `Setup`: Reihenfolge und Gründe — `Bridge::Active()` sonst `Bridge::Refusal()`; `Upscale::Hooks::Installed()`; Shader `NeuralRendering/Composite.hlsl` über `Shader::LoadSource(Util::DataDirectory() / "Shaders" / "FO4", …)` und `CompileComputeShader(…, "main")`; `StartNeuralRendering(Util::PluginDataDirectory() / "NeuralRendering", bytecode)` nur beim ersten Setup des Prozesses (danach ist NGX schon oben; Ergebnis merken); `TheCapture().Acquire(kNeuralRendering)`. Jede Ablehnung als `REX::ERROR("NeuralRendering: …")`.
-   `Frame`: `NeuralFacts` aus `UI::GetMenuOpen("LoadingMenu")`, `("PipboyMenu")`, `Main::inMenuMode`, `Menu::IsOverlayOpen()`, TAA-Schalter `REL::ID(2704658)`; `blocker = NeuralBlocker(...)`; `TheCapture().Want(kNeuralRendering, blocker == nullptr)`; `SetNeuralRendering(MakeNeuralOptions(settings frisch gelesen), blocker == nullptr)`; Grundwechsel ins Log; alle 180 Frames `neural rendering: {}, {:.2f} ms GPU` aus `Bridge::CurrentStatus()` und `TheCapture().LogSecond()`.
-   `Shutdown`: `SetNeuralRendering({}, false)`, `Want(…, false)`, `Release`.

-   [ ] **Step 1: Umsetzen.**
-   [ ] **Step 2: i18n** — `python tools/extract-i18n.py --write`, danach `python tools/extract-i18n.py` ohne Fehler.
-   [ ] **Step 3: Bauen und Paket** — `cmake --build --preset FO4`, `ctest …` grün, `pwsh tools/verify-plugin.ps1`, `cmake --build --preset FO4 --target package`, `pwsh tools/verify-package.ps1` grün (NeuralRendering-Archiv mit genau drei Dateien).
-   [ ] **Step 4: Paketprüfung brechen** — Zeile der Lizenz aus `EXTERNAL` entfernen, packen, prüfen. Erwartet: FAIL „the NeuralRendering addon carries …". Zurücknehmen, neu packen, grün.
-   [ ] **Step 5: Commit** — `feat: neural rendering feature`

---

### Task 8: Lauf 1 — NR an, FG aus

Vorher: Deploy-Bau (`FO4CS_DEPLOY_DIR` wie in der Build-Umgebung), Einstellungsdatei: `NeuralRendering/enabled` true, `FrameGeneration/enabled` true lassen (Brücke), FG im Overlay danach aus. Dem Nutzer eine nummerierte Liste geben, je Schritt was schiefgehen kann:

1. Spiel über `f4se_loader.exe` starten, Spielstand laden. Log: `NeuralRendering: running`, NGX-Zeile mit `310.9.16`, Anlegen mit Größe/Preset/VRAM. _Fehlt sie: Grund steht als `[E]`._
2. Overlay → Frame Generation an, 10 s laufen, wieder aus. _Gegenprobe zu Task 4: Logzeilen „frame generation"/„capture" wie im Kontroll-Lauf von F5.5b (79/158)._
3. Overlay → Neural Rendering → Ansicht `split`. _Rechts sichtbar anders, links unverändert. Rechts zu hell/dunkel: R1 doch offen._
4. Ansicht `mask`. _Nur HUD und Overlay magenta. Flächig magenta: R2._
5. Ansicht `off`, schnell im Kreis drehen. _Schlieren/Zittern: R3._
6. Bei offenem Overlay Stärke auf 0 und zurück; Modell `original` und zurück. _Stärke wirkt sofort; Modell ruckelt einmal._
7. Pip-Boy auf/zu, ESC-Menü auf/zu. _Log: Grundwechsel, keine Fehler._
8. An einer festen Stelle F11 mit NR aus, F11 mit NR an (`fast`), F11 mit `original`. NR-ms aus dem Log.

-   [ ] Ergebnisse in eine Stand-Notiz (Memory) und in den Roadmap-Entwurf; Befunde zu R1–R5 entscheiden, ob Korrekturen vor Lauf 2 nötig sind.

### Task 9: Lauf 2 — Abnahme, Dokumentation

1. NR + FG + DLSS Quality an einer schweren Stelle; F11 mit NR aus und an. Kriterium: Frame-Zeit-Anstieg ≤ 1,5 × NR-ms aus dem gleichen Zeitraum; angezeigt > gerendert.
2. Nutzer meldet: kein Schlieren, HUD ruhig, Menüs/Pip-Boy/Ladebildschirm schalten sauber; sein Urteil zum Look wird wörtlich festgehalten.
3. Log ohne `[E]` und ohne unerklärte `[W]`.

-   [ ] `docs/fallout4-port/ROADMAP.md`: Zeile F5.5c in der Tabelle, Abschnitt „Aus Teilprojekt F5.5c bestätigt" (Probe, Läufe, Zahlen, Ungeprüftes, Zugeständnisse) wie bei F5.5b.
-   [ ] `.claude/CLAUDE.md`, Abschnitt Bridge: NR in der Insel, `Bridge::Capture` als einziger Tap, DLL aus `cmake/DlssNr.cmake`, NR setzt das FG-Addon voraus.
-   [ ] Commit `docs: record F5.5c`, dann superpowers:finishing-a-development-branch.
