# Teilprojekt D2 — Paketierung

Spec, Stand 2026-08-31. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A, B1, B2, C und D1 voraus, insbesondere `2026-08-30-fallout4-feature-framework-design.md`.

## 1. Kontext und Ziel

D1 hat Features zur Laufzeit gebracht — an- und abschaltbar, mit persistenten Einstellungen. Einen
Weg, sie an jemanden auszuliefern, gibt es nicht. Der Bau kopiert die DLL und `package/Shaders/FO4`
in eine Spielinstallation, wenn `FO4CS_DEPLOY_DIR` gesetzt ist; das ist alles, und es funktioniert
nur auf der Maschine, die gebaut hat.

**Ziel:** Drei installierbare Archive, die aus einem Bau entstehen und deren Inhalt geprüft ist.

**Abnahmekriterium der Roadmap:** Ein ausgeliefertes Archiv installiert sich in ein sauberes Spiel.

### Warum D2 vor E

Die Roadmap ließ D2 hinter F rutschen, weil Paketierung erst lohne, wenn es echte Features zu
paketieren gebe. Das ist am 2026-08-31 umgekehrt worden. Der Grund trägt in beide Richtungen: eine
funktionierende Auslieferung früh zu haben heißt, dass jedes Feature ab F automatisch mitfährt,
statt am Ende in eine gewachsene Struktur nachgerüstet zu werden. Der Preis ist, dass die
Aufteilung in Basis und Addons heute an zwei Features geprüft wird statt an zwanzig.

### Was die Vorerkundung ergeben hat

| Sachverhalt                      | Befund                                                                                                                                        |
| -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| Skyrims Paketierung              | rund 100 Zeilen `add_custom_command` an `POST_BUILD`, gesteuert über `ZIP_TO_DIST` und `AIO_ZIP_TO_DIST`                                      |
| Aufteilung Basis/Addon           | Markerdatei `features/<Name>/CORE`: mit Marker in die Basis, ohne Marker ein eigenes Archiv, dessen Wurzel das Verzeichnis ist                |
| Inhalt eines Addons              | **nur Assets.** Feature-Code steckt immer in der DLL, auch bei Skyrim CS                                                                      |
| Geerbte Feature-Verzeichnisse    | 40 Stück, davon 27 mit `CORE` — durchweg Skyrim-Inhalt mit Skyrim-Shaderpfaden                                                                |
| `package/`                       | 184 Dateien, davon **eine** unsere: `Shaders/FO4/ImagespaceCopy.hlsl`. Der Rest ist `Interface/`, `SKSE/` und 50 Skyrim-HLSL                  |
| Skyrims Archivnamen              | UTC-Zeitstempel, **keine** Version. Die Ini-Version je Feature floss in die Menü-Anzeige, nicht in den Dateinamen                             |
| `dist/`                          | steht bereits in `.gitignore`                                                                                                                 |
| `tools/feature_version_audit.py` | liest `MOD_ID`, `GetFeatureModLink` und `GetFeatureSummary` aus den Skyrim-Feature-Headern — Felder, die unsere Basisklasse bewusst nicht hat |
| Lizenztexte                      | `COPYING` (GPL-3.0) und `EXCEPTIONS.md` (Modding-Ausnahme) liegen im Wurzelverzeichnis                                                        |

## 2. Umfang

### In D2 enthalten

-   `tools/package.ps1`: die eine Regel, wie aus dem Repo ein Mod-Baum wird, plus das Packen.
-   Ein CMake-Ziel `package`, das nicht in `ALL` hängt.
-   Umstellung des bestehenden Deploy-Schritts auf dieselbe Regel.
-   `package/Features/<Name>/` für die beiden Features aus D1; Umzug von `ImagespaceCopy.hlsl`.
-   Rückkehr des `CORE`-Markers als Entscheidung Basis gegen Addon.
-   `COPYING`, `EXCEPTIONS.md` und eine neue `package/README.md` in die Basis.
-   `tools/verify-package.ps1` mit Prüfungen im Stil von `verify-plugin.ps1`.

### Nicht in D2 enthalten

-   Ini-Versionierung je Feature und `feature_version_audit.py`. Beide bleiben ruhend: der Audit
    liest Felder, die es bei uns nicht gibt, und wäre neu zu schreiben statt zu portieren.
