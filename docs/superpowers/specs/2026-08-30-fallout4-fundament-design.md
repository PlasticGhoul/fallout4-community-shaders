# Teilprojekt A — Fundament

Spec, Stand 2026-08-30. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.

## 1. Kontext und Ziel

Dieses Repository ist ein Fork der Skyrim Community Shaders (`1.9.0-rc.1`, Commit `3d472fde`) mit
dem Ziel, Community Shaders auf Fallout 4 zu portieren. Der Port wird nicht als Übersetzung des
bestehenden Codes gebaut, sondern mit dünner Reverse-Engineering-Schicht und Interception auf
D3D11-Ebene; die Begründung steht in der Roadmap.

Teilprojekt A liefert das Fundament: ein Plugin, das unter Fallout 4 AE 1.11.240 über F4SE
geladen wird, sich meldet und sonst nichts tut.

Der Umfang ist bewusst so klein gewählt. A trennt die Frage „stimmt unser Build- und Ladeweg"
von der Frage „stimmen unsere Render-Annahmen". Scheitert später Teilprojekt B, ist damit
ausgeschlossen, dass die Ursache im Fundament liegt.

## 2. Umfang

### In A enthalten

-   Repository-Umbau: Trennung von geerbtem Skyrim-Code und neuem Fallout-Code.
-   Neues, minimales CMake-Gerüst.
-   `commonlibf4` als Submodul, eingebunden über `add_subdirectory` plus generierten CMake-Shim.
-   Auf das Nötige reduziertes vcpkg-Manifest.
-   F4SE-Entrypoints, Plugin-Deklaration, Versionsressource.
-   Konfiguration des Logkanals, den `F4SE::Init` bereitstellt.
-   Zweistufige Runtime-Erkennung mit sauberer Ablehnung nicht unterstützter Versionen.
-   Ein Nachrichten-Listener, der den F4SE-Nachrichtenweg nachweist.
-   Deploy-Hilfe für die Entwicklungsschleife.
-   Ein Unit-Test für die Runtime-Akzeptanz.
-   Prüfskript für das erzeugte Artefakt.

### Nicht in A enthalten

-   Jeglicher Hook, jeglicher D3D11-Zugriff, jegliches Trampolin (Teilprojekt B).
-   Shader-Pipeline (C), Feature-Framework (D), Menü (E), portierte Features (F+).
-   Packaging, AIO-Erzeugung, Release-Automation, CI-Workflows.
-   Validierung auf NG (1.10.980 / 1.10.984) und OG (1.10.163). Das Gerüst trägt drei Runtimes,
    getestet wird ausschließlich AE — für die anderen fehlt die Testumgebung.
-   Übernahme von Übersetzungen, Themes, Schriften und Feature-Konfigurationen aus `package/`.

## 3. Vorentscheidungen

Diese Punkte sind entschieden und werden in A nicht neu verhandelt.

| Thema             | Entscheidung                                                                 | Begründung                                                                                                                                                                                                                                           |
| ----------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Geerbter C++-Code | Radikaler Schnitt, Historie über Tag `skyrim-base`                           | Sauberer Arbeitsbaum; kein toter Code in Suche, Grep und clangd. Rückgriff über `git show` bleibt jederzeit möglich                                                                                                                                  |
| Projektgerüst     | Neues minimales CMakeLists; Packaging, Deployment und CI vorerst stillgelegt | Deren Anforderungen (Fallout-Datenpfade, Feature-Layout) stehen noch nicht fest; sie jetzt umzubauen hieße, gegen Unbekanntes zu bauen                                                                                                               |
| Plugin-Identität  | `CommunityShadersFO4`, Version `0.1.0`                                       | Keine Verwechslung mit dem Skyrim-Plugin in Logs und Absturzberichten; 0.x benennt die Reife ehrlich. Bewusst nicht `FO4CommunityShaders`: diesen Namen belegt bereits die Referenzimplementierung, beide Plugins würden sonst dieselbe DLL erzeugen |
| Engine-Bibliothek | `commonlibf4` als Submodul auf eigenem Fork, plus generierter CMake-Shim     | Bibliothek liefert nur `xmake.lua`; wir werden sie erweitern müssen, also braucht sie einen editierbaren Fork statt eines gepinnten vcpkg-Ports                                                                                                      |
| Versionsprüfung   | Exakt gegen `F4SE::RUNTIME_1_11_240`, nicht gegen `RUNTIME_LATEST`           | `RUNTIME_LATEST` ist ein bewegliches Ziel und ließe uns nach einer Spielaktualisierung stillschweigend auf eine ungetestete Runtime laufen                                                                                                           |
| Trampolin         | In A abgeschaltet                                                            | A installiert keinen Hook. Das Trampolin kommt mit B                                                                                                                                                                                                 |

