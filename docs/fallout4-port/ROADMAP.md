# Fallout 4 Port — Roadmap

Status: Umsetzung, A bis E1 abgeschlossen. Stand 2026-09-04.

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

| #   | Teilprojekt                                                                                                            | Abnahmekriterium                                                           | Status            |
| --- | ---------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------- | ----------------- |
| A   | **Fundament** — CMake/vcpkg-Umbau, commonlibf4 als Submodul + CMake-Shim, F4SE-Entrypoints, Logging, Runtime-Erkennung | DLL lädt in FO4 AE 1.11.240, schreibt eine Logzeile, stürzt nicht ab       | **abgeschlossen** |
| B1  | **Render-Anbindung** — Zugriff auf D3D11-Device/Context/SwapChain, Present-Hook, Frame-Zähler, Debug-Marker            | RenderDoc-Capture zeigt einen von uns gesetzten Marker                     | **abgeschlossen** |
| B2  | **Render-Target-Inventar** — die 101 anonymen Targets aus BSGraphics::RendererData identifizieren und benennen         | Beschriftetes RenderDoc-Capture plus Befunddokument mit der Target-Tabelle | **abgeschlossen** |
| C   | **Shader-Pipeline** — Laden, Kompilieren, Cachen, Hot-Reload, Einschleusen eigener Shader                              | Ein vorhandener FO4-Shader wird nachweislich durch einen eigenen ersetzt   | **abgeschlossen** |
| D1  | **Feature-Framework** — Feature-Basisklasse, Registrierung, Lifecycle, Settings-Persistenz                             | Zwei Features unabhängig an-/abschaltbar                                   | **abgeschlossen** |
| D2  | **Paketierung** — `dist/`, Basis-, Addon- und AIO-Archive                                                              | Ausgeliefertes Archiv installiert sich in ein sauberes Spiel               | **abgeschlossen** |
| E1  | **Overlay und Eingabe** — ImGui-Overlay, Fensterprozedur, Eingabesperre, eigener Zeiger                                | Overlay im Spiel bedienbar, Spieleingabe steht, solange es offen ist       | **abgeschlossen** |
| E2  | **Einstellungsoberfläche** — Featureliste, Schreiben von Einstellungen, Themes, Schriften, i18n                        | Einstellungen im Overlay ändern, sie überleben einen Neustart              | offen             |
| F+  | **Features einzeln** — je ein Zyklus pro portiertem CS-Feature                                                         | Sichtbarer Effekt plus CPU-/GPU-Zahlen                                     | offen             |

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
    gelegt werden, damit der `Menu`-Block überhaupt entstand. **Das gehört nach E2**, das ohnehin
    schreibend mit der Datei umgeht.
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