-   CI-Workflows, Nexus-Upload, semantic-release, Release-Zweige. Die kehren zurück, wenn der Port
    ausliefert.
-   7-Zip und das `.7z`-Format.
-   Release-Tags und daraus abgeleitete Archivnamen.
-   Die 40 geerbten Feature-Verzeichnisse. Sie bleiben, was `CLAUDE.md` über sie sagt: Rohmaterial
    für F, kein aktiver Feature-Satz. **Kein Archiv enthält je etwas daraus.**
-   `README.md` des Repos, das noch den Stand von Teilprojekt A beschreibt. Eigener Schritt.
-   Themes und Schriften unter `package/Interface` und `package/SKSE`. Skyrim-Erbe, das E zu
    entscheiden hat.

## 3. Vorentscheidungen

| Frage                    | Entscheidung                                      | Begründung                                                                                                                                                          |
| ------------------------ | ------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Aufbau                   | Basis, je Addon eines, plus AIO                   | Die Struktur, in die F hineinwächst. Sie jetzt zu bauen heißt, sie später nicht nachrüsten zu müssen                                                                |
| Basis gegen Addon        | Markerdatei `CORE` im Feature-Verzeichnis         | Wie bei Skyrim CS, und die Entscheidung steht dort, wo der Inhalt steht. Holt den `CORE`-Marker aus dem Ruhestand zurück                                            |
| Wo Feature-Assets liegen | `package/Features/<Name>/`, **nicht** `features/` | Siehe 4.1. Ein Glob über `features/*` griffe die 40 geerbten Skyrim-Verzeichnisse ab; eine Ausschlussliste müsste mit `RegisterAll` synchron gehalten werden        |
| Versionierung            | Projektversion aus CMake, keine Feature-Inis      | Zwei Features brauchen keine unabhängigen Versionen. Der Audit käme sonst als Neuschrift mit                                                                        |
| Archivname               | `<Name>-<Version>-<SHA>[-dirty].zip`              | Wir haben keine Versions-Tags; `git describe --tags` liefert heute `skyrim-base-42-g…`. Mit dem SHA ist jeder Name eindeutig einem Commit zuzuordnen, ohne Tags     |
| Format                   | ausschließlich `.zip` über `cmake -E tar`         | Skyrims 7-Zip-Zweig existierte für große Texturpakete. Unsere Nutzlast sind Kilobytes. Ein Format heißt ein Codepfad und keine Abhängigkeit von installierter 7-Zip |
| Auslöser                 | eigenes Ziel `package`, nicht `POST_BUILD`        | Der Iterationsbau ist der schnelle Weg ins Spiel und bleibt unangetastet. Skyrim schrieb bei jedem Bau drei Archive neu                                             |
| Wo die Logik wohnt       | PowerShell-Skript, CMake reicht nur Pfade hinein  | Eine Kette aus `add_custom_command` ist auf dem Host nicht prüfbar — man sieht erst am Archiv, ob sie stimmt. Ein Skript lässt sich einzeln aufrufen und brechen    |
| Deploy                   | ruft dasselbe Skript mit `-Stage`                 | Sonst gibt es die Regel zweimal, und was gespielt wird läuft von dem weg, was ausgeliefert wird                                                                     |
| PDB                      | fährt in der Basis mit                            | Addictols Crashlogs benennen damit unsere Funktionen statt Adressen. In diesem Portierungsstadium mehr wert als zwei Megabyte                                       |
| Verworfen: CPack         | —                                                 | Sein Komponentenmodell bildet „dieser Verzeichnisbaum ist die Archivwurzel" schlecht ab, und die Dateinamen sind schwer frei zu wählen                              |

## 4. Architektur

### 4.1 Wo Feature-Assets liegen

Der Entwurf sah zunächst `features/<Name>/` vor, wie bei Skyrim CS. Das trägt hier nicht: unter
`features/` liegen 40 geerbte Skyrim-Verzeichnisse, 27 davon mit `CORE`-Marker. Ein Glob über
`features/*` packte sie alle ein. Die Alternative wäre eine ausdrückliche Liste der Verzeichnisse,
die ausgeliefert werden — aber die müsste mit `RegisterAll` in `src/Feature/FeatureSystem.cpp`
von Hand synchron gehalten werden, und nichts würde ein Auseinanderlaufen bemerken.

