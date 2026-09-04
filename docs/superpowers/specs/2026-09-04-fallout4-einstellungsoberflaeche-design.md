# Teilprojekt E2 — Einstellungsoberfläche

Spec, Stand 2026-09-04. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A, B1, B2, C, D1, D2 und E1 voraus, insbesondere
`2026-08-31-fallout4-overlay-eingabe-design.md` und
`2026-08-30-fallout4-feature-framework-design.md`.

## 1. Kontext und Ziel

E1 hat ein Overlay geliefert, das sich per Taste öffnet, die Spieleingabe anhält und einen Knopf
zeichnet, der reagiert. Es zeigt eine Bildnummer und den Satz „The feature list arrives with
subproject E2." Bedient werden die Features nach wie vor, indem der Nutzer
`CommunityShadersFO4.json` von Hand bearbeitet und das Spiel neu startet.

**Ziel:** Eine Oberfläche im Overlay, die alle Einstellungen zeigt, sie ändern lässt und die
Änderung auf die Platte bringt — dazu die Schrift, das Theme und die Sprache, in denen das
geschieht.

**Abnahmekriterium der Roadmap:** „Einstellungen im Overlay ändern, sie überleben einen Neustart."

E2 bleibt als Ganzes zugeschnitten und umfasst damit alle fünf Posten der Roadmap-Zeile:
Featureliste, Schreiben von Einstellungen, Themes, Schriften und i18n. Der Zuschnitt wurde am
2026-09-04 ausdrücklich bestätigt, nachdem eine Teilung in Bedienbarkeit und Erscheinungsbild
vorgeschlagen worden war.

### Was E2 aus E1 und D1 erbt