## 4. Repository-Umbau

### 4.1 Sicherung

Vor dem ersten Löschen ein annotierter Tag `skyrim-base` auf `3d472fde`, gepusht nach `origin`.
Danach ist jede Zeile des geerbten Codes über `git show skyrim-base:<pfad>` erreichbar — beim
Portieren einzelner Features ab Teilprojekt F der Regelfall.

### 4.2 Bleibt unangetastet im Arbeitsbaum

-   `package/Shaders/` (81 Dateien) und die Shader unter `features/*/Shaders/` (74 HLSL-Dateien).
    Zusammen die 17.839 Zeilen HLSL, die das eigentliche Portierungsziel sind. In A werden sie
    weder gebaut noch verarbeitet.
-   Die 40 Verzeichnisse unter `features/` als Ganzes, einschließlich der 113 Nicht-Shader-Dateien
    (`.ini`, `CORE`-Marker, Readmes, `.dds`, `.json`). Rohmaterial für D und F.
-   `package/Interface/` (76 Dateien: Schriften, Icons) und `package/SKSE/` (26 Dateien: Themes,
    Übersetzungen). Werden in E gebraucht; die Umbenennung nach `package/F4SE/` gehört dorthin,
    nicht nach A.
-   `.clang-format`, `.clang-format-ignore`, `.pre-commit-config.yaml`, `.prettierrc.json`,
    `.prettierignore`, `.gitattributes`, `.gitignore`. Diese arbeiten über Dateimuster und
    funktionieren unverändert weiter.
-   `COPYING`, `EXCEPTIONS.md`, `CONTRIBUTING.md`, die Lizenzdateien in den Unterverzeichnissen.
-   `tools/` vollständig. `verify-shader-refactor.ps1` und `.sh` arbeiten mit `fxc` und Git und
    sind spielunabhängig; die übrigen Skripte werden ab D wieder gebraucht und stören bis dahin
    nicht.
-   `docs/development/` und `docs/weather-system-docs/`. Teils weiter gültig (Shader-Iteration),
    teils überholt (Deploy-Pfade). Wird nachgezogen, wenn C und E die tatsächlichen Abläufe
    festgelegt haben, nicht in A.

### 4.3 Verlässt den Arbeitsbaum

Alles Folgende bleibt über `skyrim-base` erreichbar.

-   `src/` vollständig (282 verfolgte Dateien).
-   `include/PCH.h`, `include/FrameAnnotations.h`, `include/FidelityFX/` (12 Dateien).
-   Submodule `extern/CommonLibSSE-NG`, `extern/Streamline-DX12`, `extern/FidelityFX-SDK` samt
    zugehöriger `.gitmodules`-Einträge; eingecheckt `extern/sk_hdr_png/`. Streamline und
    FidelityFX kehren in F zurück, wenn Upscaling ansteht — dann mit den für Fallout 4
    gepflegten Forks, nicht mit den Skyrim-Varianten.
