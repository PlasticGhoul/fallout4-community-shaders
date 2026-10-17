# Teilprojekt F5.5b — DLSS Frame Generation

Stand 2026-10-17. Branch `port/f5-5b-dlss-fg` (von `dev` 58bc4ae9). Zweite Hälfte des vor F6
vorgezogenen DLSS-Teilprojekts; die erste, F5.5a (Super Resolution), ist abgeschlossen.

## 1. Kontext und Ziel

F5.5a hat gemessen, daß Fallout 4 auf der Maschine des Nutzers (RTX 5080, 2560×1440, 180 Hz)
nicht pixelgebunden ist: Performance-Hochskalierung macht die Pixelpässe 3,5- bis 5-mal billiger,
den Frame nicht (TAA 6,99 ms, Performance 7,61 ms). Ein Bildratengewinn kann hier nur aus Frame
Generation kommen. DLSS Frame Generation (DLSS-G) läuft ausschließlich auf D3D12 oder Vulkan —
das Manifest in `sl.dlss_g.dll` nennt `"rhi": ["d3d12","vk"]` und die DLL trägt die Meldung
`"%s is not supported on D3D11!"`. Fallout 4 ist D3D11. Der Weg ist deshalb eine **Brücke**: das
Spiel rendert weiter auf D3D11, präsentiert wird über eine eigene D3D12-Swapchain, auf der
Streamline DLSS-G und Reflex betreibt.

Der Nutzer hat entschieden (alle per Rückfrage):

-   **2× Frame Generation plus Reflex.** Normale FG bringt die rund 140 gerenderten Bilder je
    Sekunde an die 180-Hz-Grenze; Multi-Frame (3×/4×) läge weit darüber. MFG bleibt außerhalb
    des Umfangs.
-   **Eigene Brücke** nach den Vorlagen Skyrim-CS (`skyrim-base`, `src/Features/Upscaling/
DX12SwapChain.cpp`) und doodlum (`fo4test`, Zweig `main`, `4096d7e`), aber mit sauberem
    Stellvertreter und **Streamline neu entworfen** — keine der beiden Vorlagen hat je DLSS-G
    betrieben, beide nutzen FSR3, und Skyrim schaltet Reflex auf dem D3D12-Weg ab. Nicht
    doodlums Mod übernommen (erbt dessen Mängel), nicht D3D11on12 (übersetzt jeden Draw-Call in
    einem Spiel, das an CPU- und Draw-Arbeit hängt).
-   **Die Brücke entsteht nur, wenn der Schalter beim Start steht.** FG selbst ist im laufenden
    Spiel schaltbar.
-   **Randloses Fenster ist Voraussetzung.** Der Nutzer stellt um; das Feature verlangt es und
    sagt im Overlay, warum es im exklusiven Vollbild nicht verfügbar ist.
-   **UI-Puffer gestuft mit Meßgate** (Abschnitt 8.4).

Ziel: mehr angezeigte Bilder je Sekunde bei stehendem Overlay, gemessen, ohne sichtbare Fehler
an HUD, Menüs und Kamera-Schnitten.

### Was F5.5b erbt

Aus F5.5a: Jitter (`Upscale::JitterOffset`), Bewegungsvektoren in `RT_029` in UV-Einheiten,
`DS_002` als Tiefe, die Reset-Erkennung (`CameraCut`, Rückkehr aus Menüs, Moduswechsel,
Shader-Neuladen), `Render::Resolution`, die Einhängestelle
`SetUseDynamicResolutionViewportAsDefault` (`2318322+0xC5`), die NGX-Projekt-ID
`99023707-327d-45dc-ba45-0c47e8445542` mit Engine-Version `Fallout 4 1.11.240`, die
`EXTERNAL`-Datei der Paketierung. Aus F1 bis F5: `Render::Profiler`/`PassScope`,
`Render::Targets`, `TexelProbe`, `StateGuard`, `FullscreenPass`, `FogCameraFromMatrix`,
`Menu::Hotkeys`, das Feature-Gerüst.

### Die Befunde, die den Entwurf bestimmen