| Sachverhalt                 | Befund                                                                                                                                                                                                                                                    |
| --------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Features::Settings`        | kann heute **nur lesen**. `DeclareBool`, `DeclareUInt32`, `GetBool`, `GetUInt32`, `Init`, `IsEnabled`, `ReloadIfChanged`. Kein `Set`, kein `Save`                                                                                                         |
| Einstellungspfade           | JSON-Pointer, nicht Punkt-Pfade. `"Block/Key"`, genau ein Schrägstrich, keine Hälfte leer (D1)                                                                                                                                                            |
| Ganze Zahlen                | `REX::TJsonSetting<T>` liest für **keinen** Ganzzahltyp aus der Datei. `glz::get` vergleicht per `std::same_as`, `glz::generic` führt jede JSON-Zahl als `double`. `DeclareUInt32` legt daher intern `TJsonSetting<double>` an und klemmt beim Lesen (E1) |
| Fehlende Schlüssel          | `glz::set` bricht bei `value.find(key) == end()` ab (`glaze/core/seek.hpp:238`). `FJsonSettingStore::Save()` kann folglich **keine** Schlüssel anlegen. In der installierten Fassung nachgeprüft                                                          |
| `FJsonSettingStore::Save()` | liest `m_fileBase` nach `glz::generic`, ruft `setting->Save(&output)` je Eintrag, schreibt mit `prettify` und `indentation_width = 4` zurück                                                                                                              |
| `glz::generic`              | `operator[](string-like)` **legt Schlüssel an** (`glaze/json/generic.hpp:201`), `contains` ist vorhanden. Ein eigener Schreiber kann, was `glz::set` nicht kann                                                                                           |
| `Features::Registry`        | kennt `StateOf`, `Count`, `ClearRefusals` — aber **keine Aufzählung**                                                                                                                                                                                     |
| `Features::Feature`         | vier Methoden: `Name`, `Setup`, `Frame`, `Shutdown`. Keine davon zeichnet oder deklariert                                                                                                                                                                 |
| `Util::FileWatch`           | zeitstempelbasiert, `Reset` setzt die Basislinie neu. Kein `Rebase` für den unveränderten Satz                                                                                                                                                            |
| `WindowHook`                | nimmt drei Rückrufe: „ist das die Umschalttaste", „merke den Wunsch", „ist offen"                                                                                                                                                                         |
| Startreihenfolge            | `Menu::StartSystem()` vor `Features::StartSystem()`, weil letzteres `Settings::Init()` ruft und eine Deklaration davor liegen muss                                                                                                                        |
| ImGui 1.92.6                | `IsItemDeactivatedAfterEdit()` vorhanden. dx11-Backend meldet `ImGuiBackendFlags_RendererHasTextures` — Glyphen laden bei Bedarf, `PushFont(font, size)` skaliert zur Laufzeit, Glyphenbereiche sind gegenstandslos                                       |
| `Plugin::VERSION`           | vorhanden, wird schon an `F4SE::PluginVersionData` gereicht                                                                                                                                                                                               |
| Geerbter Bestand            | 9 Übersetzungsdateien (2221 englische Schlüssel, alle `cs_editor.*` und `feature.*` aus Skyrim), 6 Theme-Dateien, ~40 Icons, ~50 Schriftschnitte — unter `package/Interface` und `package/SKSE`                                                           |
| `I18n` aus `skyrim-base`    | `src/I18n/I18n.{h,cpp}`, 413 Zeilen, auf `nlohmann/json`. `T(key, default)`, Sprachumschaltung zur Laufzeit, Rückfall auf Englisch, `shared_mutex`                                                                                                        |

## 2. Umfang

### In E2 enthalten

-   Ein Schema für Einstellungen: Art, Vorgabewert, Beschriftung, Erklärtext, Wertebereich,
    Auswahlliste — deklariert vom Feature selbst.
-   Der Schreibweg: `Set*`, `Save()`, ein eigener Schreiber auf `glz::generic`.
-   Das Nachrüsten fehlender Schlüssel in einer bestehenden Einstellungsdatei.
-   `Features::Registry::ForEach` und `Features::Feature::Declare`.
-   Das Panel: Featureliste mit Zustand, allgemeine Einstellungen, Rückweg auf die Vorgabewerte.
-   Tastenaufnahme für die Umschalttaste, über die Fensterprozedur.
-   i18n: der Motor auf glaze, `T()`, Sprachumschaltung zur Laufzeit, ein Bestand, der bei null
    beginnt, und `tools/extract-i18n.py`.
-   Eine ausgelieferte Schrift in wählbarer Größe und ein Theme im Quelltext.
-   Die Auslieferung von `package/F4SE/Plugins/CommunityShadersFO4/`, und damit die Antwort auf
    die von D2 offen gelassene Frage nach `package/Interface` und `package/SKSE`.

### Nicht in E2 enthalten

-   Ein Theme-Lader, ein Schriftverzeichnis-Scanner, Icons. Das Theme steht im Quelltext, genau
    eine Schriftfamilie wird ausgeliefert.
-   Der geerbte Übersetzungsbestand. Er wird **nicht** übernommen.
-   Ein Formatierer für Übersetzungen (benannte Platzhalter). Kommt, wenn die erste Zeichenkette
    ihn braucht.
-   Feature-Kategorien, Suchfeld, Nexus-Verweise, „restart required"-Kennzeichnung. Das sind
    Skyrim-CS-Bestandteile, die erst mit einer Featureliste in zweistelliger Länge etwas nützen —
    also nicht vor F.
-   Ein Seitenleisten-Layout. Bei zwei Features wäre es leere Fläche.
-   Feature-`.ini`-Versionen und ihr Audit, Release-Stufen, Constraints. Bleiben ruhend, siehe
    „Temporarily moot" in `CLAUDE.md`.

## 3. Vorentscheidungen

Am 2026-09-04 getroffen und nicht neu aufzurollen:

1.  **E2 bleibt ungeteilt.** Alle fünf Posten in einer Spec.
2.  **Die Oberfläche ist deklarativ, nicht gezeichnet.** Ein Feature beschreibt seine
    Einstellungen; das Menü zeichnet daraus. Kein Feature bindet ImGui ein. Damit fallen das
    Nachrüsten fehlender Schlüssel und die i18n-Schlüssel aus derselben Quelle, und die
    Oberfläche wird ohne Spiel prüfbar. Das Gegenmodell — Skyrims `virtual void DrawSettings()` —
    ist verworfen, weil es zwei Wahrheiten über dieselbe Einstellung führt.
3.  **Das Feature deklariert selbst.** `Feature` bekommt eine fünfte Methode `Declare()`. Zentral
    zu deklarieren hieße, in `FeatureSystem.cpp` den gesamten Oberflächentext aller Features aus
    F+ zu sammeln.
4.  **Geschrieben wird sofort, sobald das Bedienelement losgelassen wird.** Kein Speichern-Knopf,
    kein Sammeln bis zum Schließen. Fallout 4 stürzt ab, und ein Absturz darf höchstens die
    Bewegung kosten, die gerade lief.
5.  **i18n kommt jetzt, mit leerem Bestand.** Der Motor wird portiert, solange es fünf
    Zeichenketten gibt statt zweitausend; nachzurüsten hieße, jede Zeichenkettenstelle noch
    einmal anzufassen. Der geerbte Bestand beschreibt eine Oberfläche, die es bei uns nicht gibt.
6.  **Eine Schrift, ein eingebautes Theme.** Der geerbte Bestand an Themes, Icons und weiteren
    Schriften bleibt ungenutzt liegen.

## 4. Architektur

### 4.1 Modulschnitt

`src/Feature/FeatureSettings.{h,cpp}` geht in ein eigenes System `src/Settings/` auf. Zwei Gründe:
die Datei hat heute 315 Zeilen und bekäme Metadaten, Aufzählung, Schreiber und Nachrüstung dazu;
und sie heißt „Feature", obwohl `Menu` und `I18n` sie genauso benutzen.

| Datei                     | Zuständigkeit                                                                      |
| ------------------------- | ---------------------------------------------------------------------------------- |
| `src/Settings/Settings.h` | die öffentliche Fläche. Alles, was Features, Menü und i18n rufen                   |
| `src/Settings/Internal.h` | die Eintragstabelle, geteilt von den beiden `.cpp`                                 |
| `src/Settings/Schema.cpp` | Deklaration, Metadaten, Aufzählung, Vorgabewerte. Ohne REX, ohne Datei, ohne ImGui |
| `src/Settings/Store.cpp`  | REX-Bindung, Laden, Setzen, Schreiben, Nachrüsten, Dateibeobachtung                |

Die Trennung ist keine Kosmetik: `Schema` ist das, was das Panel liest, und es ist ohne jede
Datei und ohne jedes REX-Singleton prüfbar.

### 4.2 Die Deklaration

Vier Arten. Der Typ allein reicht nicht — `Menu/toggleKey` ist ein Tastencode, für den weder ein
Schieberegler noch ein Zahlenfeld das richtige Bedienelement ist.

| Art      | Deklaration                              | Speichertyp        | Bedienelement                      |
| -------- | ---------------------------------------- | ------------------ | ---------------------------------- |
| `Bool`   | `DeclareBool(pfad, vorgabe)`             | `bool`             | `ImGui::Checkbox`                  |
| `Slider` | `DeclareSlider(pfad, vorgabe, min, max)` | `double`           | `ImGui::SliderFloat`               |
| `Choice` | `DeclareChoice(pfad, vorgabe, liste)`    | `std::string`      | `ImGui::Combo`                     |
| `Key`    | `DeclareKey(pfad, vorgabe)`              | `double`, geklemmt | Knopf, nimmt den nächsten Anschlag |

Ganze Zahlen liegen als `double` in der Datei und werden beim Lesen geklemmt — die Falle aus E1
gilt unverändert und ist der Grund, warum `Slider` und `Key` beide auf `double` speichern.

**Was aus der bestehenden Fläche wird.** `DeclareBool` bleibt und ist die Art `Bool`.
`DeclareUInt32` **entfällt** und geht in `DeclareSlider` und `DeclareKey` auf — es hatte keinen
Aufrufer außer `Menu/toggleKey`, und der wird zu `DeclareKey`. Gelesen wird über `GetBool`,
`GetDouble`, `GetUInt32` (für `Key`, geklemmt wie bisher) und `GetString`; geschrieben über die
gleichnamigen `Set*`.

Aufgezählt wird über zwei Funktionen, die das Panel und der Schreiber teilen:

```cpp
void ForEachBlock(const std::function<void(std::string_view a_block)>&);
void ForEachEntry(std::string_view a_block, const std::function<void(const Entry&)>&);
```

`Entry` ist die Sicht auf einen Eintrag: Pfad, Art, Beschriftung, Erklärtext, Grenzen,
Auswahlliste, Vorgabewert und ob er der Einschalter des Blocks ist.

Jede Deklaration liefert ein `Handle` mit einer Kette:

```cpp
Settings::DeclareKey("Menu/toggleKey", 0x23)
    .Label("setting.menu.toggle_key", "Toggle key")
    .Help("setting.menu.toggle_key.help", "Opens and closes this overlay.");