-   `cmake/XSEPlugin.cmake`, `cmake/FidelityFX-SDK.cmake`, `cmake/Streamline/`,
    `cmake/AddCXXFiles.cmake`, `cmake/CleanupStaleEntries.cmake`, `cmake/FeatureVersions.h.in`,
    `cmake/ThemePresets.h.in`, `cmake/shadertoolsconfig.json.in`, `cmake/ports/` (vier
    Overlay-Ports).
-   `CMakeLists.txt` (1216 Zeilen) und `CMakePresets.json` werden ersetzt, nicht bearbeitet.
-   `BuildRelease.bat`, `BuildDev.bat`, `BuildDevFast.bat`, `BuildPR.bat`, `BuildDebug.bat`,
    `Dockerfile`, `containerbuild.ps1`, `CMakeUserPresets.json.template`.
-   `.releaserc.js` und `.coderabbit.yaml` — beide beschreiben Abläufe, die in A stillgelegt sind.

Behalten werden `cmake/Plugin.h.in`, `cmake/Version.rc.in` und
`cmake/triplets/x64-windows-static-md-release.cmake`; sie werden angepasst weiterverwendet.

### 4.4 Wird stillgelegt statt gelöscht

Die 13 Workflows wandern von `.github/workflows/` nach `.github/workflows-disabled/`. GitHub führt
ausschließlich `.github/workflows/` aus; der Inhalt bleibt damit als Vorlage lesbar, wenn
Shader-Validierung, i18n-Prüfung und Release-Automation zurückgeholt werden.

`.github/actions/` (drei zusammengesetzte Aktionen) und `.github/configs/` bleiben liegen, wo sie
sind. Beide werden ausschließlich von den Workflows referenziert und sind ohne diese inert; sie zu
verschieben wäre Bewegung ohne Wirkung und würde die Rückkehr nur erschweren.

### 4.5 Wird angepasst

-   `.clangd` zeigt heute auf `extern/CommonLibSSE-NG/include` und setzt `ENABLE_SKYRIM_AE`,
    `ENABLE_SKYRIM_SE`, `ENABLE_SKYRIM_VR`. Umzustellen auf die commonlibf4-Include-Pfade
    (`extern/CommonLibF4/include` und `extern/CommonLibF4/lib/commonlib-shared/include`) und das
    vcpkg-Include-Verzeichnis des neuen Build-Ordners. Ohne diese Anpassung ist die
    IDE-Diagnose ab dem ersten Tag durchgehend rot.
-   `.claude/CLAUDE.md` beschreibt auf voller Länge Skyrim-Build, CommonLibSSE-NG-Runtime-Targeting,
    das Skyrim-Feature-Layout und den Release-Zweigmodus. Unverändert würde die Datei jede
    künftige Sitzung fehlleiten. Sie wird in A auf den Fallout-Stand gebracht: Build- und
    Testbefehle, commonlibf4 statt CommonLibSSE-NG, Verweis auf Roadmap und Specs, und ein
    ausdrücklicher Hinweis darauf, welche Abschnitte bis zur Reaktivierung der jeweiligen
    Teilprojekte gegenstandslos sind.
-   `AI-INSTRUCTIONS.md` aus demselben Grund, in Kurzform.
-   `README.md` beschreibt Skyrim-Voraussetzungen, -Build und -Debugging. Es wird durch eine
    knappe Fassung ersetzt: Projektziel, Stand, Voraussetzungen, Bauen, Verweis auf die Roadmap.
    Ausführliche Nutzerdokumentation gehört nicht in A.
-   `TRANSLATING.md` bleibt inhaltlich gültig, bekommt aber einen Hinweis, dass das i18n-System
    erst mit Teilprojekt E wieder aktiv ist.

## 5. Build-Architektur

### 5.1 Submodul