1. **Unser Code hakt heute nichts vor `kGameDataReady` ein.** Dort steht die D3D11-Swapchain
   bereits. Die Brücke muß die Erzeugung von Gerät und Swapchain abfangen; beide Vorlagen tun
   das per IAT-Hook auf `d3d11.dll!D3D11CreateDeviceAndSwapChain` aus dem normalen
   Plugin-Load heraus, ohne Preload.
2. **`Settings::Init` läuft erst bei `kGameDataReady`** (`src/Feature/FeatureSystem.cpp:93`), also
   nach der Geräteerzeugung. Der Startschalter wird deshalb früh und eigenständig aus der Datei
   gelesen (4.1).
3. **Der Present-Hook ist ein vtable-Patch auf Slot 8** der Swapchain, die die Engine hält
   (`src/Render/SwapChainHook.cpp:22`). Hält die Engine unseren Stellvertreter, patcht derselbe
   Code dessen vtable; Features, Profiler und Overlay laufen unverändert davor.
4. **Das Overlay zeichnet auf `GetBuffer(0)`** und hält dessen RTV, bis sich die Größe ändert
   (`src/Menu/Overlay.cpp:91-144`). Mit dem Stellvertreter ist das der private Backbuffer — das
   Overlay fehlt im HUD-losen Bild und wird von DLSS-G wie HUD behandelt.
5. **doodlum greift das HUD-lose Bild aus `renderTargets[0]`** an genau der Stelle ab, die F5.5a
   schon anlegt (`2318322+0xC5`), nach dem Originalaufruf. Für uns ungeprüft: daß `RT_000` dort
   der Backbuffer des Stellvertreters ist (`render-targets.md`: „RT_000 ist unbelegt" — in den
   Daten vor der Brücke). Lauf 1 belegt es.
6. **Streamline verlangt für DLSS-G** (Streamline v2.14.16, `docs/ProgrammingGuideDLSS_G.md`):
    - Reflex über `sl.reflex`, keine fremde Reflex-Integration (DLSS_G.md:684);
    - Tiefe und Bewegungsvektoren Pflicht, HUD-loses Bild und UI stark empfohlen, HUD-los und
      UI exakt in Backbuffergröße (DLSS_G.md:36, 225-270);
    - Tags als Ressourcen genau des Geräts aus `slSetD3DDevice` (PG:578) — D3D11-Texturen müssen
      also als geteilte Texturen in D3D12 geöffnet werden;
    - `GetCurrentBackBufferIndex` jeden Frame über die Streamline-Swapchain;
    - FG aus vor Größenänderung und Vollbildwechsel (Deadlock-Gefahr, DLSS_G.md:753-759);
    - kein Waitable-Object, kein `SetMaximumFrameLatency` (DLSS_G.md:39);
    - Windows 10 20H1 und hardwarebeschleunigte GPU-Planung (HAGS). **Beim Nutzer an**
      (`dxdiag`: `Hardware Scheduling: Enabled:True`), Treiber 610.88.
7. **Manuelles Hooking ist nötig.** Ohne `eUseManualHooking` patcht Streamline die vtable der
   DXGI-Fabrikklasse prozessweit (`dxgi.cpp:318-334`) und damit auch den D3D11-Weg des Spiels.
   Mit manuellem Hooking umhüllt Streamline nur, was wir ihm über `slUpgradeInterface` reichen.
   Damit löst sich der Konflikt, den Skyrim-CS kommentiert (Stellvertreter muß äußerste Schicht
   bleiben, sonst liefert `GetDevice` kein D3D11-Gerät): unser Stellvertreter bleibt außen,
   Streamline umhüllt nur die innere D3D12-Swapchain.
8. **Beide Vorlagen haben Mängel, die wir nicht übernehmen:** `QueryInterface` gibt für jede
   Schnittstelle der echten Swapchain `this` (eine v1-vtable) zurück; Referenzen laufen an der
   echten Swapchain; doodlum ignoriert `ResizeBuffers`; Skyrim blockiert jeden Frame die CPU
   (`SetEventOnCompletion(…, nullptr)`) und läßt D3D11 auf D3D12 warten.
