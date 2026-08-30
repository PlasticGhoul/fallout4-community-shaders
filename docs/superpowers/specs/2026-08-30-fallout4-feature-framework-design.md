# Teilprojekt D1 — Feature-Framework

Spec, Stand 2026-08-30. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A, B1, B2 und C voraus, insbesondere `2026-08-30-fallout4-shader-pipeline-design.md`.

## 1. Kontext und Ziel

C hat die Shader-Pipeline gebaut, aber fest verdrahtet: `Shader::TickPipeline()` wird unmittelbar
aus dem Present-Hook gerufen, und Katalog, Watcher und Zeiger-Tausch gehören diesem einen
Untersystem allein. Für ein zweites Stück Funktionalität gäbe es heute keinen Platz außer einem
weiteren Aufruf an derselben Stelle.

**Ziel:** Eine Basisklasse, an der Features hängen, eine Registrierung, persistente Einstellungen
und das Umschalten im laufenden Spiel.

**Abnahmekriterium der Roadmap:** zwei Features unabhängig voneinander an- und abschaltbar.

### Warum D geteilt wurde

Die Roadmap-Fassung von D bündelte die **Laufzeit** eines Features mit seiner **Auslieferung** —
`dist/`, AIO-Archive und der Ini-Versionsaudit standen in `CLAUDE.md` ebenfalls unter D. Das sind
zwei Subsysteme ohne Berührung: das eine ist Laufzeitverhalten, das andere Buildsystem.

D wurde deshalb am 2026-08-30 geteilt. **D1** ist diese Spec. **D2** (Paketierung) bleibt offen und
lohnt erst, wenn es echte Features zu paketieren gibt — es rutscht damit hinter F.

### Was die Vorerkundung ergeben hat

| Sachverhalt                      | Befund                                                                                                                                |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Skyrims `Feature`                | 366 Zeilen Header, 428 Zeilen Implementierung, **42 virtuelle Methoden**                                                              |
| Dessen Abhängigkeiten            | `FeatureCategories`, `FeatureConstraints`, `FeatureVersions`, `RestartSettings` und **`I18n/I18n.h`**                                 |
| Folge                            | Eine 1:1-Übernahme zöge i18n-Arbeit aus Teilprojekt E nach D1 vor                                                                     |
| `REX::FJsonSettingStore`         | vorhanden, Singleton, mit `Init(fileBase, fileUser)`, `Load()`, `Save()` über glaze                                                   |
| `REX::TJsonSetting<T>`           | meldet sich bei der Konstruktion **selbst** beim Store an, mit Pfad und Standardwert                                                  |
| `COMMONLIB_JSON`                 | im Shim vorhanden, aber `OFF`; braucht `glaze` in `vcpkg.json`                                                                        |
| `F4SE::GetSaveFolderName()`      | öffentlich; zusammen mit `REX::W32::SHGetKnownFolderPath` lässt sich der Dokumentenpfad genauso auflösen, wie F4SE es für das Log tut |
| Bestehende Feature-Verzeichnisse | 40 Stück unter `features/`, 44 `.ini`-Dateien, Inhalt jeweils nur `[Info] Version = …`                                                |

Daraus folgt der Zuschnitt: **Einstellungen schreiben wir nicht selbst**, und die Basisklasse
wächst aus dem, was C tatsächlich braucht, statt aus Skyrims gewachsener Fassung.

## 2. Umfang

### In D1 enthalten

-   Die Basisklasse `Feature` mit fünf Methoden.
-   `FeatureRegistry`: Besitz, Zustände, Reihenfolge, Umschalten, `Frame()`.
-   `FeatureSettings`: Pfadauflösung, Anbindung an `REX::FJsonSettingStore`, Neuladen bei
    Dateiänderung.
-   Umzug von `Shader::FileWatch` nach `Util::FileWatch`.
-   Umzug von C's Pipeline in ein Feature `ImagespaceTint`, samt Entfernen der Sonderverdrahtung
    aus `SwapChainHook`.
-   Ein zweites Feature `FrameCounter`, das Unabhängigkeit sichtbar macht.
-   `COMMONLIB_JSON=ON` und `glaze` als erste neue Abhängigkeit über `spdlog` hinaus.