Deshalb liegen die Assets unserer Features unter **`package/Features/<Name>/`**. Damit gilt:

-   Ein Glob über `package/Features/*` ist sicher, weil dort nur steht, was wir hingelegt haben.
-   `features/` behält genau die Rolle, die `CLAUDE.md` ihm zuschreibt: Rohmaterial für F.
-   `package/` ist ohnehin der Baum „was ausgeliefert wird". Feature-Assets gehören dorthin.

### 4.2 Der Staging-Baum

Der Mod-Baum entsteht aus vier benannten Quellen. `package/` wird **nie** als Ganzes kopiert.

| Quelle                                             | Ziel im Baum                 |
| -------------------------------------------------- | ---------------------------- |
| gebaute DLL und PDB                                | `F4SE/Plugins/`              |
| `COPYING`, `EXCEPTIONS.md`, `package/README.md`    | Wurzel                       |
| `package/Shaders/FO4/**`                           | `Shaders/FO4/`               |
| `package/Features/<Name>/**` ohne die Datei `CORE` | Wurzel, Struktur unverändert |

Die Archivwurzel **ist** `Data`. Es gibt keinen Ordner dieses Namens im Archiv — Vortex und MO2
packen die Wurzel nach `Data` aus.

`ImagespaceCopy.hlsl` zieht dafür von `package/Shaders/FO4/` nach
`package/Features/ImagespaceTint/Shaders/FO4/`. `package/Shaders/FO4/` bleibt als Ort für
paketweite Shader bestehen und ist danach leer.

Drei Bäume entstehen:

-   **Basis** — DLL, PDB, Lizenztexte, Liesmich, `package/Shaders/FO4/**` und jedes Feature **mit**
    `CORE`.
-   **Je Addon einer** — genau ein Feature **ohne** `CORE`.
-   **AIO** — Basis plus alle Addons.

### 4.3 `tools/package.ps1`

```pwsh
param(
    [Parameter(Mandatory)] [string] $SourceRoot,   # Repo-Wurzel
    [Parameter(Mandatory)] [string] $PluginFile,   # gebaute DLL
    [Parameter(Mandatory)] [string] $Version,      # aus project()
    [string] $PdbFile,
    [string] $CMake,                               # für cmake -E tar
    [string] $Stage,                               # nur bauen, nicht packen
    [string] $WorkDir,                             # Zwischenbäume, im Bauverzeichnis
    [string] $OutDir                               # Standard <SourceRoot>/dist
)
```

Mit `-Stage <Ziel>` baut das Skript den **AIO**-Baum an das genannte Ziel und hört auf. Das ist
das, was eine Spielinstallation braucht: alles auf einmal. In dieser Betriebsart wird nicht
gepackt, `-CMake` also nicht gebraucht; ohne `-Stage` ist es Pflicht. `-WorkDir` steht ohne Angabe
auf einem temporären Verzeichnis.

Ohne `-Stage` baut es die drei Bäume unter `-WorkDir` und packt jeden nach `-OutDir`. `dist/` wird
vorher geleert, damit kein Archiv aus einem früheren Commit liegenbleibt und beim Hochladen
versehentlich mitfährt.

Gepackt wird über `& $CMake -E tar cf <ziel>.zip --format=zip -- .` aus dem jeweiligen Baum heraus.
Der Pfad zu `cmake` kommt von CMake selbst; das Skript sucht ihn nicht.

### 4.4 Das CMake-Ziel und der Deploy-Schritt

```cmake
add_custom_target(package
    COMMAND pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/package.ps1"
            -SourceRoot "${CMAKE_SOURCE_DIR}"
            -PluginFile "$<TARGET_FILE:${PROJECT_NAME}>"
            -PdbFile "$<TARGET_PDB_FILE:${PROJECT_NAME}>"
            -Version "${PROJECT_VERSION}"
            -CMake "${CMAKE_COMMAND}"
            -WorkDir "${CMAKE_CURRENT_BINARY_DIR}/package"
    DEPENDS ${PROJECT_NAME}
    VERBATIM
)
```