9. **Das Spiel des Nutzers läuft im exklusiven Vollbild** (`Fallout4Prefs.ini`: `bFull Screen=1`;
   High FPS Physics Fix übernimmt das, `Fullscreen=` ist dort auskommentiert). VSync steuert
   High FPS Physics Fix (`EnableVSync=true`, `VSyncPresentInterval=1`, `AllowTearing=true`).

## 2. Umfang

### In F5.5b enthalten

-   Die D3D12-Brücke: Abfangen der Geräteerzeugung, Stellvertreter-Swapchain, D3D12-Gerät,
    geteilte Texturen und Fence, Präsentation über Streamline.
-   DLSS-G mit einem erzeugten Bild (2×), im laufenden Spiel schaltbar.
-   Reflex über Streamline (`on`, `boost`) mit PCL-Markern.
-   Abschalten von FG in Menüs, Pip-Boy, Ladebildschirm, Pause und bei offenem Overlay.
-   Zählung der angezeigten Bilder, im Log und in der Leistungsanzeige.
-   Der UI-Puffer, **falls** das Meßgate in Lauf 2 durchfällt (8.4).
-   Das Addon-Archiv `FrameGeneration` mit den Streamline-Laufzeitdateien.

### Nicht in F5.5b enthalten

-   Multi-Frame-Generation über 2× und Dynamic MFG.
-   HDR-Ausgabe.
-   ENB (ersetzt `d3d11.dll` selbst; beide Vorlagen brauchen dafür einen eigenen Weg).
-   Exklusives Vollbild.
-   DLSS 5 (DLSS-NR) — kein öffentliches SDK; die Brücke ist sein Andockpunkt (Abschnitt 11).
-   FSR3 FG für andere Karten (bleibt F13).

## 3. Vorentscheidungen

| Frage                       | Entscheidung                                                                                                 |
| --------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Streamline-Version          | v2.14.16 (2026-10-16), Produktionsbinärdateien, signiert                                                     |
| Bezug                       | CMake-Download des Release-Zips, festgenagelt mit SHA256; Header und DLLs daraus                             |
| API                         | `eD3D12`, `eUseManualHooking \| eUseFrameBasedResourceTagging`                                               |
| OTA                         | aus (`eAllowOTA`, `eLoadDownloadedPlugins` nicht gesetzt) — feste Dateien                                    |
| Projekt-ID                  | dieselbe wie F5.5a                                                                                           |
| Super Resolution            | bleibt NGX auf D3D11; Streamline nur auf D3D12                                                               |
| Laden                       | `WinVerifyTrust` plus `sl::security::verifyEmbeddedSignature`, voller Pfad, `LoadLibraryW`, `GetProcAddress` |
| Ort der Laufzeitdateien     | `Data/F4SE/Plugins/CommunityShadersFO4/Streamline/`, als `pathsToPlugins`                                    |
| Abgleich der Geräte         | geteilter Fence, nur GPU-seitige Waits, Ring aus zwei Slots                                                  |
| Einhängestelle der Eingaben | `2318322+0xC5`, nach dem Originalaufruf                                                                      |

## 4. Die Bausteine

Neues Verzeichnis `src/Bridge/`. Reine Teile mit Host-Test sind markiert.

### 4.1 `Bridge::StartupSwitch` (rein, Host-Test)

Liest `FrameGeneration/enabled` aus `CommunityShadersFO4.json`, bevor `Settings::Init` gelaufen
ist. Eine Funktion von Dateiinhalt auf `bool` plus Grund: Datei fehlt, nicht lesbar, kein
gültiges JSON, Block oder Schlüssel fehlt, kein `bool` → aus, mit benanntem Grund. Den Pfad
formt dieselbe Stelle wie für `Settings::Init`. Die Reihenfolge von `Settings::Init` bleibt
unangetastet: vorgezogen kämen alle `Declare` zu spät.

### 4.2 `Bridge::Decision` (rein, Host-Test)

Entscheidet aus einer Struktur von Fakten, ob die Brücke entsteht, und nennt sonst den ersten
gerissenen Grund:

