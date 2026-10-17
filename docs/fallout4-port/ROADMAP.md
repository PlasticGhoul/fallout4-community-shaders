# Fallout 4 Port — Roadmap

Status: Umsetzung, A bis E2 abgeschlossen — Teilprojekt E ist damit vollständig. F+ ist nach einem
Messspike in **F1…F14 aufwärts** zerlegt; **F1 (Performance Overlay), F2 (Screen-Space Shadows),
F3 (Exponential Height Fog), F4 (Cloud Shadows), das dazwischengeschobene F4.5 (Menü in zwei
Spalten), F5 (Skylighting) und die vorgezogenen F5.5a (DLSS Super Resolution) und F5.5b (DLSS
Frame Generation) sind abgeschlossen**. Als Nächstes steht **F6** an. Stand 2026-10-17.

Dieses Dokument ist die Übersicht über die Portierung von Community Shaders auf Fallout 4.
Es hält den Zuschnitt der Arbeit fest, nicht deren Details — jedes Teilprojekt bekommt eine
eigene Spec unter `docs/superpowers/specs/` und daraus einen eigenen Implementierungsplan.

## Ziel

Community Shaders auf Fallout 4 verfügbar machen. Primäre Ziel-Runtime ist **AE 1.11.240**
(Release 18.08.2026, F4SE 0.7.9). NG (1.10.980 / 1.10.984) und OG (1.10.163) sollen im Lauf
der Entwicklung folgen; die Architektur muss von Beginn an mehrere Runtimes tragen können.

