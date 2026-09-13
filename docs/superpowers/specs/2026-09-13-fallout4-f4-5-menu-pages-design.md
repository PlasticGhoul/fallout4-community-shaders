# Teilprojekt F4.5 — Menü in zwei Spalten

Spec, Stand 2026-09-13. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`. Setzt E2 voraus
(`2026-09-04-fallout4-einstellungsoberflaeche-design.md`: das Panel zeichnet aus Schema und
Registry, ein Feature bekommt seine Oberfläche allein durch `Declare`) und F1
(`2026-09-05-fallout4-f1-performance-overlay-design.md`: die Passtabelle und das HUD).

## 1. Kontext und Ziel

Das Overlay aus E2 zeichnet ein Fenster von 560 × 520 Pixeln, darin zwei Klappköpfe „General" und
„Features", und unter dem zweiten jeden Feature-Block untereinander: Schalter im Kopf, Regler
eingerückt darunter. Mit sieben Blöcken nach F4 ist das Fenster voll; nach F5 wird gescrollt, und
mit F11 hätte die Liste zwanzig Bildschirmhöhen. Die Skyrim-Vorlage löst das mit zwei Spalten:
links die Liste der Seiten, rechts nur die gewählte.

**Ziel:** dasselbe Overlay mit demselben Inhalt in zwei Spalten — links die Liste mit den
Schaltern, rechts eine Seite je Eintrag — ohne daß ein Feature dafür eine Zeile ändert.

**Abnahmekriterium der Roadmap:** „Jede Seite erreichbar, Schalten aus der Liste, die Zahlen der
Tafel unverändert."

### Was F4.5 erbt

-   **Die Naht aus E2.** `Settings::ForEachBlock`, `ForEachEntry`, der `Entry` mit Kind, Label,
    Help und `isFeatureSwitch`; `Features::TheRegistry().ForEach` mit Name und Zustand; das Panel
    liest jede Einstellung beim Zeichnen über `Settings::Get*` und kennt kein D3D.
-   **Der Mauszeiger, die Tastenaufnahme, das Gate.** Nichts davon wird angefaßt: das Fenster
    bleibt ein ImGui-Fenster, die Eingabe kommt auf demselben Weg an.
-   **Die Passtabelle aus F1.** `DrawTable` und `DrawHistory` in `PerformancePanel.cpp` zeichnen
    heute in ein eigenes Fenster; sie zeichnen genauso in einen Kindbereich.
-   **`T("key", "English")`** für jede sichtbare Zeichenkette und `tools/extract-i18n.py`.
-   **Das Vorbild `Menu::Gate`:** ein Stück Zustand, das weder ImGui noch die Engine kennt, mit
    einem Host-Test aus handgeschriebenen `Check`-Aufrufen.

### Was die Vorlage tut, und was davon übernommen wird

Skyrim CS 1.9 (`skyrim-base:src/Menu/FeatureListRenderer.cpp`): ein Fenster auf 80 Prozent des
Bildschirms; links eine `ListBox` mit den festen Einträgen Home, General, Advanced, Profiling,
Display, dann „Features" mit Suchfeld und den Features nach Kategorien aufklappbar; rechts die
Seite des gewählten Eintrags mit Titel, Version, Beschreibung und einer Release-Stage-Marke; oben
Logo und Knöpfe, unten Spielversion und GPU. **Übernommen werden die zwei Spalten, die festen
Einträge General und Performance, die Featureliste mit Schaltern und die Seite mit Titel und
Beschreibung.** Nicht übernommen: Kategorien, Suche, Home, Kopfzeile mit Logo, Themes, Icons,
Fußzeile mit GPU — Themes und Icons stehen in CLAUDE.md als „nicht vor F", der Rest lohnt sich
erst mit deutlich mehr Features, und jedes davon kann später an das Seitenmodell andocken.

## 2. Umfang

### In F4.5 enthalten

-   `Menu::PageList` — das Seitenmodell, rein, mit Host-Test.
-   `Menu::SettingsPanel` neu geschnitten: Kopf und Fuß bleiben, dazwischen zwei Spalten.
-   `Menu::PerformancePanel` gibt die Tabelle als Baustein her; das eigene Performance-Fenster
    bei offenem Overlay entfällt, das HUD in der Ecke bleibt.
-   `Overlay::Draw` reicht die Meßdaten an das Panel weiter.
-   Neue i18n-Schlüssel, `en.json` regeneriert.
-   Roadmap: Zeile F4.5 und, nach der Abnahme, ein Abschnitt „Aus Teilprojekt F4.5 bestätigt".
    CLAUDE.md, Abschnitt Menu: ein Absatz zum Seitenmodell.

### Nicht in F4.5 enthalten

-   Kategorien, Suchfeld, Home-Seite, Logo, Themes, Icons, GPU-Fußzeile (siehe oben).
-   Merken der gewählten Seite in der Einstellungsdatei. Die Auswahl lebt für die Sitzung.
-   Ein Versions- oder Beschreibungsfeld je Feature. Die Beschreibung ist der Hilfetext des
    Schalters, den jedes Feature schon deklariert.
-   Änderungen an Schema, Store, Gate, Mauszeiger, Tastenaufnahme, i18n-Mechanik.

## 3. Vorentscheidungen

-   **Ein Modell neben dem Panel, nicht im Panel.** Die eine Logik, die hier falsch sein kann, ist
    die Zuordnung Block → Seite und die Auswahl. Beides steht in `PageList`, das nur Namen kennt,
    und wird gegen den Host geprüft. Das Panel bleibt Zeichnen.
-   **Der Schalter sitzt links in der Liste**, nicht auf der Seite. Man sieht auf einen Blick, was
    an ist, und schaltet, ohne die Seite zu wechseln. Die Seite zeigt den Zustand als Text.
-   **Performance ist eine Seite.** Die Tabelle gehört zum Overlay, nicht in ein zweites Fenster,
    das sich über das erste legt. Der Kompaktmodus in der Ecke bleibt, weil er beim Spielen
    gebraucht wird und nicht im Menü.
-   **Registrierungsreihenfolge, nicht alphabetisch.** Die Reihenfolge aus `RegisterAll` ist die
    Startreihenfolge und damit die, in der ein Feature das nächste voraussetzt; sie ist auch die
    Reihenfolge der Einstellungsdatei. Eine zweite Ordnung wäre eine zweite Wahrheit.
-   **Fenstergröße je Sitzung, nicht gespeichert.** `io.IniFilename` ist `nullptr`, ImGui merkt
    sich nichts; das bleibt so. Beim ersten Öffnen einer Sitzung wird das Fenster auf 60 × 70
    Prozent des Bildschirms gesetzt und mittig gelegt (`ImGuiCond_FirstUseEver`), danach ist es
    frei.

## 4. `Menu::PageList`

Dateien: `src/Menu/PageList.h`, `src/Menu/PageList.cpp`.

```cpp
namespace Menu
{
	enum class PageKind
	{
		kGeneral,
		kPerformance,
		kFeature
	};

