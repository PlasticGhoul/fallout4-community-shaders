# Teilprojekt F5.5a — DLSS Super Resolution

Stand 2026-10-04. Branch `port/f5-5a-dlss-sr`. Vorgezogen vor F6 auf Wunsch des Nutzers, wie F4.5.

## 1. Kontext und Ziel

Der Nutzer will DLSS 5 in Fallout 4. Die Recherche vom 2026-10-04 (Erinnerung
`fallout4-dlss-research`) ergab: **DLSS 5 (DLSS-NR) hat kein öffentliches SDK.** Streamline 2.14.1
kennt nur den Namen der Funktion, Header, Plugin und Laufzeit fehlen; was Mods benutzen, ist eine
geleakte oder aus dem Treiber gezogene DLL. Legal und heute möglich sind DLSS Super Resolution
(DLSS 4.5, Presets K, L, M) auf D3D11 und DLSS Frame Generation über eine D3D12-Brücke.

Der Nutzer hat entschieden:

-   **Teilen** in F5.5a (Super Resolution, dieses Dokument) und F5.5b (Frame Generation über eine
    D3D12-Brücke mit Reflex). Die Brücke aus F5.5b ist zugleich der Andockpunkt für DLSS 5, sobald
    NVIDIA das SDK freigibt.
-   **DLAA und Hochskalierung** (Quality, Balanced, Performance, Ultra Performance).
-   **NGX direkt auf D3D11** (DLSS-SDK 310.9.1), ohne Streamline. Streamline nimmt je Prozess eine
    API; F5.5b braucht es auf D3D12 für DLSS-G und Reflex. Super Resolution über NGX kommt damit
    nicht in die Quere und muss später nicht umziehen.
-   **doodlums Weg übernehmen.** Sein Fallout-4-Upscaler (`doodlum/fo4test`, Zweig `upscaler`,
    GPL-3.0, Stand 2025-12-15) hat Jitter, Renderauflösung und TAA-Ersatz für diese Engine gelöst.
-   **`nvngx_dlss.dll` im eigenen DLSS-Archiv mitliefern**, mit NVIDIA-Nennung im Overlay.

Ziel: DLSS ersetzt das TAA der Engine, als DLAA bei voller Auflösung oder als Hochskalierung aus
einer niedrigeren Renderauflösung. Abnahmekriterium wie alle F-Teilprojekte: sichtbarer Effekt
plus CPU-/GPU-Zahlen.

### Was F5.5a erbt

`Render::Phase` und `PhaseDispatcher`, `Render::Profiler` und `PassScope`, `Render::Targets`,
`TargetView` als Sonde, `TexelProbe`, `StateGuard`, das Feature-Gerüst mit Einstellungen und
Overlay-Seite, `Util::PluginDataDirectory()`, die Paketierung mit Addon-Archiven.

### Die Befunde, die den Entwurf bestimmen

1. **doodlums Einhängestellen gelten auf 1.11.240.** Offline gegen `version-1-11-240-0.bin` und
   `Fallout4.exe` geprüft (Skript im Scratchpad, `addrcheck.py`): alle zehn Aufrufstellen tragen
   am erwarteten Offset ein `E8`, jedes Aufrufziel ist eine Funktions-ID der Address Library, und
   die Forward-Stelle ruft genau `2318315`, die Funktion, die doodlum selbst als ForwardAlpha
   einhängt. Die Datenadressen liegen in `.data`.

    | Zweck                                        | ID                                         | Offset  | Ziel-ID   |
    | -------------------------------------------- | ------------------------------------------ | ------- | --------- |
    | `BSGraphics::State::UpdateTemporalData`      | `2318286`                                  | `0x38C` | `2277095` |
    | `DrawWorld_Imagespace` `RenderEffectRange`   | `2318322`                                  | `0x83`  | `2316593` |
    | `SetUseDynamicResolutionViewportAsDefault…`  | `2318322`                                  | `0xC5`  | `2277194` |
    | `Render_PreUI` DeferredPrePass               | `2318321`                                  | `0x2E3` | `2318301` |
    | `Render_PreUI` Forward                       | `2318321`                                  | `0x3A6` | `2318315` |
    | `Render_PreUI` 5-Byte-Nop                    | `2318321`                                  | `0x29F` | `2277195` |
    | ForwardAlpha `PostResolveDepth`              | `2318315`                                  | `0x4C6` | `2317870` |
    | DeferredComposite `RenderPassImmediately`    | `2318313`                                  | `0x915` | `2318699` |
    | `BSImagespaceShaderSSLRRaytracing` Begin     | `2317302`                                  | `0x1C`  | `2318876` |
    | `BSImagespaceShaderLensFlare` (Prolog)       | `2317547`                                  | —       | —         |
    | Sampler-States (320), Near, Far, `enableTAA` | `2704455`, `2712882`, `2712883`, `2704658` | —       | —         |