### Nicht in D1 enthalten

-   Menü und jede Art von Oberfläche (**E**).
-   Paketierung, `dist/`, AIO-Archive, Ini-Versionsaudit (**D2**).
-   Kategorien, Constraints zwischen Features, Versionsfelder, Release-Stufen, `CORE`-Marker,
    Nexus-Verlinkung, i18n.
-   Shader-Hilfen in der Basisklasse. Was ein Feature mit Shadern tut, ist seine Sache, bis F uns
    sagt, welche Hilfen sich wiederholen.
-   Anfassen der 40 geerbten Feature-Verzeichnisse.

## 3. Vorentscheidungen

| Frage                                       | Entscheidung                                                  | Begründung                                                                                                                                                                                  |
| ------------------------------------------- | ------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Zuschnitt                                   | D wird in D1 und D2 geteilt                                   | Laufzeit und Auslieferung sind zwei Subsysteme; D2 lohnt erst nach F                                                                                                                        |
| Basisklasse                                 | fünf Methoden, aus C gewachsen                                | Entspricht der Roadmap-Grundsatzentscheidung „Architektur neu denken". Skyrims 42 Virtuals beantworten Fragen, die für FO4 noch niemand gestellt hat                                        |
| Persistenz                                  | `REX::FJsonSettingStore` mit glaze                            | Verschachtelt und typisiert, ein Block je Feature. Skyrim CS speichert ebenfalls JSON, was das Portieren von Feature-Einstellungen in F erleichtert. Und wir schreiben keine Serialisierung |
| Schaltzeitpunkt                             | im laufenden Spiel                                            | Zwingt `Shutdown` zur Ehrlichkeit, und genau das braucht das Menü in E ohnehin. Das Werkzeug zur Dateiüberwachung steht seit C                                                              |
| Verhältnis zu C                             | C's Pipeline **wird** das erste Feature                       | Beweist das Framework gegen Code mit echtem Zustand statt gegen Attrappen, und beseitigt die Sonderverdrahtung im Present-Hook                                                              |
| Verworfen: zwei Attrappen                   | die wörtliche Roadmap-Abnahme                                 | Ein Feature, das nichts besitzt, kann nicht zeigen, ob `Shutdown` etwas taugt. Und zwei Wege in den Frame blieben bestehen                                                                  |
| Verworfen: Shader-Hilfen in der Basisklasse | Framework schluckt die Shader-Schicht                         | Hieße, die Schnittstelle zu entwerfen, bevor ein echtes Feature ihre Anforderungen kennt — genau der Fehler, den wir bei der Basisklasse vermeiden                                          |
| Einstellungsdatei                           | `<Dokumente>/My Games/Fallout4/F4SE/CommunityShadersFO4.json` | Neben dem Log, nutzerschreibbar, außerhalb des von Vortex verwalteten `Data`                                                                                                                |

## 4. Architektur

Alles neu unter `src/Feature/`, die beiden Features unter `src/Features/`.

### 4.1 Die Basisklasse

```cpp
class Feature
{
public:
	virtual ~Feature() = default;

	/// Stable key. Also the settings path prefix, so it must not change once
	/// a user has a settings file on disk.
	[[nodiscard]] virtual std::string_view Name() const = 0;

	/// Acquires everything the feature needs. Returning false refuses the
	/// enable: the registry logs it once and leaves the feature off.
	[[nodiscard]] virtual bool Setup() = 0;

	/// Once per Present, only while running.
	virtual void Frame() {}

	/// Releases everything Setup acquired. Must be callable after a failed
	/// Setup, and must leave the engine as it was found.
	virtual void Shutdown() = 0;
};
```

`Shutdown` ist bewusst **nicht** optional. Ein Feature, das nicht sagen kann, wie es sich abbaut,
gehört nicht in ein Framework, das zur Laufzeit schaltet.

### 4.2 `FeatureRegistry`

Besitzt die Features als `std::unique_ptr`, hält je Feature einen Zustand und führt Umschaltungen
aus. Es kennt **keine** Einstellungen; der Wunschzustand kommt als Abfrage herein:

```cpp
using EnabledQuery = std::function<bool(std::string_view a_name)>;

class FeatureRegistry
{
public:
	void Register(std::unique_ptr<Feature> a_feature);

	/// Brings every feature into the state the query asks for, then calls
	/// Frame() on the running ones.
	void Tick(const EnabledQuery& a_query);

	/// Lets refused features try again. Called when the settings file changed.
	void ClearRefusals() noexcept;
};
```

Dieser Schnitt existiert **wegen der Prüfbarkeit**, nicht aus Eleganz: mit einer Lambda als Abfrage
ist die gesamte Zustandslogik ohne Spiel prüfbar.

### 4.3 `FeatureSettings`

Löst den Pfad auf, richtet `REX::FJsonSettingStore` ein, lädt, und beantwortet die `EnabledQuery`.
Legt je registriertem Feature eine `REX::TJsonSetting<bool>` unter `<Name>.enabled` an.

**Fallstrick, der hierher gehört:** `FJsonSettingStore::Save()` schreibt nach `m_fileBase`, nicht
nach `m_fileUser` (`src/REX/FJsonSettingStore.cpp`). Wir setzen `fileBase` deshalb auf genau die
eine Nutzerdatei und lassen `fileUser` leer. Andernfalls überschriebe das erste Speichern eine
mitgelieferte Standarddatei.

### 4.4 `Util::FileWatch`

Der Umzug von `Shader::FileWatch`. Es hat mit Shadern nichts zu tun, D1 braucht es für die
Einstellungsdatei, und C hat es bereits mit sieben Prüfungen abgesichert. Umzug plus
Namensraumwechsel, keine Änderung an der Logik; der bestehende Test zieht mit um.

### 4.5 `ImagespaceTint`

C's `ShaderPipeline`, umgezogen:

-   `Setup()` — Katalog laufen lassen, Pass wählen, Original merken, übersetzen, tauschen.
-   `Frame()` — Warteschlange leeren, Zeiger-Wächter.
-   `Shutdown()` — **zurücktauschen**, eigenen Shader freigeben, Watcher-Zustand verwerfen.

`PixelShaderOverride::Restore()` existiert seit C, wurde aber nie gerufen. D1 ruft es zum ersten
Mal und prüft es damit zum ersten Mal.

**Wo der Katalog-Wiederholungsversuch aus C bleibt.** Die Engine füllt ihre Technikkarten erst,
wenn die Spielwelt geladen ist; im Hauptmenü findet der Katalog nichts. Das darf **nicht** zu
`Setup() == false` führen, sonst landete das Feature im Zustand „Verweigert" und bliebe dort, bis
der Nutzer die Einstellungsdatei anfasst — obwohl niemand etwas falsch gemacht hat.

Deshalb der Schnitt: `Setup()` beschafft nur, was sofort beschaffbar ist — Watcher starten,
Absicht vermerken — und gibt `true` zurück. Der Katalogversuch lebt in `Frame()`, im selben
Rhythmus wie in C, bis er greift. `false` aus `Setup()` bleibt echten Fehlern vorbehalten.

Das ist die allgemeine Regel für Features, und sie gehört in die Feature-Dokumentation: **`Setup()`
scheitert nur an Dingen, die auch beim nächsten Versuch scheitern würden.** Warten auf die Engine
ist Sache von `Frame()`.

### 4.6 Verdrahtung im Present-Hook

`Shader::TickPipeline()` verschwindet aus `SwapChainHook` und wird durch einen Aufruf ersetzt, der
zwei Dinge in fester Reihenfolge tut:

```cpp
void FeatureSystem::Tick() noexcept
{
	// Reloads only when the watcher saw the file change, and clears refusals
	// when it did: a settings change is the moment a refused feature deserves
	// another go.
	FeatureSettings::ReloadIfChanged();

	Registry().Tick([](std::string_view a_name) { return FeatureSettings::IsEnabled(a_name); });
}
```

Damit gibt es genau **einen** Einstieg pro Frame, und die Sonderverdrahtung aus C ist weg.

### 4.7 `FrameCounter`

