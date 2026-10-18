# Teilprojekt F5.5c — DLSS 5 Neural Rendering

Stand 2026-10-18. Branch `port/f5-5c-neural-rendering` (von `dev` 0f1cd602). Dritter Teil des vor
F6 vorgezogenen DLSS-Blocks; F5.5a (Super Resolution) und F5.5b (Frame Generation) sind
abgeschlossen.

## 1. Kontext und Ziel

„DLSS 5" ist genau ein neues Feature: **DLSS Neural Rendering** (SDK-Name DLSS-NR, NGX-Feature
18, Laufzeit `nvngx_dlssnr.dll`). Ein Netz nimmt das fertige Bild in Anzeigegröße und rechnet es
neu — Licht, Material, lokaler Kontrast, Haut. Es skaliert nicht hoch und erzeugt keine Bilder;
Super Resolution und Frame Generation sind DLSS 4 und liegen schon in F5.5a/b.

Der Nutzer hat entschieden (alle per Rückfrage):

-   **Ziel ist das Gesamtbild**: überall glaubwürdigeres Licht und Material, wie in NVIDIAs
    Vorführungen. Keine gezielte Steuerung auf Gesichter oder Bereiche, also keine eigene
    Steuermaske.
-   **Kosten wählbar über das Modell**: Preset B (neu, schnell) als Vorgabe, Preset A (alt) zum
    Vergleich. Keine weiteren Stufen.
-   **Wann aktiv**: wie Frame Generation aus im Ladebildschirm, Pip-Boy und Spielmenüs — **aber an
    bei offenem Overlay**, damit die Regler live am Bild eingestellt werden können. Dialoge zählen
    nicht als Menü.
-   **Erst Probe, dann F5.5c**: die Probe ist bestanden (Abschnitt 1.2).
-   **NR setzt das Addon `FrameGeneration` voraus.** Die Brücke steht nur mit Streamline
    (`Streamline::Load` scheitert ohne `sl.interposer.dll`), und Streamline liegt in diesem Addon.
    FG selbst darf dabei aus bleiben. Kein zweiter Startweg der Brücke ohne Streamline.

Ziel: Neural Rendering im laufenden Spiel, mit HUD und Overlay unberührt, mit und ohne Frame
Generation, gemessen in Kosten, beurteilt vom Nutzer am bewegten Bild.

### 1.1 Was F5.5c erbt

Aus F5.5b die D3D12-Brücke (`src/Bridge/`): eigenes D3D12-Gerät, ein Ring aus drei Slots
geteilter Texturen — **Endbild** (mit HUD und Overlay), **HUD-loses Bild** (nach Tonemap, in
Anzeigegröße, abgegriffen an `SetUseDynamicResolutionViewportAsDefault(false)`, `2318322+0xC5`),
**Tiefe** (`R32_FLOAT`) und **Bewegung** (`R16G16_FLOAT`, beide in Größe der Bewegungsvektoren)
— zwei geteilte Fences mit je einem Schreiber, der Startschalter, `Presenter::Present` als Ort, an
dem D3D12 das Bild in die Swapchain kopiert. Aus F5.5a die NGX-Projekt-ID
`99023707-327d-45dc-ba45-0c47e8445542` mit Engine-Version `Fallout 4 1.11.240`, die Reset-Erkennung
und `nvsdk_ngx_d.lib` aus `extern/DLSS`.

### 1.2 Was die Probe belegt hat

Testprogramm außerhalb des Repos (Quelle im Memory-Ordner, `dlss5-material/probe/`), RTX 5080,
Treiber 617.42, 2026-10-18:

1. **Keine Sperre greift.** `NVSDK_NGX_D3D12_Init_with_ProjectID` mit unserer Projekt-ID und
   `PathListInfo` auf den Ordner der DLL: `Success`. Der Treiber-NGX-Kern lädt die DLL selbst
   (`NGXSecureLoadFeature … version: 310.9.16`), die Aufruferprüfung der DLL ist damit erfüllt.
   NGX bildet die Projekt-ID auf die generische CMS-ID `876232c` ab, die Sperrliste meldet für
   `dlssnr` „denial 0". `DLSSNR.Available = 1`, Mindesttreiber 615.0.