2. **Die Engine hat eine dynamische Auflösung.** `RenderTargetManager::dynamicWidthRatio` und
   `dynamicHeightRatio` steuern die Renderauflösung, `BSGraphics::State::offsetX/Y` den Jitter in
   NDC. doodlum ersetzt zwölf Ziele durch kleinere (`20, 57, 24, 25, 23, 58, 59, 28, 3, 9, 60,
61`), setzt einen negativen Mip-Bias auf alle 320 Sampler-States der Engine und schaltet nach
   der Hochskalierung die dynamische Auflösung ab.
3. **TAA läßt sich über `ImageSpaceEffectTemporalAA` vtable-Slot 8 (`IsActive`) abschalten.**
4. **DLSS läuft im HDR.** Ein- und Ausgabe sind `RT_004` (`R11G11B10_FLOAT`), die Tiefe
   `DS_002`, die Bewegungsvektoren `RT_029` (`R16G16_FLOAT`) nach einer Dilatation auf 5×5 über
   die nähere Tiefe. doodlums Konstanten: Bewegungsvektoren mit Kamerabewegung, nicht gejittert,
   Skala 1 (UV-Einheiten); Tiefe nicht invertiert; Jitter negiert.
5. **NGX braucht keinen Hook auf die Geräteerzeugung.** Anders als Streamline setzt es auf dem
   fertigen Gerät auf (`NVSDK_NGX_D3D11_Init_with_ProjectID`).
6. **Lizenz.** Das DLSS-SDK erlaubt die Weitergabe der Laufzeit eingebettet in eine Anwendung
   mit eigener Funktion (Abschnitt 1.c, 2.a), verbietet die Unterwerfung unter eine
   Open-Source-Lizenz (4.e) und verlangt die Nennung (Ergänzung 7.1). Unsere `EXCEPTIONS.md`
   nimmt Modding-Bibliotheken ausdrücklich von der GPL aus; die Skyrim-Vorlage und doodlum
   liefern die DLL genauso aus. Eine Restunsicherheit bleibt und ist benannt.

## 2. Umfang

### In F5.5a enthalten

-   Feature `DLSS` mit Modus, Preset, Schärfe; DLAA und vier Hochskalier-Modi.
-   Die Engine-Hooks aus Befund 1, die Ersatzziele, der Mip-Bias, der TAA-Schalter.
-   NGX-Hülle, Dilatation der Bewegungsvektoren, Tiefe in voller Auflösung, RCAS.
-   doodlums Ersatz für den SSR-Shader, mit Rückfall (Abschnitt 9).
-   `Render::Resolution` und die Umstellung von F2, F3, F4 und F5 darauf.
-   DLSS-SDK als Submodul, die DLL im DLSS-Archiv, die NVIDIA-Nennung.

### Nicht in F5.5a enthalten

-   Frame Generation, Reflex, die D3D12-Brücke — F5.5b.
-   DLSS 5 (DLSS-NR) — ohne öffentliches SDK nicht möglich; F5.5b bereitet den Andockpunkt.
-   FSR und XeSS als Rückfall für andere Grafikkarten. Ohne NVIDIA-Karte lehnt das Feature ab.
-   Ray Reconstruction — braucht G-Buffer-Eingaben eines Denoisers, die Fallout 4 nicht hat.
-   Eine eigene Belichtungstextur. DLSS bekommt `AutoExposure`.

## 3. Vorentscheidungen

-   **NGX direkt auf D3D11**, nicht Streamline (Abschnitt 1).
-   **Hooks beim Laden des Plugins**, nicht in `Setup`. Ein Call-Hook im laufenden Spiel zu setzen
    hieße, Code zu patchen, den der Render-Thread gerade ausführen kann. Die Hooks reichen durch,
    solange das Feature nicht läuft; das Feature schaltet nur ein Flag.
-   **Menüs nativ.** In `PipboyMenu` und `ExamineMenu` rendert das Spiel mit voller Auflösung und
    eigenem TAA, wie bei doodlum: der Pip-Boy hat eigene Ziele und eigene TAA-Techniken.