Zählt Frames, solange es läuft, protokolliert den Stand beim Hoch- und Herunterfahren. Berührt den
Renderer nicht. Seine Aufgabe ist, Unabhängigkeit sichtbar zu machen; dass sein Zähler bei jedem
Einschalten wieder bei null beginnt, belegt zugleich, dass `Shutdown` lief.

## 5. Zustände und Ablauf

| Zustand        | Bedeutung                                                             |
| -------------- | --------------------------------------------------------------------- |
| **Aus**        | in den Einstellungen abgeschaltet                                     |
| **Läuft**      | `Setup()` war erfolgreich, `Frame()` wird gerufen                     |
| **Verweigert** | eingeschaltet, aber `Setup()` schlug fehl oder `Frame()` hat geworfen |

Ohne den dritten Zustand versuchte die Registrierung in **jedem Frame** erneut, ein kaputtes
Feature hochzufahren, und das Log liefe in Sekunden voll. Ein verweigertes Feature wird erst wieder
angefasst, wenn sich die Einstellungsdatei ändert.

Der Wunsch des Nutzers wird dabei **nicht** zurückgeschrieben: steht `enabled: true` und scheitert
`Setup()`, bleibt `true` stehen. Wir korrigieren nicht heimlich, was jemand hingeschrieben hat.

| Zeitpunkt        | Was geschieht                                                                           |
| ---------------- | --------------------------------------------------------------------------------------- |
| `kGameDataReady` | Einstellungen laden, Watcher auf die Einstellungsdatei starten                          |
| erstes `Present` | eingeschaltete Features in Registrierungsreihenfolge hochfahren                         |
| jedes `Present`  | Umschaltungen abarbeiten, dann `Frame()` je laufendem Feature                           |
| Datei geändert   | Watcher-Thread setzt ein Flag; `Load()` und das Umschalten laufen im nächsten `Present` |

Alles Umschalten läuft auf dem **Render-Thread**, in `Present` — dieselbe Begründung wie in C: nur
dort ist sicher, auf welchem Thread wir sind und dass gerendert wird.

**Reihenfolge:** hochfahren in Registrierungsreihenfolge, herunterfahren in umgekehrter.

## 6. Einstellungen ohne Benachrichtigung

Ein Feature liest seine Werte direkt aus seinen `TJsonSetting`-Membern, jedes Mal wenn es sie
braucht. Kein Zwischenspeichern, also auch kein Benachrichtigungsmechanismus — `Load()` schreibt
die neuen Werte hinein, und beim nächsten Zugriff stehen sie da.

Das spart ein ganzes Subsystem, um den Preis einer Regel: **wer einen Einstellungswert
zwischenspeichert, muss ihn selbst erneuern.** In D1 tut das niemand.

## 7. Fehlerbehandlung

| Fall                     | Verhalten                                                                        |
| ------------------------ | -------------------------------------------------------------------------------- |
| `Setup()` gibt `false`   | einmalig protokollieren, Zustand „Verweigert", Datei unverändert                 |
| `Frame()` wirft          | Ausnahme fangen, Text protokollieren, `Shutdown()` rufen, Zustand „Verweigert"   |
| `Shutdown()` wirft       | Text protokollieren, Zustand trotzdem auf „Aus" — es gibt nichts Besseres zu tun |
| Einstellungsdatei fehlt  | alle Features bleiben aus, eine Logzeile; beim ersten `Save()` entsteht sie      |
| Einstellungsdatei kaputt | glaze meldet den Fehler, alte Werte bleiben stehen, Logzeile                     |

**`Frame()` wird in `try`/`catch` gerufen.** Das ist eine bewusste Abweichung von der sonst
ausnahmefreien Linie des Projekts: eine Ausnahme aus einem Feature liefe sonst durch unseren
Present-Hook in die Engine, und was Fallout 4 damit tut, will niemand herausfinden. Die Kosten sind
null, solange nichts fliegt.

## 8. Tests und Abnahme

### 8.1 Ohne Spiel prüfbar