1. Startschalter aus;
2. das Spiel fordert kein Fenster an (`pSwapChainDesc->Windowed == FALSE`);
3. kein NVIDIA-Adapter (Vendor `0x10DE`);
4. ENB geladen (`d3d11.dll` stammt nicht aus dem Systemverzeichnis) oder eine fremde FG-Mod
   geladen (`AAAFrameGeneration.dll`, doodlums Plugin, und die in F5.5a geführten Upscaler);
5. Streamline nicht ladbar oder Signatur ungültig;
6. `slInit` gescheitert;
7. `slIsFeatureSupported(kFeatureDLSS_G)` auf dem Adapter nein.

Die Gründe 1–4 werden vor jedem Streamline-Aufruf geprüft; 5–7 entstehen im Hook.

### 4.3 `Bridge::DeviceHook`

IAT-Hook auf `d3d11.dll!D3D11CreateDeviceAndSwapChain` in `Fallout4.exe`, gesetzt in
`F4SEPlugin_Load` nach `Runtime::IsSupported` und nach `Upscale::Hooks::Install`. Im Hook:

1. `Decision` mit den Fakten 1–4. Nein → Originalaufruf, Grund ins Log.
2. Streamline laden und initialisieren (4.6). Scheitert es → Originalaufruf, Grund ins Log.
3. `D3D11CreateDevice` mit den Argumenten des Spiels, ohne Swapchain.
4. `Presenter` aufbauen (4.5). Scheitert ein Schritt → alles bisher Angelegte freigeben,
   Streamline herunterfahren, Originalaufruf. Ins Log der Schritt und sein HRESULT.
5. Dem Spiel Gerät, Kontext und den `SwapChainProxy` zurückgeben.

Ins Log kommt in jedem Fall die Beschreibung, die der Hook erhielt (Größe, Format,
`Windowed`, `SwapEffect`, `Flags`, `BufferCount`) — High FPS Physics Fix hakt dieselbe
Erzeugung ein, und welche Fassung bei uns ankommt, ist nicht vorherzusagen (Risiko R4).

### 4.4 `Bridge::SwapChainProxy`

Was das Spiel als `IDXGISwapChain` bekommt. Eigene Referenzzählung; der letzte `Release` baut
den `Presenter` ab.

| Methode              | Verhalten                                                                                                                                                                                                                      |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `QueryInterface`     | `this` nur für `IUnknown`, `IDXGIObject`, `IDXGIDeviceSubObject`, `IDXGISwapChain`; sonst `E_NOINTERFACE`                                                                                                                      |
| `AddRef`/`Release`   | eigener Zähler                                                                                                                                                                                                                 |
| `Present`            | `Presenter::Present` (5.)                                                                                                                                                                                                      |
| `GetBuffer`          | nur Index 0, der private D3D11-Backbuffer, per `QueryInterface` mit `AddRef`                                                                                                                                                   |
| `GetDevice`          | das D3D11-Gerät für `ID3D11Device` bis `ID3D11Device5`, mit `AddRef`                                                                                                                                                           |
| `ResizeBuffers`      | FG aus, D3D12-Queue leerlaufen lassen, Backbuffer-Referenzen freigeben, Streamline-Swapchain anpassen, privaten Backbuffer und geteilte Texturen neu anlegen, FG-Zustand wiederherstellen; `BufferCount 0` heißt „unverändert" |
| `SetFullscreenState` | `TRUE` → `DXGI_ERROR_NOT_CURRENTLY_AVAILABLE`, `FALSE` → `S_OK`                                                                                                                                                                |
| `GetFullscreenState` | immer gefenstert                                                                                                                                                                                                               |
| `GetDesc`            | die Beschreibung des Spiels mit aktueller Größe                                                                                                                                                                                |
| übrige               | an die native D3D12-Swapchain                                                                                                                                                                                                  |

Unser bestehender Present-Hook patcht dann die vtable dieser Klasse. Sein Aufruf des Originals
landet in `SwapChainProxy::Present`.

### 4.5 `Bridge::Presenter`

Besitzt die D3D12-Seite:

-   `ID3D12Device` auf dem Adapter des D3D11-Geräts (über `IDXGIDevice::GetAdapter`), FL 12_0;
    `slSetD3DDevice(nativ)`, dann `slUpgradeInterface` für die Command-Queue-Erzeugung;