`extern/CommonLibF4` zeigt auf `PlasticGhoul/commonlibf4`, einen Fork von
`Dear-Modding-FO4/commonlibf4`, verfolgt dessen `main`. Das Submodul bringt seinerseits
`lib/commonlib-shared` mit, weshalb `git clone --recursive` beziehungsweise
`git submodule update --init --recursive` zwingend ist.

Der eigene Fork ist keine Vorsichtsmaßnahme, sondern eine Notwendigkeit: `BSRenderPass` ist in
allen geprüften commonlibf4-Forks nur vorwärtsdeklariert, ein benanntes `RENDER_TARGET`-Enum
existiert nicht (die Targets liegen als anonymes `renderTargets[101]` in
`BSGraphics::RendererData`), und `ShadowSceneNode` sowie `BSLight` fehlen ganz. Spätestens
Teilprojekt B muss diese Strukturen ergänzen.

### 5.2 Einbindung

Zwei Fremd-Targets, beide außerhalb unserer Warnungsstrenge:

1.  `commonlib-shared` wird direkt per `add_subdirectory` aus
    `extern/CommonLibF4/lib/commonlib-shared` eingebunden. Die Bibliothek liefert ein
    vollwertiges, 149 Zeilen langes `CMakeLists.txt` mit Alias-Target
    `commonlib-shared::commonlib-shared`, expliziter Quellenliste, spdlog-Anbindung, allen
    benötigten System-Bibliotheken, den nötigen Warnungsunterdrückungen (`C4200`, `C4201`,
    `C4324`) und vorkompiliertem Header. Es besteht kein Grund, das nachzubauen.
2.  `CommonLibF4` selbst liefert nur `xmake.lua` und bekommt deshalb einen Shim: eine Vorlage
    unter `cmake/CommonLibF4.cmake.in`, die per `configure_file` in das Build-Verzeichnis
    geschrieben und von dort per `add_subdirectory` eingebunden wird. Der Shim sammelt
    `src/**.cpp` und `include/**.h` per `GLOB_RECURSE` mit `CONFIGURE_DEPENDS`, setzt das
    Include-Verzeichnis und `cxx_std_23`, verwendet `include/F4SE/Impl/PCH.h` als
    vorkompilierten Header, ergänzt `/bigobj` und `/utf-8` und verlinkt gegen
    `commonlib-shared`. Er exportiert das Alias-Target `CommonLibF4::CommonLibF4`.

`COMMONLIB_RUNTIMECOUNT` wird nicht gesetzt: `REX/FModule.h` definiert selbst `3` als Standard,
was der Fallout-4-Aufteilung `kOG`, `kNG`, `kAE` genau entspricht.

Die Shim-Vorlage wird im Kopf mit einem Kommentar versehen, der festhält, gegen welchen
Upstream-Stand sie geschrieben wurde, damit ein struktureller Umbau stromaufwärts als Ursache
erkennbar bleibt.

### 5.3 Abhängigkeiten

Das vcpkg-Manifest wird auf eine Abhängigkeit reduziert: `spdlog` mit Feature `wchar`, verlangt
von `commonlib-shared`. Mehr nicht: der Unit-Test aus Abschnitt 7.1 kommt ohne Test-Framework aus
— ein schlichtes Executable mit Rückgabewert ungleich null genügt für eine einzige
Entscheidungsfunktion und erspart eine weitere Abhängigkeit. `xbyak` und `detours` folgen mit den
Hooks in B, ImGui mit dem Menü in E.

`builtin-baseline` bleibt auf `dddca6fa87f177e0678e2545c4b4636a44aa05bd` — dem Stand, auf den die
lokale vcpkg-Installation bereits ausgecheckt ist und mit dem der geerbte Build nachweislich
durchläuft. Eine Änderung dieses Werts erfordert, das lokale vcpkg-Repository mit umzuhängen, und
ist in A ausdrücklich nicht vorgesehen.