| Test                   | Prüft                                                                                                                                                                                                                                                                                                                                                                        |
| ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `FeatureRegistryTests` | Eingeschaltetes wird hochgefahren, Ausgeschaltetes nicht; Umschalten fährt hoch und herunter; `Setup()` gibt `false` → keine `Frame()`-Aufrufe und kein Wiederversuch ohne Einstellungsänderung; `Frame()` wirft → `Shutdown()` wird gerufen und der Zustand ist verweigert; Herunterfahren in umgekehrter Reihenfolge; das Umschalten des einen lässt den anderen unberührt |
| `FeatureSettingsTests` | Ein `bool` überlebt Schreiben und Lesen durch `REX::FJsonSettingStore` in einer temporären Datei; fehlende Datei ergibt Standardwerte; kaputte Datei lässt die alten Werte stehen                                                                                                                                                                                            |
| `FileWatchTests`       | unverändert aus C übernommen, nur Namensraum und Pfad angepasst                                                                                                                                                                                                                                                                                                              |

`FeatureSettingsTests` prüft fremde Bibliothek, und zwar absichtlich: `REX::JSON` ist neu für uns,
glaze ist eine neue Abhängigkeit, und `Save()` schreibt nach `m_fileBase` statt nach `m_fileUser`.
Das gehört einmal auf dem Host bestätigt, statt es im Spiel zu vermuten.

Jeder Test wird nach dem Grünwerden absichtlich gebrochen, mit vorher benanntem erwartetem
Fehlschlag, und dabei ist zu prüfen, dass die Mutation auch übersetzt.

### 8.2 Nur im Spiel prüfbar

Der Rückbau des Zeiger-Tauschs und das Zusammenspiel mit dem Present-Hook.

### 8.3 Abnahmekriterien

1.  Das Log zeigt beide Features registriert, mit ihrem Zustand.
2.  **Tint im laufenden Spiel abschalten**: `enabled` auf `false`, Alt-Tab — der Farbstich
    verschwindet binnen einer Sekunde, das Log zeigt `Shutdown`. Wieder einschalten: er kommt
    zurück.
3.  **Unabhängigkeit**: `FrameCounter` umschalten, während der Tint läuft — seine Zeilen beginnen
    oder enden, der Stich bleibt unberührt. Und umgekehrt.
4.  Kaputte JSON: alte Werte bleiben, Spiel läuft, Logzeile.
5.  Die sechs bestehenden Host-Tests aus A bis C bleiben grün.

Kriterium 2 ist der eigentliche Gewinn dieses Zuschnitts: es prüft einen Rückbauweg, den C zwar
gebaut, aber nie gegangen ist.

## 9. Annahmen, die D1 bestätigen muss

-   Dass `PixelShaderOverride::Restore()` den Zeiger sauber zurücktauscht und die Engine danach
    ihren eigenen Shader wieder bindet.
-   Dass `REX::TJsonSetting<bool>` sich so verhält wie gelesen, insbesondere die Anmeldung bei der
    Konstruktion und das Zusammenspiel von `fileBase` und `fileUser`.
-   Dass `glaze` über vcpkg mit unserer Baseline sauber baut und unter `/W4 /WX` keine Warnung in
    unserem Target auslöst. Falls doch, wird sie eng unterdrückt, mit einem Kommentar, der den
    Header nennt.
-   Dass ein `Setup()`, das beim ersten Versuch scheitert, weil die Spielwelt noch nicht geladen
    ist, sich mit dem Zustand „Verweigert" verträgt, ohne dass der Nutzer die Datei anfassen muss.

## 10. Übergabe

-   **Roadmap:** Zeile D wird in D1 und D2 geteilt; D2 bleibt offen und rutscht hinter F.
-   **`CLAUDE.md`:** „Temporarily moot" nachziehen — Feature-Framework kehrt mit D1 zurück,
    Paketierung mit D2. Kategorien, Release-Stufen und `CORE`-Marker bleiben moot, weil D1 sie
    bewusst nicht mitnimmt.
-   **Für E:** das Menü setzt auf `FeatureRegistry` auf und ersetzt das Bearbeiten der JSON von
    Hand. `EnabledQuery` ist die Naht dafür.
-   **Für F:** jedes portierte Feature implementiert `Feature`. Die Regel aus Abschnitt 6 gehört in
    die Feature-Dokumentation.