-   **ENB: nur DLAA.** Mit geladenem ENB keine Hochskalierung, wie bei doodlum.
-   **Ein Upscaler zur Zeit.** Ist doodlums `Upscaling.dll`, ein anderes bekanntes Upscaler-Plugin
    oder OptiScaler geladen, lehnt das Feature ab und nennt das Modul.

## 4. Die Bausteine

### 4.1 `Upscale::Settings` (rein, Host-Test)

-   `RenderSize(display, mode)` — Rückfall-Tabelle der DLSS-Faktoren (DLAA 1,0; Quality 0,667;
    Balanced 0,58; Performance 0,5; Ultra Performance 0,333), gerundet auf ganze Pixel. Im Spiel
    gilt, was NGX über `NGX_DLSS_GET_OPTIMAL_SETTINGS` meldet; die Tabelle greift, wenn die Abfrage
    scheitert.
-   `JitterPhaseCount(render, display)` = `ceil(8 · (display / render)²)`.
-   `Halton(index, base)` und `JitterOffset(frame, phases)` in Pixeln, Bereich (−0,5, 0,5].
-   `EngineOffset(jitter, display)` → `State::offsetX/Y` in NDC, x negiert (doodlum, Befund 4).
-   `MipBias(render, display)` = `log2(render / display) − 1` (NVIDIA-Empfehlung für DLSS).

### 4.2 `Upscale::CameraCut` (rein, Host-Test)

Entscheidet je Frame, ob DLSS seinen Verlauf verwerfen muss: Kamerasprung über eine Schwelle in
Einheiten, Drehung über eine Schwelle in Grad, geändertes Sichtfeld, oder ein von außen gesetztes
Ereignis (Laden, Menü zu, Moduswechsel). Schwellen als Konstanten mit Begründung.

### 4.3 `Upscale::Ngx`

Dünne Hülle um das DLSS-SDK: `Init(device, dataPath)` mit eigener Projekt-ID und
`NVSDK_NGX_ENGINE_TYPE_CUSTOM`, die DLL über `FeatureCommonInfo.PathListInfo` aus
`Util::PluginDataDirectory()`; `Available()` aus den Capability-Parametern;
`Ensure(render, display, quality, preset)` legt das DLSS-Objekt an oder neu an
(`NGX_D3D11_CREATE_DLSS_EXT`, Flags `IsHDR | MVLowRes | AutoExposure`, Preset über
`DLSS.Hint.Render.Preset.<Modus>` = `Default`/`K`/`L`/`M`); `Evaluate(...)` über
`NGX_D3D11_EVALUATE_DLSS_EXT` mit Jitter, Subrect, Reset und MV-Skala; `Shutdown()`. Jeder
Fehler kommt mit dem `NVSDK_NGX_Result` ins Log. Die statische Bibliothek ist `nvsdk_ngx_d.lib`
(unsere Laufzeit ist `/MD`).

### 4.4 `Upscale::EngineHooks`

Installiert beim Laden alle Stellen aus Befund 1 über `REL::THook` (Call-Stellen) und unseren
`VTablePatch` (TAA `IsActive`). Vor jedem Schreiben werden die Bytes geprüft: steht an einer
Stelle kein `E8`, wird **nichts** installiert, ein `[E]` nennt die Stelle, und das Feature lehnt
später mit diesem Grund ab. Der Prolog-Hook auf LensFlare geht über dessen Aufrufstellen (eine
Sonde zählt sie offline wie in Befund 1); gibt es keine eindeutige, entfällt er, und die Tiefe
für den Lens Flare wird stattdessen vor seinem Pass ersetzt.

### 4.5 `Upscale::ProxyTargets`

Die zwölf kleineren Ersatzziele (Befund 2), angelegt bei Modus- oder Fensterwechsel, gesetzt vor
der Welt und zurückgegeben nach der Hochskalierung; dazu die 320 Sampler-States mit Mip-Bias.
Bei DLAA entfällt beides.

### 4.6 Shader

Unter `package/Features/DLSS/Shaders/FO4/DLSS/`: `DilateMotionVectors.hlsl`, `FullDepth.hlsl`
(Tiefe auf volle Auflösung, linear dazu), `RCAS.hlsl`, und `SSLRRaytracing.hlsl` (doodlums
bearbeiteter Engine-Shader). Übersetzung wie alle unsere Shader zur Laufzeit, mit Hot-Reload.

### 4.7 `Render::Resolution`