Der Triplet `x64-windows-static-md-release` bleibt unverändert: statische Bibliotheken bei
dynamischer CRT ist für ein Spiel-Plugin das Richtige und entspricht der bisherigen Konvention.

### 5.4 CMake

Neues Wurzel-`CMakeLists.txt`, Zielgröße unter 150 Zeilen. `cmake_minimum_required(VERSION 4.2)`
bleibt: der Generator `Visual Studio 18 2026` verlangt ein aktuelles CMake, und die vorhandene
Toolchain erfüllt das nachweislich.

-   Projektversion `0.1.0`, Zielname `CommunityShadersFO4`, Bibliothekstyp `SHARED`.
-   `Plugin.h` und `version.rc` werden aus den vorhandenen Vorlagen `cmake/Plugin.h.in` und
    `cmake/Version.rc.in` erzeugt, angepasst auf Name, Version und Autor.
-   Unser Target führt `/W4 /WX` und `cxx_std_23`. Die beiden Fremd-Targets behalten ihre
    eigenen Optionen.
-   Ein vorkompilierter Header `include/PCH.h` mit `<F4SE/F4SE.h>`, `<RE/Fallout.h>`, spdlog und
    `<Windows.h>` unter `WIN32_LEAN_AND_MEAN`.
-   Option `FO4CS_DEPLOY_DIR`: ist sie gesetzt, kopiert ein Post-Build-Schritt die DLL nach
    `<FO4CS_DEPLOY_DIR>/F4SE/Plugins/`. Mehr nicht.
-   Option `FO4CS_BUILD_TESTS`, Standard `ON`, für das Test-Executable; `enable_testing()`
    plus `add_test`, ausführbar über `ctest`.

### 5.5 Presets

`CMakePresets.json` wird ersetzt und enthält zwei Konfigurations-Presets:

-   `FO4` — Generator `Visual Studio 18 2026`, x64, Release, vcpkg-Toolchain, Triplet wie oben.
    Der kanonische Build.
-   `FO4-Fast` — Generator `Ninja`, `/Od`, inkrementelles Linken. Für die Entwicklungsschleife.
    Verzichtbar, falls A schlanker ausfallen soll; die Erfahrung mit dem geerbten Projekt spricht
    dafür, ihn mitzunehmen.

Ein einziger Build-Befehl aus frischem Klon heraus muss genügen; die geerbten `.bat`-Wrapper
werden nicht ersetzt.

## 6. Laufzeit

### 6.1 Entrypoints

Alle drei Symbole werden exportiert. Die verwendete API ist an `commonlibf4` verifiziert, nicht
aus der SKSE-Entsprechung abgeleitet.

-   `F4SEPlugin_Version` als `constinit auto` mit `F4SE::PluginVersionData`. Gesetzt werden
    `PluginName`, `PluginVersion`, `AuthorName`, `UsesAddressLibrary(true)`,
    `UsesSigScanning(false)`, `IsLayoutDependent(true)`, `HasNoStructUse(false)` und
    `CompatibleVersions({ F4SE::RUNTIME_1_11_240 })`.
-   `F4SEPlugin_Query` für den älteren Ladepfad, füllt `F4SE::PluginInfo`.
-   `F4SEPlugin_Load` mit `F4SE::InitInfo`, dabei `trampoline = false` und `hook = false`, weil A
    keinen Hook installiert. `hook` wäre auch auf `true` folgenlos -- `InitHook` aktiviert nur
    vom Plugin registrierte `REL::FHook`-Objekte, und davon gibt es keine --, aber die
    Hook-Freiheit von A soll im Code stehen und nicht aus einer Abwesenheit folgen.

### 6.2 Runtime-Erkennung

Zweistufig, und das ist die inhaltlich wichtigste Entscheidung in A.

`REX::FModule::GetRuntimeIndex()` liefert `Runtime::kOG`, `kNG` oder `kAE`. Auf diesen Wert allein
darf man sich nicht verlassen: eine unbekannte, neuere Spielversion fällt auf `kAE` durch, und das
Plugin würde dann Adressen anschreiben, die dort etwas anderes bedeuten.