-   eine DIRECT-Queue über das Streamline-Gerät, zwei Allocator, zwei Command-Lists;
-   eine Fabrik, per `slUpgradeInterface` umhüllt; darüber `CreateSwapChainForHwnd`: Flip-Discard,
    zwei Puffer, Format des Spiels ohne sRGB, `ALLOW_TEARING`, wenn die Fabrik es meldet;
-   einen Fence mit `D3D12_FENCE_FLAG_SHARED`, in D3D11 über `ID3D11Device5::OpenSharedFence`;
-   den privaten D3D11-Backbuffer (Ziel des Spiels und unseres Overlays);
-   je Slot _k_ ∈ {0, 1} vier geteilte Texturen, auf D3D11 mit
    `D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE` angelegt und in D3D12 über
    `OpenSharedHandle` geöffnet: Endbild, HUD-loses Bild (Format des Backbuffers), Tiefe
    (`R32_FLOAT`), Bewegungsvektoren (`R16G16_FLOAT`). Tiefe und Bewegungsvektoren in
    Anzeigegröße; bei Hochskalierung wird nur der Renderausschnitt beschrieben und als Ausschnitt
    getaggt.

### 4.6 `Bridge::Streamline`

Laden, Initialisieren, Funktionszeiger. Prüft die Signatur von `sl.interposer.dll`, lädt sie mit
vollem Pfad, holt alle `sl*`-Funktionen und `slGetFeatureFunction` per `GetProcAddress`.
`sl::Preferences`: `featuresToLoad = { kFeatureDLSS_G, kFeatureReflex }` (PCL lädt von selbst),
`pathsToPlugins` absolut, `engine = eCustom`, Projekt-ID und Engine-Version wie F5.5a,
Log-Rückruf auf `REX::INFO`/`REX::ERROR`. Nach `slSetD3DDevice`: `slDLSSGGetState`,
`slDLSSGSetOptions`, `slReflexSetOptions`, `slReflexSleep`, `slPCLSetMarker` binden.
`slShutdown` beim Abbau des `Presenter`, nach Freigabe der Swapchain.

### 4.7 `Bridge::SlotRing` (rein, Host-Test)

Buchhaltung des Rings: welcher Slot in diesem Frame beschrieben wird, welcher Fence-Wert ihn als
frei meldet, auf welchen Wert D3D11 vor dem Beschreiben warten muß, und daß ein Slot nie
beschrieben wird, solange D3D12 ihn liest.

### 4.8 `Bridge::FrameToken` (rein, Host-Test)

Übergabe des Streamline-Frame-Tokens vom Hauptthread an den Render-Thread. Der Hauptthread
erzeugt den Token am Beginn des Spielframes und legt ihn mit dem Frame ab; der Render-Thread
nimmt ihn, wenn er diesen Frame zu rendern beginnt. Ein fehlender Token führt zu „FG dieses Frame
aus", nie zu einem erfundenen.

### 4.9 `Bridge::CameraConstants` (rein, Host-Test)

Baut `sl::Constants` aus den Kameradaten des Frames: `cameraViewToClip`, `clipToCameraView`,
`clipToPrevClip`, `prevClipToClip` ohne Jitter, Kameraposition und drei orthonormale Achsen,
`cameraNear`, `cameraFar`, `cameraFOV`, `cameraAspectRatio`, `jitterOffset` in Pixeln,
`mvecScale`, `depthInverted`, `cameraMotionIncluded = true`, `motionVectors3D = false`,
`motionVectorsJittered`, `reset`. Die Matrizen kommen über dieselben Lesestellen wie
`FogCameraFromMatrix`; Near und Far über doodlums IDs `2712882`/`2712883`, vor Gebrauch gegen
1.11.240 geprüft. Vorzeichen und Skala der Bewegungsvektoren werden in Lauf 1 gemessen.

### 4.10 `Bridge::PresentStats` (rein, Host-Test)

Zählt gerenderte und angezeigte Bilder aus `numFramesActuallyPresented` je Sekunde. Das Menü
liest es für die Leistungsanzeige; kein ImGui in `src/Bridge/` oder `src/Features/`.