Der bestehende Deploy-Schritt verliert seine beiden `copy`-Aufrufe und ruft stattdessen dasselbe
Skript mit `-Stage "${FO4CS_DEPLOY_DIR}"`. Damit gibt es die Regel einmal, und die
Spielinstallation ist Zeichen für Zeichen das, was im AIO-Archiv steckt.

### 4.5 `tools/verify-package.ps1`

Im Stil von `verify-plugin.ps1`: eine Zeile `ok` oder `FAIL` je Prüfung, am Ende die Zahl der
Beanstandungen, Rückgabewert ungleich null wenn eine bleibt. Es liest die Archive über
`System.IO.Compression.ZipFile`, ohne sie auszupacken.

## 5. Was in welches Archiv geht

Mit dem Stand von D1 — `FrameCounter` ohne Assets und mit `CORE`, `ImagespaceTint` mit einem
Shader und ohne `CORE`:

```
dist/CommunityShadersFO4-0.1.0-g47466a1.zip
  COPYING
  EXCEPTIONS.md
  README.md
  F4SE/Plugins/CommunityShadersFO4.dll
  F4SE/Plugins/CommunityShadersFO4.pdb

dist/ImagespaceTint-0.1.0-g47466a1.zip
  Shaders/FO4/ImagespaceCopy.hlsl

dist/CommunityShadersFO4-AIO-0.1.0-g47466a1.zip
  COPYING
  EXCEPTIONS.md
  README.md
  F4SE/Plugins/CommunityShadersFO4.dll
  F4SE/Plugins/CommunityShadersFO4.pdb
  Shaders/FO4/ImagespaceCopy.hlsl
```

**Was das über die Beweislage sagt:** `FrameCounter` hat keine Assets, trägt also nichts zur Basis
bei. Der `CORE`-Pfad wird begangen, aber mit null Dateien. Die Basis hat trotzdem echten Inhalt —
Lizenztexte und Liesmich — sodass das Basis-Archiv für sich genommen sinnvoll ist. Dass ein Feature
**mit** Assets in die Basis wandert, prüft erst F.

## 6. Fehlerbehandlung

| Fall                                 | Verhalten                                                                                               |
| ------------------------------------ | ------------------------------------------------------------------------------------------------------- |
| DLL fehlt                            | Abbruch mit dem Pfad, der fehlt. Kann über das Ziel nicht passieren, wird trotzdem geprüft              |
| PDB fehlt                            | Warnung, Archiv entsteht ohne sie. Ein anderer Generator muss nicht scheitern                           |
| `package/Features/<Name>/` ist leer  | kein Archiv, eine Zeile im Protokoll. Ein leeres Addon-Archiv wäre eine Zumutung                        |
| kein Feature ohne `CORE`             | Basis und AIO entstehen, kein Addon. Kein Fehler                                                        |
| `dist/` enthält Altes                | wird vor dem Schreiben geleert                                                                          |
| `pwsh` fehlt                         | das Ziel scheitert mit CMakes eigener Meldung. `pwsh` ist bereits Voraussetzung für `verify-plugin.ps1` |
| Git nicht verfügbar oder kein Commit | der SHA-Teil des Namens entfällt, das Archiv heißt `<Name>-<Version>.zip`                               |

## 7. Tests und Abnahme

### 7.1 Ohne Spiel prüfbar

`verify-package.ps1` prüft:

| Prüfung          | Worauf sie ansetzt                                                                |
| ---------------- | --------------------------------------------------------------------------------- |
| Vollzähligkeit   | die drei Archive existieren und lassen sich öffnen                                |
| Basis-Inhalt     | DLL, PDB, `COPYING`, `EXCEPTIONS.md`, `README.md` sind drin                       |
| Addon-Inhalt     | genau `Shaders/FO4/ImagespaceCopy.hlsl`, sonst nichts                             |
| AIO              | ist die Vereinigung von Basis und Addons, ohne Dubletten                          |
| kein Marker      | in keinem Archiv steckt eine Datei `CORE`                                         |
| kein Skyrim-Erbe | kein `SKSE/`, kein `Interface/`, keine der 50 HLSL-Dateien aus `package/Shaders/` |
| Archivwurzel     | kein Eintrag beginnt mit `Data/` — die Wurzel **ist** `Data`                      |
| Staging-Regel    | `-Stage` in ein temporäres Verzeichnis ergibt denselben Baum wie das AIO-Archiv   |