	struct Page
	{
		PageKind kind;
		std::string name;  // "General", "Performance" oder der Featurename
	};

	/// Which pages the overlay has and which one is open. Knows neither ImGui
	/// nor the registry: it is handed names and answers with names.
	class PageList
	{
	public:
		/// Fixed pages first, then one per feature in the order given.
		void SetFeatures(std::span<const std::string_view> a_names);

		[[nodiscard]] std::span<const Page> Pages() const noexcept;
		[[nodiscard]] const Page& Selected() const noexcept;

		/// Selecting a name that is no page leaves the selection where it is
		/// and returns false. The panel never gets to point at nothing.
		bool Select(std::string_view a_name) noexcept;

		/// The page a settings block belongs to: "Performance" to the
		/// performance page, a feature's block to its page, every other block
		/// to General. That is the one rule the panel used to hold, now here.
		[[nodiscard]] const Page& PageOf(std::string_view a_block) const noexcept;
	};
}
```

Verhalten:

-   Nach Konstruktion gibt es General und Performance, gewählt ist General.
-   `SetFeatures` ersetzt die Featureseiten und behält die Auswahl, wenn es sie noch gibt; sonst
    General. Es wird je Frame aus der Registry gerufen (die ist konstant, aber das Modell soll es
    nicht wissen müssen).
-   `PageOf` vergleicht den Blocknamen mit den Featurenamen und mit `"Performance"`. Der Block
    `"Menu"` und alles Unbekannte gehört zu General — genau die Regel, die `DrawGeneral` heute
    hält („Blöcke ohne Feature dieses Namens").
-   Feste Seitennamen sind Schlüssel (`"General"`, `"Performance"`), keine Anzeigetexte; die
    Anzeige läuft über `T()` im Panel.

## 5. Das Panel

`DrawSettingsPanel(const PanelContext&, const PerformanceContext&)` — der zweite Parameter ist
neu; `Overlay::Draw` hat ihn schon und reicht ihn weiter. Der Aufruf von
`DrawPerformancePanel(…, Detail::kFull)` in `Overlay::Draw` entfällt.

**Fenster.** Wie heute `ImGui::Begin(T("menu.title", …))`, davor `SetNextWindowSize` auf
60 × 70 Prozent von `io.DisplaySize` und `SetNextWindowPos` auf die Mitte, beides
`ImGuiCond_FirstUseEver`. Kopfzeile (Name, Build, Frame) und Fußzeile (Restore defaults mit
Rückfrage, Close) unverändert.

**Körper.** Eine `BeginTable` mit zwei Spalten, `ImGuiTableFlags_Resizable |
ImGuiTableFlags_BordersInnerV`, Spaltenbreiten `SizingStretchProp` 1 : 3. Jede Spalte ist ein
`BeginChild`, das bis zur Fußzeile reicht und für sich scrollt.

**Links.** Zwei Abschnitte mit `SeparatorText`: `T("menu.section.general", "General")` mit den
Zeilen General und Performance als `Selectable`; `T("menu.section.features", "Features")` mit
einer Zeile je Featureseite. Eine Featurezeile in dieser Reihenfolge, in einer `PushID` des
Namens:

1. Die Checkbox des Schalters, gezeichnet über den bestehenden `DrawBool` des Schalter-Entry,
   ohne Label (`"##enabled"`); der Hilfetext des Schalters kommt hier **nicht** als Tooltip, der
   steht auf der Seite. Ein Feature ohne Schalter bekommt einen leeren Platz derselben Breite
   (`ImGui::Dummy`).
2. `Selectable` mit dem Namen, gewählt, wenn es die gewählte Seite ist; ein Klick ruft
   `PageList::Select`. Ohne Schalter gedimmt (`TextDisabled`-Farbe über `PushStyleColor`).
3. Rechtsbündig gedimmt der Zustand aus `StateText` (running / refused / off), wie heute.

**Rechts.** Nach `PageList::Selected().kind`:

-   **General:** Überschrift `T("menu.page.general", "General")` in der Überschriftenschrift.
    Dann `ForEachBlock`, und für jeden Block mit `PageOf(block).kind == kGeneral` alle Einträge
    über `DrawEntry` — heute der Block Menu.
-   **Performance:** Überschrift `T("menu.page.performance", "Performance")`. Dann
    `DrawPerformanceTable(a_performance)` (Abschnitt 6), ein `Separator`, dann die Einträge des
    Blocks `"Performance"` über `DrawEntry`.
-   **Feature:** der Name in der Überschriftenschrift, daneben gedimmt der Zustand. Darunter der
    Hilfetext des Schalters als Fließtext (`TextWrapped`), sofern der Schalter einen hat; ohne
    Schalter statt dessen `T("menu.no_switch", "This feature declares no switch and cannot be
turned on.")`. Ein `Separator`, dann die übrigen Einträge des Blocks über `DrawEntry` mit
    ihren Tooltips wie heute. Ein Feature ohne weitere Einträge zeigt darunter nichts.

`DrawEntry`, `DrawBool`, `DrawSlider`, `DrawChoice`, `DrawKey`, `DrawHelp`, `StateText` und
`DrawFooter` bleiben, wie sie sind. `DrawGeneral`, `DrawFeature` und `DrawFeatures` entfallen; an
ihre Stelle treten `DrawPageList`, `DrawGeneralPage`, `DrawPerformancePage`, `DrawFeaturePage`.

Die `PageList` lebt als `static` in `SettingsPanel.cpp`, wie `categoryExpansionStates` in der
Vorlage: Zustand des Panels für die Sitzung, kein Systemzustand.

## 6. Der Performance-Baustein

`PerformancePanel.h` bekommt

```cpp
/// The table and the frame history, drawn into whatever window is current.
/// The performance page of the overlay calls it; the compact display in the
/// corner does not.
void DrawPerformanceTable(const PerformanceContext& a_context);
```

und `Detail::kFull` samt dem `ImGui::Begin(T("performance.title", …))`-Zweig in
`DrawPerformancePanel` entfällt; `DrawPerformancePanel(ctx, Detail)` wird zu
`DrawPerformanceHud(ctx)` für den Kompaktmodus, der Rückgabewert (ob etwas gezeichnet wurde)
bleibt. `DrawPerformanceTable` enthält, was heute zwischen `Begin` und `End` des vollen Fensters
steht: `RefreshIfDue`, die Zeile „Measurement is off" oder die Anmerkung zum Frame, `DrawTable`,
`DrawHistory`. Der Schlüssel `performance.title` wird von der Seitenüberschrift weiterbenutzt.

## 7. Prüfung

### 7.1 Host-Test `tests/MenuPagesTests.cpp`

Nach dem Muster von `MenuGateTests.cpp`, ein `main` mit `Check`:

1. Frisch: zwei Seiten, General dann Performance, gewählt General.
2. `SetFeatures({"A", "B"})`: vier Seiten in der Reihenfolge General, Performance, A, B.
3. `Select("B")` gibt `true`, `Selected()` ist B; `Select("C")` gibt `false`, Auswahl bleibt B.
4. `SetFeatures({"A"})` nach gewähltem B: Auswahl fällt auf General.
5. `SetFeatures({"A", "B"})` nach gewähltem A: Auswahl bleibt A.
6. `PageOf("Performance")` ist die Performance-Seite, `PageOf("A")` die von A, `PageOf("Menu")`
   und `PageOf("Unknown")` General.
7. `Select("General")` und `Select("Performance")` treffen die festen Seiten.

Nach Grün jede Prüfung einmal brechen (Reihenfolge vertauscht, `Select` ohne Rückfall,
`PageOf` ohne Performance-Regel), und vorher benennen, welche Prüfung fallen muß; erst prüfen,
daß der Bau geglückt ist. Eintrag in `CMakeLists.txt` wie die anderen Host-Tests.

### 7.2 Was nicht host-getestet wird

Das Zeichnen. ImGui hat auf dem Host keinen Kontext, und die vorhandenen Panels sind aus demselben
Grund ungetestet. Der Spiellauf deckt es ab.

### 7.3 Abnahme, ein Spiellauf

| #   | Schritt                                                                                         | Geht es schief, heißt das                                                           |
| --- | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| 1   | Overlay öffnen: großes Fenster mittig, zwei Spalten, links General/Performance und die Features | Fenster klein oder einspaltig: Tabelle oder Größe greift nicht                      |
| 2   | Trennlinie ziehen, Fenster verschieben und skalieren                                            | Spalte springt: Sizing-Flags                                                        |
| 3   | Jede Featurezeile anklicken: rechts Titel, Zustand, Beschreibung, Regler                        | Leere Seite: `PageOf` oder `ForEachEntry` auf falschem Block                        |
| 4   | Ein Feature aus der Liste ausschalten: Zustand wechselt auf off, Bild ändert sich               | Kein Wechsel: Schalter zeichnet nicht in den Store                                  |
| 5   | General: Sprache wechseln, Schriftgröße schieben, Taste neu belegen (auch auf sich selbst)      | Aufnahme trifft falsches Feld: `armCapture`-Pfad                                    |
| 6   | Performance: Tabelle mit allen Passzeilen und Verlauf, darunter die vier Einstellungen          | Tabelle fehlt: Baustein nicht gerufen; Zahlen anders als vor F4.5: falscher Kontext |
| 7   | Overlay schließen: HUD in der Ecke wie vorher, `hud` aus und an                                 | HUD weg: Kompaktpfad beschädigt                                                     |
| 8   | Restore defaults mit Ja: Seite bleibt, Werte springen; Close                                    | Absturz: Popup in der Tabelle                                                       |
| 9   | F11: Schnappschuß im Log, Zahlen wie in F4                                                      | Fehlt: Log-Taste durch das Panel verdeckt                                           |

Keine `[E]`-Zeile im Log. Die Zahlen des Schnappschusses werden gegen die aus F4 gestellt; das
Overlay selbst kostet in der Zeile `Overlay` weiterhin unter 0,1 ms.

## 8. Risiken

-   **Der Schalter in der Liste zeichnet über `DrawBool`, das den Label-Text als ImGui-ID nimmt.**
    In der Liste hat er keinen Text; die `PushID` des Featurenamens hält die IDs auseinander.
    Zeigt ein Klick auf einen Schalter Wirkung bei einem anderen, ist das die Ursache.
-   **Das Popup „Restore defaults" liegt in der Fußzeile, außerhalb der Tabelle.** Bleibt es dort,
    ist nichts zu tun; wandert es je in eine Zelle, muß `OpenPopup` und `BeginPopupModal` in
    derselben ID-Ebene stehen.
-   **Die Seitentitel `General` und `Performance` sind auch Blocknamen** (`Performance` ist ein
    Block, `General` nicht). Das Modell vergleicht Blocknamen nur mit `"Performance"` und den
    Featurenamen; ein Feature, das sich `General` nennen wollte, bekäme eine eigene Seite, und
    das ist richtig so.

## 9. Was F4.5 für F5 aufwärts hinterläßt

-   Ein Feature bekommt seine Seite weiterhin allein durch `Declare`. Der Hilfetext des Schalters
    ist jetzt sichtbarer als zuvor — er steht als Beschreibung oben auf der Seite und sollte den
    Effekt in ein, zwei Sätzen erklären.
-   `Menu::PageList` als Ort für alles, was später an der Liste hängt: Kategorien wären eine
    Gruppierung der Featureseiten, eine Suche ein Filter darauf, eine Home-Seite eine weitere
    feste Seite. Nichts davon braucht das Panel neu zu schneiden.
-   `DrawPerformanceTable` als Baustein, den auch eine spätere Profiling-Ansicht mit mehr Spalten
    einbetten kann.