```

Das `Handle` ist eine Sicht auf den Eintrag in der Tabelle, kein Besitzer. Es ist nur bis zum
Ende des Deklarationsausdrucks zu verwenden. Die Tabelle bleibt weiterhin knotenbasiert, weil
REX übergebene Zeichenketten als `string_view` behält (D1) — ein `vector` machte bei jedem
Wachsen jede Einstellung ungültig.

`DeclareFeature(name, vorgabe)` bleibt die Kurzform für `"<name>/enabled"` und markiert den
Eintrag zusätzlich als **Einschalter**. Das Panel zieht ihn aus der Einstellungsliste des
Features heraus, weil er die Checkbox der Überschrift ist und nicht eine Zeile darunter.

Ein missgebildeter Pfad wird wie bisher mit einer Logzeile abgelehnt, nicht angenommen.

### 4.3 Der Schreibweg

**Ein Schreiber, nicht zwei.** `FJsonSettingStore::Save()` wendet die Einstellungen über
`glz::set` an und kann deshalb keine Schlüssel anlegen; für die Nachrüstung bräuchte es daneben
einen zweiten Schreiber, und zwei Schreiber auf einer Datei sind zwei Formate. `Store` schreibt
stattdessen selbst:

1.  Bestehende Datei nach `glz::generic` einlesen, falls vorhanden und lesbar.
2.  Jeden deklarierten Pfad setzen oder anlegen — `root[block][key] = wert`.
3.  Mit `glz::write_file_json` und `prettify`, Einrückung 4, zurückschreiben.

Weil auf dem eingelesenen Baum aufgesetzt wird, bleiben **unbekannte Schlüssel erhalten**. Weil
`operator[]` anlegt, entstehen **fehlende Schlüssel**.

Das ersetzt zugleich `WriteDefaultFile`, das die JSON heute per `std::format` zusammenbaut, ohne
jede Maskierung. Mit `Menu/language` bekommt E2 die erste Einstellung vom Typ `std::string` —
also den ersten Wert, bei dem das aufginge.

**Nachrüsten.** `Init` prüft nach dem Laden, ob jeder deklarierte Pfad in der Datei steht. Fehlt
einer, wird einmal geschrieben. Vorhandene Werte bleiben, die fehlenden entstehen mit ihrem
Vorgabewert. Damit ist der in E1 vermerkte Mangel erledigt, ohne eigenen Migrationsapparat.

**Speichern und Rückkopplung.** `Set*` schreibt über `TSetting::SetValue` in die REX-Einstellung
— `Get*` sieht es sofort — und merkt sich „verändert". Der Aufrufer ruft `Save()`, wenn ImGui
`IsItemDeactivatedAfterEdit()` meldet.

`Save()` schreibt die Datei und setzt danach die Basislinie der `FileWatch` neu. Ohne das sähe
der nächste `Poll` unseren eigenen Schreibvorgang als fremde Änderung und setzte jedes Feature
neu auf. `Util::FileWatch` bekommt dafür ein `Rebase()`, das die Zeitstempel des unveränderten
Satzes erneuert.

`ReloadIfChanged()` wird zu `ConsumeChanged()`: wahr, wenn die Datei sich geändert hat **oder**
seit dem letzten Aufruf etwas gesetzt wurde. Beides ist derselbe Anlass, Verweigerungen noch
einmal zu versuchen — ein abgeschaltetes und wieder eingeschaltetes Feature soll seine Chance
bekommen.

Weil `Menu::TickSystem` nach `Features::TickSystem` läuft, wirkt eine Änderung im Overlay einen
Frame später. Bei 60 Hz ist das unsichtbar und der Preis dafür, dass das Overlay über allem
liegt, was die Features gezeichnet haben.

### 4.4 Die Aufzählung

`Features::Registry` bekommt

```cpp
void ForEach(const std::function<void(std::string_view a_name, State a_state)>&) const noexcept;
```

Name und Zustand, nicht das `Feature`-Objekt. Das Panel braucht nicht mehr, und die Registry gibt
nichts preis, was sie besitzt.

`Feature` bekommt `virtual void Declare() {}`. `Registry::Register` ruft es beim Registrieren
auf. Die Startreihenfolge wird damit bindend:

```
Menu::StartSystem()            deklariert Menu/*
Features::StartSystem()
    RegisterAll()              registriert und ruft je Feature Declare()
    Settings::Init()           schreibt oder ergänzt die Datei, lädt sie
Render::InstallSwapChainHook() ab hier läuft Present
```

Die bestehende Begründung in `XSEPlugin.cpp` — Deklaration vor `Init`, Features vor dem Hook —
bleibt gültig und wird nur um `Declare()` ergänzt.

### 4.5 Das Panel

`src/Menu/SettingsPanel.{h,cpp}`. Ein Fenster, ein Scrollbereich, keine Seitenleiste.

```
┌─ Community Shaders ────────────────── [x] ─┐
│ Community Shaders for Fallout 4  0.1.0     │
│ Frame 148230                               │
├────────────────────────────────────────────┤
│ ▼ General                                  │
│     Language        [ English      ▾]      │
│     Font size       [────●────] 18.0       │
│     Toggle key      [ End           ]      │
├────────────────────────────────────────────┤
│ ▼ Features                                 │
│   [x] Imagespace Tint            running   │
│   [ ] Frame Counter              off       │
├────────────────────────────────────────────┤
│              [ Restore defaults ] [ Close ]│
└────────────────────────────────────────────┘
```

**Was „General" ist:** die Schema-Blöcke, zu denen **kein** Feature gleichen Namens registriert
ist. Heute genau `Menu`. Der Abgleich läuft über `ForEach`; kein Feature muss sich irgendwo als
„hat eine Oberfläche" anmelden.

**Warum der Zustand rechts steht:** `refused` ist der einzige Weg, auf dem ein Spieler erfährt,
dass sein Häkchen nichts bewirkt hat. Ohne die Anzeige steht das nur im Log.

**Der Rückweg.** „Restore defaults" setzt jede deklarierte Einstellung auf ihren Vorgabewert und
schreibt. Mit Rückfrage-Dialog, weil sofort gespeichert wird und es damit kein „einfach nicht
speichern" gibt. Das Schema kennt jeden Vorgabewert, der Knopf kostet also eine Schleife.

### 4.6 Tastenaufnahme

Nicht über ImGui. Wir speichern einen Win32-Tastencode, ImGui rechnet in `ImGuiKey`, und die
Rückrichtung dieser Abbildung ist im Win32-Backend nicht öffentlich.

`WindowHook` sieht die `WM_KEYDOWN` ohnehin und fragt bereits einen Rückruf, ob es die
Umschalttaste war. Dazu kommt ein zweiter: „nimmst du gerade eine Taste auf?" Ist er scharf, wird
der Tastencode gemerkt und die Aufnahme entschärft.

**Diese Prüfung steht vor der Umschalttaste.** Sonst ließe sich die Umschalttaste nie auf sich
selbst legen: der Anschlag würde das Overlay schließen, statt aufgenommen zu werden.

Es ist derselbe Weg wie bei `Menu::Gate` — die Fensterprozedur setzt, der Renderfaden liest, und
der Zustandsautomat dazwischen ist ohne Spiel prüfbar.

### 4.7 i18n

`src/I18n/I18n.{h,cpp}`, aus `skyrim-base` portiert, aber **auf glaze**. Eine zweite
JSON-Bibliothek für 400 Zeilen wäre nicht zu rechtfertigen; glaze liegt über REX ohnehin da.

`T(schlüssel, "English default")` liefert einen `const char*`, der gilt, bis die Sprache
wechselt. Das Panel liest jeden Frame neu, also trägt das. Fehlt ein Schlüssel in der aktuellen
Sprache, wird auf Englisch zurückgefallen; fehlt er auch dort, gilt der im Quelltext
mitgegebene Vorgabetext.

**Schlüssel werden ausgeschrieben, nicht aus dem Pfad abgeleitet.** Eine Ableitung
(`Menu/toggleKey` → `setting.menu.toggle_key`) hieße, dieselbe Regel in C++ **und** in
`extract-i18n.py` zu führen; laufen sie auseinander, fehlt die Übersetzung, ohne dass etwas
bricht. Ausgeschriebene Schlüssel geben dem Skript **eine** Regel für alles — `T(…)`,
`.Label(…)` und `.Help(…)` haben dieselbe Form aus Schlüssel und englischem Vorgabetext — und
ein umbenannter Pfad verliert seine Übersetzung nicht.

**Kein Formatierer.** Der geerbte Motor kann benannte Platzhalter. Heute formatiert keine einzige
Zeichenkette, und ein Formatstring aus einer Übersetzungsdatei ist eine Angriffsfläche, die man
sich nicht ohne Not baut.

`Menu/language` ist ein `Choice`, dessen Liste beim Deklarieren aus dem Übersetzungsverzeichnis
gelesen wird. Fehlt das Verzeichnis ganz, bleibt Englisch und es steht eine Zeile im Log — die
Oberfläche funktioniert ohne jede Übersetzungsdatei.

Bestand zum Abschluss von E2: `en.json`, erzeugt von `tools/extract-i18n.py --write`, und
`de.json` von Hand.

### 4.8 Schrift und Theme

`src/Menu/Fonts.{h,cpp}` und `src/Menu/Theme.{h,cpp}`.

IBM Plex Sans Regular und SemiBold, 213 KB je Schnitt, aus dem vorhandenen Baum.
`Menu/fontSize` als `Slider` von 12 bis 32, Vorgabe 18.

Hier zahlt sich ImGui 1.92 aus. Das dx11-Backend meldet `ImGuiBackendFlags_RendererHasTextures`,
also werden Glyphen **bei Bedarf** nachgeladen und Größen sind zur Laufzeit frei
(`PushFont(font, size)`). Zweierlei folgt daraus: der Schieberegler wirkt beim Ziehen ohne
Atlas-Neubau, und es sind **keine** Glyphenbereiche zu deklarieren. Wer später `zh_CN.json`
beisteuert, braucht eine Schrift mit den Zeichen, keinen Eingriff im Quelltext.

Das Theme steht im Quelltext, nicht in einer Datei: dunkel, mit **deckendem**
Fensterhintergrund — ein durchscheinendes Panel über nächtlichem Boston ist unlesbar. Ändert sich
die Schriftgröße, wird das Theme frisch gesetzt und mit `ImGuiStyle::ScaleAllSizes` mitskaliert;
sonst wächst die Schrift und die Abstände bleiben stehen.

Fehlt die Schriftdatei, bleibt ImGuis eingebaute Schrift und es steht eine Zeile im Log. Das
Overlay ist dann hässlich, aber bedienbar.

### 4.9 Paketierung

Fallout 4 lädt aus `Data/F4SE/Plugins/`, nicht aus `SKSE`. Unsere Laufzeitdaten bekommen deshalb
einen neuen, eigenen Ort:

```
package/F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-Regular.ttf
package/F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-SemiBold.ttf
package/F4SE/Plugins/CommunityShadersFO4/Translations/en.json
package/F4SE/Plugins/CommunityShadersFO4/Translations/de.json
```

`tools/package.ps1` nimmt `package/F4SE` in die Basis auf — das Menü ist Basis, kein Addon.
`tools/verify-package.ps1` prüft die neuen Pfade mit.

**Damit ist die von D2 offen gelassene Frage beantwortet:** `package/Interface` und
`package/SKSE` werden **nicht ausgeliefert und nicht gelöscht**. Sie sind Rohmaterial wie
`features/`; die Icons und Themes werden interessant, sobald F+ Kategorien hat. Das gehört nach
`CLAUDE.md` und in die Roadmap, damit die Frage nicht ein drittes Mal aufkommt.

## 5. Zustände und Ablauf

Ein Frame mit offenem Overlay, in der Reihenfolge des Ablaufs:

1.  `Features::TickSystem` ruft `Settings::ConsumeChanged()`. Wahr, wenn die Datei sich geändert
    hat oder seit dem letzten Frame etwas gesetzt wurde → `ClearRefusals()`.
2.  `Features::TickSystem` treibt die Registry gegen `IsEnabled`.
3.  `Menu::TickSystem` treibt Tor, Zeiger und Eingabeschicht wie in E1.
4.  `Overlay::Draw` setzt Schrift und Theme, zeichnet das Panel.
5.  Das Panel liest das Schema, zeichnet je Eintrag sein Bedienelement, und ruft bei einer
    Änderung `Settings::Set*`.
6.  Meldet ImGui `IsItemDeactivatedAfterEdit()`, ruft das Panel `Settings::Save()`.
7.  `Save()` schreibt und setzt die Basislinie der `FileWatch` neu.
8.  Im nächsten Frame meldet `ConsumeChanged()` die Änderung — aus dem Merker, nicht aus der
    Datei — und die Features setzen auf, was aufzusetzen ist.

## 6. Fehlerbehandlung

| Fall                               | Verhalten                                                                                                                                                                         |
| ---------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Einstellungsdatei fehlt            | wird aus den Vorgabewerten geschrieben (wie D1)                                                                                                                                   |
| Einstellungsdatei unvollständig    | fehlende Schlüssel werden beim Start ergänzt, vorhandene Werte und Fremdschlüssel bleiben                                                                                         |
| Einstellungsdatei nicht lesbar     | Logzeile, alle Werte bleiben auf ihren Vorgaben, die Oberfläche funktioniert                                                                                                      |
| Einstellungsdatei nicht schreibbar | **eine** Logzeile, danach geschwiegen — sonst schriebe ein festgehaltenes Handle je Bedienelement eine Zeile. Die Änderung wirkt in dieser Sitzung, überlebt aber keinen Neustart |
| Übersetzungsverzeichnis fehlt      | Englisch, eine Logzeile. Kein Ausfall                                                                                                                                             |
| Übersetzungsdatei missgebildet     | wird übersprungen, Logzeile mit Dateinamen. Andere Sprachen bleiben nutzbar                                                                                                       |
| Schlüssel fehlt in der Sprache     | Rückfall auf Englisch, dann auf den Vorgabetext im Quelltext                                                                                                                      |
| Schriftdatei fehlt                 | ImGuis eingebaute Schrift, eine Logzeile                                                                                                                                          |
| Feature verweigert `Setup`         | `refused` im Panel, wie in D1 im Log                                                                                                                                              |

Jede dieser Logzeilen nennt den Grund. Mehrere Ablehnungsgründe dürfen nicht denselben Satz
schreiben — der Befund aus C gilt weiter.

## 7. Tests und Abnahme

### 7.1 Ohne Spiel prüfbar

| Test                               | Prüft                                                                                                                                                                                                              |
| ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `SettingsSchemaTests`              | Arten und Metadaten, Blockbildung, Reihenfolge, Markierung des Einschalters, missgebildete Pfade, doppelte Deklaration                                                                                             |
| `SettingsStoreTests`               | Datei aus Vorgaben; unvollständige Datei wird ergänzt; vorhandene Werte und Fremdschlüssel überleben; `Set` + `Save` im Rundlauf; `ConsumeChanged` aus Datei **und** aus Merker; Basislinie nach eigenem Schreiben |
| `I18nTests`                        | Sprachfund im Verzeichnis, Rückfall auf Englisch, Rückfall auf den Vorgabetext, fehlendes Verzeichnis, missgebildete Datei, Umschaltung zur Laufzeit                                                               |
| `FeatureRegistryTests` (erweitert) | `ForEach` liefert Namen und Zustände; `Declare` läuft beim Registrieren und genau einmal                                                                                                                           |
| `MenuGateTests` (erweitert)        | scharfe Aufnahme schlägt die Umschalttaste; nach der Aufnahme ist sie entschärft                                                                                                                                   |
| `FileWatchTests` (erweitert)       | `Rebase` macht eine soeben erfolgte Änderung unsichtbar, eine spätere aber nicht                                                                                                                                   |

Jeder Test wird nach dem Grünwerden absichtlich gebrochen, um zu belegen, dass er greift; die
erwarteten Fehlschläge werden vorher benannt.

### 7.2 Nur im Spiel prüfbar

-   Dass die Bedienelemente auf Klicks reagieren und der Schieberegler dem Zeiger folgt.
-   Dass die aufgenommene Taste das Overlay nach einem Neustart öffnet.
-   Dass die Schriftgröße beim Ziehen wirkt.
-   Dass ein Sprachwechsel die Beschriftungen ohne Neustart tauscht.
-   Dass das Theme über hellem und dunklem Spielbild lesbar bleibt.

### 7.3 Abnahmekriterien

1.  Das Overlay zeigt beide Features mit Namen und Zustand; ein Häkchen schaltet sie im laufenden
    Spiel.
2.  **Eine Änderung überlebt einen Neustart** — das Kriterium der Roadmap.
3.  Eine Einstellungsdatei ohne die neuen Schlüssel wird beim Start ergänzt; vorhandene Werte und
    ein von Hand eingetragener Fremdschlüssel bleiben stehen.
4.  Der Schriftgrößenregler wirkt beim Ziehen; geschrieben wird beim Loslassen — ein
    Schreibvorgang, nicht sechzig.
5.  Die Umschalttaste lässt sich im Overlay neu belegen, und die neue Taste öffnet nach einem
    Neustart.
6.  Ein Sprachwechsel tauscht die Beschriftungen ohne Neustart; ein fehlender Schlüssel fällt auf
    Englisch zurück statt leer zu bleiben.
7.  Der eigene Schreibvorgang wird nicht als fremde Änderung gelesen: nach einer Änderung im
    Overlay steht **kein** `settings changed, reloading` im Log. Eine Änderung an der Datei von
    außen erzeugt die Zeile weiterhin.

## 8. Annahmen, die E2 bestätigen muss

Diese sind aus dem Quelltext belegt, aber nicht im Spiel gemessen:

-   Dass `glz::write_file_json` mit `prettify` eine Datei erzeugt, die `FJsonSettingStore::Load`
    unverändert wieder einliest. Beide gehen durch `glz::generic`, es sollte trivial sein.
-   Dass die Blockreihenfolge in der geschriebenen Datei stabil ist. `glz::generic`s `object_t`
    ist ein `MapType`; ist es keine sortierte Abbildung, muss die Reihenfolge beim Schreiben
    selbst hergestellt werden, sonst mischt jeder Schreibvorgang die Datei neu durch.
-   Dass `PushFont(font, size)` je Frame ohne messbare Kosten läuft, wenn die Größe sich nicht
    ändert.
-   Dass die Tastenaufnahme in der Fensterprozedur alle Tasten sieht, die ein Spieler belegen
    will. E1 hat belegt, dass `WM_KEYDOWN` ankommt; ob das auch für Tasten gilt, die das Spiel
    selbst greift, ist nicht gemessen.
-   Dass IBM Plex Sans die Zeichen für Deutsch führt. Latin-1 ist bei dieser Familie zu erwarten,
    aber nicht nachgesehen.

## 9. Übergabe

Nach E2 bleibt aus Teilprojekt E nichts offen. Die Roadmap geht damit auf **F+**: je ein Zyklus
pro portiertem CS-Feature, mit dem Abnahmekriterium „sichtbarer Effekt plus CPU-/GPU-Zahlen".

Was F+ von E2 bekommt:

-   `Feature::Declare()` als die Naht, an der ein Feature seine Oberfläche bekommt — ohne eine
    Zeile ImGui im Feature.
-   Eine Einstellungsdatei, die sich selbst ergänzt, wenn ein Feature neue Schlüssel mitbringt.
-   `T()` und ein Extraktionsskript, sodass jede neue Beschriftung von Anfang an übersetzbar ist.

Was F+ selbst entscheiden muss: Kategorien und die Frage, ab welcher Länge die Featureliste ein
anderes Layout braucht als einen Scrollbereich.