Die letzte Prüfung braucht keine Archive und ist damit die, die beim Ändern der Regel zuerst
anschlägt.

Jede Prüfung wird nach dem Grünwerden absichtlich gebrochen, erwarteter Fehlschlag vorher benannt.

### 7.2 Nur im Spiel prüfbar

Dass Vortex ein Archiv mit `Data` als Wurzel richtig einsortiert, und dass das Ergebnis startet.

### 7.3 Abnahmekriterien

1.  `cmake --build --preset FO4 --target package` schreibt drei Archive nach `dist/`.
2.  Ein Bau **ohne** dieses Ziel schreibt keine Archive und ist nicht messbar langsamer als heute.
3.  `verify-package.ps1` meldet null Beanstandungen.
4.  Das AIO-Archiv über Vortex in das Spiel installiert: das Spiel startet, der Stich ist da.
5.  Das Basis-Archiv allein installiert: das Spiel startet, der Stich fehlt, das Log meldet
    `ImagespaceTint: running` und danach, dass die Shaderdatei nicht gefunden wurde.
6.  Die acht Host-Tests aus A bis D1 bleiben grün.

Kriterium 5 ist der eigentliche Prüfstein der Aufteilung: es belegt, dass Basis und Addon
tatsächlich trennbar sind und dass das Fehlen eines Addons kein Absturz ist, sondern eine Logzeile.

## 8. Annahmen, die D2 bestätigen muss

-   Dass `cmake -E tar cf --format=zip` Archive schreibt, die Vortex und MO2 ohne Murren lesen.
-   Dass Vortex ein Archiv mit `F4SE/` und `Shaders/` in der Wurzel als Data-relativ erkennt und
    nicht nach einem Ordner `Data` verlangt.
-   Dass der `pwsh`-Start im Deploy-Schritt die Bauzeit nicht spürbar verschlechtert. Das ist zu
    **messen**, nicht zu schätzen; fällt es auf, wird der Deploy-Schritt auf die alten
    `copy`-Aufrufe zurückgedreht und die doppelte Regel in Kauf genommen.
-   Dass `ImagespaceTint` das Fehlen seiner Shaderdatei als Logzeile behandelt und nicht als
    Absturz. `LoadSource` gibt ein `std::expected` zurück und C hat den Fall protokolliert, aber
    mit einem installierten Shader — ohne jeden ist er ungeprüft.

    Beim Nachlesen von `WatcherLoop` fällt dabei etwas auf, das Kriterium 5 sichtbar machen wird:
    `loadedOnce` wird auch dann gesetzt, wenn `CompileAndPublish` an der fehlenden Datei
    scheitert, und weil `_watch.Reset` in diesem Zweig nicht mehr erreicht wird, bleibt der
    Watcher leer und `Poll` für immer falsch. Wer das Addon **nachträglich** installiert, bekommt
    seinen Shader also erst nach einem Neustart des Spiels. Ob D2 das mitrepariert oder als
    Befund an F weitergibt, entscheidet sich, wenn Kriterium 5 gelaufen ist — vorher ist es eine
    Lesart des Quelltextes, keine Messung.

## 9. Übergabe

-   **Roadmap:** Zeile D2 auf abgeschlossen; festhalten, dass D2 vor E gezogen wurde und warum.
-   **`CLAUDE.md`:** Die Zeile „Packaging, AIO archives, `dist/`, feature `.ini` version audit"
    verliert alles außer dem Ini-Audit, der ruhend bleibt. Der `CORE`-Marker ist zurück und gehört
    beschrieben, die Release-Stufen bleiben ruhend. Dazu ein Absatz, wo Feature-Assets liegen.
-   **Für E:** `package/Interface` und `package/SKSE` sind unangetastetes Skyrim-Erbe. Bevor davon
    etwas ausgeliefert wird, muss E entscheiden, was davon für Fallout 4 überhaupt gilt.
-   **Für F:** Jedes portierte Feature bekommt `package/Features/<Name>/` für seine Assets, mit
    einer Datei `CORE`, wenn es in die Basis gehört. Sonst ist nichts zu tun — Archive entstehen
    von selbst.