### 4.11 Zwei Einhängestellen

-   **Beginn des Spielframes auf dem Hauptthread**, für `slGetNewFrameToken`, `slReflexSleep`,
    `eSimulationStart` — in CommonLibF4 nicht vorhanden. Kandidat ist das Abfragen der
    Eingabegeräte, wie in Skyrim-CS (`BSInputDeviceManager::PollInputDevices`) und in doodlums
    früherer Fassung. Die Adresse wird vor dem Code offline gegen 1.11.240 belegt wie die
    Aufrufstellen in F5.5a.
-   **Die Eingaben** am Hook `2318322+0xC5`. `Upscale::Hooks` trägt dort heute einen einzigen
    Handler (den von DLSS); die Stelle bekommt eine kleine Beobachterliste, damit beide
    Features unabhängig ein- und aushängen.

## 5. Ablauf im Frame

Haupt-Thread:

1. Beginn des Spielframes: `slGetNewFrameToken`, `slReflexSleep(token)`, `eSimulationStart`;
   Token an `FrameToken`.
2. Ende der Simulation: `eSimulationEnd`.

Render-Thread:

3. Beginn des Renderframes: Token übernehmen, `eRenderSubmitStart`; D3D11 wartet (GPU) auf
   „Slot _k_ frei" — in jedem Frame, denn 7.1 beschreibt Slot _k_ auch ohne FG.
4. Am Hook `+0xC5`, nach dem Original, wenn FG in diesem Frame läuft: D3D11 kopiert
   `RT_000` → HUD-los[*k*], zeichnet `DS_002` → Tiefe[*k*], kopiert `RT_029` → Bewegung[*k*].
5. Engine zeichnet HUD und Menüs in den privaten Backbuffer.
6. Unser Present-Hook: Features, Profiler, Overlay (in den privaten Backbuffer).
7. `SwapChainProxy::Present`:
    1. D3D11 kopiert den privaten Backbuffer → Endbild[*k*], signalisiert Fence _n_;
       `eRenderSubmitEnd`.
    2. D3D12-Queue wartet (GPU) auf _n_.
    3. `GetCurrentBackBufferIndex`; Endbild[*k*] → Backbuffer der Streamline-Swapchain.
    4. `slSetTagForFrame` (HUD-los, Tiefe, Bewegung; `eValidUntilPresent`), `slSetConstants`,
       `slDLSSGSetOptions` nur bei Änderung.
    5. `ePresentStart`, Present mit dem `SyncInterval` und den Flags des Spiels, `ePresentEnd`.
    6. D3D12 signalisiert „Slot _k_ frei"; `k ← 1 − k`.

Läuft FG in einem Frame nicht, entfallen 4 und die Tags; 7.1–7.3 und 7.5 laufen trotzdem, das
Bild kommt also immer über die Brücke. Die CPU wartet an keiner Stelle auf eine GPU.

## 6. Das Feature

### 6.1 Einstellungen

| Pfad                      | Art     | Vorgabe | Bedeutung                                    |
| ------------------------- | ------- | ------- | -------------------------------------------- |
| `FrameGeneration/enabled` | Feature | aus     | FG an; zugleich der Startschalter der Brücke |
| `FrameGeneration/reflex`  | Choice  | `on`    | `on` oder `boost`                            |

Die Help von `DeclareFeature` ist ein Satz über den Effekt und nennt NVIDIA. Alle Texte über
`T()`, `en.json` per `tools/extract-i18n.py --write`.

### 6.2 Lebenszyklus