Deshalb prüft `IsSupportedRuntime(REL::Version)` zusätzlich die von
`F4SE::LoadInterface::RuntimeVersion()` gemeldete Version exakt gegen `F4SE::RUNTIME_1_11_240`.
Jede andere Version wird abgelehnt: eine Logzeile mit erwarteter und vorgefundener Version, dann
`return false` aus `F4SEPlugin_Load`. F4SE meldet das Plugin daraufhin als nicht geladen, das
Spiel läuft weiter. Es gibt in A bewusst keine modale Fehlermeldung.

`IsSupportedRuntime` ist eine freie Funktion ohne Abhängigkeit auf den Spielzustand, damit sie
ohne laufendes Spiel testbar ist.

### 6.3 Logging

Anders als zunächst angenommen bringt die Bibliothek Logging mit. Es gibt zwar kein
`F4SE/Logger.h` -- das existiert nur in `alandtse/CommonLibF4` --, aber `F4SE::Init` richtet den
Kanal vollständig ein, sobald `InitInfo::log` gesetzt ist (Standard: `true`):

-   Zielpfad `<Dokumente>/My Games/{GetSaveFolderName()}/F4SE/{Plugin-Name}.log`, aufgelöst über
    `SHGetKnownFolderPath(FOLDERID_Documents)`. `GetSaveFolderName()` liefert ab F4SE 0.7.1 den
    Wert des Interfaces und fällt darunter auf `Fallout4` zurück; die Ziel-F4SE 0.7.9 nutzt also
    den echten Wert.
-   Sinks: MSVC-Ausgabefenster plus Datei, wahlweise rotierend über `InitInfo::logRotate`.
-   Level und Flush-Level aus `InitInfo::logLevel`, Muster aus `InitInfo::logPattern`.
-   Die erste Zeile mit Plugin-Name und Version schreibt die Bibliothek selbst.

Wir setzen daher lediglich `InitInfo` und schreiben über `REX::INFO` / `REX::ERROR`. Ein eigenes
Log-Modul entfällt.

Daraus folgt eine Reihenfolge-Bedingung: `F4SE::Init` muss **vor** der Versionsprüfung laufen,
sonst lässt sich eine Ablehnung nicht protokollieren. Das ist unbedenklich, weil `Init` mit
`trampoline = false` und `hook = false` nur den Plugin-Zustand aufbaut, den Logkanal öffnet und
die F4SE-Interfaces abfragt.

### 6.4 Nachrichten

Ein Listener auf `F4SE::MessagingInterface` schreibt je eine Logzeile bei `kPostPostLoad` und bei
`kGameDataReady`. Zu beachten: die zweite Nachricht heißt unter F4SE `kGameDataReady`, nicht
`kDataLoaded` wie unter SKSE. Der Listener tut in A nichts weiter; er belegt, dass der
Nachrichtenweg steht, auf dem B aufsetzt.

### 6.5 Dateien

`src/XSEPlugin.cpp`, `src/Runtime.h`, `src/Runtime.cpp`, `include/PCH.h`. Rund zweihundert
Zeilen insgesamt.

## 7. Verifikation und Abnahme

### 7.1 Ohne Spiel prüfbar

1.  **Kaltbau.** `git clone --recursive` in ein leeres Verzeichnis, dann `cmake --preset FO4` und
    `cmake --build --preset FO4` — ohne manuelle Zwischenschritte. Fängt fehlende
    Submodul-Einträge, vergessene Abhängigkeiten und Fehler im Shim.
2.  **Warmbau ist ein No-Op.** Ein zweiter Durchlauf meldet, dass nichts zu tun ist. Fängt
    Dateien, die bei jedem Lauf neu erzeugt werden, und fehlerhafte
    `CONFIGURE_DEPENDS`-Abhängigkeiten.