Testumgebung: Fallout 4 AE mit F4SE, High FPS Physics Fix und Address Library for F4SE.
Voraussichtlich zusätzlich [Addictol](https://www.nexusmods.com/fallout4/mods/84214)
(Sammel-Enginefix-Plugin, löst Buffout4 / X-Cell / Mentats ab). ENB ist inkompatibel.

## Getroffene Entscheidungen

| Entscheidung      | Wahl                                                                                                                                          | Begründung                                                                                                     |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| Vorgehen          | Eigener Port, Architektur neu gedacht                                                                                                         | Keine 1:1-Übersetzung der Skyrim-BSShader-Pipeline; dünne RE-Schicht plus D3D11-Interception                   |
| Engine-Bibliothek | [libxse/commonlibf4](https://github.com/libxse/commonlibf4) bzw. der [Dear-Modding-FO4](https://github.com/Dear-Modding-FO4/commonlibf4)-Fork | Mit Abstand beste RE-Abdeckung (1455 RE-Header), täglich gepflegt, OG/NG/AE, GPL-3.0 passend zur Projektlizenz |
| Buildsystem       | CMake bleibt                                                                                                                                  | commonlibf4 liefert nur `xmake.lua`; wird über einen im Build-Tree erzeugten CMake-Shim eingebunden            |

Verworfen wurde: auf [northaxosky/fallout4-community-shaders](https://github.com/northaxosky/fallout4-community-shaders)
aufzusetzen (aktiver, unabhängiger FO4-Port desselben Autors, der High FPS Physics Fix pflegt).
Das Projekt bleibt trotzdem die wichtigste **Referenzimplementierung** — insbesondere für den
CMake-Shim, die Runtime-ID-Tripel und den Zuschnitt der Render-Schicht.

Ebenfalls geprüft und nicht gewählt: [alandtse/CommonLibF4](https://github.com/alandtse/CommonLibF4)
(natives CMake, MIT, gleicher Maintainer wie unser bisheriges CommonLibSSE-NG — aber nur 355
RE-Header und laut eigenem README unfertige NG-Unterstützung) sowie
[Ryan-rsm-McKenzie/CommonLibF4](https://github.com/Ryan-rsm-McKenzie/CommonLibF4)
(Original, für Grafikarbeit unbrauchbar: `BSGraphics.h` mit 133 Zeilen ohne `Renderer`).

## Zerlegung

Reihenfolge ist bindend, solange nichts anderes vereinbart wird: jedes Teilprojekt setzt auf dem
vorherigen auf. Der Zuschnitt existiert, damit keine Spec mehr als ein Subsystem beschreibt.

| #     | Teilprojekt                                                                                                                 | Abnahmekriterium                                                            | Status            |
| ----- | --------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ----------------- |
| A     | **Fundament** — CMake/vcpkg-Umbau, commonlibf4 als Submodul + CMake-Shim, F4SE-Entrypoints, Logging, Runtime-Erkennung      | DLL lädt in FO4 AE 1.11.240, schreibt eine Logzeile, stürzt nicht ab        | **abgeschlossen** |
| B1    | **Render-Anbindung** — Zugriff auf D3D11-Device/Context/SwapChain, Present-Hook, Frame-Zähler, Debug-Marker                 | RenderDoc-Capture zeigt einen von uns gesetzten Marker                      | **abgeschlossen** |
| B2    | **Render-Target-Inventar** — die 101 anonymen Targets aus BSGraphics::RendererData identifizieren und benennen              | Beschriftetes RenderDoc-Capture plus Befunddokument mit der Target-Tabelle  | **abgeschlossen** |
| C     | **Shader-Pipeline** — Laden, Kompilieren, Cachen, Hot-Reload, Einschleusen eigener Shader                                   | Ein vorhandener FO4-Shader wird nachweislich durch einen eigenen ersetzt    | **abgeschlossen** |
| D1    | **Feature-Framework** — Feature-Basisklasse, Registrierung, Lifecycle, Settings-Persistenz                                  | Zwei Features unabhängig an-/abschaltbar                                    | **abgeschlossen** |
| D2    | **Paketierung** — `dist/`, Basis-, Addon- und AIO-Archive                                                                   | Ausgeliefertes Archiv installiert sich in ein sauberes Spiel                | **abgeschlossen** |
| E1    | **Overlay und Eingabe** — ImGui-Overlay, Fensterprozedur, Eingabesperre, eigener Zeiger                                     | Overlay im Spiel bedienbar, Spieleingabe steht, solange es offen ist        | **abgeschlossen** |
| E2    | **Einstellungsoberfläche** — Featureliste, Schreiben von Einstellungen, Themes, Schriften, i18n                             | Einstellungen im Overlay ändern, sie überleben einen Neustart               | **abgeschlossen** |
| F1    | **Performance Overlay** — CPU- und GPU-Zeitmessung je Pass, im Overlay dargestellt                                          | Zahlen im Spiel ablesbar, die sich unter Last bewegen                       | **abgeschlossen** |
| F2    | **Screen-Space Shadows** — die Naht: eigener Pass, G-Buffer lesen, Ergebnis in die Beleuchtung                              | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | **abgeschlossen** |
| F3    | **Exponential Height Fog** — erster Pass hinter der opaken Szene, dazu `FrameTrace` als Werkzeug für alle Anker             | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | **abgeschlossen** |
| F4    | **Cloud Shadows** — die Wolken der Engine ein zweites Mal gezeichnet, in eine eigene Cubemap; dazu der Draw-Hook            | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | **abgeschlossen** |
| F4.5  | **Menü in zwei Spalten** — Seitenliste links mit den Schaltern, eine Seite je Eintrag rechts, Performance als Seite         | Jede Seite erreichbar, Schalten aus der Liste, Zahlen der Tafel unverändert | **abgeschlossen** |
| F5    | **Skylighting** — Sondengitter wie die Vorlage, Erfassung über die Schattenwerfer, Anbringen im SSAO-Ziel vor dem Composite | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | **abgeschlossen** |
| F5.5a | **DLSS Super Resolution** — NGX auf D3D11, DLAA und vier Hochskalier-Modi über die dynamische Auflösung der Engine          | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | **abgeschlossen** |
| F5.5b | **DLSS Frame Generation** — D3D12-Brücke mit Streamline, DLSS-G/MFG und Reflex; Andockpunkt für DLSS 5                      | Mehr Bilder je Sekunde, gemessen, bei stehendem Overlay                     | **abgeschlossen** |
| F6    | **Volumetric Lighting**                                                                                                     | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F7    | **Volumetric Shadows**                                                                                                      | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F8    | **Screen Space GI**                                                                                                         | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F9    | **Subsurface Scattering**                                                                                                   | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F10   | **Dynamic Cubemaps**                                                                                                        | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F11   | **IBL**                                                                                                                     | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| F12   | **Forschung: Permutations-Cache** — Shader-Cache über die Technikkarten, belegt am kleinsten Gruppe-2-Feature               | Eine Permutation von `kDFPrepass` nachweislich durch eine eigene ersetzt    | offen             |
| F13   | **Frame Generation** — DX12-Swapchain-Stellvertreter, FSR3 über die FidelityFX-SDK, optional DLSS über Streamline           | Mehr Bilder je Sekunde, gemessen, bei stehendem Overlay                     | offen             |
| F14+  | **Objekt-Shader** — die tragenden Gruppe-2-Features, Zuschnitt und Zahl folgen aus F12                                      | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                      | offen             |
| Fx    | **Werkzeuge** — Screenshot, RenderDoc-Anbindung                                                                             | Werkzeug tut, was sein Name sagt                                            | offen             |

Das ursprüngliche Teilprojekt D wurde am 2026-08-30 in D1 und D2 geteilt: Laufzeitverhalten und
Auslieferung sind zwei Subsysteme ohne Berührung. Das Abnahmekriterium von D1 heißt „zwei
Features", nicht mehr „zwei Dummy-Features" — siehe die Begründung im Abschnitt zu D1.

D2 sollte dabei zunächst **hinter F** rutschen, weil Paketierung erst lohne, wenn es echte
Features zu paketieren gebe. Das ist am 2026-08-31 umgekehrt worden: eine funktionierende
Auslieferung früh zu haben heißt, dass jedes Feature ab F automatisch mitfährt, statt am Ende in
eine gewachsene Struktur nachgerüstet zu werden. Der Preis war, dass die Aufteilung in Basis und
Addons an zwei Features geprüft wurde statt an zwanzig. Der Ini-Versionsaudit ist dabei **nicht**
mitgekommen und bleibt ruhend, siehe den Abschnitt zu D2.

Das ursprüngliche Teilprojekt E wurde am 2026-08-31 in E1 und E2 geteilt. E bündelte fünf Dinge:
Overlay-Rendering, Eingabe, Einstellungsoberfläche, Themes und Schriften, und i18n. Die ersten
beiden bilden ein Subsystem — „können wir zeichnen und Eingaben nehmen" — und tragen sämtliche
riskanten Unbekannten; die übrigen drei beantworten die andere Frage, „was zeigen wir". Die
Teilung hat sich bezahlt gemacht: der größte Einzelposten in E1 war ein Problem, das in der Spec
mit keinem Wort vorkam und vier Spielstarts zur Klärung brauchte, siehe den Abschnitt zu E1.

Das ursprüngliche Teilprojekt B wurde am 2026-08-30 in B1 und B2 geteilt. B1 ist begrenzte
Ingenieursarbeit mit klarem Ende; B2 ist offene Reverse-Engineering-Forschung, deren Aufwand sich
vorher nicht seriös schätzen lässt. Zusammen hätte B kein vorhersagbares Ende gehabt. Die
Marker aus B1 sind zugleich das Werkzeug, mit dem sich in B2 überhaupt sinnvoll suchen lässt.

A bis C sind die eigentliche Portierungsarbeit. Für D und E galt die Annahme, sie seien
weitgehend aus dem bestehenden Skyrim-Code übernehmbar, weil sie kaum engine-gekoppelt sind —
**für D1 und E1 hat sich das nicht bestätigt**, siehe die Abschnitte zu D1 und E1. In E1 war die
Skyrim-Vorlage sogar dort stumm, wo sie am meisten geholfen hätte: sie enthält keine einzige
Zeile zum Umgang mit dem Systemzeiger, weil Skyrim das Problem nicht hat. Die vorhandenen HLSL-Shader werden
erst ab F relevant — sie sind das Ziel der Portierung, nicht ihr Anfang.

### Wie F+ in F1…Fn zerfallen ist

Am 2026-09-05 zerlegt, nachdem ein Messspike die Ausgangslage geklärt hatte. Die Befunde stehen in
`shader-techniques.md`, die Entscheidungen sind diese:

**Kuratierte Auswahl statt aller 39.** Gruppe 1 (Bildschirmraum) vollständig, aus Gruppe 2
(Objekt-Shader) die tragenden, aus Gruppe 5 die billigen Werkzeuge. Die Gruppen 3 (Skyrim-Weltdaten),
4 (Light Limit Fix, braucht `BSLight`, `ShadowSceneNode` und `BSRenderPass`) und 6 (Effects11, und
ENB gilt für FO4 ohnehin als inkompatibel) bleiben draußen.

**Das Messwerkzeug steht vorn.** Jedes F hat als Abnahmekriterium „sichtbarer Effekt plus
CPU-/GPU-Zahlen", und Zahlen gibt es bislang keine — der `FrameCounter` zählt Frames. Das
Performance Overlay ist deshalb F1 und kein Feature unter vielen: es macht die siebzehn folgenden
Teilprojekte überhaupt abnehmbar.

**Gruppe 1 vor der Forschung.** Der Spike hat gezeigt, dass Bildschirmraum-Features den
Permutations-Cache nicht brauchen: sie sind eigene Pässe, die den G-Buffer lesen. Kompilieren und
Einschleusen liegen seit C vor, die Targets seit B2. Der Cache wird erst für Gruppe 2 gebraucht,
und die ist teurer geworden — siehe unten. F2 trägt die Naht für alle folgenden: eigener Pass,
G-Buffer-Zugriff, Ergebnis zurück in die Beleuchtung. F3 hat dazu `FrameTrace` hinterlassen, ein
Diagnose-Feature, das auf Tastendruck einen Frame der Engine als Liste ins Log schreibt: der Anker
jedes weiteren Bildschirmraum-Features (F4 bis F11) wird damit gemessen statt vermutet — und der
Zähler in der Sekundenzeile hat gezeigt, daß zwei gemessene Frames noch keinen Anker belegen.

**Gruppe 2 bekommt ein eigenes Forschungs-Teilprojekt**, ausdrücklich wie B2 markiert. F12 baut den
Permutations-Cache und belegt ihn am kleinsten Gruppe-2-Feature. Zahl und Zuschnitt von F14
aufwärts werden erst danach festgelegt: vorher lässt sich nicht seriös schätzen, was ein einzelnes
Objekt-Shader-Feature kostet, wenn es nicht übersetzt, sondern gegen `kDFPrepass` neu gedacht
werden muss.

### F13 — Frame Generation

Am 2026-09-05 recherchiert und in den Zuschnitt aufgenommen, zwischen dem Forschungs-Teilprojekt
und den Objekt-Shadern.

**Warum es überhaupt geht, obwohl Fallout 4 DX11 ist.** Frame Generation gibt es weder bei AMD noch
bei NVIDIA für DX11. Der Weg ist ein **Stellvertreter für die DXGI-Swapchain**, hinter dem eine
echte DX12-Swapchain sitzt, mit zwischen `ID3D11Device5` und `ID3D12Device` geteilten
Bildtexturen. Der Generator läuft auf DX12, das Spiel merkt nichts. Der Skyrim-Stand hat das als
Feature `Upscaling`: `DX12SwapChain`, `FidelityFX`, `Streamline` und `RCAS`, zusammen 3.344 LOC
C++ über vier Dateien plus zwei SDKs als Submodule.

**Für Fallout 4 ist es bereits gebaut, vom Autor der Skyrim Community Shaders.** doodlums freie Mod
[Frame Generation](https://www.nexusmods.com/fallout4/mods/98208) ist ein F4SE-Plugin mit FSR3
Frame Generation über die FidelityFX-SDK und genau diesem DX12-Stellvertreter; Quelltext in
[`doodlum/fo4test`](https://github.com/doodlum/fo4test) unter **GPL-3.0-or-later mit
Modding-Ausnahmen**, also unter unserer eigenen Lizenz. Sie verlangt randlosen Fenstermodus, die
Address Library und High FPS Physics Fix, und verträgt sich mit ENB. **Übernehmen statt neu
schreiben** ist deshalb die Vorgabe für F13.

**Die Eingaben liegen vor.** FSR3 FG will Farbe, Tiefe, Bewegungsvektoren und möglichst eine
UI-Maske. Aus dem Messspike: Bewegung `FO4_RT_029`, Tiefe `FO4_DS_002`, HDR-Farbe `FO4_RT_058` /
`FO4_RT_059`.

**Warum nicht früher.** Der Stellvertreter **besitzt die Swapchain** — dasselbe Objekt, auf das
unser Overlay seit E1 im `Present`-Hook zeichnet. F13 und das Overlay müssen zusammen entworfen
werden, und das geht erst, wenn das Overlay steht und F1 die Zahlen liefert, an denen sich der
Gewinn zeigen lässt. Offen und billig zu klären: ob doodlums Mod und unser Present-Hook sich
**schon jetzt** in die Quere kommen. Ein Spielstart beantwortet das.

**Stand 2026-10-04: DLSS vorgezogen als F5.5a/b.** Der Nutzer wollte DLSS 5; eine Recherche
(Abschnitt „Aus Teilprojekt F5.5a bestätigt") ergab, daß DLSS 5 (DLSS-NR) kein öffentliches SDK
hat — Streamline 2.14.1 kennt nur den Namen. Super Resolution kam deshalb als F5.5a über NGX auf
D3D11, Frame Generation folgt als F5.5b auf einer D3D12-Brücke, die zugleich der Andockpunkt für
DLSS 5 ist. F13 behält FSR3 als Weg für Karten ohne DLSS-G. Der folgende Absatz ist der Stand
vom 2026-09-05.

**DLSS ja, DLSS 5 nein.** DLSS Frame Generation läuft über Streamline und braucht ebenfalls DX12 —
derselbe Stellvertreter trägt es, und der Skyrim-Stand hat `Streamline.cpp` bereits. DLSS 5 ist am
2026-09-03 erschienen, RTX-50-exklusiv, und scheidet aus zwei Gründen aus: es steht in keinem
öffentlichen Streamline-Release (aktuell 2.11.1 / 2.12.0 mit DLSS 4.5), und es ist kein Upscaler
mehr, sondern neuronale Neubeleuchtung, die **objektbezogene Masken** erwartet. Fallout 4 zeichnet
HUD und Pip-Boy ins selbe Bild und kennt keine Semantik, die sich dem Modell reichen ließe. Zu
prüfen, wenn F13 drankommt — nicht vorher.

## Ausgangslage (gemessen am Skyrim-Stand, 2026-08-30)

Umfang des geerbten Codes: 100.970 LOC C++ (135 `.cpp`, 147 `.h`), 17.839 LOC HLSL (153 Dateien),
40 Features.

Kopplung an Skyrim in `src/` und `include/`:

| Kopplungspunkt              | Vorkommen                       |
| --------------------------- | ------------------------------- |
| `RE::`                      | 2666                            |
| `REL::`                     | 433                             |
| `RE::BSShader`              | 324                             |
| `stl::write_vfunc`          | 321                             |
| `RE::BSGraphics`            | 176                             |
| `RE::TESWeather`            | 143                             |
| `RE::RENDER_TARGET::k*`     | 132                             |
| `SKSE::`                    | 34                              |
| `REL::RelocationID(se, ae)` | 158 Aufrufe, 110 eindeutige IDs |

Zum Vergleich: die Referenzimplementierung kommt mit 49 `RE::`-Referenzen aus (28.955 LOC C++,
23.245 LOC HLSL, 10.871 LOC Tests, 8 Features). Dieser Unterschied ist der Kern der Entscheidung
„Architektur neu denken": nicht Skyrims Engine-Typen nachbauen, sondern auf D3D11-Ebene abfangen
und nur wenige Engine-Anker über die Adressbibliothek ziehen.

`add_compile_definitions(SKYRIM)` steht an genau einer Stelle (`cmake/XSEPlugin.cmake:1`), und
`ENABLE_SKYRIM_SE/AE/VR` wird im Projektcode nirgends ausgewertet — beides geht ausschließlich an
CommonLibSSE-NG. Der Build-Layer ist also dünn; die Kopplung sitzt im Code, nicht im Buildsystem.

## Aus Teilprojekt A bestätigt

Teilprojekt A ist am 2026-08-30 abgenommen worden. Die folgenden Punkte sind damit gemessen und
nicht mehr Annahme; B setzt sie als gegeben voraus.

| Sachverhalt                               | Bestätigter Wert                                                                  |
| ----------------------------------------- | --------------------------------------------------------------------------------- |
| Spielversion der Testumgebung             | `1.11.240.0` (Fallout4.exe und `LoadInterface::RuntimeVersion()` stimmen überein) |
| F4SE                                      | `0.7.9`, `f4se_1_11_240.dll`                                                      |
| Von commonlibf4 aufgelöster Runtime-Eimer | `AE`                                                                              |
| `GetSaveFolderName()`                     | `Fallout4`                                                                        |
| Tatsächlicher Logpfad                     | `<Dokumente>/My Games/Fallout4/F4SE/CommunityShadersFO4.log`                      |
| `<RE/Fallout.h>`                          | übersetzt sauber als eigene Übersetzungseinheit (116 KB Objektdatei)              |
| `/W4 /WX` gegen commonlibf4-Header        | kein einziger Treffer; die `PUBLIC`-Unterdrückungen von commonlib-shared reichen  |
| `commonlib-shared` per `add_subdirectory` | funktioniert unverändert, der Shim deckt nur commonlibf4 ab                       |

Beobachtetes Verhalten, das B kennen sollte:

-   Lehnt `F4SEPlugin_Load` mit `false` ab, protokolliert F4SE das als
    `reported as incompatible during load` und zeigt dem Nutzer **vor dem Spielstart einen
    Warndialog**. Das Spiel startet danach normal weiter. Die Ablehnung ist also sichtbar, ohne
    dass wir dafür etwas bauen müssten.
-   `kGameDataReady` trifft rund neun Sekunden nach `kPostPostLoad` und auf einem **anderen
    Thread** ein.
-   Der Visual-Studio-Generator bricht bei zu langen Checkout-Pfaden mit `MSB6003` ab und zeigt
    dabei fälschlich auf `link.exe`. Ursache sind die `.tlog`-Pfade unter `build/FO4/CMakeFiles`.

## Aus Teilprojekt B1 bestätigt

B1 ist am 2026-08-30 abgenommen worden. Der Marker war in zwei RenderDoc-Captures nachweisbar
(`CommunityShadersFO4 Frame 379` und `... Frame 1925`), verifiziert durch Suche im Capture-File
statt durch die Oberfläche.

| Sachverhalt                | Bestätigter Wert                                                                                                                                                                               |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Adressauflösung für AE** | **trägt.** Der Kreuzvergleich ist bestanden: das Device aus `GetRendererData()` ist dasselbe, das Context und SwapChain melden. `REL::VariantID{og, ng}` liefert für AE also korrekte Adressen |
| Feature-Level              | `0xb000`, also D3D 11.0                                                                                                                                                                        |
| SwapChain                  | 2560x1440, Format 28 (`R8G8B8A8_UNORM`), 2 Puffer                                                                                                                                              |
| `IDXGISwapChain::Present`  | vtable-Slot 8, bestätigt durch einen laufenden Frame-Zähler                                                                                                                                    |
| Verkettungsziel            | `0x7ffa699338e0`, Systemmodul-Bereich — es saß nichts unter uns, wir waren Erste auf der Kette                                                                                                 |
| Installationszeitpunkt     | `kGameDataReady` ist früh genug, der SwapChain steht dort                                                                                                                                      |
| D3D-Typen                  | `REX::W32` deckt D3D11 und DXGI vollständig ab, `<d3d11.h>` wird nirgends gebraucht                                                                                                            |

Beobachtetes Verhalten, das spätere Teilprojekte kennen sollten:

-   Der PCH bringt nur `REL` und `REX` mit. Wer D3D- oder DXGI-Typen in einer eigenen Schnittstelle
    nennt, muss `REX/W32/D3D11.h`, `DXGI.h` oder `D3D11_1.h` selbst einbinden.
-   Der Marker aus B1 sitzt am Frame-**Ende**, weil er an Present hängt: im RenderDoc-Event-Browser
    erscheint er ganz unten, direkt vor dem abschließenden `Present`. Ein Marker, der eine ganze
    Frame-Struktur umschließt, braucht einen Hook am Frame-Anfang.

## Aus Teilprojekt B2 bestätigt

B2 ist am 2026-08-30 abgenommen worden. Die vollständige Tabelle steht in
`docs/fallout4-port/render-targets.md`.

| Sachverhalt                  | Bestätigter Wert                                                                                                                                |
| ---------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Belegte Targets              | 65 von 101 Render-Targets, 10 von 13 Tiefenpuffern, 1 von 2 Cubemaps                                                                            |
| Beschriftung                 | wirkt. 267 Objekte benannt, keine Ablehnung; 59 Namen in einem Spielwelt-Capture sichtbar. `SetPrivateData` greift also auch nachträglich       |
| Verbindung der beiden Arrays | über `renderTargetID` und **rückwärts**: `renderTargetID[j] == i` nennt den Renderer-Slot der Manager-Zeile `j`. Direkte Indizierung ist falsch |
| `BSGraphics::Format`         | **ist** `DXGI_FORMAT`. In allen 65 Fällen numerisch identisch, sobald über die richtige Zeile gelesen                                           |
| Schattenkarten               | `DS_007` und `DS_008`, 4096x4096 `R16_TYPELESS`, auflösungsunabhängig                                                                           |

Die beiden mittleren Erkenntnisse ließen sich an commonlibf4 zurückgeben.

## Aus Teilprojekt C bestätigt

C ist am 2026-08-30 abgenommen worden. Das Befunddokument ist
`docs/fallout4-port/imagespace-passes.md`.

| Sachverhalt             | Bestätigter Wert                                                                                                    |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------- |
| Einschleusweg           | Zeiger-Tausch in der Technikkarte **trägt**. Kein Engine-Hook, kein Trampolin, keine Änderung an commonlibf4        |
| Klassen-Identifikation  | über die MSVC-RTTI des Objekts, ohne jeden Adressbibliothek-Zugriff. 226 von 226 Objekten benannt                   |
| `BSImagespaceShader`    | erbt von `BSShader` **und** `ImageSpaceEffect`; der zweite Anteil liegt bei `+0x190`, bei allen 160 Objekten gleich |
| Pässe mit Pixel-Technik | 121, jeder mit genau einer Technik der ID `0`, jeder mit engine-eigenem `fxp`-Namen                                 |
| Sicherheitsnetz         | 0 Ablehnungen im Endstand                                                                                           |
| Ersetzter Pass          | `BSImagespaceShaderCopy` (`ISCopy`), Technik `0`, Quervergleich der Zeiger bestanden                                |
| Hot-Reload              | wirkt binnen einer Sekunde, ohne Spielneustart                                                                      |
| Übersetzungsfehler      | Bild behält den letzten guten Shader, Spiel läuft weiter, Compilerfehler mit echter Datei und Zeile im Log          |
| `REX::W32::D3DCompile`  | nutzbar, `d3dcompiler.lib` wird von `commonlib-shared` bereits `PUBLIC` gelinkt                                     |
| Nicht geprüft           | das RenderDoc-Capture mit unserem Shader-Namen. Bewusst ausgelassen, Begründung in `imagespace-passes.md`           |

**Korrekturen an Annahmen, die diese Roadmap über C getroffen hatte:**

-   C schaltet das Trampolin **nicht** ein und braucht `REL::THook` nicht. `InitInfo::trampoline`
    und `hook` stehen weiterhin auf `false`.
-   `BSRenderPass` wird für C **nicht** gebraucht. Die Lücke bleibt bestehen, sie wird später
    fällig als angenommen.

**Fallstricke, die spätere Teilprojekte kennen sollten:**

-   `REL::ID::offset()` ruft bei unbekannter ID `REX::FAIL` und beendet den Prozess
    (`IDDB.cpp:442`). Eine Tabelle vieler IDs blind aufzulösen ist ein Absturzrisiko.
-   `REX::W32` deklariert weder `ReadDirectoryChangesW` noch `FindFirstChangeNotification`.
    Dateiüberwachung geht über `std::filesystem::last_write_time`, nicht über `<Windows.h>`.
-   `effectList.size()` meldet 225, die Iteration findet 226 gefüllte Plätze. Der Iteration
    trauen, nicht `size()`.

## Aus Teilprojekt D1 bestätigt

D1 ist am 2026-08-31 abgenommen worden. Spec und Plan liegen unter `docs/superpowers/`.

| Sachverhalt         | Bestätigter Wert                                                                                                                                                                            |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Basisklasse         | vier Methoden reichen (`Name`, `Setup`, `Frame`, `Shutdown`). Skyrims Fassung hat 42 Virtuals und hängt an `I18n`                                                                           |
| Registrierung       | drei Zustände, Aufbau in Registrierungsreihenfolge, Abbau rückwärts. Ohne Spiel prüfbar, weil der Wunschzustand als Lambda hereinkommt statt aus den Einstellungen gelesen zu werden        |
| Persistenz          | `REX::FJsonSettingStore` mit glaze `7.2.1`, erste neue Abhängigkeit über `spdlog` hinaus. Baut sauber unter `/W4 /WX`                                                                       |
| Einstiegspunkt      | genau einer pro Frame (`Features::TickSystem`), C's Sonderverdrahtung im Present-Hook ist weg                                                                                               |
| Erstes Feature      | C's Pipeline als `ImagespaceTint`, mit echtem Zustand: erzeugter `ID3D11PixelShader` plus Zeiger in Engine-Speicher                                                                         |
| Hochfahren im Spiel | belegt im Log: `2 features registered`, `ImagespaceTint: running`, Katalog wählte `BSImagespaceShaderCopy` Technik `0`                                                                      |
| Abschalten im Spiel | Stich verschwindet in **unter einer Sekunde** nach dem Speichern der JSON, ohne Spielneustart. Erster Aufruf von `PixelShaderOverride::Restore()` überhaupt — der Zeiger geht sauber zurück |
| Unabhängigkeit      | `FrameCounter` und `ImagespaceTint` lassen sich einzeln umschalten, ohne einander zu stören                                                                                                 |
| Kaputte JSON        | glaze meldet den Fehler, beide Features behalten den letzten korrekten Stand, das Spiel läuft weiter                                                                                        |
| Host-Tests          | acht, davon drei neu (`FileWatch`, `FeatureSettings`, `FeatureRegistry`), jeder durch Mutation als greifend belegt                                                                          |

**Korrekturen an Annahmen, die diese Roadmap über D getroffen hatte:**

-   D ist **nicht** „weitgehend aus dem bestehenden Skyrim-Code übernehmbar". Skyrims `Feature`
    zieht `FeatureCategories`, `FeatureConstraints`, `FeatureVersions`, `RestartSettings` und
    `I18n` nach sich — eine 1:1-Übernahme hätte i18n-Arbeit aus E nach D1 vorgezogen. Die
    Basisklasse ist stattdessen aus dem gewachsen, was C tatsächlich braucht.
-   Das Abnahmekriterium hieß „zwei **Dummy**-Features". Ein Feature, das nichts besitzt, kann
    nicht zeigen, ob `Shutdown` etwas taugt. C's Pipeline wurde deshalb selbst zum ersten Feature.

**Fallstricke in `REX`, die spätere Teilprojekte kennen sollten** — alle vier im Quelltext von
commonlib-shared nachgeschlagen, nicht vermutet:

-   **`TJsonSetting::m_path` ist ein JSON-Pointer, kein Punkt-Pfad.** `JsonSettingLoadEx` stellt
    ein `/` voran und reicht das an `glz::get` weiter, das je Segment ein Objekt tiefer geht
    (`src/REX/TJsonSetting.cpp:8`). `Name.enabled` adressiert einen obersten Schlüssel mit einem
    Punkt im Namen; richtig ist `Name/enabled`. Mit dem Punkt greift **keine** Einstellung, still
    und ohne Fehlermeldung — durch eine Mutation im Test belegt.
-   **`FJsonSettingStore::Save()` kann keine Schlüssel anlegen.** Es schreibt über `glz::set`, und
    dessen `seek_op<generic_json>` bricht bei `obj.find(key) == end()` ab
    (`glaze/json/generic.hpp:714`). Auf einer fehlenden Datei ist `Save()` damit wirkungslos: die
    erste Fassung muss von uns geschrieben werden, sonst hätte auch das Menü in E nichts, worin es
    speichern könnte.
-   **`Save()` schreibt nach `m_fileBase`, nicht nach `m_fileUser`** (`FJsonSettingStore.cpp`).
    Wir setzen `fileBase` auf die eine Nutzerdatei und lassen `fileUser` leer.
-   **`m_fileBase`, `m_fileUser` und `m_path` sind `string_view`s** (`FSettingStore.h`,
    `TJsonSetting.h:48`). Die Zeichenketten müssen ihre Einstellung überleben und dürfen nach der
    Konstruktion nicht umziehen — ein `std::vector` als Ablage würde beim Wachsen jeden View
    ungültig machen.
-   `JsonSettingLoad`/`Save` sind **nicht für `float`** instanziiert. Wer einen Gleitkommawert
    braucht, nimmt `double`.

**Offene Beobachtung, bewusst nicht weiterverfolgt:** Ein Spielstart am 2026-08-31 blieb schwarz
hängen und war nach einem Neustart nicht wieder auffällig; ein Absturz war es nicht, es entstand
kein Crashlog. Die Logs der betroffenen Läufe sind überschrieben — daher rührt die inzwischen
eingeschaltete Log-Rotation (`InitInfo::logRotate = 5`), die die letzten fünf Läufe aufhebt.

Beim Lesen des Quelltextes fiel dabei auf, dass `kGameDataReady` erst `InstallSwapChainHook()` und
danach `StartSystem()` rief: acht Millisekunden lang war der Present-Hook scharf, während
`Registry::Register` noch an seinen Vektor anhängte, und der Lauf von 19:53 belegt, dass beide auf
verschiedenen Threads laufen (`3012` und `20772`). Die Reihenfolge ist daraufhin umgedreht worden,
sodass das Fenster nicht mehr existiert. Ob es die Ursache des Hängers war, bleibt **ungeprüft**.

## Aus Teilprojekt D2 bestätigt

D2 ist am 2026-08-31 abgenommen worden. Spec und Plan liegen unter `docs/superpowers/`.

| Sachverhalt         | Bestätigter Wert                                                                                                                    |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Archive             | drei aus einem Lauf: Basis `2,48 MB`, Addon `0,9 KB`, AIO `2,48 MB`. Nur `.zip`, kein 7-Zip                                         |
| Auslöser            | eigenes Ziel `package`, nicht in `ALL`. Der normale Bau schreibt keine Archive                                                      |
| Eine Regel          | `tools/package.ps1` bedient beides: `-Stage` für die Spielinstallation, ohne `-Stage` die Archive                                   |
| Kosten des Stagings | **0,33 s je Bau** gemessen. Die Spec hätte ab einer Sekunde zurückgedreht                                                           |
| `cmake -E tar`      | schreibt saubere Data-relative Namen **ohne** `./`-Präfix, dazu eigene Verzeichniseinträge                                          |
| Vortex              | packt **beide** Archivformen richtig aus, ohne Hüllordner — auch ein Addon, dessen Wurzel nur `Shaders/` enthält                    |
| Basis ohne Addon    | vierfach belegt: `cannot read …/ImagespaceCopy.hlsl`, dann `replacing BSImagespaceShaderCopy technique 0`, kein Stich, kein Absturz |
| Basis mit Addon     | `installed … in place of …`, Stich sichtbar — die getrennten Archive ergeben zusammen dasselbe wie das AIO                          |
| PDB                 | im Release-Bau entstand **keine**. `/Zi` plus `/DEBUG /OPT:REF /OPT:ICF` nachgerüstet; PDB `7,8 MB`, DLL unverändert `0,7 MB`       |

**Korrekturen an Annahmen, die Spec und Plan getroffen hatten:**

-   Die Spec sah Feature-Assets unter `features/<Name>/` vor, wie bei Skyrim CS. Das trägt nicht:
    dort liegen die 40 geerbten Skyrim-Verzeichnisse, 27 davon mit `CORE`-Marker, und ein Glob
    packte sie alle ein. Sie liegen deshalb unter **`package/Features/<Name>/`**, wo nur steht,
    was wir hinlegen. `features/` behält seine Rolle als Rohmaterial für F.
-   Der Plan nahm an, `cmake -E tar` stelle den Einträgen ein `./` voran. Tut es nicht.
-   Der Testschritt „Addon bei laufendem Spiel nachinstallieren" ist **nicht durchführbar**: ein
    Mod-Manager liefert nicht aus, solange das Spiel läuft. Vortex schrieb die Datei erst, als das
    Spiel beendet war — vier Läufe davor sahen sie deshalb nicht. Wer das prüfen will, muss die
    Datei von Hand kopieren.

**Fallstricke, die spätere Teilprojekte kennen sollten:**

-   **Der Deploy-Schritt schreibt an Vortex vorbei direkt nach `Data`.** Während einer
    Mutationsprobe hat er 185 fremde Dateien in die Spielinstallation getragen. Wer die
    Staging-Regel anfasst, ändert damit unmittelbar das installierte Spiel.
-   **Lizenztexte gehören nicht in den Staging-Baum.** Ein Mod-Manager liefert sie als Hardlink
    aus; sie zu überschreiben schlägt in seinen Staging-Ordner durch. `-Stage` lässt sie deshalb
    weg, das Archiv führt sie.
-   **Ein Prüfer, der Archive liest, kann auf veralteten Archiven grün melden.**
    `verify-package.ps1` vergleicht ihr Alter deshalb gegen DLL **und** `tools/package.ps1`.
-   `Get-ChildItem -Filter` reicht sein Muster an das Dateisystem weiter, das nur `*` und `?`
    kennt — eine Zeichenklasse wie `[0-9]` trifft nichts. Und PowerShell packt einelementige
    Arrays beim Zuweisen aus, weshalb `.Count` dort leer ist.

**Offener Befund für F, aus dem Quelltext gelesen und bewusst ungeprüft geblieben:** In
`ImagespaceTint::WatcherLoop` wird `loadedOnce` auch dann gesetzt, wenn `CompileAndPublish` an
einer fehlenden Datei scheitert. Weil `_watch.Reset` in diesem Zweig nicht mehr erreicht wird,
bleibt der Watcher für die **ganze Sitzung** leer — damit greift auch das Hot-Reload aus
Teilprojekt C nicht mehr, sobald ein Shader beim ersten Versuch fehlte.

## Aus Teilprojekt E1 bestätigt

E1 ist am 2026-09-04 abgenommen worden, alle sieben Abnahmekriterien erfüllt. Spec und Plan liegen
unter `docs/superpowers/`.

| Sachverhalt         | Bestätigter Wert                                                                                                                                    |
| ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| ImGui               | `1.92.6` über vcpkg, `dx11-binding` und `win32-binding`. Übersetzt unter `/W4 /WX` **ohne eine einzige Unterdrückung**                              |
| Die vier IDs        | zeigen auf das, wofür wir sie hielten. Bei offenem Overlay bewegt sich der Spieler nicht, die Kamera dreht nicht, ein Klick löst keinen Angriff aus |
| `AllocateNewLayer`  | liefert `layerID 0` — der erste Layer. `EnableUserEvent(kAll, false, kMenu)` schaltet alles ab, `DecRef` gibt es zurück                             |
| Aufsetzen           | erstes `Present`: `overlay ready, window 0x…, ImGui 1.92.6`, dann `window procedure chained`                                                        |
| `SetWindowLongPtrW` | liefert als Vorgänger `0xffff19bb` — kein Codezeiger, sondern ein Server-seitiges Handle. Genau dafür gibt es `CallWindowProcW`; es trägt           |
| D3D-Typen           | `imgui_impl_dx11.h` deklariert `ID3D11Device` und `ID3D11DeviceContext` selbst vorwärts. `<d3d11.h>` blieb draußen, `reinterpret_cast` reicht       |
| Einstellungsdatei   | bekommt beim ersten Start einen Block `Menu` mit `toggleKey`. `112` dort öffnet nach einem Neustart mit F1, `Ende` tut dann nichts                  |
| Host-Tests          | neun, alle grün. `MenuGateTests` prüft den Zustandsautomaten ohne Spiel, `FeatureSettingsTests` die erweiterten Einstellungen                       |

**Der Systemzeiger ist gefangen, und zwar nicht über `ClipCursor`.** Das war der teuerste Befund
des Teilprojekts und steht in keiner Spec. Solange das Spiel läuft, hält etwas den Systemzeiger in
den **mittleren 1280×720 eines 2560×1440-Schirms** fest, also `[640, 1919] × [360, 1079]`.
Gemessen mit gezielten Sonden:

| gesetzt auf    | gelandet auf   |
| -------------- | -------------- |
| `(50, 50)`     | `(640, 360)`   |
| `(2500, 1400)` | `(1919, 1079)` |
| `(639, 359)`   | `(640, 360)`   |
| `(1921, 1081)` | `(1919, 1079)` |

Schon **ein** Pixel darüber hinaus wird korrigiert. Dabei meldet `GetClipCursor` durchgehend den
vollen virtuellen Desktop, und ein `ClipCursor(nullptr)` in jedem Frame ändert nichts — es ist
also keine Klemmung im Sinne von USER32. Die Engine **bewegt** den Zeiger auch nicht von selbst:
über eine Sekunde ohne Handbewegung erzeugt keine einzige Positionsänderung. Sie korrigiert nur.

Warum ausgerechnet die halbe Kantenlänge, und wer korrigiert, ist **offen**. Für die Lösung war es
nicht nötig: `Menu::MousePointer` parkt den Systemzeiger jeden Frame in der Fenstermitte und
verwendet nur die zurückgelegte Strecke. Von der Mitte aus erreicht eine Handbewegung pro Frame
die Grenzen nie, also kommt der Mechanismus gar nicht zum Zug. Weil ImGui seinen Zeiger ohnehin
selbst zeichnet (`io.MouseDrawCursor`), ist der geparkte Systemzeiger unsichtbar. Geparkt wird nur,
solange das Spielfenster im Vordergrund ist — sonst risse es dem Spieler nach Alt-Tab die Maus aus
dem Fenster, in das er gewechselt ist.

**`REX::TJsonSetting<T>` liest für keinen Ganzzahltyp jemals aus der Datei.** Zweiter stiller
Befund, derselben Familie wie die JSON-Pointer-Falle aus D1. `JsonSettingLoadEx` ruft
`glz::get<T>(json, path).value_or(default)`; `glz::get` vergleicht den angeforderten Typ per
`std::same_as` gegen die Variantenalternative (`glaze/core/seek.hpp:271`), und `glz::generic` ist
`generic_json<num_mode::f64>`, dessen Variante eine JSON-Zahl **immer** als `double` führt
(`glaze/json/generic.hpp:68`). `TJsonSetting<std::uint32_t>` übersetzt, linkt und läuft — und
liefert stets den deklarierten Standardwert. Betroffen sind `uint8/16/32` und `int8/16/32`; nur
`bool`, `double` und `std::string` funktionieren. `Features::Settings::DeclareUInt32` legt deshalb
intern ein `TJsonSetting<double>` an und klemmt beim Lesen.

**Fallstricke, die spätere Teilprojekte kennen sollten:**

-   **`REX::W32`s `enum WM` endet bei `WM_CHILDACTIVATE` (`0x0022`)** und enthält keine einzige
    Eingabemeldung — kein `WM_KEYDOWN`, keine Maus. Die `VK_`-Konstanten daneben sind vollständig.
    Was fehlt, steht in `src/Menu/Win32.h`: dort auch `SetWindowLongPtrW`, `CallWindowProcW`,
    `GetCursorPos`, `SetCursorPos`, `ClipCursor`, `GetClipCursor` und `GetForegroundWindow`.
-   **`ImGui_ImplWin32_WndProcHandler` muss gegen `::HWND__` deklariert werden.** ImGui hält die
    Deklaration in einem `#if 0`, damit der Header nicht von `<windows.h>` abhängt. Die Funktion
    hat C++-Bindung, ihr dekorierter Name trägt also die Parametertypen:
    `?ImGui_ImplWin32_WndProcHandler@@YA_JPEAUHWND__@@I_K_J@Z`. `REX::W32::HWND` ist
    `REX::W32::HWND__*` — ein **anderer** Typ, der in ein Symbol dekoriert, das es nicht gibt;
    ein `void*` dekoriert zu `PEAX`. Beides scheitert erst im Linker, mit einer Meldung, die nicht
    auf die Ursache zeigt.
-   **Eine bestehende Einstellungsdatei wird nie um neue Schlüssel ergänzt.** `Settings::Init`
    schreibt nur, wenn die Datei fehlt, und REX kann keine Schlüssel anlegen (D1). Wer ein Update
    installiert, sieht neue Einstellungen also nie — in E1 musste die Datei von Hand beiseite
    gelegt werden, damit der `Menu`-Block überhaupt entstand. **In E2 erledigt:** `Init` prüft die
    Datei und ergänzt fehlende Schlüssel, siehe den Abschnitt zu E2.
-   Der Systemzeiger wird beim Öffnen einmal auf den Monitor des Spiels geklemmt. Auf einem
    Desktop mit mehr als einem Schirm ist das ein zweites, eigenständiges Ärgernis; deshalb löst
    `MenuSystem` die Klemmung beim Öffnen einmal.

**Bewusste Abweichungen von Spec und Plan:**

-   Der Plan gruppierte die Standardwerte der Einstellungsdatei nach C++-Typ, wodurch die
    Reihenfolge innerhalb eines Blocks davon abhing, welchen Typ eine Einstellung zufällig hat.
    Jetzt entscheidet der Schlüsselname.
-   Der Plan-Test für `Menu::Gate` widersprach sich selbst: `Check(!gate.Tick(), "and the next
tick does not close it again")` erwartete „geschlossen", während sein Text „bleibt offen"
    sagt. `Tick` liefert, ob das Overlay offen ist; der Test wurde korrigiert, nicht der Code.
-   `Overlay::Draw` liefert zurück, ob der Knopf gedrückt wurde, statt selbst zu schließen. So
    nehmen Klick und Umschalttaste denselben Weg durch das Tor, und die Eingabeschicht wird in
    beiden Fällen gleich freigegeben — im Log als ein `acquired` und ein `released` belegt.

## Aus Teilprojekt E2 bestätigt

E2 ist am 2026-09-04 abgenommen worden, alle sieben Abnahmekriterien erfüllt, sechs davon in
einem einzigen Spiellauf. Spec und Plan liegen unter `docs/superpowers/`.

| Sachverhalt      | Bestätigter Wert                                                                                                                                                    |
| ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Nachrüstung      | eine Datei mit nur `toggleKey` bekam `fontSize` und `language` dazu; vorhandene Werte und Feature-Schalter blieben. Im Log `is missing declared keys, extending it` |
| Rückkopplung     | in zwei Läufen mit zusammen mehr als zehn Änderungen **kein einziges** `settings changed, reloading`                                                                |
| Schriftgröße     | wirkt beim Ziehen, ohne Atlas-Neubau. Geschrieben wird beim Loslassen                                                                                               |
| Sprachwechsel    | `switched to locale de` und zurück, ohne Neustart. `i18n ready, locale en, 2 locale(s) available, 23 fallback key(s)`                                               |
| Tastenaufnahme   | `overlay toggle key rebound to 0x70`, ohne dass das Overlay dabei zuging. F1 öffnet danach                                                                          |
| `ImagespaceTint` | ließ sich **im laufenden Spiel** abschalten — `ImagespaceTint: off`. Zeiger zurückgegeben, Watcher-Thread beendet, kein Absturz                                     |
| Schrift          | `overlay font loaded from …/Data/F4SE/Plugins/CommunityShadersFO4`                                                                                                  |
| Host-Tests       | zwölf, alle grün. Neu: `SettingsSchema`, `SettingsStore`, `I18n`, `KeyNames`                                                                                        |

**Der teuerste Befund: ImGui wertet einen Klick an der Position aus, die zuletzt _vor_ dem
Tastenereignis in der Warteschlange stand.** `UpdateInputEvents` rieselt die Ereignisse und bricht
bei der ersten Positionsmeldung ab, nachdem in diesem Frame eine Maustaste verarbeitet wurde
(`imgui.cpp:11034`, „Trickling Rule"). Unsere Warteschlange sah so aus:

```
[Systemposition aus WM_MOUSEMOVE] [Klick] [unsere virtuelle Position]
```

Der Klick wurde also an der Systemposition ausgewertet — und die parkt `Menu::MousePointer` seit
E1 jeden Frame in der Fenstermitte. Sichtbar wurde das als vier scheinbar unabhängige Fehler:
Fenster sprangen bei Klicks, aufgeklappte Abschnitte klappten zu, ein Häkchen brauchte mehrere
Versuche, und die Tastenaufnahme reagierte nie.

Entscheidend für das Verständnis war, **wann** es funktionierte: `SetCursorPos` erzeugt kein
`WM_MOUSEMOVE`, wenn der Zeiger schon dort steht. Bei stillgehaltener Maus enthält die
Warteschlange nur `[Klick] [unsere Position]`, und der Klick trifft. Wer zielt, bewegt aber — die
Tastenaufnahme ging deshalb _nie_, das Häkchen _manchmal_.

**Die Lösung ist eine Zeile Weiterleitung:** solange das Overlay offen ist, erreichen
`WM_MOUSEMOVE` und `WM_NCMOUSEMOVE` das ImGui-Backend nicht mehr. Der Overlay besitzt seinen
Zeiger, also darf das Backend keine Position beisteuern. E1 hat den Fehler nicht gefunden, weil
sein einziges Bedienelement ein Knopf war und die Prüfung „reagiert er" lautete.

**Weitere bestätigte Befunde:**

-   **`glz::generic` benutzt `ordered_small_map`**, also Einfügereihenfolge, nicht sortiert —
    glaze bietet `generic_sorted` ausdrücklich als sortierte Alternative an. Eine bestehende Datei
    behält damit ihre Reihenfolge, ein neuer Schlüssel landet am Ende seines Blocks, und nichts
    mischt sich beim Schreiben durch. Die in der Spec offen gebliebene Frage ist damit beantwortet.
-   **ImGui 1.92 lädt Glyphen bei Bedarf und skaliert Schriften zur Laufzeit**, weil das
    dx11-Backend `ImGuiBackendFlags_RendererHasTextures` meldet (`imgui_impl_dx11.h:7`). Folge:
    keine Glyphenbereiche zu deklarieren, `PushFont(font, size)` statt einer festen Größe, und ein
    Größenregler ohne Atlas-Neubau. Die einargumentige `PushFont` wurde in 1.92 **entfernt**.
-   **`ImGuiStyle::ScaleAllSizes` multipliziert, was schon da ist.** Ein Theme, das bei jeder
    Größenänderung erneut angewandt wird, muss den Stil vorher zurücksetzen, sonst wächst die
    Oberfläche bei jedem Schritt weiter.
-   **`REX::FSettingStore` lässt seine Registrierung nicht leeren.** `m_settings` ist `protected`,
    und weder `ISettingStore` noch `FSettingStore` bieten Zugriff. Ein einmal konstruiertes
    `TJsonSetting` muss den Prozess überleben; die Bindungen entstehen deshalb einmal und werden
    bei einem zweiten `Init` nur auf die deklarierten Vorgabewerte zurückgesetzt.
-   **Unsere Einstellungsdatei geht als `fileUser` in den Store, nicht als `fileBase`.**
    `FJsonSettingStore::Load` reicht `fileBase` an `TJsonSetting::Load(…, a_isBase = true)`, und
    das schreibt den Dateiwert in **`m_valueDefault`** — der deklarierte Vorgabewert wird also vom
    ersten Laden überschrieben, und jeder spätere Rückfall liefert, was zufällig in der ersten
    Datei stand. D1 hatte das umgangen, indem es den Vorgabewert separat mitführte. Als `fileUser`
    landen die Werte in `m_value` und die Vorgaben bleiben unberührt. REX' eigenes `Save()`
    schreibt zwar nach `fileBase`, wird aber nicht benutzt: es kann keine Schlüssel anlegen und
    könnte unsere Datei ohnehin nie schreiben.
-   **`MapVirtualKey` ohne die EX-Form benennt die falsche von zwei Tasten**, die sich einen
    Scancode teilen. `Ende` und die `1` des Nummernblocks sind beide `0x4F`; unterschieden werden
    sie allein durch Bit 24 im Parameter von `GetKeyNameText`. `MAPVK_VK_TO_VSC_EX` liefert dafür
    `0xE0` im hohen Byte. Ohne das hieß `Ende` im Overlay „1 (Zehnertastatur)".

**Bewusste Abweichungen von Spec und Plan:**

-   **Ergänzen und Speichern sind zwei Vorgänge.** Der erste Entwurf des Schreibers überschrieb
    beim Ergänzen jeden Schlüssel mit dem gebundenen Wert — und der stand zu diesem Zeitpunkt auf
    dem Vorgabewert, wodurch eine vorhandene Tastenbelegung zerstört wurde. Vier Tests haben es
    gefangen. `kFillMissing` fügt nur hinzu, `kOverwrite` schreibt alles.
-   **Eine unlesbare Einstellungsdatei fällt auf die deklarierten Vorgabewerte zurück**, statt zu
    behalten, was zufällig vorher geladen war. `Init(X)` heißt „spiegle X, und die Vorgabe überall
    dort, wo X nichts sagt" — und eine kaputte Datei sagt über alles nichts. Der Plan-Test hatte
    das andere Verhalten formuliert; die Spec hatte recht.
-   **Eine Übersetzungsdatei, die sich nicht parsen lässt, wird nicht angeboten.** Die
    Skyrim-Vorlage führt sie unter ihrem bloßen Code auf, was dem Spieler eine Sprache anbietet,
    auf die nicht umgeschaltet werden kann.
-   **`Util::GamePaths` ist neu und stand in keinem Plan.** Der `Data`-Pfad wäre sonst an drei
    Stellen aus dem ausführenden Modul hergeleitet worden.
-   **Kein Formatierer für Übersetzungen, kein `DetectSystemLocale`.** Ersterer, weil nichts
    formatiert und ein Formatstring aus einer Übersetzungsdatei eine Angriffsfläche ist; letzteres
    sind siebzig Zeilen Windows-`LANGID`-Abbildung für einen Wert, der bei uns eine Einstellung ist.

## Aus dem Messspike vor F bestätigt

Drei Spielläufe am 2026-09-05, Sanctuary und Root Cellar, AE `1.11.240`. Vollständig in
`shader-techniques.md`; hier das, was den Zuschnitt bestimmt hat.

-   **Fallout 4 zeichnet seine Welt über `kDFPrepass`, nicht über `kLighting`.** `BSLightingShader`
    hat in keinem Lauf gezeichnet, innen wie außen, und trägt 18 Techniken mit Skyrim-Vokabular in
    den Namen — bis hin zu `BSLightingVcMenu`. Der Prepass trägt 315 Vertex- und 470 Pixel-Techniken
    und bindet den G-Buffer. **Folge:** die gesamte Gruppe der Objekt-Shader-Features der Vorlage
    hängt an `BSLightingShader` und ist nicht übersetzbar.
-   **Der Zeiger-Tausch aus C trägt nicht über Imagespace hinaus.** Ein Imagespace-Pass hat eine
    Technik mit ID 0; die zwölf Weltshader haben zusammen 3.445, und die IDs sind
    Permutations-Bitmasken.
-   **Die Technikmengen stehen beim Laden fest.** Über drei Läufe und über Innen- wie Außenzelle
    bitgleich, keine Karte ist zur Laufzeit gewachsen. Ein Cache kann vollständig aufgebaut werden,
    statt nachziehen zu müssen.
-   **Die Engine hält zwölf Shader-Singletons nebeneinander im Datenbereich von `Fallout4.exe`**, in
    fester Reihenfolge. Ein bekannter Zeiger erschließt alle übrigen, ohne vtable-Patch und ohne auf
    einen Aufruf zu warten. Vier Klassen waren anders gar nicht messbar.
-   **Der G-Buffer ist bestätigt:** `RT_022 RT_020 RT_057 RT_024 RT_023 RT_029` auf `DS_002`, davon
    `RT_020` die Normalen und `RT_029` die Bewegung — zwei offene Vermutungen aus B2 sind damit
    erledigt.
-   **`BSDFLightShader` meldet `shaderType` 4, nicht 5**, und teilt den Wert mit
    `BSDFPrePassShader`. Wer über `shaderType` unterscheidet, unterscheidet diese beiden nicht.
-   **Die Hull- und Domain-Karten von `kDFPrepass` zeigen auf Speicher, der ihnen nicht mehr
    gehört.** Die Ablehnung durch unseren Kartenleser ist richtig; unsere Offsets sind dadurch
    bestätigt, nicht in Frage gestellt. Die Beweisführung steht in `shader-techniques.md`,
    Abschnitt 7.

## Aus Teilprojekt F1 bestätigt

Vier Spielläufe am 2026-09-05. Die Grundlinie des Ports, gemessen in Sanctuary mit
abgeschaltetem `ShaderCensus` — die ersten echten Zahlen, die dieses Projekt hat:

```
=== performance snapshot over 300 frames ===
  pass                     gpu avg   cpu avg   gpu p95   gpu p99
  Frame                      6.598     6.503     7.163     7.678
    Features                 0.000     0.126     0.000     0.000
      FrameCounter           0.000     0.003     0.000     0.000
    Overlay                  0.001     0.051     0.001     0.001
  150.4 fps, cpu 6.50 ms, gpu 6.65 ms, 0 frame(s) discarded
```

Ab F2 wird jedes Teilprojekt gegen diese Zeilen abgenommen.

### Die fünf Annahmen der Spec

-   **Timestamp-Queries stehen auf Fallout 4s Gerät zur Verfügung** — bestätigt.
-   **Queries dürfen aus unserem `Present`-Hook heraus ausgestellt werden** — bestätigt.
-   **Ein `NoInputs`-Fenster stört den Systemzeiger nicht** — bestätigt. Maus und Kamera
    verhalten sich bei geschlossenem Overlay unverändert. **Die Nachbarannahme brach dafür:**
    ImGui zeichnet seit E1 seinen eigenen Zeiger (`io.MouseDrawCursor`), und der wurde sichtbar,
    sobald überhaupt etwas außerhalb des Overlays ausgegeben wurde. Er wird jetzt je Frame
    geschaltet statt dauerhaft gesetzt.
-   **Die Ausgabe der Zeichendaten bei geschlossenem Overlay kostet nichts Nennenswertes** —
    bestätigt **mit Zahl**: 0,051 ms CPU und 0,001 ms GPU.
-   **`Guarded` läuft auf dem Render-Thread** — bestätigt: die Pässe je Feature schachteln sich
    korrekt in `Features` und `Frame`, was von einem anderen Thread aus nicht möglich wäre.

### Was die Läufe zusätzlich ergeben haben

-   **Drei Frames Latenz reichen nicht.** Ein Platz wird wiederverwendet, sobald der Schreibzeiger
    erneut auf ihm landet — bei drei Plätzen sind das zwei Frames Gnadenfrist. Gemessen:
    **4.664 von 9.600 Frames verworfen**. Mit sechs Plätzen: **null**. Eine GPU läuft weiter
    hinterher, als die naheliegende Zahl vermuten lässt.
-   **Eine Anzeige, die je Frame aktualisiert, ist bei 180 fps unlesbar.** Sie erneuert sich
    deshalb viermal je Sekunde und zeigt Mittelwerte statt der zufällig erwischten Momentaufnahme.
-   **GPU-Zeit allein genügt nicht.** Der erste Abnahmelauf zeigte vier von fünf Zeilen mit 0,00 —
    zu Recht, denn Features, die nichts zeichnen, verbrauchen keine GPU-Zeit. Die CPU-Zeit wurde
    bereits gemessen und erreichte nur keine Spalte. Beide Anzeigen führen sie jetzt.
-   **Ein Feature koppelt sich nicht an den Renderer.** Der Versuch, `PassScope` direkt in
    `FeatureRegistry` zu benutzen, hat `FeatureRegistryTests` zerrissen — D1 hat diese
    Zustandsmaschine bewusst ohne Engine gebaut. Die Registry bekommt ihre Zeitmessung jetzt über
    zwei Funktionszeiger gereicht, so wie sie schon ihre Einstellungsabfrage gereicht bekommt.
-   **Der Frame-Wert ist Wandzeit zwischen zwei `Present`**, einschließlich einer etwaigen
    Vsync-Wartezeit. Er steht in der Tafel mit genau diesem Hinweis. Pässe ab F2 sind exakt.

## Aus Teilprojekt F2 bestätigt

Dreizehn Spielläufe, zehn am 2026-09-06 und drei am 2026-09-12. Der erste eigene Renderpass des
Ports zeichnet, und die Naht für F3 bis F11 steht: `Render::FramePhase`, `Render::Targets`,
`Render::Resources`, `Render::StateGuard`, `Render::FullscreenPass`, dazu `Shader::ShaderCompiler`
mit drei Profilen und Defines.

### Die Zahlen

Gemessen in Sanctuary, 2560×1440, 80 Samples, Vorgaben 0,005 / 0,02 / 1:

```
=== performance snapshot over 300 frames ===
  pass                     gpu avg   cpu avg   gpu p95   gpu p99
  Frame                      6.041     5.748     6.547     7.494
    Features                 0.000     0.135     0.000     0.000
      FrameCounter           0.000     0.003     0.000     0.000
    ScreenSpaceShadows       0.100     0.017     0.202     0.202
    Overlay                  0.000     0.048     0.001     0.001
      ScreenSpaceShadows/RayMarch     0.154     0.005     0.155     0.157
      ScreenSpaceShadows/Modulate     0.043     0.002     0.047     0.047
  156.2 fps, cpu 6.08 ms, gpu 6.40 ms, 0 frame(s) discarded
```

**Das Feature kostet 0,197 ms GPU und 0,007 ms CPU je Frame**, die Summe der beiden Passzeilen.
Die Zeile `ScreenSpaceShadows` darüber ist ein Artefakt, das dieser Schnappschuss aufgedeckt hat:
der Profiler schlüsselt seine Zeilen nach dem Namen allein, und die Registry misst `Frame()`
jedes Features unter dessen Namen aus `Present` heraus, während die Frame-Phase den Rückruf unter
demselben Namen gemessen hat — zwei Messungen je Frame in einer Historie, das Mittel halbiert,
das p95 exakt das Doppelte. Der Rückruf heißt seit `ScreenSpaceShadows/Draw`;
`SubscribeFramePhase` dokumentiert die Regel. Der Frame ist gegen die F1-Grundlinie (6,598 ms
GPU) nicht direkt vergleichbar, weil Blickrichtung und Tageszeit andere waren; die Passzeilen
sind es.

### Die Gabelungen der Spec

-   **`RT_058` und `RT_059` tragen das direkte Licht getrennt vom Rest** — bestätigt. Himmel und
    Leuchtreklamen bleiben unverändert, wenn die Maske aufmultipliziert wird.
-   **Tiefenkonvention nah 0, fern 1** — bestätigt, aus `worldToCam` abgeleitet: `z/w = 1 − 15/d`,
    ohne Fernebene. Bends Header nennt das Gegenteil „typisch".
-   **Die Lichtrichtung wird wie in der Vorlage negiert** — bestätigt. Die Kontaktschatten fallen
    mit Sonne vorn wie hinten auf dieselbe Seite wie die Schatten des Spiels.
-   **Der Einhängepunkt** — bestätigt: Slot 02 der Haupt-vtable von `BSDFCompositeShader`, einmal
    installiert, feuert genau einmal je Frame und in Innenräumen gar nicht.

### Die Abnahme

Schritte 1 bis 8 der Spec stehen: Block im Overlay, Schalter aus und an, Himmel unverändert,
Regler wirken (der `sampleCount`-Regler übersetzt nachweislich von 80 bis 168 Samples neu),
Passzeilen in der Tafel, Root Cellar meldet „no sun, standing down", Pip-Boy und Alt-Tab ohne
Befund, sechsmal im laufenden Spiel aus- und angeschaltet ohne Absturz. **Schritt 9, das
Nachladen bei Dateiänderung, ist in dieser Fassung nicht ausgeführt worden**; der Nutzer hat ihn
als abgenommen gesetzt. Der Mechanismus ist derselbe wie bei `ImagespaceTint`, mit dem in D2
gefundenen Fehler behoben.

### Was die Läufe ergeben haben

Die zehn Läufe des 6.9. galten einer einzigen Größe, der Lichtkoordinate. Was dabei herauskam,
steht ausführlich in der Spec-Notiz und in `Render/Camera.h`; das Wesentliche:

-   **`NiCamera::worldToCam` ist die vollständige Welt-nach-Clip-Matrix**, Spaltenvektor-
    Konvention, Rotation eingeschlossen. Auf dem Ladebildschirm ist die Kamerarotation eine reine
    Achsenvertauschung, und die Matrix las sich dort als rotationsfrei — zwei Commits haben
    daraufhin die Kamerarotation davorgeschaltet und doppelt rotiert. Der Weg, der trägt, ist
    `worldToCam · (Richtung, 0)`, genau das, was Bends Kopfkommentar verlangt und was der
    unabhängige FO4-Port ebenso tut. Der Host-Test hält die Rechnung gegen drei Frames aus dem
    Log; im Spiel stimmen über 200 Vergleichszeilen mit `WorldPtToScreenPt3` auf drei Stellen.
-   **`NiCamera::WorldPtToScreenPt3` teilt durch |w|.** Hinter der Kamera liefert sie die
    gespiegelte Position und eine negative Tiefe. Als Schiedsrichter fürs Log taugt sie, als
    Quelle für Bend nicht: der Rückbau trug drei Fehler auf einmal — falsche Vorwärtsachse für das
    Vorzeichen von w, Spiegelung nicht zurückgenommen, `z/w = −1` statt `+1` hinten. Der letzte
    war der dunkle Blitz am Sonnenübergang: mit falschem `LightCoordinate.z` liest Bends
    perspektivische Korrektur flachen Boden als Schatten, sobald die Lichtkoordinate weit
    außerhalb liegt.
-   **Die Kamerarotation hält vorwärts in Zeile 0, oben in Zeile 1, rechts in Zeile 2.**
    Gamebryo: eine Kamera blickt und ein Licht leuchtet entlang der lokalen x-Achse. Die
    Sonnenrichtung steht deshalb in Zeile 0 der Rotation des `NiDirectionalLight`, dessen
    Zeilen 1 und 2 die Engine gar nicht erst füllt.
-   **`Main::WorldRootCamera()` ist die Kamera, mit der gezeichnet wird.**
    `BSGraphics::State::cameraState` ist an `kDFComposite` die Schattenkamera der Sonne, und
    `BSGraphics::RendererData::shadowState` ist kein Zeiger, sondern der Offset `0x1B70`; ihm zu
    folgen stürzt ab.
-   **Die These, Fallout 4 rebasiere die Welt um die Kamera, war falsch.** Sie stammte aus dem
    Ladebildschirm. In der Welt trägt die Kamera eine echte Weltposition; der Sonnenknoten liegt
    ein paar hundert Einheiten daneben und neun Grad höher als die Lichtrichtung.
-   **Ein Vollbildpass darf Rasterizer- und Depth-Stencil-State nicht erben.** Die erste Fassung
    tat es und funktionierte zufällig. `DrawFullscreen` bindet jetzt eigenen State, `StateGuard`
    sichert ihn.
-   **Bends `SurfaceThickness` steht auf dessen eigener Vorgabe 0,005**, Bereich 0,0005 bis 0,05.
    Die Begründung, Fallout 4s Tiefenverteilung sei gröber als Skyrims, trägt nicht — beide
    projizieren mit Nahebene 15 und ohne nennenswerte Fernebene —, die Vorgabe ist trotzdem die
    richtige: mit ihr war das Bild plausibel, mit Skyrims 0,02 flächig dunkel.
-   **Werte, die sich ändern, gehören periodisch ins Log.** Drei einmalige Logzeilen haben in
    einer Sitzung dreimal den Ladebildschirm erwischt. Ebenso: eine Ablehnung, die nur beim
    ersten Mal spricht, macht danach jeden Lauf zum Scheinerfolg.

## Aus Teilprojekt F3 bestätigt

Sämtliche Läufe am 2026-09-12. Der erste Pass des Ports, der hinter der opaken Szene auf das
fertige Bild zeichnet, und das Werkzeug, mit dem sein Platz gemessen wurde: `FrameTrace`,
`Render::Phase::kAfterOpaque`, `Render::Targets::kSceneHDR`, `Render::FogCameraFromMatrix`,
`Menu::Hotkeys`. Die Frame-Reihenfolge der zwölf Shader-Klassen steht vollständig in
`frame-order.md`.

### Die Zahlen

Gemessen in Sanctuary, 2560×1440, Vorgaben der Regler, Vanilla-Regler auf 0:

```
=== performance snapshot over 300 frames ===
  pass                     gpu avg   cpu avg   gpu p95   gpu p99
  Frame                      5.557     4.802     5.680     5.712
    Features                 0.000     0.137     0.000     0.000
      ScreenSpaceShadows     0.000     0.016     0.000     0.000
      ExponentialHeightFog     0.000     0.005     0.000     0.000
    Overlay                  0.005     0.176     0.005     0.005
    ScreenSpaceShadows/Draw     0.161     0.015     0.160     0.166
    ExponentialHeightFog/VanillaFog     0.000     0.002     0.000     0.000
    ExponentialHeightFog/Draw     0.027     0.007     0.027     0.027
      ExponentialHeightFog/Fog     0.026     0.002     0.026     0.026
  179.9 fps, cpu 5.08 ms, gpu 5.56 ms, 0 frame(s) discarded
```

**Das Feature kostet 0,027 ms GPU und rund 0,014 ms CPU je Frame** (Draw, VanillaFog und die
Frame-Zeile der Registry zusammen). Ein Vollbild-Pixelshader ohne Schleife, ein Zehntel der
Kontaktschatten aus F2. Der Frame ist gegen die F1-Grundlinie (6,598 ms GPU) nicht direkt
vergleichbar, weil Blickrichtung und Tageszeit andere waren.

### Der Trace

Zwei Frames, einmal Blick über den Ort, einmal mit Wasser im Bild, je rund 113 Aufrufe:
Prepass (≈40, G-Buffer) → Wasser-Setup → Schattenkarte `DS_008` (≈40, `BSUtilityShader`) →
`BSDFLight 0x4000000` auf `RT_058/059` → `BSDFCompositeBase 0x0088` noch auf `RT_058/059` (F2s
Anker liegt also vor dem Basisanteil) → Wasser-Stencil → `BSDFCompositeEnvmapSslrApplyAO` nach
`RT_009` → **`BSDFComposite 0x0040` schreibt die fertige HDR-Szene nach `FO4_RT_004`** →
`BSWaterDpth` → Himmel (`BSSky`, `BSSkyTexture`, `BSSkyClouds`) → Wasserreflexion (`RT_002`) →
Wasserflächen → `BSUtilityTStencil` → Effekte → `BSSkySunOcclude` → `BSEffectTexUimr` (UI).
`kSceneHDR` ist damit Slot 4, der Tiefenpuffer `DS_002`.

### Die Gabelungen der Spec

-   **Der zweite Anker — das tragende Risiko, und es ist eingetreten.** Der Trace zeigte die erste
    `BSEffectShader`-Technik in beiden Frames nach dem Himmel, und so wurde der Anker gebaut. Ein
    Zähler in der Sekundenzeile über **jeden** Frame zeigte dann: die auslösende Technik wechselt
    bis zu 30-mal je Sekunde, und bei `0x10000407` und `0x10000021` war der am Composite
    geschriebene Clamp beim Zeichnen in 180 von 180 Frames noch nicht drin — diese Effect-Aufrufe
    laufen **vor dem Composite**. Der Nebel wurde in solchen Frames gezeichnet und vom Composite
    übermalt: ein Frame ohne Nebel, das vom Nutzer gemeldete Blitzen im Himmel, mit und ohne
    Vanilla-Regler. Seit `596a82b8` ist `BSSkyShader` als dritter Anker ohne Abonnenten gepatcht,
    und `kAfterOpaque` feuert beim ersten Effect-Aufruf, nachdem der Himmel den Frame gesehen hat.
    Im Abnahmelauf blieb die Technik in 86 von 87 Sekunden-Intervallen konstant `0x805015`, das
    Blitzen ist weg. **Lehre: zwei Frames belegen keine Frame-Reihenfolge; der Anker jedes
    weiteren Features bekommt denselben Zähler.**
-   **Der Vanilla-Regler — Ausgang 1 mit Vorbehalt.** Die Probe schrieb `clamp · 0,5` aus
    `Present` und las am Composite: in 67 von 69 Stichproben überlebte der Wert, in **2 nicht** —
    dort stand `Sky::fogClamp`, die frische Kopie der Engine. Der Regler schreibt seit `8a278e14`
    aus `kBeforeComposite`, in der `SetupTechnique` des Composite selbst, unmittelbar vor dem
    Lesen. Der Zähler „clamp foreign" stand im Abnahmelauf in jedem Intervall auf 0 von 180: die
    Engine frischt `fogState` zwischen Composite und Effekten nie auf. Sichtprüfung: bei 0 ist die
    Distanztrübung des Spiels sichtbar weg.
-   **Die Nebelfarbe.** Aus `fogState` nachgerechnet, wobei `rangeData` rohe Distanzen hält
    (`[near far heightMid heightRange]`, Sanctuary `[1600 250000 64 15000]`, später
    `[3000 250000 …]`) und nicht die Rampenkoeffizienten der Spec-Formel; der Shader baut die
    Rampe selbst. Das Höhenband ist als „Mitte ± Spanne/2" angesetzt. **Der Abgleich am Horizont
    über die Debug-Ansicht `color` ist vom Nutzer nicht eigens berichtet worden** und gilt als
    nicht bestätigt; ein Fehler dort wäre eine Zeile in `HeightRamp`.

### Die Abnahme

Block mit Schalter, sieben Reglern und der Debug-Ansicht im Overlay; Schalter aus und an mit
sichtbarem Unterschied; die Schicht wandert mit `Height`; Sonneneinstreuung sichtbar; Vanilla-
Regler auf 0 nimmt die Distanztrübung weg; beide Passzeilen in der Tafel und der Schnappschuß
per F11; Pip-Boy und Alt-Tab ohne Befund; Feature im laufenden Spiel mehrfach aus- und
angeschaltet ohne Absturz, der Spielnebel beim Abschalten sofort zurück. Keine `[E]`-Zeile im
Abnahmelauf.

**Root Cellar:** der Pass läuft in Innenräumen gar nicht mehr, weil der Himmelsmarker dort nie
feuert — die Zeile „no sun, standing down" erscheint deshalb nicht mehr für dieses Feature; der
Nutzer meldete keinen Befund. **Das Nachladen bei Dateiänderung ist nicht ausgeführt worden**
(0 Neuübersetzungen im Log), wie schon in F2; der Mechanismus ist derselbe. **Der Farbabgleich**
ist nicht eigens berichtet, siehe oben.

### Was die Läufe ergeben haben

-   **Der Nebel wird über die Tiefe rekonstruiert, ohne Matrixinversion:** Sichtstrahl aus den
    Spalten von `worldToCam`, `d = near / (1 − z)`. **Die Nahebene ist nicht konstant 15**: im
    Spiel schwankte sie zwischen 10,5 und 19,4 je nach Blick, Fallout 4 stellt sie zur Laufzeit
    nach. Sie wird je Frame aus der Matrix gelesen.
-   **Am hinteren Ende des Tiefenpuffers liegen LOD-Terrain und Himmel dicht unter 1**;
    `near / (1 − z)` macht daraus Millionen Einheiten und volle Deckung, nach dem Bloom weiß.
    Der Abstand ist bei `min(far, 200000)` gedeckelt, für Boden und Himmel gleich. Die
    Skyrim-Vorlage nebelt den Himmel im Hauptbild gar nicht (`Sky.hlsl`, nur `inReflection`);
    wir tun es mit dem Deckel, und es sieht stimmig aus.
-   **Die Vorgaben sind für Boston gedacht, nicht für Sanctuary.** Die Kamera steht dort bei
    z ≈ 7900; mit `height` 0 und `heightFalloff` 0,2 bleibt der Nebel dort fast unsichtbar, eine
    Schicht wird erst mit `height` ≈ 7600 und Falloff ≈ 1,0 sichtbar. Die `density`-Vorgabe ist
    0,03 statt der 0,005 der Vorlage, sonst war zwischen an und aus kein Unterschied. Die Hilfe
    nennt die Höhe von Sanctuary.
-   **Ein Feature kann eine Taste haben:** `Menu::Hotkeys` bietet übrige Tasten jedem
    registrierten `KeyLatch` an. Dabei fiel ein Fehler aus E2 auf und wurde behoben: die
    Tastenaufnahme im Overlay schrieb jede gefangene Taste in den Overlay-Schalter statt in das
    Feld, dessen Knopf gedrückt war. **Tastenfallen:** F10 ist `WM_SYSKEYDOWN` und erreicht den
    `WM_KEYDOWN`-Haken nie, F9 Quick-Load, F5 Quick-Save, F12 Steam; `FrameTrace` liegt auf F8.
-   **Ein Fehlalarm kostete eine Halbierungsrunde:** „Eingabe komplett tot" war ein zwischendurch
    angesteckter Controller, den Fallout 4 der Tastatur vorzieht. Bei einem Symptom, das der Diff
    nicht erklärt, zuerst nach der Umgebung fragen.
-   **Debug-Ansichten sind Einstellungen, keine Shader-Defines.** Die erste Fassung verlangte,
    ein `#define` in die Datei im Spielordner zu schreiben; der Nutzer hat klargestellt, daß er
    das nicht tut. `debugView` ist eine Choice im Block und geht als Zahl im freien `w` von
    `CameraUp` an den Shader.

### Ein bekanntes Zugeständnis

Der Pass liegt vor den Transparenzen. Wasser, Glas und Effekte sehen unseren Nebel nur, soweit er
auf dem liegt, was hinter ihnen ist; der Spielnebel auf ihnen bleibt. Das ist der Preis eines
Passes gegenüber F12. Ob F3 einen volumetrischen Anteil bekommt (die Vorlage hat einen, mit
Froxel-Volumen und Lichtstreuung), wird bei **F6** mitentschieden, wenn feststeht, welche
Volumeninfrastruktur dort entsteht.

## Aus Teilprojekt F4 bestätigt

Läufe am 2026-09-12 (Sonde, drei Anläufe) und 2026-09-13 (Abnahme). Das erste Feature, das einen
Zeichenaufruf der Engine umgibt statt einen eigenen Pass anzuhängen, und das Werkzeug dafür:
`Render::TechniqueTracker` (Slot 02 aller dreizehn Shader-Klassen → aktuelle Technik),
`Render::DrawObserver` mit `TechniqueFilter` und `DrawCall::Repeat`, `Render::DrawHook` (Draw,
DrawIndexed, DrawIndexedInstanced, DrawInstanced, ExecuteCommandList), `Render::ContextTable`
(eigene vtable für den Gerätekontext), `Render::CubeTarget`, `Clouds::CloudProjection` (rein,
`double`, mit Host-Test) und ein `StateGuard`, der jetzt auch PS-Sampler 0 und zwei PS-SRVs
sichert. Die Erfassung ist **Weg B** der Spec.

### Die Zahlen

Gemessen in Sanctuary am 2026-09-13, 2560×1440, an derselben Stelle ohne Bewegung, erst mit dem
Feature, neun Sekunden später ohne:

```
=== performance snapshot over 300 frames ===          Cloud Shadows an
  pass                     gpu avg   cpu avg   gpu p95   gpu p99
  Frame                      6.021     4.945     6.882     8.297
      CloudShadows           0.000     0.012     0.000     0.000
    CloudShadows/Draw        0.050     0.013     0.050     0.050
      CloudShadows/Shadow    0.050     0.003     0.050     0.050
    ScreenSpaceShadows/Draw  0.377     0.010     0.379     0.379
    ExponentialHeightFog/Draw 0.029    0.008     0.029     0.030

=== performance snapshot over 300 frames ===          Cloud Shadows aus
  Frame                      5.836     5.072     6.591     7.587
    ScreenSpaceShadows/Draw  0.380     0.016     0.381     0.382
    ExponentialHeightFog/Draw 0.028    0.009     0.029     0.029
```

**Das Feature kostet rund 0,19 ms GPU je Frame** (Differenz der Frame-Zeilen; p95 0,29 ms, p99
0,71 ms). Davon sind 0,05 ms der Schattenpass in der eigenen Zeile; der Rest sind die **45
Wiederholungen der Wolkenzüge je Frame**, die innerhalb der Sky-Züge der Engine laufen und
deshalb in keiner Passzeile auftauchen, sondern nur in der Differenz. Die CPU-Seite dieser
Wiederholungen ist aus demselben Grund nicht gemessen; die eigenen Zeilen zeigen 0,025 ms. Die
fps-Zeile unter dem Schnappschuß ist das eine Frame des Tastendrucks und taugt nicht zum
Vergleich.

**Der Draw-Hook** sieht je Frame rund 4.000–4.400 `DrawIndexed`, 730–860 `Draw`, 120–130
`DrawIndexedInstanced`, kein `DrawInstanced` und kein `ExecuteCommandList` — Fallout 4 zeichnet
alles auf dem unmittelbaren Kontext, vom Render-Thread; beim Laden kurz auch von einem zweiten.
Jeder Aufruf kostet einen Vergleich der aktuellen Technik mit dem Filter; in der Frame-Zeile ist
davon nichts abzulesen.

### Die Sonde

Lauf 1 brauchte drei Anläufe, weil die ersten beiden **null Aufrufe** zählten, auch für einen
eigenen `Draw(3, 0)`: **die D3D11-Runtime hält die vtable des Gerätekontexts im Objekt selbst
(Offset 8) und schreibt die Einträge `Draw`, `DrawIndexed` und `DrawIndexedInstanced` bei jedem
`Flush` auf andere Varianten um** — Present flusht jedes Frame, ein Slot-Patch hält also keinen
Frame. Offline mit Python und ctypes gegen ein nacktes Gerät in einer Viertelstunde geklärt, statt
einen dritten Spiellauf zu raten. `Render::ContextTable` biegt stattdessen den vtable-**Zeiger**
auf eine eigene Tabelle aus Neun-Byte-Weiterleitern (`mov rax,[rcx+8+i*8]; jmp rax`), die zur
Aufrufzeit in die eingebettete Tabelle springen; Overrides ketten genauso. Host-Test mit 18
Prüfungen. Der Present-Patch aus B1 ist nicht betroffen, die Swap-Chain-vtable liegt im Abbild.

Was der dritte Anlauf ergab, bei 180 fps je Sekunde:

| Technik                      | Ziel                      | Aufrufe je Sekunde | Bedeutung                                |
| ---------------------------- | ------------------------- | ------------------ | ---------------------------------------- |
| `0x0001` `BSSky`             | `FO4_RT_004`              | 180                | einmal je Frame                          |
| `0x0004` `BSSkyTexture`      | `FO4_RT_004`              | 180                | einmal je Frame                          |
| `0x0005` `BSSkyClouds`       | `FO4_RT_004` (+ `RT_029`) | 1620               | **neun Lagen je Frame**, im Kamerabild   |
| `0x0005` `BSSkyClouds`       | `FO4_RT_002`              | 180                | einmal je Frame, planare Wasserreflexion |
| `0x0008`                     | `FO4_RT_004`              | 213 seit Start     | selten                                   |
| `0x0000` `BSSkySunOcclude` … | `RT_037`, `RT_064–070`, … | —                  | danach ungetrackte Imagespace-Pässe      |
| beliebig                     | **`FO4_CUBE_000`**        | **0**              | die Engine zeichnet ihre Cubemap **nie** |

Blend-State der Wolken: Farbe und Alpha `SRC_ALPHA / INV_SRC_ALPHA`, Schreibmaske RGB — **das
Shader-Alpha ist die Deckung**, der Blend-Trick der Spec trägt. `A8_UNORM` ist auf der RTX 5080
Renderziel und mischbar. Die Wolken laufen über `DrawIndexed`, nicht instanziert.

**Die Matrix.** Beim Wolkenzug sind sieben VS-Konstantenpuffer gebunden. Slot 12 (752 B, je Sicht)
hält vier Matrizen in Zeilen: Kamerarotation, Projektion `[1,192 0 0 0][0 2,119 0 0][0 0 1 −15]
[0 0 1 0]`, ihr Produkt **ohne Translation**, die transponierte Rotation — die Suche nach
`worldToCam` schlug deshalb fehl: der Himmel ist kamerazentriert. Slot 2 (224 B, je Lage) hält bei
Byte 0 die **WorldViewProj**, mit der der Sky-Vertex-Shader tatsächlich positioniert, bei Byte 64
die Weltmatrix der Lage als drei Zeilen (Drehung um die Senkrechte, Translation `0 0 −15`), bei
112 eine Farbe mit Alpha, ab 176 PreviousWorld. Slots 9 und 10 tragen weitere Weltmatrizen, die
der Sky-VS nicht liest.

### Der gewählte Weg

Weg A (Umlenken an der Engine-Cubemap) und A′ (`bReflectSky`) entfielen mit der Zeile
`FO4_CUBE_000: nie`. **Weg B:** jeder `BSSkyClouds`-Zug, dessen RTV0 `kSceneHDR` ist (der eine
je Frame in die Wasserreflexion wird ausgelassen), wird nach dem Zug der Engine **fünfmal
wiederholt**, einmal je Fläche 0–4 einer eigenen `A8_UNORM`-Cubemap mit 512 Pixeln Kantenlänge
(`FO4CS_CUBE_CloudCoverage`); die sechste Fläche, der Blick nach unten, wird nicht gebraucht. Dabei werden
alle acht Render-Targets, Tiefensicht, Blend-, Rasterizer-State und Viewports gesichert, kein
Tiefenziel gebunden (die Cubemap hat keines, ohne eines entfallen Tiefen- und Stencil-Test), Cull
None gesetzt (die Flächenbasen haben die andere Händigkeit als die Engine-Sicht, was die Wicklung
umdreht) und Alpha über `ONE / INV_SRC_ALPHA` mit Schreibmaske `ALPHA` akkumuliert. Slot 12 wird
durch eine Kopie mit den vier Flächenmatrizen ersetzt, Slot 2 durch eine Kopie mit
`Flächenprojektion × Weltmatrix der Lage` an Byte 0. Die Kopien der Engine-Puffer laufen über
Staging-Ringe (siehe unten). Ein Feature, das einen Zeichenaufruf wiederholt, sieht seine eigenen
Wiederholungen im selben Hook; `Wants` blendet sie über ein Flag aus.

Der Schattenpass hängt wie F2 an `kBeforeComposite` und multipliziert in `RT_058/059`: je Pixel
der Weltpunkt aus Tiefe und Kamerabasis (`FogCameraFromMatrix` aus F3), von dort der Strahl zur
Sonne bis zur Wolkenschale in `cloudHeight` über einer Kugel mit Erdradius, die Richtung des
Schnittpunkts in die Cubemap, `Faktor = 1 − Deckung · Opacity`. Debug-Ansichten `coverage`,
`direction` und `cube` (die sechs Flächen als 3×2-Gitter, grüne Rahmen) über `DeclareChoice`.

### Die Gabelungen der Spec

-   **Das tragende Risiko ist eingetreten, aber nur für A und A′.** Weg B war gangbar, weil die
    Matrix gefunden wurde — nicht in Slot 12, wie die Sonde zuerst nahelegte, sondern in Slot 2.
    Slot 12 allein zu ersetzen zeigte in der `cube`-Ansicht das Kamerabild in jeder Fläche; erst
    die WVP in Slot 2 brachte die Flächen zum Stimmen (`5dc20069`).
-   **Alpha ist die Deckung** — bestätigt durch den Blend-State. **Die Engine-Cubemap ist nicht
    weltachsig** — gegenstandslos, wir zeichnen in eine eigene. **`A8_UNORM`** — Renderziel, ja.
    **Instanziert** — nein.
-   **Spec 9.1 nannte eine Erweiterung von `SettingsSchemaTests`** — ein Irrtum: der Test prüft
    das Schema an synthetischen Blöcken, nicht an Features. Die dort auch genannte Erweiterung von
    `CameraTests` um die Flächenmatrizen ist nicht gebaut; die Flächen wurden im Spiel über die
    `cube`-Ansicht geprüft (Fläche `+Z` zeigt Wolken, wenn oben Wolken sind, und ist leer bei
    blauem Zenit). Neu sind `CloudProjectionTests` (14 Prüfungen), `ContextTableTests` (18) und
    neun weitere in `DrawObserverTests`.

### Die Abnahme

Block mit Schalter, zwei Reglern und der Debug-Ansicht im Overlay; Schalter aus und an mit
sichtbarem Unterschied; `coverage` zeigt die Wolkenkarte, sie zieht mit dem Himmel; `direction`
ohne Sprünge; Schatten wandern in Zugrichtung der Wolken; beide Passzeilen in der Tafel und zwei
Schnappschüsse per F11; Feature im laufenden Spiel aus und an, Shader neu übersetzt, Erfassung
sofort wieder auf 1620 Zügen je 180 Frames; **Pip-Boy und Alt-Tab** vom Nutzer ohne Befund
gemeldet. Keine `[W]`- und keine `[E]`-Zeile im Abnahmelauf.

**Nicht ausgeführt, vom Nutzer als OK gesetzt:** Root Cellar (das Log zeigt den ganzen Lauf zwei
Kamerapositionen im Freien) und der Hot-Reload beider Shader (0 Neuübersetzungen; der Mechanismus
ist derselbe wie in F2 und F3, dort ebenfalls nicht ausgeführt). **Nicht berichtet:** der
Farbabgleich des Nebels über dessen `color`-Ansicht aus F3, zum dritten Mal; der Nebel wurde im
Lauf einmal aus- und wieder eingeschaltet.

### Was die Läufe ergeben haben

-   **Nur RTV0 zu sichern nahm den Wolkenlagen `RT_029`**, die Bewegungsvektoren als RTV1 — TAA
    flackerte am Himmel. Seit `9a0e922c` werden alle acht Ziele gesichert und zurückgegeben. Wer
    einen Engine-Zug umgibt, gibt **alles** zurück, nicht nur, was er selbst gebraucht hat.
-   **`DO_NOT_WAIT` auf eine Staging-Kopie braucht einen Ring von sechs.** Ein Frame nach
    `CopyResource` schlug die Map in etwa der Hälfte der Frames fehl (Flackern: eine Lage fiel
    für ein Frame aus). Zwei Frames später aus einem Ring von drei war noch einen Frame zu kurz:
    D3D11 lässt die CPU bis zu drei Frames vor der GPU laufen, und sobald die GPU der Engpass war,
    schlugen **1800 von 1800** Maps fünf Sekunden lang fehl — die Matrix der Lage fror ein und
    sprang danach. Seit `ab218615` ist der Ring sechs tief und wird fünf Frames später gelesen,
    dieselbe Tiefe wie die Timestamp-Queries des Profilers; im Abnahmelauf 0 Fehlschläge in 108
    Intervallen. Bei Fehlschlag behält die Lage ihr altes Bild, der Render-Thread wartet nie.
-   **Debug-Ausgaben in `RT_058` sind auf Himmelspixeln unsichtbar** — der Himmel wird nach dem
    Composite gemalt. Eine `sky`-Ansicht, die die Deckung über den Himmel legt, war deshalb ein
    Denkfehler und wurde entfernt (`86959c16`); die `cube`-Ansicht liest man auf Boden und
    Objekten.
-   **Opacity 0,5 war unsichtbar.** Die Wolken-Alpha ist wispy; bei 0,5 lag der Faktor über 0,85.
    Der Nutzer fand 0,75 bis 1,0 richtig, die Vorgaben stehen seit `ab218615` auf 0,75 und einer
    Wolkenhöhe von 500 m — dem geprüften Wert; 2000 m sind auf ihre Wirkung nicht geprüft.
-   **Der Sky-VS nimmt die WVP aus Slot 2**, nicht aus den Sichtmatrizen in Slot 12 — siehe
    Gabelungen. Ein Rohabzug beider Puffer ins Log hätte das vor dem ersten Bau gezeigt.

### Zugeständnisse

Die Deckungskarte ist alt: die Cubemap wird im Himmelsabschnitt eines Frames beschrieben und vom
Pass des nächsten gelesen, die Konstanten der Lagen stammen von fünf Frames davor — bei 180 fps
zusammen rund 35 ms auf Wolken, die sich in der Zeit nicht messbar bewegen. Die Kosten der
Wiederholungen stehen in keiner Passzeile, nur in der Differenz zweier Schnappschüsse. Der Blick
nach unten (Fläche `−Z`) wird nicht erfaßt. Die Wasserreflexion behält ihre Wolken, weil ihr Zug
nicht wiederholt wird. Das Feature steht ab, sobald `Sky::mode` nicht `kFull` ist oder die Sonne
unter dem Horizont steht — in Innenräumen also von selbst.

## Aus Teilprojekt F4.5 bestätigt

Ein Tag, 2026-09-13, zwischen F4 und F5 geschoben, weil das Overlay aus E2 mit sieben Blöcken
untereinander voll war. Nach dem Vorbild der Skyrim-Vorlage (`FeatureListRenderer`), aber im
kleinen Zuschnitt: zwei Spalten, links die Liste der Seiten, rechts die gewählte. Übernommen
wurden die festen Einträge General und Performance, die Featureliste mit Schaltern und die Seite
mit Titel und Beschreibung; **nicht** übernommen Kategorien, Suche, Home, Logo, Themes, Icons und
die GPU-Fußzeile — Themes und Icons bleiben „nicht vor F", der Rest lohnt erst mit mehr Features.

### Was entstanden ist

-   **`Menu::PageList`** (`src/Menu/PageList.h`), ein reines Modell: feste Seiten vorn, eine je
    Feature in Registrierungsreihenfolge, Auswahl per Name mit Rückfall auf General, und die eine
    Regel des Panels — der Block `Performance` gehört zur Performance-Seite, der Block eines
    Features zu dessen Seite, alles andere zu General. `MenuPagesTests` mit 24 Prüfungen; zwei
    Mutationen fielen wie benannt, die dritte (Suche ab Index 0 statt 1 in `PageOf`) war
    wirkungsgleich und ist so dokumentiert.
-   **Das Panel** zeichnet Kopf und Fuß wie zuvor, dazwischen eine ImGui-Tabelle mit zwei
    Spalten (`Resizable`, 1 : 3), jede ein für sich scrollender Kindbereich. Links `SeparatorText`
    „General" mit zwei `Selectable`, „Features" mit einer Zeile je Feature: Checkbox des
    Schalters ohne Label in einer `PushID` des Namens, der Name als `Selectable`, rechts gedimmt
    der Zustand. Rechts die Seite: General (alle Blöcke, die `PageOf` dorthin legt), Performance
    (Tabelle, Verlauf, die vier Einstellungen), Feature (Name in der Überschriftenschrift, Zustand,
    der Hilfetext des Schalters als Beschreibung, dann die übrigen Einträge).
-   **`DrawPerformanceTable`** als Baustein und **`DrawPerformanceHud`** für die Ecke; das eigene
    Performance-Fenster bei offenem Overlay ist weg. `Overlay::Draw` reicht den
    `PerformanceContext` an `DrawSettingsPanel` weiter.
-   Vier neue i18n-Schlüssel, zwei entfallen (`menu.general`, `menu.features`); 86 Schlüssel.
-   **Kein Feature hat eine Zeile geändert.** Die Beschreibung oben auf der Seite ist die `Help`
    von `DeclareFeature`, die jedes Feature schon hatte.

### Die Zahlen

Zwei Schnappschüsse im Abnahmelauf, Overlay offen:

```
  pass                     gpu avg   cpu avg   gpu p95   gpu p99
  Frame                      5.685     4.592     6.662     7.463
    Overlay                  0.003     0.096     0.009     0.009
```

Das Overlay kostet 0,003 ms GPU und rund 0,1 ms CPU je Frame, solange es offen ist. Vor F4.5
standen dort 0,050 ms CPU (Schnappschuß aus F4): die Liste wird je Frame aus Registry und Schema
neu aufgebaut, `CollectEntries` läuft je Featurezeile einmal. Verdoppelt, und ein Zehntel einer
Millisekunde, während das Menü offen ist — bei geschlossenem Overlay entfällt es ganz.

### Die Abnahme

Alle neun Schritte der Prüfliste vom Nutzer als bestanden gemeldet („Hat alles geklappt, sehr
gut"): großes Fenster mittig, zwei Spalten, Trennlinie und Fenstergröße frei; jede Featureseite
mit Titel, Zustand, Beschreibung und Reglern; Cloud Shadows aus der Liste aus- und angeschaltet
(im Log `off` / `running`); Sprache gewechselt und zurück (im Log `switched to locale de`, die
Datei steht wieder auf `en`), Schriftgröße, Taste neu belegt; Performance-Seite mit Tabelle und
Einstellungen; HUD in der Ecke; Restore defaults; F11. Keine `[W]`- und keine `[E]`-Zeile.

**Ungeprüft** blieb, was kein Schritt verlangte: das Verhalten der Kindbereiche in der Tabelle
bei einer Fensterhöhe unter der Fußzeile (das Fenster läßt sich so klein ziehen), und die
Seitenliste mit mehr Featurezeilen, als in die linke Spalte passen — beides erst mit F5 aufwärts
relevant, und der Kindbereich scrollt dann.

### Was F5 aufwärts davon hat

Ein Feature bekommt seine Seite weiterhin allein durch `Declare`; die `Help` des Schalters ist
jetzt die sichtbare Beschreibung und sollte den Effekt in ein, zwei Sätzen erklären. Kategorien
wären eine Gruppierung der Featureseiten in `PageList`, eine Suche ein Filter darauf, eine
Home-Seite eine weitere feste Seite — nichts davon braucht das Panel neu zu schneiden.

## Aus Teilprojekt F5 bestätigt

Läufe am 2026-09-13 (Sonde, Abnahme, sechs Messläufe) und 2026-10-04 (Durchsicht, drei
Messläufe). Das zweite Feature, das Engine-Züge wiederholt, und das erste, das dafür eine
**fremde Projektion in einen Engine-Puffer schreibt**. Neu: `Render::Phase::kComposite` (zweiter
Anker auf `BSDFCompositeShader`, feuert bei Technik `0x0040`), `Render::DepthTarget`,
`Render::Texture3D`, `Render::TexelProbe` (16 Texel, Staging-Ring sechs, ohne Warten), die
Beobachterliste im Draw-Hook (bis zu vier), `Targets::DepthStencilView(slot, view)`,
`Features::TargetView` (Engine-Ziel über das Bild, CORE, bleibt als Sonde), `SkyOcclusion`
(rein, `double`, 27 Host-Prüfungen) und die gemeinsamen Shaderteile
`Common/SphericalHarmonics.hlsli` und `Common/Reconstruct.hlsli` im Basisarchiv.

### Der gewählte Weg

Jeder `BSUtilityShader`-Zug in eine der drei Ansichten von `DS_008` wird in eine eigene
2048²-Tiefenkarte (`D32_FLOAT`) wiederholt, mit einer Orthoprojektion aus einer Himmelsrichtung
über einen Würfel von 10000 Einheiten um das Auge. Dafür wird der Sichtpuffer der Engine in Slot 12
je Frame per `CopyResource` gespiegelt und darin **genau zwei Werte** ersetzt, per
`CopySubresourceRegion` aus einem 80-Byte-Patchpuffer: die ViewProj bei Byte 128 und der
`CameraPosAdjust` bei Byte 560, auf das Auge gesetzt. Die Werfer kommen damit augenrelativ an,
eine Matrix dient Erfassung und Gitter. Wiederholt wird mit eigenem Tiefentest (`LESS_EQUAL`
gegen 1) und dem Rasterizer-State der Engine **ohne dessen Tiefen-Bias**.

Eine Karte wird über **sieben Frames** gefüllt: Richtung, Matrix und Auge bleiben einen Zyklus
lang stehen, jeder Werfer wird nur in der Phase wiederholt, die `CaptureBucket` aus seinen
Puffern und Draw-Argumenten ableitet, und erst im letzten Frame faltet ein Compute-Shader die Karte
in ein 256×256×128-Gitter aus SH2-Sonden (laufendes Mittel, Ringpuffer um das Auge). Der Pass
multipliziert in `kComposite` den Faktor in `RT_028`, den das Composite auf seinen Ambient-Term
anwendet. Debug-Ansichten `visibility`, `occlusion`, `grid`.

### Die Zahlen

**Nicht vollständig gemessen.** Das einzige Paar Schnappschüsse stammt vom 2026-09-13, als nur die
nahe Kaskade erfaßt wurde: `Skylighting/Probes` 0,157 ms GPU, `Skylighting/Shadow` 0,021 ms. Die
Wiederholungen tauchen wie in F4 nur in der Differenz der Frame-Zeilen auf, und das Paar an/aus
mit F11 wurde nach dem Umbau auf drei Kaskaden **nicht** gemacht. Was es gibt, sind die Zählungen
und die Bildrate nach Augenmaß:

| Stand                                        | Werfer je Frame gesehen | wiederholt | Bildrate (Nutzer)           |
| -------------------------------------------- | ----------------------- | ---------- | --------------------------- |
| nur `dsView[0]`, nahe Kaskade                | 138                     | 138        | 180                         |
| alle drei Kaskaden, jedes Frame              | 1750–2970               | alle       | **30**                      |
| alle drei Kaskaden, Zyklus von sieben Frames | ≈ 1900                  | ≈ 272      | 120+ mit Spitzen (Diagnose) |

Die Spitzen im letzten Lauf kamen von der Diagnose, die danach entfernt wurde (rund 1600
blockierende Rücklesungen und 16 MB Tiefenkarte je Sekunde). Ein Lauf ohne sie fehlt. Die
Kosten liegen in den Draw-Aufrufen: je Wiederholung rund 30 D3D-Aufrufe für Sichern und
Zurückgeben plus der Zug — bei 2500 Wiederholungen je Frame rund 25 ms.

### Die Sonde und was die Läufe danach geklärt haben

-   **Der Sichtpuffer der Werfer ist Skyrims `PerFrame`.** Slot 12, 752 B, ViewProj in Zeilen bei
    Byte 128, `CameraPosAdjust` bei 560. Disassembliert aus `Shaders011.fxp` im Archiv
    `Fallout4 - Shaders.ba2` (3939 DXBC-Blobs, Kopf `44332211 | Größe | Technik`): `clip =
cb12[8..11] · (World aus cb2[0..3] mit Translation − cb12[35]) · pos`, danach `clip.z =
max(z, 0)` — Pancaking, Tiefe 0 heißt „vor der Nahebene". **Alle elf Techniken der Zählung**,
    auch die Wind- und Instanzvarianten `0x400C09B` und `0x4A0C09B` (Vertizes aus Structured
    Buffers), lesen genau diese Register; die Matrizen bei 0, 64 und 192 liest keine.
-   **Die Weltmatrix in Slot 2 trägt die absolute Translation** (≈ −79489 90517 7843). Der
    `CameraPosAdjust` der Engine ist nicht das Auge, sondern die Position der Schattenkamera.
-   **`DS_008` ist ein Texture-Array, eine Schicht je Kaskade.** `DepthStencilTarget::dsView[4]`
    hält je Schicht eine Ansicht: `[0]` ≈ 1700 Einheiten breit (≈ 100–140 Werfer), `[1]` ≈ 6000
    (≈ 620), `[2]` ≈ 40000 (1200–1600); `[3]` ist leer. Dieselben Puffer, jedes Frame — kein
    seltenes Nachzeichnen. Die Spec (Abschnitt 6) nahm an, die Werfer deckten
    `fDirShadowDistance` 20000 über eine Ansicht; drei Läufe lang sah die Erfassung deshalb nur
    die nahe Kaskade und reichte rund 800 Einheiten.
-   **`DS_005`, `DS_007` und `DS_009` bekommen keine Utility-Züge, auch nicht bei Regen.** Neben
    `DS_008` gehen je Frame zwei bis sechs nach `DS_002` und drei nach `DS_010`.
-   **Die Werfer zeichnen mit zwei Rasterizer-States** (Cull Back und Cull None), beide Bias 12,
    Slope 6, Clamp −100. Durch unsere Ortho über 10000 Einheiten schob Slope 6 ein 45°-Dach rund
    30 Einheiten, eine steile Wand über 100 vom Himmel weg — mehr als eine halbe Gitterzelle.
-   **`RT_028` ist der Ambient-Faktor des Composite**, alle drei Kanäle gleich, im Freien 0,48 bis
    1,0. Eine Multiplikation dort dunkelt das Umgebungslicht; der Rückfall auf `kAfterOpaque` war
    nicht nötig.

### Die Gabelungen der Spec

-   **Das tragende Risiko ist nicht eingetreten:** die Matrix liegt in einem Puffer je Sicht, und
    `RT_028` ist der Ambient-Faktor.
-   **„Die Schattenwerfer der Sonne decken die Sicht von oben nicht"** — eingetreten, aber
    anders als befürchtet: nicht wegen des schrägen Frustums, sondern weil die Erfassung nur eine
    der drei Kaskaden sah. Mit allen dreien gelöst; ein Dach genau über der Kamera bei flacher
    Sonne ist nicht gesondert geprüft. Der `Precipitation`-Weg bleibt unnötig.
-   **Terrain als Schattenwerfer** — nicht geklärt. Mit der nahen Kaskade allein war die Karte zu
    3,5 % gefüllt, genau deren Fläche; mit allen dreien zu 7 bis 13 %, weniger als die Fläche der
    mittleren Kaskade. Das spricht dafür, daß das Gelände nur in der nahen Kaskade gezeichnet wird.
-   **y-Achse** — nicht eingetreten. **Normalen an Kanten** — nicht sichtbar geworden.

### Die Abnahme

Vom Nutzer bestätigt: die Seite im Overlay mit Schalter, zwei Reglern und Debug-Ansicht;
`occlusion` zeigt die Karte, die je Zyklus die Richtung wechselt; `visibility` und `grid` „sehen
aus, als würden sie wirklich was machen"; im letzten Lauf „sieht alles OK aus". Im Log: keine
`[W]`- und keine `[E]`-Zeile, die Sekundenzeile mit `0x0040` in jedem Intervall, und
`48 918 × 7 ≈ 342 180` — jeder gesehene Werfer genau einmal je Zyklus wiederholt.

**Nicht ausgeführt, vom Nutzer als OK gesetzt** („sollte alles gut sein"): das Paar
Schnappschüsse an/aus mit F11 nach dem Umbau, ein Lauf ohne die entfernte Diagnose (die Spitzen),
Schalter aus/an unter einem Vordach, 200 m laufen, `maxZenith` 30, `minVisibility` 0 und 1, Root
Cellar, Pip-Boy, Alt-Tab, Feature im laufenden Spiel aus und an, Hot-Reload. Der Grid-Neuaufbau
nach einem großen Sprung (Schnellreise) ist nur durch die Durchsicht belegt, nicht im Spiel.

### Was die Läufe ergeben haben

-   **D3D11 verwirft eine geboxte `CopySubresourceRegion` aus einer Depth-Stencil-Textur stumm.**
    Zwei Läufe lang las die Sonde uninitialisierten Staging-Speicher und meldete die Karte leer.
    Tiefen nur ganz kopieren. **Eine Diagnose, die in zwei verschiedenen Szenen bitgleich mißt,
    mißt nichts.**
-   **Eine Debug-Ansicht mit 1,0 im HDR-Bild ist nach dem Tonemapping weiß.** Das HDR-Bild liegt
    im Freien bei 0,03 bis 0,06; die Ansichten werden mit 0,05 belichtet. Zahlen ins Log, nicht
    Bilder deuten.
-   **Ein konstanter Puffer beweist keine Kamerarelativität**, wenn das Frustum größer ist als
    der Weg, den der Nutzer geht. Der Schluß aus der Sonde war voreilig und hat einen Lauf gekostet.
-   **Die Vorlage rechnet die Gültigkeit des Gitters vorzeichenlos.** Ein Sprung um 256 Zellen
    oder mehr in negativer Richtung erklärte alle alten Zellen für gültig. Bei uns signed
    (`eb13e9d5`); der Host-Test hatte das richtige Verhalten schon verlangt.
-   **`DepthStencilTarget` hat vier Ansichten**, und Targets gab nur die erste heraus — die
    Ursache der fehlenden Reichweite, gefunden durch eine Zählung je Tiefenziel, nicht durch eine
    weitere Vermutung.

### Zugeständnisse

Die Sichtbarkeit ist bis zu sieben Frames plus Einblendzeit alt und nach einem Ortswechsel erst
nach rund einer Sekunde da. Die spekulare Dämpfung fehlt. Rund 72 MB GPU-Speicher für die Gitter.
Die Kosten der Wiederholungen stehen in keiner Passzeile. Geht ein Werfer in einem Zyklus in
einem anderen Frame ein als dem seiner Phase verloren (Culling), fehlt er in dieser einen Karte.
Das Feature steht in Innenräumen ab (`Sky::mode` nicht `kFull`).

### Was F6 aufwärts davon hat

`kComposite` für alles, was Composite-Eingaben ändert; `DepthTarget` und `Texture3D` für F6
(Volumetric Lighting); `TargetView` als Sonde jedes weiteren Features; `Reconstruct.hlsli` bis F8
die Kodierung von `RT_020` klärt; `TexelProbe` für Zahlen statt Bilder. **F7 (Volumetric Shadows)
liest dieselbe Schattenkarte** und erbt den Befund: drei Kaskaden in `DS_008`, Slot-12-Layout,
`dsView[n]`. Und das Verfahren, einen Engine-Puffer zu spiegeln und darin nur die Bytes zu
ersetzen, die der Shader nachweislich liest.

## Aus Teilprojekt F5.5a bestätigt

Ein Tag, 2026-10-04, vor F6 gezogen. Drei Läufe (DLAA, Quality zweimal, Abnahme). Neu:
`Upscale::Hooks` (acht Call-Hooks, ein vtable-Eintrag, ein Detours-Prolog, je Stelle geprüft),
`Upscale::Ngx` (DLSS-SDK 310.9.1, NGX auf D3D11), `Upscale::Passes` (Dilatation, Farbkopie, RCAS,
volle Tiefe, doodlums SSR-Shader), `Upscale::ProxyTargets`, `Upscale::Math` und
`Upscale::CameraCut` (rein, Host-Tests), `Render::Extent`, `Render::Resolution`, das Feature
`DLSS` und eine Paketregel `EXTERNAL`, mit der die 59 MB `nvngx_dlss.dll` aus dem SDK-Submodul ins
DLSS-Archiv kommt, ohne die Historie zu belasten. Vorlage: doodlums Fallout-4-Upscaler
(`doodlum/fo4test`, Zweig `upscaler`, `ce587f5`, GPL-3.0).

### Die Recherche

DLSS 5 heißt im SDK DLSS-NR (`kFeatureDLSS_NR`), erschienen 2026-09-03 mit NBA 2K27, RTX 50,
Treiber ab 616.64 (aktuell 617.14). Es ist eine generative Nachstufe auf das fertige Bild, kein
Upscaler; Pflicht sind Farbe und Bewegungsvektoren, Masken sind optional (unsere Notiz vom
September war zu streng). **Kein öffentliches SDK**: Streamline 2.14.1 hat Enum und Puffertypen,
keinen Header, kein Plugin, keine DLL; Mods nutzen eine geleakte oder aus dem Treiber gezogene
DLL. Kein NVIDIA-App-Override. DLSS SR läuft auf D3D11, DLSS-G nur auf D3D12/Vulkan mit
Pflicht-Reflex; Streamline nimmt je Prozess eine API — daher NGX für SR, Streamline nur in
F5.5b auf D3D12.

### Was die Läufe ergeben haben

-   **doodlums Einhängestellen gelten auf 1.11.240.** Offline gegen die Address Library und
    `Fallout4.exe` geprüft, im Spiel bestätigt: alle Hooks feuern 180-mal je Sekunde.
    `BSImagespaceShaderLensFlare` hat keine Aufrufstelle (nur ein `lea` in einer Registrierung),
    daher Detours.
-   **`REL::THook` liest seine Stelle im Konstruktor, und F4SE aktiviert registrierte Hooks in
    `F4SE::Init`** — vor unserer Versionsprüfung. Die Hooks werden deshalb nach der Prüfung
    angelegt und selbst aktiviert; das Trampolin (256 Byte) kommt aus `InitInfo`.
-   **`RT_029` hält UV-Einheiten**: im Stand ~1e-6, beim Gehen 0,001–0,012. Skala für NGX ist
    die Rendergröße, doodlums Vorzeichen stimmen, der Jitter geht negiert an NGX und mit negiertem
    x in `State::offsetX/Y`, bezogen auf die Anzeigegröße — auch beim Hochskalieren kein Zittern.
-   **Die Engine erzeugt Jitter und Bewegungsvektoren nur mit eingeschaltetem TAA.** Das Feature
    zwingt den Schalter an (`2704658`) und hält TAA über `IsActive` aus der Kette.
-   **`SetUseDynamicResolutionViewportAsDefaultViewport` gehört auf das RTM-Singleton.** Der erste
    Quality-Lauf nahm das Objekt aus dem ersten Argument des `RenderEffectRange`-Hooks — ein
    anderes —, der Viewport blieb klein, und die Effekte nach DLSS zeichneten das fertige Bild in
    die Ecke („Bild im Bild"). Fix `4daed29a`.
-   **Was die Phasen sehen** (Sonde, wieder entfernt): in `kBeforeComposite`, `kComposite` und
    `kAfterOpaque` volle Texturen, Viewport gleich Renderrechteck (1707×960 bei Quality), die
    Tiefe der Engine. `kComposite` liegt also **vor** doodlums Ersatzziel-Tausch, der erst im
    `RenderPassImmediately`-Aufruf geschieht. F2–F5 nehmen seither den Bildteil ihres Ziels
    (`Render::Resolution::RenderPart`) als Viewport. Bends `DynamicRes` bleibt 1: seine Pixel
    sind die der Tiefentextur.
-   **doodlums Ersatzziele hatten nie eine RTV** — sein Aufruf übergibt den Wert des Zeigers statt
    seiner Adresse. Übernommen, weil das der gespielte Zustand ist.

### Die Zahlen

Abnahmelauf 2026-10-04, 2560×1440, RTX 5080, je ein Schnappschuß über 300 Frames an derselben
Stelle (ms):

| Zustand            | Frame GPU | `DLSS/Evaluate` | SSS/Draw | Nebel | Wolken |
| ------------------ | --------- | --------------- | -------- | ----- | ------ |
| DLSS aus (TAA)     | 6,989     | —               | 0,377    | 0,030 | 0,054  |
| DLAA               | 7,607     | 0,764           | 0,378    | 0,030 | 0,054  |
| Performance (720p) | 7,608     | 0,831           | 0,070    | 0,010 | 0,015  |

**Die Hochskalierung wirkt, bringt hier aber keine Bilder.** Die pixelabhängigen Pässe werden
in Performance 3,5- bis 5-mal billiger, der Frame nicht: DLSS selbst und die Kopien der
Ersatzziele fressen die Ersparnis. Fallout 4 ist an dieser Stelle auf einer RTX 5080 nicht
pixelgebunden, sondern hängt an auflösungsunabhängiger Arbeit (Schattenkarten, CPU 4,7 ms).
Der Gewinn von F5.5a ist die Bildqualität (DLAA); der Bildratengewinn ist F5.5b vorbehalten. Der
Quality-Schnappschuß fiel zwei Sekunden nach dem Umschalten und ist nicht gewertet.

### Die Abnahme

Vom Nutzer als gut gemeldet: DLAA „sieht sehr gut aus", nichts schmiert oder zittert; Quality
füllt den Schirm, die Debug-Ansichten von F2–F5 sitzen, Spiegelungen passen; Pip-Boy (nativ, im
Log 14 Sekunden `native true`, Reset danach), Feature aus und an im laufenden Spiel, F11.
**Hot-Reload belegt** („shaders changed, recompiling" 18:35:15). Keine `[W]`- und keine
`[E]`-Zeile in allen drei Läufen.

**Nicht belegt:** Balanced und Ultra Performance (keine Ersatzziele bei 0,58 oder 0,333 im
Log), Ladebildschirm und Alt-Tab (im Log nicht erkennbar, vom Nutzer als erledigt gemeldet), das
Abnahmekriterium „FPS-Gewinn in Quality" — gemessen und verneint, siehe oben. ENB und fremde
Upscaler sind nur durch den Code abgedeckt.

### Zugeständnisse

Keine Hochskalierung im Pip-Boy und im Examine-Menü. Kein FSR oder XeSS für andere Karten. Mit
ENB nur DLAA. Die DLL steht unter NVIDIAs Lizenz; die Linking-Ausnahme trennt sie von der GPL,
eine Restunsicherheit zu Abschnitt 4.e der SDK-Lizenz bleibt. Das Feature ist standardmäßig aus.

### Was F5.5b davon hat

Jitter, Bewegungsvektoren in UV, Tiefe und die Reset-Erkennung, belegt; `Render::Resolution`;
die Einhängestelle nach `RenderEffectRange`, vor der das HUD-lose Bild für DLSS-G liegt; der
Befund, daß ein Bildratengewinn auf dieser Maschine nur aus Frame Generation kommen kann.

## Aus Teilprojekt F5.5b bestätigt

Spec `docs/superpowers/specs/2026-10-17-fallout4-f5-5b-dlss-fg-design.md`, Plan
`docs/superpowers/plans/2026-10-17-fallout4-f5-5b-dlss-fg.md` (dort oben die Abweichungen von der
Spec, die schon beim Planen feststanden). Sechs Spielstarts am 2026-10-17, RTX 5080, 2560×1440,
180 Hz, Treiber 610.88, randloses Fenster über High FPS Physics Fix.

### Die Brücke

Fallout 4 rendert auf D3D11 und präsentiert über eine eigene D3D12-Swapchain aus Streamlines
Fabrik (manuelles Hooking, v2.14.16, per CMake-Download mit SHA256). Ein IAT-Hook auf
`D3D11CreateDeviceAndSwapChain` gibt dem Spiel ein eigenes D3D11-Gerät und einen
`SwapChainProxy`, der nur `IDXGISwapChain` beantwortet; dahinter ein Ring aus drei Slots
geteilter Texturen (Endbild, HUD-los, Tiefe, Bewegung) und zwei geteilte Fences, je einer pro
Richtung und mit genau einem Schreiber (siehe unten, Abschlussreview). Der D3D12-Teil ist
eine native Insel ohne PCH unter `src/Bridge/Native/`, die mit dem Rest nur über
`src/Bridge/Bridge.h` spricht. Streamline und das D3D12-Gerät leben bis zum Prozessende. Der
Schalter wirkt nur beim Start, weil die Brücke beim Anlegen des Geräts entsteht.

### Was die Läufe ergeben haben

-   **Niemand hakt sich vor uns in die Geräteerzeugung ein**: der Import führt direkt in die
    System-`d3d11.dll`, auch mit High FPS Physics Fix.
-   **Die Eingabeabfrage `PollInputDevices` (`2268382`) hat je nach Ort einen anderen Aufrufer**:
    draußen `2228927+0x13`, in Gebäuden `Main::OnIdle+0x8E5`, im Ladebildschirm
    `2227631+0x21E` — immer einer je Frame. Der Plan hatte nur die OnIdle-Stelle eingehängt, die
    draußen nie läuft: im ersten Lauf bekam kein Frame ein Streamline-Token. Seither ein
    Prolog-Detour auf die Funktion selbst, wie bei doodlum.
-   **`FO4_RT_000` hat keine Textur** (in 180 von 180 Frames null). doodlums Abgriff über
    `renderTargets[0]` greift bei uns ins Leere; das HUD-lose Bild kommt direkt aus dem
    Backbuffer des Stellvertreters, an `SetUseDynamicResolutionViewportAsDefaultViewport(false)`
    (`2318322+0xC5`). Die Debug-Ansicht `split` hat belegt, dass es dort noch ohne HUD ist.
-   **Tiefe und Bewegungsvektoren müssen dieselbe Auflösung haben.** Beim Hochskalieren bindet
    `DLSS::AfterEffectRange` die Tiefe in Anzeigegröße, bevor unser Abgriff läuft; DLSS-G bekam
    2560×1440 gegen 1707×960, und der rechte Rand schlierte beim Rechtsdrehen. Die Tiefe wird
    seither auf die Größe der Bewegungsvektoren abgetastet.
-   **Die GPU-Zeit im Present ist kein Warten auf D3D12.** Der Verdacht, die D3D11-Queue stehe
    mit zwei Slots still, führte zum dritten Slot. Die Messung dazu war aber wertlos: beide
    Queues signalisierten **einen** Fence aus einem Zähler, sodass D3D11s Wait auf einen Slot von
    seinem eigenen früheren Signal erfüllt wurde und nie wartete (Abschlussreview, behoben in
    `b409ee06`). Mit getrennten Fences zeigt der Kontroll-Lauf 0 blockierende Waits bei 79
    gerendert / 158 angezeigt — jetzt ein echter Befund: D3D12 liegt nie drei Frames zurück. Die
    2,4 ms (FG aus) und 3,7 ms (FG an) im Present sind Konkurrenz mit der D3D12-Arbeit auf
    derselben GPU. Ob zwei Slots ebenfalls reichten, ist nicht gemessen; drei bleiben, weil sie
    D3D11 einen Frame Vorlauf geben und nur Speicher kosten.
-   **Die Kamera passt**: `worldToCam` trägt keinen Jitter (Neigung der Zeilen 0), der Restwert
    der abgeleiteten Projektion liegt unter 0,02.
-   **Hauptschleife und Present laufen auf einem Thread**, im Ladebildschirm auf einem anderen —
    dort wechselt auch der präsentierende Thread selbst, beide nacheinander, nie gleichzeitig.
    Die Frame-Indizes kommen über eine gesperrte Warteschlange an.

### Die Zahlen

An derselben Stelle, je ein F11-Schnappschuss über 300 Frames, DLSS Quality an:

| Zustand              | gerendert | angezeigt | Frame GPU | Present GPU | `DLSS/Evaluate` |
| -------------------- | --------- | --------- | --------- | ----------- | --------------- |
| FG aus, Brücke steht | 98,5      | 98,5      | 9,946     | 2,421       | 1,335           |
| FG an                | 81,4      | 158       | 12,682    | 3,708       | 2,079           |

**+60 % angezeigte Bilder.** Gerendert wird 2,7 ms langsamer, im Rahmen dessen, was NVIDIA für
DLSS-G angibt (1,35–2,8 ms auf einer 4090). Die GPU-Zeiten der D3D11-Pässe wachsen mit FG an,
weil die D3D12-Arbeit gleichzeitig dieselbe GPU belegt. Die Ausgabe lag konstant bei 158, unter
der 180-Hz-Grenze: die GPU ist ausgelastet.

### Die Abnahme

Vom Nutzer gemeldet: flüssiger, kein Schlieren mehr, HUD ruhig (UI-Meßgate bestanden, der
UI-Puffer aus Task 15 entfällt). Menüs, Pip-Boy, Ladebildschirm und das offene Overlay schalten
FG ab und wieder an; Streamline meldet durchgehend den DLSS-G-Status `0x0`.

**Nicht belegt:** Was die Brücke allein gegenüber keiner Brücke kostet, ist an dieser Stelle nicht
gemessen; der Nutzer hat ohne diesen Lauf abgeschlossen. Die 2,4 ms Present-GPU mit FG aus sind
der Anlass, das nachzuholen. Alt-Tab und Minimieren sind vom Nutzer als gut gemeldet, lösen im
randlosen Fenster aber keinen `ResizeBuffers` aus — der Resize-Pfad samt seiner Fehlerbehandlung
ist im Spiel nie gelaufen. Exklusives Vollbild, ENB und eine zweite Geräteerzeugung sind nur durch
den Code abgedeckt.

### Abschlussreview

Eine Review über den ganzen Zweig fand einen kritischen Fehler: **ein gemeinsamer Fence mit zwei
Schreibern**. D3D11 signalisierte „Slot beschrieben", D3D12 „Slot frei", beide aus einem Zähler.
Damit war D3D11s Wait auf einen freien Slot immer schon von seinem eigenen letzten Signal erfüllt,
die CPU-Prüfung vor dem Zurücksetzen eines Command Allocators ebenso, und ein spätes D3D12-Signal
konnte den Wert zurücksetzen — mit dauerhaft einem Frame Verzug und möglichem Hänger beim Resize
oder Beenden als Folge. Behoben in `b409ee06`, mit drei weiteren Punkten: ein nach dem Resize
gescheitertes Anlegen der Puffer ließ Null-Zeiger zurück, die `Present` und `GetBuffer`
dereferenziert hätten; ein Timeout beim Leeren der D3D12-Queue blieb stumm; das Overlay band nach
einem reinen Formatwechsel den alten Backbuffer weiter. `7acdd67a` nimmt die Diagnosezähler der
Läufe heraus, hakt bei aus geschaltetem Startschalter weder den Import noch `PollInputDevices`, und
setzt Reflex beim Abschalten des Features von boost zurück.

Kontroll-Lauf danach (18:06, gleiche Umgebung): FG an 79 gerendert / 158 angezeigt, FG aus 97,
DLSS-G-Status durchgehend `0x0`, 0 blockierende Waits, keine Timeout-Zeile. Reflex boost wurde
an- und wieder abgeschaltet (Streamline: Ergebnis 0), FG im Overlay aus- und eingeschaltet,
Pip-Boy, Ladebildschirm, Alt-Tab, Minimieren und Beenden — vom Nutzer ohne Auffälligkeiten
gemeldet.

### Zugeständnisse

Kein exklusives Vollbild, kein ENB, kein MFG über 2×, kein HDR. FG aus in Menüs und bei offenem
Overlay. Die Streamline- und NGX-Dateien stehen unter NVIDIAs Lizenz und liegen unverändert im
eigenen Addon `FrameGeneration`. Das Feature ist standardmäßig aus.

### Was F5.5b hinterlässt

Die D3D12-Präsentation hinter einer sauberen D3D11-Swapchain als Andockpunkt für DLSS 5
(DLSS-NR verlangt Farbe und Bewegungsvektoren auf D3D12, beides liegt in der Brücke);
`Upscale::Hooks::Tap` als zweiter Hörer an den F5.5a-Stellen; den Frame-Beginn am
`PollInputDevices`-Prolog; `Bridge::PresentStats` und die Zeile „shown" in der Ecke.

## Bekannte Lücken in CommonLibF4

Diese fehlen in **allen** geprüften Forks und müssen selbst reverse-engineert werden:

-   `RE::BSShader` beschreibt **nicht** das Laufzeitobjekt: jeder Offset nach `shaderType` liegt
    `0x78` zu niedrig, `sizeof` ist `0x190` statt `0x118`, `fxpFilename` sitzt bei `0x188`.
    Gemessen in C, Einzelheiten in `imagespace-passes.md`. Wir lesen über eigene Offsets in
    `src/Shader/BSShaderLayout.h`.
-   `REX::W32::ID3DInclude` erbt fälschlich von `IUnknown`; das SDK deklariert die Schnittstelle
    ohne Basis und mit zwei vtable-Einträgen. Die REX-Fassung ist unbrauchbar.
-   `BSImagespaceShader` und seine 161 Geschwister fehlen als Header, obwohl RTTI- und
    vtable-IDs vorhanden sind.
-   `BSRenderPass` — überall nur vorwärtsdeklariert, nie definiert.
-   Ein benanntes `RENDER_TARGET`-Enum — FO4s Targets liegen als anonymes `renderTargets[101]`
    in `BSGraphics::RendererData`. Der geerbte Code referenziert `RE::RENDER_TARGET::k*` 132-mal.
-   `ShadowSceneNode`, `BSLight`.
-   `BSGraphics::RendererData::shadowState` ist als Zeiger deklariert und keiner: das Feld liest
    `0x1B70`, den Offset von `RendererShadowState` innerhalb von `BSGraphics::Context`. Ihm zu
    folgen stürzt ab. Gemessen in F2.
-   `RE/S/Sun.h` ist nicht selbstgenügsam — nennt `SkyObject`, `NiBillboardNode`, `BSTriShape`,
    `BSShaderAccumulator`, `NiDirectionalLight` ohne Deklaration — und `NiDirectionalLight` ist
    überall nur vorwärtsdeklariert. Gelesen wird über `NiLight` bei Offset null.

Vorhanden und brauchbar sind dagegen `BSGraphics::{Renderer, RendererData, RendererShadowState,
RenderTargetManager, Context, State, ViewData}`, `BSShader`, `BSShaderManager`, `TESWeather`,
`Sky`, `ImageSpaceManager`, `BSLightingShaderProperty`.

Beachtenswert: Fallout 4 rendert nativ deferred (`BSShaderManager::ShaderEnum` kennt `kDFPrepass`,
`kDFLight`, `kDFComposite`), während die Skyrim-CS ihre Deferred-Pipeline erst nachrüstet. Ein
Teil der geerbten Feature-Logik ist damit gegenstandslos, ein anderer muss an einer anders
geformten Pipeline ansetzen.

## Prozess

Pro Teilprojekt: Spec unter `docs/superpowers/specs/YYYY-MM-DD-<thema>-design.md`, daraus ein
Implementierungsplan, dann Umsetzung. Diese Roadmap wird bei jedem Statuswechsel mitgepflegt.