Eine Stelle für `Render()` und `Display()` (Breite, Höhe) und den Faktor. Ohne DLSS sind beide
gleich der Größe von `RT_004`. F2 bis F5 lesen ihre Bildgröße künftig hier statt aus einer
Zieltextur, und ihre Viewports decken nur den Renderbereich. Host-Test für die Umrechnung von
Pixel- und UV-Koordinaten zwischen beiden.

## 5. Ablauf im Frame

1. **`UpdateTemporalData`** (Frame-Anfang): Modus → Renderauflösung → Jitter → `offsetX/Y` und
   die Auflösungsfaktoren; bei Wechsel Ersatzziele, Sampler und DLSS-Objekt neu. In den Menüs
   aus Abschnitt 3 Faktor 1 und kein Jitter.
2. **Die Welt** rendert in Renderauflösung, mit unseren Phasen `kBeforeComposite`, `kComposite`
   und `kAfterOpaque`.
3. **Nach `RenderEffectRange`**: dynamische Auflösung aus, Dilatation, Kopie des Renderbereichs
   von `RT_004` in eine eigene Textur (NGX bekommt Ein- und Ausgabe getrennt), DLSS nach `RT_004`
   in voller Auflösung, RCAS, Tiefe in voller Auflösung für die folgenden Effekte.
4. **Danach** Tonemapping, Effekte, HUD und Menüs in voller Auflösung, unser Overlay am Present.

Reset bei Laden, nach Pip-Boy oder Examine, bei Moduswechsel und bei einem Kameraschnitt (4.2).

## 6. Das Feature

### 6.1 Einstellungen

| Pfad             | Art    | Vorgabe   | Bereich / Liste                                                   |
| ---------------- | ------ | --------- | ----------------------------------------------------------------- |
| `DLSS/enabled`   | Bool   | aus       | — (`DeclareFeature`)                                              |
| `DLSS/mode`      | Choice | `quality` | `dlaa`, `quality`, `balanced`, `performance`, `ultra_performance` |
| `DLSS/preset`    | Choice | `default` | `default`, `k`, `l`, `m`                                          |
| `DLSS/sharpness` | Slider | 0,0       | 0,0 – 1,0                                                         |

Vorgabe **aus**: ein Feature, das die Renderauflösung der Engine ändert, schaltet niemand
unbemerkt ein. Die `Help` des Schalters nennt den Effekt und „NVIDIA DLSS"; die Seite trägt die
Nennung aus Befund 6.

### 6.2 Lebenszyklus

`Setup`: NGX starten, Fähigkeit prüfen, Hooks installiert? Jede Ablehnung mit eigenem Grund
(keine NVIDIA-Karte oder Treiber zu alt, DLSS nicht verfügbar, DLL fehlt, Hooks nicht
installiert, fremder Upscaler). `Frame`: liest die Einstellungen, legt bei Bedarf neu an.
`Shutdown`: Faktor 1, Jitter 0, TAA wieder an, Ersatzziele und Sampler zurück, DLSS-Objekt und
NGX frei — im laufenden Spiel, wie E2 es für `ImagespaceTint` belegt hat.

### 6.3 Auslieferung

Eigenes Archiv (kein `CORE`): `F4SE/Plugins/CommunityShadersFO4/nvngx_dlss.dll` (310.9.1, 59 MB) und die
Shader. `verify-package.ps1` prüft, daß die DLL im DLSS-Archiv und im All-in-One liegt und in
keinem anderen. Das DLSS-SDK kommt als Submodul `extern/DLSS` auf den Tag `v310.9.1`.

## 7. Zusammenspiel

-   **F2 bis F5** auf `Render::Resolution` (4.7). Skylighting und Screen-Space Shadows lesen die
    Tiefe in Renderauflösung, Nebel und Wolkenschatten schreiben in den Renderbereich.
-   **Leistungstafel**: Zeile `DLSS/Evaluate` (GPU und CPU), dazu `DLSS/Prepare` für Dilatation
    und Kopie.
-   **Overlay** am Present, unberührt.
-   **TargetView** zeigt Ziele weiterhin in ihrer ganzen Größe; bei Hochskalierung ist das obere
    linke Rechteck der gültige Teil, und genau das ist die erste Sonde.

## 8. Prüfung

### 8.1 Host-Tests

`UpscaleSettingsTests`: Renderauflösungen je Modus bei 2560×1440 und 1920×1080, Phasenzahl,
Halton gegen bekannte Werte, Jitter im Bereich, Vorzeichen von `EngineOffset`, Mip-Bias.
`CameraCutTests`: Sprung, Drehung, Sichtfeld, Ereignis, und kein Reset bei normaler Bewegung.
`ResolutionTests`: Umrechnung Render ↔ Anzeige. Jeder Test wird einmal absichtlich gebrochen, und
vorher wird geprüft, daß der Bau geglückt ist.