3.  **Artefaktprüfung** durch ein neues `tools/verify-plugin.ps1`: PE-Signatur, Maschinentyp
    `0x8664`, gesetztes DLL-Bit, Exporttabelle enthält `F4SEPlugin_Load`, `F4SEPlugin_Query`
    und `F4SEPlugin_Version`, Versionsressource trägt `CommunityShadersFO4` und `0.1.0`.
4.  **Unit-Test** für `IsSupportedRuntime`: `1.11.240` wird akzeptiert; `1.10.163`, `1.10.984`
    und eine erfundene höhere Version werden abgelehnt. Dies ist die einzige Stelle in A, die
    still und folgenschwer falsch sein kann.

### 7.2 Nur im Spiel prüfbar

5.  DLL nach `Data/F4SE/Plugins/` deployen, Fallout 4 AE 1.11.240 starten, Hauptmenü erreichen.
6.  Unser Log existiert am erwarteten Ort und enthält Name, Version, erkannte Runtime sowie je
    eine Zeile aus `kPostPostLoad` und `kGameDataReady`.
7.  Das F4SE-eigene Log meldet das Plugin als geladen, nicht als abgelehnt.
8.  **Negativtest.** Die erwartete Version wird im Code vorübergehend auf einen falschen Wert
    gesetzt, das Plugin gebaut und das Spiel gestartet. Das Plugin muss sich ablehnen, eine
    verständliche Zeile loggen, und das Spiel muss dennoch das Hauptmenü erreichen. Danach wird
    der Wert zurückgesetzt. Ohne diesen Test ist die Ablehnungslogik aus
    Abschnitt 6.2 im laufenden Spiel unbelegt; Schritt 4 deckt allein die reine
    Entscheidungsfunktion ab.

### 7.3 Abnahmekriterien

A gilt als abgeschlossen, wenn alle acht Punkte aus 7.1 und 7.2 erfüllt sind, und wenn
`docs/fallout4-port/ROADMAP.md` den Status von A auf abgeschlossen gesetzt hat, ergänzt um den
tatsächlich bestätigten Log-Pfad und die F4SE-Version, gegen die getestet wurde.

## 8. Annahmen, die A bestätigen muss

Diese Punkte sind begründet, aber nicht bewiesen. Sie werden im Verlauf von A verifiziert und das
Ergebnis wird in der Roadmap festgehalten.

-   `GetSaveFolderName()` liefert unter Fallout 4 AE `Fallout4`, der Logpfad lautet also
    `<Dokumente>/My Games/Fallout4/F4SE/`. Der Pfad wird von der Bibliothek gebildet, nicht von
    uns; Abgleich bei Prüfschritt 6.
-   `commonlib-shared` lässt sich per `add_subdirectory` ohne Anpassung in unseren Build
    einhängen. Fällt das durch, weicht der Shim aus Abschnitt 5.2 auf den Nachbau aus, den die
    Referenzimplementierung verwendet.
-   Die Quellen von `CommonLibF4` übersetzen als schlichte CMake-Static-Library mit den im Shim
    gesetzten Optionen. Die Referenzimplementierung belegt dies für ihren Upstream-Stand.
-   Fallout 4 AE 1.11.240 meldet sich über `F4SE::LoadInterface::RuntimeVersion()` als
    `{1, 11, 240, 0}`.

## 9. Übergabe an Teilprojekt B

A hinterlässt für B: einen bauenden CMake-Baum mit eingebundenem commonlibf4, ein Plugin, das
nachweislich geladen wird, einen funktionierenden Logkanal, einen belegten Nachrichtenweg, und
einen eigenen commonlibf4-Fork, in dem die fehlenden Strukturen (`BSRenderPass`, benanntes
`RENDER_TARGET`-Enum) ergänzt werden können. B schaltet als Erstes das Trampolin ein.