-   **Registrierung** nach `DLSS`: FG liest dessen Jitter und Reset.
-   **`Setup`** scheitert nur, wenn die Brücke beim Start nicht entstanden ist — mit dem Grund
    aus `Decision`, der auf der Seite im Overlay steht („exklusives Vollbild", „ENB geladen",
    „Neustart nötig: beim Start war der Schalter aus", …). Das wiederholt sich bis zum Neustart
    ohnehin.
-   **`Frame`** setzt FG für den nächsten Frame an oder aus: aus bei Spielmenü, Pip-Boy,
    Ladebildschirm, Pause, offenem Overlay, Status von `slDLSSGGetState` ungleich `eOk` oder Bild
    kleiner als `minWidthOrHeight`. Jeder Wechsel des Grundes kommt ins Log, mit Grund.
    `slReflexSetOptions` bei Änderung der Einstellung. `PresentStats` einmal je Sekunde ins Log.
-   **`Shutdown`** schaltet FG aus (`eOff` mit `eRetainResourcesWhenOff`), leert die Tags,
    hängt den Beobachter am Hook aus. Die Brücke bleibt bis Spielende als Durchreichung.

### 6.3 Auslieferung

`package/Features/FrameGeneration/EXTERNAL` nennt sechs Dateien aus dem Streamline-Zip:
`sl.interposer.dll`, `sl.common.dll`, `sl.dlss_g.dll`, `sl.reflex.dll`, `sl.pcl.dll`,
`nvngx_dlssg.dll` (zusammen etwa 10,4 MB), alle nach
`F4SE/Plugins/CommunityShadersFO4/Streamline/`, dazu die NVIDIA-Lizenztexte. Nur Dateien aus
`bin/x64`, nie aus `development/`. Kein `CORE`. `verify-package.ps1` bekommt einen Block, der
die sechs Dateien im Addon und ihr Fehlen im Basisarchiv prüft.

## 7. Zusammenspiel

-   **DLSS SR (F5.5a)**: unverändert auf NGX/D3D11. Streamline initialisiert NGX eigenständig
    auf D3D12. Ein Konflikt ist nicht dokumentiert, aber ungeprüft (R3).
-   **Overlay**: zeichnet weiter auf `GetBuffer(0)`; das ist nun der private Backbuffer. Hält es
    beim `ResizeBuffers` des Spiels noch eine Referenz, scheitert dieses — der Stellvertreter
    meldet die offenen Referenzen ins Log, und das Overlay gibt seine RTV vor dem Weiterreichen
    frei.
-   **Profiler**: mißt weiter Present zu Present, also gerenderte Bilder.
-   **High FPS Physics Fix**: hakt dieselbe Erzeugung ein und steuert VSync. Wir reichen
    `SyncInterval` durch; Streamline taktet. Kein eigener Limiter.
-   **F2–F5** sehen keinen Unterschied: ihre Ziele sind Engine-Ziele.

## 8. Prüfung

### 8.1 Host-Tests

Sechs Testbinaries, je ein Gegenstand: `StartupSwitch`, `Decision`, `SlotRing`, `FrameToken`,
`CameraConstants`, `PresentStats`. Jeder wird nach dem Grünwerden absichtlich gebrochen, mit vorher
benanntem erwartetem Fehlschlag, und erst nach geglücktem Bau ausgeführt.

### 8.2 Vor dem ersten Spielstart

Mit Zustimmung des Nutzers wird High FPS Physics Fix auf randloses Fenster gestellt
(`Fullscreen=false`, `Borderless=true`).

### 8.3 Lauf 1 — Brücke an, FG aus

Belegt: die Brücke entsteht (Logzeile mit Beschreibung des Spiels), das Bild ist normal, Overlay,
F2–F5 und DLSS SR arbeiten, Alt-Tab und Pip-Boy gehen. Gemessen, nicht beurteilt:
`RT_000` an `+0xC5` ist der private Backbuffer und HUD-los (`TexelProbe` an einer HUD-Stelle vor
und nach dem HUD), Vorzeichen und Skala der Bewegungsvektoren für Streamline (Seitwärtsgehen),
die Fadenführung (Token-Erzeuger und -Nehmer mit Thread-ID, einmal je Sekunde), Kosten der Brücke
gegen F5.5a-Grundlinie per F11.

### 8.4 Lauf 2 — FG an, mit dem UI-Meßgate

Angezeigte gegen gerenderte Bilder; Menüs, Pip-Boy, Ladebildschirm, Overlay offen; Reflex im
Log; keine Streamline-Meldung „common constants cannot be found for frame N".

**UI-Meßgate:** der Nutzer dreht sich schnell und schaut auf Kompass, Munitionsanzeige und
Fadenkreuz. Schmiert oder zittert das HUD, wird der UI-Puffer Teil von F5.5b: den Einhängepunkt
am Beginn des Scaleform-Zeichnens suchen und messen, das Framebuffer-Ziel dort auf einen eigenen
`R8G8B8A8`-Puffer umlenken (wie Skyrim-CS), das Overlay ebenfalls dorthin zeichnen, als
`kBufferTypeUIColorAndAlpha` taggen, mit einem weiteren Lauf. Bleibt das HUD sauber, wird das
festgehalten und der UI-Puffer entfällt.

### 8.5 Lauf 3 — Abnahme

An einer schweren Stelle F11-Schnappschüsse mit FG aus und FG an an derselben Stelle.
Bestanden, wenn die angezeigten Bilder je Sekunde über den gerenderten liegen, die gerenderten
um nicht mehr als die Kosten von DLSS-G fallen, und der Nutzer keine Fehler an HUD, Menüs und
Kameraschnitten meldet. Keine `[E]`- und keine unerklärte `[W]`-Zeile.

## 9. Risiken und Gabelungen

-   **R1 `RT_000` ist an `+0xC5` nicht das HUD-lose Endbild.** Gabel: die Stelle suchen, an der
    das Tonemapping fertig ist und Scaleform noch nicht gezeichnet hat — Lauf 1 mißt es, bevor FG
    angeschaltet wird.
-   **R2 Der Render-Thread liegt anders zum Hauptthread als angenommen.** Gabel: Token-Übergabe
    über die Frame-Nummer der Engine statt über eine Warteschlange.
-   **R3 NGX auf D3D11 und Streamline auf D3D12 vertragen sich nicht.** Gabel mit dem Nutzer: FG
    verweigert, solange DLSS SR läuft, oder SR zieht auf Streamline/D3D12 um.
-   **R4 High FPS Physics Fix und wir haken dieselbe Erzeugung ein.** Wer zuerst eingehängt hat,
    wird umhüllt. Das Log zeigt, welche Beschreibung bei uns ankommt; fragt High FPS Physics Fix
    nach `IDXGISwapChain3` oder höher, bekommt es `E_NOINTERFACE`, und das Log zeigt, ob es das
    verträgt.
-   **R5 Reflex mißt die Arbeit auf der D3D11-Queue nicht.** Die Latenz-Zahlen sind dann nur
    eingeschränkt aussagekräftig; FG funktioniert trotzdem. Wird dokumentiert, nicht gelöst.
-   **R6 HUD schmiert.** Das Meßgate in 8.4.
-   **R7 VSync und Tearing.** Streamline klemmt `SyncInterval` auf 0 oder 1 und braucht für
    VSync mit FG unabhängiges Flip; die Systemsteuerung von NVIDIA überstimmt das Spiel.
-   **R8 Speicher.** DLSS-G braucht bei 1440p etwa 489 MB VRAM.

## 10. Zugeständnisse

Kein exklusives Vollbild, kein ENB, kein MFG, kein HDR. FG aus in Menüs und bei offenem Overlay.
Die Brücke entsteht nur mit gesetztem Schalter beim Start. Die Streamline- und NGX-Dateien stehen
unter NVIDIAs Lizenz und werden unverändert als eigene Dateien mitgeliefert, nicht unter der GPL;
die Restunsicherheit zu Abschnitt 4.e der SDK-Lizenz aus F5.5a bleibt. Das Feature ist
standardmäßig aus.

## 11. Was F5.5b hinterläßt

Eine D3D12-Präsentation hinter einer sauberen D3D11-Swapchain, mit geteilten Texturen, Fence und
Streamline auf D3D12: der Andockpunkt für DLSS 5. DLSS-NR verlangt Farbe und Bewegungsvektoren auf
D3D12 (`kBufferTypeUpliftInputColor`, `UpliftOutputColor`) — beides liegt dann schon in der
Brücke. Dazu die Einhängestelle am Beginn des Spielframes, die Beobachterliste an `+0xC5` und die
Zählung der angezeigten Bilder.
