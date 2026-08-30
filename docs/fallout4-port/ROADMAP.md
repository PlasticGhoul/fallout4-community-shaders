# Fallout 4 Port — Roadmap

Status: Planung. Stand 2026-08-30.

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

| #   | Teilprojekt                                                                                                            | Abnahmekriterium                                                         | Status            |
| --- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ | ----------------- |
| A   | **Fundament** — CMake/vcpkg-Umbau, commonlibf4 als Submodul + CMake-Shim, F4SE-Entrypoints, Logging, Runtime-Erkennung | DLL lädt in FO4 AE 1.11.240, schreibt eine Logzeile, stürzt nicht ab     | **abgeschlossen** |
| B1  | **Render-Anbindung** — Zugriff auf D3D11-Device/Context/SwapChain, Present-Hook, Frame-Zähler, Debug-Marker            | RenderDoc-Capture zeigt einen von uns gesetzten Marker                   | **abgeschlossen** |
| B2  | **Render-Target-Inventar** — die 101 anonymen Targets aus BSGraphics::RendererData identifizieren und benennen         | Benanntes RENDER_TARGET-Enum im commonlibf4-Fork plus Dokumentation      | offen             |
| C   | **Shader-Pipeline** — Laden, Kompilieren, Cachen, Hot-Reload, Einschleusen eigener Shader                              | Ein vorhandener FO4-Shader wird nachweislich durch einen eigenen ersetzt | offen             |
| D   | **Feature-Framework** — Feature-Basisklasse, Registrierung, Lifecycle, Settings-Persistenz, Ini-Versionierung          | Zwei Dummy-Features unabhängig an-/abschaltbar                           | offen             |
| E   | **Menü** — ImGui-Overlay, Input-Handling, Einstellungs-UI                                                              | Overlay im Spiel bedienbar, Einstellungen überleben Neustart             | offen             |
| F+  | **Features einzeln** — je ein Zyklus pro portiertem CS-Feature                                                         | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                   | offen             |

Das ursprüngliche Teilprojekt B wurde am 2026-08-30 in B1 und B2 geteilt. B1 ist begrenzte
Ingenieursarbeit mit klarem Ende; B2 ist offene Reverse-Engineering-Forschung, deren Aufwand sich
vorher nicht seriös schätzen lässt. Zusammen hätte B kein vorhersagbares Ende gehabt. Die
Marker aus B1 sind zugleich das Werkzeug, mit dem sich in B2 überhaupt sinnvoll suchen lässt.

A bis C sind die eigentliche Portierungsarbeit. D und E sind weitgehend aus dem bestehenden
Skyrim-Code übernehmbar, weil sie kaum engine-gekoppelt sind. Die vorhandenen HLSL-Shader werden
erst ab F relevant — sie sind das Ziel der Portierung, nicht ihr Anfang.

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

## Bekannte Lücken in CommonLibF4

Diese fehlen in **allen** geprüften Forks und müssen selbst reverse-engineert werden:

-   `BSRenderPass` — überall nur vorwärtsdeklariert, nie definiert.
-   Ein benanntes `RENDER_TARGET`-Enum — FO4s Targets liegen als anonymes `renderTargets[101]`
    in `BSGraphics::RendererData`. Der geerbte Code referenziert `RE::RENDER_TARGET::k*` 132-mal.
-   `ShadowSceneNode`, `BSLight`.

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