### 8.2 Sonde (Lauf 1, DLAA)

Sekundenzeile: Modus, Render- und Anzeigegröße, Jitter des Frames, Auswertungen, Resets,
NGX-Ergebnis. Dazu zweimal je Sekunde eine `TexelProbe` auf `RT_029` (Größenordnung und Vorzeichen
der Bewegungsvektoren beim Seitwärtsgehen) — sie belegt Befund 4, bevor die Hochskalierung
darauf baut.

### 8.3 Abnahme (Lauf 2)

| #   | Schritt                                                             | Geht es schief, heißt das                                       |
| --- | ------------------------------------------------------------------- | --------------------------------------------------------------- |
| 1   | Overlay: Seite `DLSS` mit Schalter, Modus, Preset, Schärfe, Nennung | `Declare` lief nicht                                            |
| 2   | DLAA an und aus, an Zäunen und Stromleitungen                       | Kein Unterschied: TAA läuft noch, oder DLSS wertet nicht aus    |
| 3   | Seitwärts gehen und Kamera drehen: kein Schmieren, kein Zittern     | Schmieren: MV-Skala oder Vorzeichen; Zittern: Jitter-Vorzeichen |
| 4   | Quality, Balanced, Performance, Ultra Performance                   | Bild in einer Ecke: Faktor nicht gesetzt; unscharf: Mip-Bias    |
| 5   | Debug-Ansichten von F2–F5 in Quality: deckungsgleich mit dem Bild   | Verschoben: eine Größe nicht auf `Render::Resolution`           |
| 6   | Pfütze oder Wasser mit Spiegelung in Quality                        | Falsche Spiegelung: SSR-Ersatz paßt nicht (Abschnitt 9)         |
| 7   | Pip-Boy auf und zu, Laden, Alt-Tab                                  | Ghosting danach: Reset fehlt; Absturz: Ersatzziele              |
| 8   | Feature im laufenden Spiel aus und an                               | Bild bleibt klein: `Shutdown` gibt nicht alles zurück           |
| 9   | F11-Paar je Modus und aus                                           | Zeile fehlt: Pass läuft nicht                                   |
| 10  | Hot-Reload der vier Shader (ich berühre die Dateien)                | Keine „recompiling"-Zeile                                       |

Log: keine `[W]`- und `[E]`-Zeilen, die Sekundenzeile in jedem Intervall.

## 9. Risiken und Gabelungen

**Das tragende Risiko:** daß doodlums Konventionen (MV-Skala, Jitter-Vorzeichen, Ersatzziele)
auf 1.11.240 nicht mehr stimmen, obwohl die Aufrufstellen es tun. Die Sonde (8.2) entscheidet;
jede Abweichung ist eine Konstante in `Upscale::Settings`, kein neuer Entwurf.

**Kleinere Gabelungen:**

-   **SSR-Ersatz paßt nicht** (der Engine-Shader hat sich seit 2025 geändert). Dann schaltet das
    Feature SSR in den Hochskalier-Modi ab und nennt das als Zugeständnis.
-   **LensFlare hat keine eindeutige Aufrufstelle** — siehe 4.4.
-   **NGX lehnt die gemeinsame Textur ab** — die eigene Kopie aus Abschnitt 5 ist schon der
    Ausweg; scheitert auch sie, liegt es am Format, und `RT_004` wird vorher in `R16G16B16A16`
    kopiert.
-   **Eine Größe in F2–F5 übersehen** — zeigt sich in Schritt 5 der Abnahme.
-   **ENB oder ein fremder Upscaler** — Abschnitt 3.

## 10. Zugeständnisse

Keine Hochskalierung im Pip-Boy und im Examine-Menü. Kein FSR oder XeSS für andere Karten. Die
DLL kommt von NVIDIA unter deren Lizenz, nicht unter unserer. Die Restunsicherheit aus Befund 6.

## 11. Was F5.5a für F5.5b hinterläßt

Jitter, Bewegungsvektoren, Tiefe und Kamerakonstanten, die DLSS-G ebenfalls braucht, belegt
durch die Sonde; `Render::Resolution`; die Reset-Erkennung; die Einhängestelle nach
`RenderEffectRange`, vor der das HUD-lose Bild für DLSS-G abzugreifen ist.