2. **Anlegen und Auswerten gehen über die generische D3D12-API.** `NVSDK_NGX_D3D12_CreateFeature`
   mit Feature 18 nach `DLSSNR.Width/Height`, auf einer eigenen Kommandoliste, etwa 0,35–0,45 s.
   Ressourcen per `NVSDK_NGX_Parameter_SetD3d12Resource`, Auswertung per
   `NVSDK_NGX_D3D12_EvaluateFeature_C`. Farbe `R8G8B8A8_UNORM` in `NON_PIXEL_SHADER_RESOURCE`,
   Ausgabe `R8G8B8A8_UNORM` mit UAV in `UNORDERED_ACCESS`.
3. **Kosten bei 2560×1440**: Preset A (= Default) 5,4 ms GPU und 547 MB VRAM, **Preset B
   3,05 ms und 482 MB**. C bis G liefern byte-identisch dasselbe Bild wie B. Absolute Zahlen
   schwankten (ein anderes Spiel lief im Hintergrund), das Verhältnis A zu B nicht.
4. **`DLSSNR.ScalingRatio` ist kein Tempo-Regler**: DLAA/Quality ergeben 1,0, die übrigen
   Stufen werden abgelehnt, die Zeit bleibt gleich.
5. **Alle Regler wirken je Auswertung**, ohne Neuanlegen: Intensity 0 gibt die Eingabe bitgenau
   zurück, 0,5 halbiert die Abweichung; Tone, Structure und Style ändern das Ergebnis sofort.
   Nur der Wechsel des Presets braucht ein neues Feature. (OptiScalers Angabe, die Regler würden
   beim Anlegen eingefroren, gilt für diesen Weg nicht.)
6. **`Style` 0/1/2 sind drei Look-Varianten** (Remix: „Model A/B/C"), unabhängig vom Preset.
7. **NGX auf D3D11 und D3D12 in einer Instanz vertragen sich**: DLSS SR auf einem D3D11-Gerät
   angelegt und ausgewertet, danach NR auf D3D12, danach SR erneut ausgewertet — alles
   `Success`, auch nach `NVSDK_NGX_D3D12_Shutdown1`.

Die Quelle der DLL ist das Paket, das NVIDIAs RTX Remix (MIT) für seine NR-Integration lädt:
`https://d1oqksizpb0e3d.cloudfront.net/rtx-remix-ngx_sdk_dlnr@2.7z`, 368 MB, SHA256
`4bb8651b3ac2180f23df1f39422c489810a13d445a94dc836c876c18c93ecefe`. Es enthält
`nvngx_dlssnr.dll` 310.9.16.0 (gültig NVIDIA-signiert, SHA256 `2bc19bc0…789f`), die Header
`nvsdk_ngx_defs_dlssnr.h` und Vulkan-Helfer, und `LICENSE.txt` = NVIDIA RTX SDKs License — dieselbe
Lizenz wie `nvngx_dlss.dll` und Streamline, die schon ausgeliefert werden.

## 2. Umfang

### In F5.5c enthalten

-   Neural Rendering auf der D3D12-Seite der Brücke, Eingang HUD-loses Bild, Tiefe, Bewegung.
-   Zusammensetzen mit HUD und Overlay, sodass beide unverändert bleiben.
-   Zusammenspiel mit Frame Generation (FG interpoliert das verbesserte Bild) und DLSS SR.
-   Herauslösen des Abgriffs (Tiefe, Bewegung, HUD-los, Kamera) aus `FrameGeneration`, damit NR
    ohne FG läuft.
-   Startschalter der Brücke: `FrameGeneration/enabled` **oder** `NeuralRendering/enabled`.
-   Feature `NeuralRendering` mit Einstellungen und Overlay-Seite, Vergleichs- und Maskenansicht.
-   Bezug der DLL per CMake mit SHA256, eigenes Addon `NeuralRendering`.

### Nicht in F5.5c enthalten

-   Steuermaske (`DLSSNR.ControlMask`), UI-Puffer für NR (`DLSSNR.UI`/`UIAlpha`), das
    Verzerrungsfeld.
-   Rechnen auf verkleinertem Bild (OptiScalers „reduced"-Pfad), Highlight-Recovery wie Remix.
-   NR ohne Brücke (die DLL exportiert D3D11-Einsprünge, OptiScaler meldet dort
    `FeatureNotSupported` — nicht verfolgt).
-   RTX 40 (NVIDIA: „later this fall"); die Verfügbarkeit fragt NGX selbst ab.
-   HDR-Ausgabe, exklusives Vollbild, ENB — wie F5.5b.

### Anti-Ziele

Ein Ergebnis gilt trotz laufender Technik als gescheitert, wenn:

-   HUD-Text, Fadenkreuz, Kompass oder das Overlay sichtbar verändert werden;
-   das Bild in Bewegung schliert oder flimmert, weil Bewegungsvektoren falsch übergeben werden;
-   das Bild bei ausgeschaltetem NR auch nur ein Bit anders ist als in F5.5b;
-   eine veränderte oder ungeprüfte DLL ausgeliefert wird.

## 3. Vorentscheidungen

Vom Nutzer: Abschnitt 1. Meine Entscheidungen, als solche hier festgehalten:

-   **Direkt über NGX, nicht über Streamline.** Streamline 2.14.16 kennt NR nur als Enum; die
    Probe hat den NGX-Weg belegt.
-   **Platzierung nach Tonemap und SR, in Anzeigegröße** — NVIDIAs und Remix' Vertrag (LDR 0–1,
    Ausgabe getrennt). Der Brückenabgriff liegt genau dort.
-   **Das HUD wird per Vergleich herausgehalten**, nicht über NRs UI-Eingänge: wo Endbild und
    HUD-loses Bild sich unterscheiden, ist HUD oder Overlay, dort bleibt das Endbild stehen.
-   **Die Regler sind Remix' Regler mit Remix' Vorgaben** (Abschnitt 6.1). Alle wirken live.
-   **Die DLL kommt per CMake-Download mit SHA256** wie Streamline und wird unverändert in einem
    eigenen Addon ausgeliefert, nach `F4SE/Plugins/CommunityShadersFO4/NeuralRendering/`.

## 4. Die Bausteine

### 4.1 `Bridge::StartupSwitch` (rein, Host-Test, erweitert)

Liest künftig zwei Schalter und meldet die Brücke an, wenn mindestens einer `true` ist. Der Grund
im Aus-Fall nennt beide. Die bestehenden Fälle des Host-Tests bleiben, neu kommen hinzu: nur NR an,
beide an, NR-Block fehlt bei FG an, NR-Wert kein Boolean.

### 4.2 Abgriff (heraus aus `FrameGeneration`)

Was `FrameGeneration` heute in `AfterTemporalData`, `AfterEffectRange` und
`AfterDynamicViewportDefault` tut — Render-Beginn melden, Tiefe auf Bewegungsgröße abtasten,
Bewegung kopieren, Kamera und Reset bauen, HUD-loses Bild kopieren — wandert in einen eigenen
Baustein, der der einzige `Upscale::Hooks::Tap` ist. Er greift ab, solange mindestens ein
Abnehmer es in diesem Frame verlangt (FG will generieren **oder** NR will rechnen), und meldet
jeden Abgriff an die Brücke wie bisher. FG und NR melden ihren Bedarf je Frame aus `Frame`. Die
einmal je Sekunde geloggten Zähler und Ablehnungsgründe ziehen mit um. Name und Ort (etwa
`src/Bridge/Capture.*` außerhalb der nativen Insel) entscheidet der Plan. Verhalten von FG
ändert sich dabei nicht; Lauf 1 belegt das.

### 4.3 NR in der nativen Insel

Ein neuer Teil von `src/Bridge/Native/`, ohne PCH, mit `nvsdk_ngx.h` aus `extern/DLSS/include`
und **nur** `nvsdk_ngx_defs_dlssnr.h` aus dem Paket (dessen eigene `nvsdk_ngx.h`/`_params.h`
dürfen die des DLSS-SDK nicht überschatten). Er besitzt:

-   NGX auf dem D3D12-Gerät der Brücke (dem echten, nicht Streamlines Stellvertreter),
    Capability-Parameter, das Feature, eine Ausgabetextur (D3D12-eigen, UAV, Anzeigegröße) und
    eine Zieltextur für das zusammengesetzte Bild;
-   den Compute-Pass für das Zusammensetzen (4.4) mit Root-Signatur und Deskriptoren — der erste
    Compute-Pass auf der D3D12-Seite;
-   Zeitstempel um Auswertung und Zusammensetzen, ausgewertet mit Ring wie bei den
    D3D11-Profilern, einmal je Sekunde ins Log und in `Bridge::Status`.

**Start.** `Bridge.h` bekommt einen Aufruf, der NGX auf D3D12 startet und das Ergebnis
zurückgibt — Erfolg oder ein Grund aus Ergebniscode, `DLSSNR.Available`,
`NeedsUpdatedDriver`/`MinDriverVersion*` und `FeatureInitResult`. Das Feature ruft ihn aus
`Setup`: alles, was dort scheitert, scheitert bis zum Neustart wieder (keine RTX 50, DLL fehlt,
Treiber zu alt).

**Anlegen.** Beim ersten gewünschten Frame, nach jeder Größenänderung und bei Wechsel des
Presets, auf einer eigenen Kommandoliste, mit Fence vor der ersten Auswertung (OptiScaler sah
Hänger beim Anlegen in derselben Liste). Ein abgelöstes Feature und alte Texturen werden erst
freigegeben, wenn die Queue an ihnen vorbei ist — nie unter der GPU.

**Auswerten.** In `Presenter::Present`, nachdem D3D12 auf den Slot gewartet hat und bevor das Bild
in die Swapchain kopiert wird. Parameter (Namen aus `nvsdk_ngx_defs_dlssnr.h`):

| Parameter                                                                                                                    | Wert                                                                                     |
| ---------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `Color`                                                                                                                      | HUD-loses Bild des Slots, Subrect Anzeigegröße                                           |
| `Output`                                                                                                                     | NR-Ausgabetextur, Subrect Anzeigegröße                                                   |
| `MVec`, `Depth`                                                                                                              | Slot, Subrect `motionWidth`×`motionHeight`                                               |
| `MVecScaleX/Y`                                                                                                               | Umrechnung der UV-Vektoren nach NGX-Konvention (R3)                                      |
| `DepthInverted`                                                                                                              | 0                                                                                        |
| `Reset`                                                                                                                      | Reset der Kamera (Schnitt, Rückkehr nach Aus-Strecke)                                    |
| `Enabled`                                                                                                                    | 1                                                                                        |
| `Intensity`, `LocalToneStrength`, `LocalStructureStrength`, `SkinStructureStrength`, `Style`, `UseAutoMask`, `MaskBlendMode` | aus den Einstellungen (6.1); `MaskBlendMode` = Min bei Auto-Maske, sonst None, wie Remix |

Die geteilten Texturen verfallen nach jeder Übergabe in `COMMON`; vor der Auswertung werden sie
explizit in `NON_PIXEL_SHADER_RESOURCE` gebracht und danach zurück.

### 4.4 Zusammensetzen

Ein Compute-Shader mit drei Eingängen (Endbild, HUD-loses Bild, NR-Ausgabe) und einem Ausgang:
je Pixel das NR-Ergebnis, wo Endbild und HUD-loses Bild gleich sind, sonst das Endbild. Damit
bleiben HUD, halbtransparente HUD-Flächen, Fader und das Overlay das Original. Der Vergleich ist
exakt (beide Bilder sind Kopien desselben Backbuffers im selben Format); ob eine Toleranz nötig
ist, zeigt die Maskenansicht in Lauf 1.

Die Quelle liegt unter `package/Features/NeuralRendering/Shaders/…` und wird auf der D3D11-Seite
mit dem vorhandenen Shader-Compiler zu DXBC übersetzt; der Bytecode geht über `Bridge.h` als
einfacher Puffer in die Insel (D3D12 nimmt SM-5.x-DXBC mit Root-Signatur).

Die Ansichten (`NeuralRendering/view`):

-   `off` — das Zusammengesetzte;
-   `split` — linke Hälfte Endbild ohne NR, rechte Hälfte zusammengesetzt;
-   `mask` — das Zusammengesetzte, wo das Endbild stehen blieb, eingefärbt. Zeigt, ob nur HUD
    und Overlay ausgespart sind.

### 4.5 Reihenfolge in `Present`

1. D3D11: Endbild in den Slot, Signal, Flush (unverändert).
2. D3D12 wartet auf den Slot (unverändert).
3. **Wenn NR in diesem Frame rechnet** und Tiefe, Bewegung und HUD-loses Bild da sind:
   Auswertung → Zusammensetzen → Kopie in die Swapchain. Sonst die Kopie des Endbilds wie bisher.
4. Tags für DLSS-G: rechnet NR, wird **die NR-Ausgabe** als `kBufferTypeHUDLessColor` getaggt,
   sodass FG zwischen verbesserten Bildern interpoliert und das HUD aus dem Original nimmt.
5. Present, Signal (unverändert).

FGs bestehende `split`-Ansicht bleibt, wie sie ist.

### 4.6 Bezug der DLL

`cmake/DlssNr.cmake` nach dem Muster von `cmake/Streamline.cmake`: einmal nach
`extern/.downloads` laden, gegen die SHA256 aus 1.2 prüfen, mit `file(ARCHIVE_EXTRACT)` (liest
7z, geprüft) nur `include/nvsdk_ngx_defs_dlssnr.h`, `lib/Windows_x86_64/rel/nvngx_dlssnr.dll`
und `LICENSE.txt` nach `extern/DlssNr` (nicht versioniert) ziehen, Versionsstempel daneben.
Fehlt das Netz, scheitert die Konfiguration mit klarer Meldung, wie bei Streamline.

## 5. Ablauf im Frame

`Frame` des Features entscheidet, ob NR im nächsten Frame rechnet, und meldet das an Brücke und
Abgriff; der Abgriff greift während des Frames ab; `Present` rechnet, setzt zusammen und
präsentiert. Ein Wechsel von aus nach an setzt `Reset`.

## 6. Das Feature

### 6.1 Einstellungen

| Pfad                         | Art     | Vorgabe | Bedeutung                                                    |
| ---------------------------- | ------- | ------- | ------------------------------------------------------------ |
| `NeuralRendering/enabled`    | Feature | aus     | NR an; zugleich Startschalter der Brücke                     |
| `NeuralRendering/model`      | Choice  | `fast`  | `fast` = Preset B, `original` = Preset A                     |
| `NeuralRendering/style`      | Choice  | `1`     | `1`/`2`/`3` = `Style` 0/1/2                                  |
| `NeuralRendering/intensity`  | Slider  | 1,0     | 0–1, Gesamtstärke; 0 gibt das Bild unverändert zurück        |
| `NeuralRendering/structure`  | Slider  | 0,7     | 0–1, `LocalStructureStrength`: Details, Schatten, Material   |
| `NeuralRendering/tone`       | Slider  | 0,3     | 0–1, `LocalToneStrength`: Helligkeit, Dunkelheit, Farbe      |
| `NeuralRendering/characters` | Bool    | an      | `UseAutoMask`: Figuren automatisch erkennen                  |
| `NeuralRendering/skin`       | Slider  | 0,5     | 0–1, `SkinStructureStrength`, wirkt nur mit Figurenerkennung |
| `NeuralRendering/view`       | Choice  | `off`   | `off`, `split`, `mask`                                       |

Die Help von `DeclareFeature` ist ein Satz über die Wirkung, nennt NVIDIA und sagt, dass das erste
Einschalten erst beim nächsten Spielstart wirkt, wenn die Brücke noch nicht läuft. Alle Texte über
`T()`, `en.json` per `tools/extract-i18n.py --write`.

### 6.2 Lebenszyklus

-   **Registrierung** nach `FrameGeneration`.
-   **`Setup`** scheitert, wenn die Brücke nicht steht (Grund aus `Bridge::Refusal`, etwa
    „Neustart nötig"), die Engine-Hooks fehlen, der Shader nicht übersetzt oder NGX auf D3D12
    ablehnt — jeweils mit dem Grund im Log.
-   **`Frame`** setzt NR an oder aus: aus im Ladebildschirm, Pip-Boy, bei `inMenuMode`, und wenn
    TAA aus ist (ohne TAA schreibt die Engine keine Bewegungsvektoren). **Nicht** aus bei offenem
    Overlay. Jeder Wechsel des Grundes kommt ins Log. Die Einstellungen werden jeden Frame frisch
    gelesen und an die Brücke gereicht; nur ein Wechsel von `model` legt das Feature neu an.
-   **`Shutdown`** schaltet NR aus, meldet den Bedarf am Abgriff ab, gibt Feature und Texturen
    frei, sobald die Queue an ihnen vorbei ist, und lässt NGX auf D3D12 bis Spielende stehen
    (Neustart des Features ohne neues `Init`).

### 6.3 Auslieferung

Eigenes Addon `NeuralRendering`, kein `CORE`. `package/Features/NeuralRendering/EXTERNAL` nennt
`nvngx_dlssnr.dll` und den Lizenztext (als `nvngx_dlssnr.license.txt`) nach
`F4SE/Plugins/CommunityShadersFO4/NeuralRendering/`; der Shader liegt im Addon wie bei
FrameGeneration. `Util::PluginDataDirectory()` liefert den Ordner für `PathListInfo`.
`verify-package.ps1` prüft die DLL im Addon und ihr Fehlen im Basisarchiv. Das Addon ist mit
etwa 270 MB das größte des Projekts.

Das Addon setzt `FrameGeneration` voraus (Abschnitt 1). Das steht in der Help von
`DeclareFeature` und im Readme des Archivs. Fehlt Streamline, nennt der Ablehnungsgrund der
Brücke — heute „is the FrameGeneration archive installed?" — beide Features, und `Setup` von
NR gibt ihn wörtlich weiter. Die Adapterprüfung der Brücke (`SupportsFrameGeneration`: DLSS-G und
Reflex) bleibt, wie sie ist; jede Karte, die NR kann (RTX 50), besteht sie.

## 7. Zusammenspiel

-   **Frame Generation**: unabhängig schaltbar. Beide an: NR vor DLSS-G, FG bekommt das NR-Bild
    als HUD-loses. Beide brauchen dieselbe Brücke und denselben Abgriff.
-   **DLSS SR**: NR rechnet hinter SR auf dem hochskalierten Bild in Anzeigegröße. NGX auf D3D11
    (SR) und D3D12 (NR) in einer Instanz: in der Probe belegt.
-   **Streamline** hält im selben Prozess eine eigene NGX-Instanz auf demselben D3D12-Gerät (für
    DLSS-G). Nicht durch die Probe gedeckt: R5.
-   **Overlay**: zeichnet weiter ins Endbild und bleibt durch das Zusammensetzen unberührt; NR läuft
    bei offenem Overlay weiter.
-   **Profiler**: misst D3D11. Die NR-Zeit kommt aus den D3D12-Zeitstempeln der Insel.

## 8. Prüfung

### 8.1 Host-Tests

`StartupSwitch` erweitert (4.1). Rein und testbar sind außerdem die Abbildung der Einstellungen auf
NGX-Werte (`model`, `style`, `characters` → Preset, Style, AutoMask, MaskBlendMode) und die
Entscheidung, ob NR in einem Frame rechnet (Ladebildschirm, Pip-Boy, Menü, TAA, Overlay); wie diese
geschnitten werden, legt der Plan fest. Jeder Test wird nach dem Grünwerden absichtlich gebrochen,
mit vorher benanntem Fehlschlag, erst nach geglücktem Bau ausgeführt.

### 8.2 Lauf 1 — NR an, FG aus

Belegt: NGX startet im Spiel (Logzeile mit Version und `Available`), FG verhält sich nach dem
Herauslösen des Abgriffs unverändert (kurz FG an, Vergleich mit dem Kontroll-Lauf von F5.5b), das
Format des HUD-losen Bildes (Log) und ob NR es richtig liest (R1). Der Nutzer schaltet `split` und
`mask` und schaut: rechte Hälfte sichtbar anders, links unverändert; in `mask` nur HUD und Overlay
eingefärbt. Er dreht sich schnell (R3), öffnet Pip-Boy, ein Menü und das Overlay und bewegt einen
Regler bei offenem Overlay. F11-Schnappschüsse NR aus/an an derselben Stelle, `model` `fast` und
`original`; dazu die NR-Zeit aus dem Log.

### 8.3 Lauf 2 — Abnahme, NR + FG + DLSS SR

An einer schweren Stelle F11-Schnappschüsse mit NR aus und an, FG an. Bestanden, wenn

-   die Frame-Zeit der gerenderten Bilder (F11) mit NR um höchstens das 1,5-Fache der
    gemessenen D3D12-Zeit von Auswertung und Zusammensetzen (Zeitstempel der Insel, gleicher
    Schnappschuss) steigt und angezeigt weiterhin über gerendert liegt. Der Faktor deckt die
    Konkurrenz mit D3D11 und DLSS-G auf derselben GPU; F5.5b sah dafür +2,7 ms bei 1,35–2,1 ms
    DLSS-G-Zeit. (Meine Festlegung.);
-   der Nutzer kein Schlieren, kein Flimmern, kein verändertes HUD meldet und Menüs, Pip-Boy,
    Ladebildschirm NR sauber ab- und anschalten;
-   keine `[E]`- und keine unerklärte `[W]`-Zeile im Log steht.

Ob ihm der Look gefällt, ist sein Urteil und wird so festgehalten; die Regler sind dafür da.

## 9. Risiken und Gabelungen

-   **R1 Format des HUD-losen Bildes.** Der Backbuffer des Spiels kann `_SRGB` getypt sein; NGX
    würde beim Lesen linearisieren, NR erwartet sRGB-kodierte Werte 0–1 (so in der Probe).
    Gabel: Slot-Textur `TYPELESS` anlegen und NR eine `UNORM`-Sicht geben bzw. vorher in eine
    `UNORM`-Kopie wandeln. Lauf 1 loggt das Format und zeigt es am Bild (zu hell/zu dunkel).
-   **R2 Die Maske spart mehr aus als das HUD.** Zeichnet die Engine nach `+0xC5` noch flächig
    (Fader, Bildschirmeffekte), ist dort das Endbild ≠ HUD-los und NR unsichtbar. Gabel: Toleranz
    im Vergleich, oder bei flächigen Unterschieden das NR-Bild mit dem Unterschied
    (Endbild − HUD-los) überlagern. `mask` zeigt es.
-   **R3 Skala und Vorzeichen der Bewegungsvektoren.** Die Engine liefert UV-Einheiten;
    NGX-Features erwarten Pixel der Vektorauflösung mal `MVecScale`. Falsch: Schlieren beim
    Drehen. Der Plan leitet den Wert aus F5.5a (`motionScaleX/Y` der SR-Auswertung) ab.
-   **R4 Die Paket-URL verschwindet.** Sie gehört zu Remix' Build, nicht zu einem SDK. Das Archiv
    bleibt in `extern/.downloads` liegen; erscheint NR in einem offiziellen SDK, zieht der Bezug
    dorthin um.
-   **R5 Zwei NGX-Instanzen auf einem D3D12-Gerät** (unsere, Streamlines). Gabel: Reihenfolge der
    Initialisierung tauschen; scheitert es, NR nur ohne FG anbieten und das als Zugeständnis
    festhalten — mit dem Nutzer.
-   **R6 Lizenz.** Die RTX-SDK-Lizenz erlaubt Weitergabe in Objektform als Teil einer Anwendung mit
    eigener Funktion, verlangt Benachrichtigung vor kommerzieller Veröffentlichung und verbietet,
    das SDK einer Open-Source-Lizenz zu unterwerfen (4.e, wie in F5.5a). Die DLL liegt unverändert
    und getrennt im Addon. Vor einer Veröffentlichung die Anfrage an
    `nvidia-rtx-license-questions@nvidia.com` — das entscheidet der Nutzer, nicht dieses
    Teilprojekt.
-   **R7 Speicher.** NR B 482 MB zusätzlich zu DLSS-G (etwa 489 MB) und SR.
-   **R8 Wechsel des Modells ruckelt** einmal etwa 0,4 s (Neuanlegen). Hingenommen, steht in der
    Help.

## 10. Zugeständnisse

Nur RTX 50 (bis NVIDIA RTX 40 freigibt), nur mit installiertem FrameGeneration-Addon, nur mit Brücke und damit nur im randlosen Fenster, kein
ENB, kein HDR. NR aus in Menüs, Pip-Boy und Ladebildschirm. Erstes Einschalten wirkt erst nach
einem Neustart, wenn die Brücke noch nicht läuft. Die DLL stammt aus Remix' Paket statt aus einem
öffentlichen SDK und steht unter NVIDIAs Lizenz. Das Feature ist standardmäßig aus.

## 11. Was F5.5c hinterlässt

Den ersten Compute-Pass auf der D3D12-Seite, einen Abgriff, den mehrere Abnehmer teilen,
D3D12-Zeitstempel in der Brücke, und den NGX-Weg auf D3D12, an dem eine spätere Steuermaske oder
ein UI-Puffer für NR andocken kann.
