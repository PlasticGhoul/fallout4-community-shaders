# Teilprojekt F1 — Performance Overlay

Spec, Stand 2026-09-05. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A, B1, B2, C, D1, D2, E1 und E2 voraus, insbesondere
`2026-08-31-fallout4-overlay-eingabe-design.md` und
`2026-09-04-fallout4-einstellungsoberflaeche-design.md`.

## 1. Kontext und Ziel

Die Roadmap nimmt jedes Teilprojekt ab F2 auf „sichtbarer Effekt plus CPU-/GPU-Zahlen" ab. Zahlen
gibt es bisher keine: `FrameCounter` zählt Bilder und schreibt alle 600 eine Logzeile. Damit lässt
sich weder sagen, was ein Feature kostet, noch ob es sich lohnt.

**Ziel:** Eine Zeitmessung je benanntem Pass, in CPU- und GPU-Zeit, sichtbar im laufenden Spiel und
ausführlich im Overlay, mit einer Taste zum Festhalten.

**Abnahmekriterium der Roadmap:** „Zahlen im Spiel ablesbar, die sich unter Last bewegen."

F1 steht deshalb vor allen Feature-Teilprojekten: es ist das Werkzeug, das die siebzehn folgenden
überhaupt abnehmbar macht.

### Was F1 erbt

-   **`Render::MarkerScope`** (B1) — eine RAII-Klammer, die eine benannte Region für RenderDoc
    öffnet. Ein Messpass reicht seinen Namen dorthin weiter, damit Capture und Overlay dieselben
    Namen führen.
-   **Der `Present`-Hook** (B1) — der eine Punkt je Frame, an dem wir Kontrolle haben.
-   **`Features::Registry`** (D1) — ruft jedes Feature über `Guarded(name, "Frame", …)`. Das ist die
    Stelle, an der ein einziger Eingriff jedes gegenwärtige und zukünftige Feature misst.
-   **Das Overlay** (E1) und **das Panel samt Einstellungsschema, Tastenaufnahme und i18n** (E2).

## 2. Umfang

### In F1 enthalten

1. Ein Messkern mit D3D11-Timestamp-Queries, verschachtelten Pässen, gleitender Historie und
   Perzentilen.
2. Die Frame-Grenze im `Present`-Hook.
3. Eine Klammer in `Features::Registry`, die jedes Feature misst.
4. Eine Anzeige in zwei Ausbaustufen: HUD im laufenden Spiel, Tabelle mit Verlauf im Overlay.
5. Eine Taste, die eine Momentaufnahme ins Log schreibt.
6. Ein Einstellungsblock `Performance` mit vier Einträgen, samt Beschriftungen im Katalog.
7. Zwei Host-Tests.

### Nicht in F1 enthalten

-   **Zeitmessung der Engine-Pässe.** F1 patcht **keine** vtable. Zu wissen, wo Fallout 4 selbst
    seine Zeit lässt, ist wertvoll, aber es ist ein eigenes Vorhaben mit offenem Ende.
-   **A/B-Testing.** Die Vorlage hat dafür 273 Zeilen; wir haben das Problem nicht.
-   **Mitschrift in eine Datei.** Die Logtaste deckt den Bedarf, der besteht.
-   **Selbstzeiten.** Ein Pass wird inklusive seiner Kinder ausgewiesen, eingerückt. Eine
    Selbstzeitspalte hat noch niemand gebraucht.
-   **GPU-Speicher, Draw-Call-Granularität, Ruckler-Erkennung.**

## 3. Vorentscheidungen

Aus dem Brainstorming vom 2026-09-05, jeweils mit Begründung:

-   **Gemessen werden am Tag eins nur Rahmenwerte** — Frame, unsere eigene Tick-Kette, je Feature
    eine Zeile. Eigene Render-Pässe gibt es erst ab F2; das Gerüst trägt sie dann ohne Änderung.
-   **Zwei Ausbaustufen statt einer.** Eine Anzeige, die nur bei offenem Overlay erscheint, kann man
    nie beim Spielen ablesen, weil das Overlay die Eingabe anhält. Eine, die es nur im Spiel gibt,
    müsste ohne Maus bedienbar sein und zeichnete beim Abnehmen über das Einstellungsfenster.
-   **Eine Taste schreibt ins Log.** Die Abnahme jedes folgenden Teilprojekts verlangt Zahlen in
    einem Dokument. Sie von einem Bildschirmfoto abzutippen, während sie sich bewegen, ist die
    schlechtere Hälfte der Arbeit.
-   **Messkern nach `src/Render/`, Anzeige nach `src/Menu/`, kein neues Obersystem.** Ein viertes
    System neben Settings, Features und Menu wäre Gerüst für rund 400 Zeilen Messcode, das sich sein
    Gerät trotzdem aus `Render` holte und sein Bild von `Menu` zeichnen ließe.
-   **F1 wird nicht geteilt.** Bei diesem Umfang wären Messung und Darstellung zwei Specs für eine
    Sache.

## 4. Architektur

### 4.1 Modulschnitt

| Modul                    | Aufgabe                                                               |
| ------------------------ | --------------------------------------------------------------------- |
| `Render::PassStatistics` | Ringpuffer, Mittelwert, Perzentile. **Kein D3D.** Der getestete Teil  |
| `Render::Profiler`       | Queries, Frame-Ring, Passstapel, Historien, Momentaufnahme ins Log    |
| `Render::PassScope`      | RAII-Klammer, öffnet intern einen `MarkerScope`                       |
| `Menu::KeyLatch`         | Ein Tastendruck vom Fenster-Thread, einmal abgeholt vom Render-Thread |
| `Menu::PerformancePanel` | Eine Zeichenfunktion, zwei Ausbaustufen                               |

Kein ImGui in `src/Render/`, kein D3D in `src/Menu/PerformancePanel` — die Tafel bekommt eine
Ergebnisliste gereicht und weiß nicht, woher sie kommt. Das ist derselbe Schnitt, den E2 zwischen
`Settings::Schema` und `Settings::Store` gezogen hat, und aus demselben Grund: der rechnende Teil
muss ohne Spiel prüfbar sein.

### 4.2 Der Messkern

```cpp
namespace Render
{
    class Profiler
    {
    public:
        static constexpr std::size_t kFrameLatency  = 3;    // Frame-Plätze im Ring
        static constexpr std::size_t kMaxPasses     = 128;  // verschiedene Namen
        static constexpr std::size_t kHistorySize   = 300;  // ~1,7 s bei 180 fps
        static constexpr std::uint64_t kRetireFrames = 60;  // dann fällt ein Pass raus

        bool Initialize(device, context) noexcept;   // idempotent, verweigert dauerhaft
        void Release() noexcept;

        void BeginFrame() noexcept;
        void EndFrame() noexcept;
        void Collect() noexcept;                     // nicht blockierend

        void BeginPass(std::string_view name) noexcept;
        void EndPass() noexcept;

        [[nodiscard]] std::span<const PassResult> Results() const noexcept;
        void LogSnapshot() const noexcept;
    };
}
```

Ein `PassResult` trägt Namen, Tiefe, die letzte GPU- und CPU-Zeit, Mittelwert, p95, p99 und einen
Zeiger auf die Historie für den Verlaufsgraphen.

Je Frame-Platz eine `D3D11_QUERY_TIMESTAMP_DISJOINT` und je Pass zwei
`D3D11_QUERY_TIMESTAMP`. CPU-Zeiten kommen aus `QueryPerformanceCounter` an denselben Punkten.
Ausgelesen wird mit `D3D11_ASYNC_GETDATA_DONOTFLUSH`, damit der Render-Thread **nie** wartet.

### 4.3 Die Frame-Grenze

Wir haben genau einen Punkt je Frame. Ein Frame ist deshalb **Present zu Present**, und die
Reihenfolge im Hook ist der Entwurf:

```
Present(swapChain, syncInterval, flags)
├─ Profiler::EndFrame()        Frame-Ende; der beim letzten Present geöffnete Platz ist fertig
├─ Profiler::Collect()         einsammeln, was bereitsteht; Historien fortschreiben
├─ Profiler::BeginFrame()      frischer Platz, Frame-Beginn
├─ PassScope{"Features"}  →  Features::TickSystem()
├─ PassScope{"Overlay"}   →  Menu::TickSystem()
└─ g_originalPresent(…)        danach zeichnet die Engine den nächsten Frame in diesen Platz
```

Der Frame-Wert ist damit **Wandzeit einschließlich allem, was die Engine tut** — und
einschließlich einer Vsync-Wartezeit, falls eine anliegt. Er ist keine „GPU beschäftigt"-Zahl. Für
unsere eigenen Pässe ab F2 ist die Messung dagegen exakt, weil deren Timestamps unmittelbar um
unsere eigenen Aufrufe liegen. Diese Unterscheidung gehört in den Erklärtext der Anzeige, damit
später niemand die Frame-Zahl überinterpretiert.

### 4.4 Verschachtelte Pässe

Pässe schachteln sich zwangsläufig: `Features` enthält je Feature einen, ein Feature ab F2 enthält
eigene. Der Profiler führt deshalb einen **Stapel offener Pässe**; `EndPass` schließt den
innersten, und jeder Pass merkt sich seine Tiefe beim Öffnen. Die Ausgabe rückt danach ein:

```
Frame              5.62   5.71   7.90
  Features         0.31   0.29   0.61
    ImagespaceTint 0.02   0.02   0.05
  Overlay          0.21   0.19   0.44
```

Der Frame selbst ist der Pass der Tiefe 0 und heißt `Frame`; `BeginFrame` und `EndFrame` sind sein
Begin und sein Ende. Damit gibt es nur einen Mechanismus, nicht zwei.

Zeiten sind **inklusive**: ein Pass enthält seine Kinder.

### 4.5 Die eine Klammer

`Features::Registry` ruft heute `Guarded(name, "Frame", [&]{ feature->Frame(); })`. Dort kommt ein
`PassScope` mit dem Feature-Namen hinein — an genau einer Stelle. Folge: jedes heutige und jedes
zukünftige Feature erscheint in der Tabelle, ohne dass in `src/Features/` eine Zeile dafür steht.

Nur der `Frame`-Aufruf wird geklammert, nicht `Setup` und nicht `Shutdown`: die laufen nicht je
Frame, und eine Historie über Einzelereignisse hat keinen Aussagewert.

### 4.6 Die Anzeige

`Menu::PerformancePanel::Draw(const PerformanceContext&, Detail)` mit `Detail::kCompact` und
`Detail::kFull`, gerufen aus `Overlay::Draw` — dort, wo heute schon `SettingsPanel` gezeichnet
wird.

`Overlay::Draw` bekommt dafür einen vierten Parameter, einen `PerformanceContext` mit der
Ergebnisliste, dem HUD-Schalter und der Ecke. Er wird in `Menu::TickSystem` gefüllt, wo die
Einstellungen ohnehin gelesen werden; der `PanelContext` aus E2 bleibt unangetastet, weil er die
Einstellungsoberfläche beschreibt und nicht die Messung. Die Tafel liest keine Einstellung selbst
und kennt kein D3D — sie bekommt eine Liste und zeichnet sie.

**Kompakt** — gezeichnet, wenn das Overlay zu ist und `Performance/hud` gesetzt: ein Fenster mit
`NoDecoration | NoInputs | AlwaysAutoResize | NoFocusOnAppearing | NoNav`, in der Ecke aus
`Performance/corner`, mit Bildrate, Frame-Zeit, CPU- und GPU-Zeit.

**Voll** — gezeichnet, wenn das Overlay offen ist: ein eigenes ImGui-Fenster neben dem
Einstellungsfenster, mit der eingerückten Tabelle `Pass | ms | Ø | p95 | p99` und einem
`PlotLines`-Verlauf der Frame-Zeit.

**Eine Änderung an `Overlay::Draw` ist dafür nötig.** Heute lautet die Ausgabebedingung
`if (a_visible && BindBackBuffer())` (`src/Menu/Overlay.cpp:191`) — die Zeichendaten werden nur
ausgegeben, wenn das Overlay sichtbar ist. Sie wird zu „sichtbar **oder** es wurde ein HUD
gezeichnet". Alles andere bleibt: `MousePointer` wird weiterhin **nur** bei sichtbarem Overlay
eingesetzt, und die Unterdrückung von `WM_MOUSEMOVE` gegenüber dem ImGui-Backend bleibt an den
Overlay-Zustand gebunden. Das HUD nimmt keine Eingabe und darf an dieser in E2 teuer erkauften
Regelung nichts ändern.

### 4.7 Die Logtaste

`Menu::KeyLatch` ist die kleine Schwester von `Gate`: die Fensterprozedur merkt einen Tastendruck
in einem `std::atomic<bool>`, `Take()` holt ihn auf dem Render-Thread genau einmal ab. Kein
Zustand, keine Eingabesperre — `Gate` ist auf das Öffnen und Schließen des Overlays zugeschnitten
und wird dafür nicht aufgebohrt.

Die Reihenfolge in der Fensterprozedur ist: **Tastenaufnahme, dann Overlay-Taste, dann Logtaste.**
Die Aufnahme zuerst, weil sonst keine der beiden Tasten auf sich selbst umbelegbar wäre — das ist
die Regel aus E2. Die Logtaste zuletzt, damit eine Taste, die versehentlich auf beides gelegt
wurde, das Overlay öffnet statt beides zu tun.

`Profiler::LogSnapshot` schreibt:

```
=== performance snapshot over 300 frames ===
  pass                   ms      avg      p95      p99
  Frame                5.62     5.71     6.90     7.90
    Features           0.31     0.29     0.55     0.61
      ImagespaceTint   0.02     0.02     0.04     0.05
    Overlay            0.21     0.19     0.38     0.44
  178.2 fps, cpu 3.10 ms, gpu 5.40 ms, 0 frame(s) discarded
```

Das ist das Format, das in die Roadmap wandert.

### 4.8 Einstellungen und i18n

Ein allgemeiner Block `Performance`, deklariert in `Menu::StartSystem` neben dem `Menu`-Block —
also vor `Settings::Init`, wie es die Deklarationsregel verlangt. Es gibt kein Feature dieses
Namens, also zeichnet `SettingsPanel` ihn von selbst als allgemeine Einstellung.

| Pfad                  | Art    | Vorgabe      | Wirkung                                                    |
| --------------------- | ------ | ------------ | ---------------------------------------------------------- |
| `Performance/measure` | Bool   | `true`       | Aus: keine Query wird ausgestellt, beide Anzeigen sagen es |
| `Performance/hud`     | Bool   | `true`       | HUD, solange das Overlay zu ist                            |
| `Performance/corner`  | Choice | `top-right`  | `top-left`, `top-right`, `bottom-left`, `bottom-right`     |
| `Performance/logKey`  | Key    | `0x7A` (F11) | Schreibt die Momentaufnahme                                |

`F11`, weil Fallout 4 `F5` und `F9` belegt und Steam auf `F12` liegt. Umbelegen geht über den
Aufnahmeknopf, den eine `Key`-Einstellung aus E2 ohne Zutun bekommt.

Alle Beschriftungen und Erklärtexte laufen durch `.Label` und `.Help`, jede Zeichenkette der Tafel
durch `T()`. `tools/extract-i18n.py --write` erzeugt `en.json` neu; der Lauf ohne `--write` muss
danach fehlerfrei durchgehen.

## 5. Zustände und Ablauf

Der Profiler kennt drei Zustände:

| Zustand        | Wie erreicht                                    | Verhalten                               |
| -------------- | ----------------------------------------------- | --------------------------------------- |
| **bereit**     | `Initialize` geglückt, `Performance/measure` an | misst, sammelt ein, liefert Ergebnisse  |
| **ruhend**     | `Performance/measure` aus                       | stellt keine Query aus, Ergebnisse leer |
| **verweigert** | `Initialize` gescheitert                        | wie ruhend, wird nie erneut versucht    |

Der Übergang von **ruhend** nach **bereit** verwirft die Historien: Werte von vor der Pause neben
Werten von danach ergäben ein Mittel, das es nie gab.

## 6. Fehlerbehandlung

-   **Kein Gerät oder `CreateQuery` scheitert** → **verweigert**, eine Logzeile, kein erneuter
    Versuch. Dieselbe Regel wie `Overlay::EnsureReady`: was einmal scheiterte, scheitert wieder.
-   **`Disjoint == TRUE`** → sämtliche Werte dieses Frames werden **verworfen**, nicht geklemmt. Die
    GPU-Uhr hat sich verstellt; eine Zahl daraus wäre gelogen. Die Zahl verworfener Frames steht in
    der Momentaufnahme.
-   **Daten noch nicht bereit** → der Platz bleibt in der Luft. Ist er nach `2 × kFrameLatency`
    Frames immer noch nicht bereit, wird er verworfen und wiederverwendet; **das** wird
    protokolliert, ratenbegrenzt auf eine Zeile je 600 Frames statt einer je Frame.
-   **Mehr als `kMaxPasses` verschiedene Namen** → neue werden ignoriert, mit einer einmaligen
    Warnung, die den abgewiesenen Namen nennt.
-   **Ein Pass, der `kRetireFrames` lang nicht vorkam, verschwindet.** Sonst zeigt ein abgeschaltetes
    Feature auf Dauer seine letzte Zahl. Nach dem Ausmustern wird die Namensbuchhaltung neu
    aufgebaut — Löschen macht Indizes ungültig.
-   **`EndPass` ohne offenen Pass** → ignoriert, mit einer Warnung. Kann über `PassScope` nicht
    auftreten, aber der Kern verlässt sich nicht darauf.
-   **Unbalancierte Klammern durch eine Ausnahme** → `PassScope` ist RAII, und ein werfendes Feature
    fängt bereits `Guarded` ab; der Destruktor läuft.

## 7. Tests und Abnahme

### 7.1 Ohne Spiel prüfbar

`tests/ProfilerStatsTests.cpp` gegen `Render::PassStatistics`:

-   Einfügen, Umlauf über die Puffergrenze, Reihenfolge der Historie nach dem Umlauf.
-   Mittelwert bei leer, bei einem Wert, bei vollem Puffer.
-   Perzentile: `p0`, `p50`, `p100`, ein interpolierter Wert zwischen zwei Nachbarn, und der Fall
    „weniger Werte als Puffergröße".
-   Ausmustern nach `kRetireFrames`, und **die Namensbuchhaltung danach** — dass der zweite Pass
    nach dem Entfernen des ersten noch auf seine eigene Historie zeigt.
-   Die Obergrenze `kMaxPasses`: der 129. Name wird abgewiesen, die ersten 128 bleiben heil.

`tests/KeyLatchTests.cpp` gegen `Menu::KeyLatch`:

-   `Take()` liefert genau einmal `true`.
-   Mehrere `Request()` zwischen zwei `Take()` ergeben eine Auslösung, nicht mehrere.
-   Taste `0` trifft nichts.
-   Ein Tastenwechsel bei anstehendem Druck verliert den Druck nicht.

Beide Tests werden nach dem Grünwerden absichtlich gebrochen, mit vorher benannten Erwartungen,
und vorher wird geprüft, dass der Bau geglückt ist — eine Mutation, die sich nicht übersetzen
lässt, hinterlässt das alte Executable.

### 7.2 Nur im Spiel prüfbar

Die Query-Mechanik und das Zeichnen. Kein Host-Test erreicht sie; sie hängen an einem echten
Gerät, an der Reihenfolge im `Present`-Hook und an ImGui.

### 7.3 Abnahmekriterien

Ein Spielstart, in dieser Reihenfolge:

1. Overlay zu: das HUD steht in der eingestellten Ecke und zeigt Bildrate, Frame-Zeit, CPU und
   GPU. Beim Drehen in eine belebte Szene **bewegen sich die Zahlen** — das ist der Wortlaut der
   Roadmap.
2. Overlay auf: die Tabelle zeigt `Frame`, darunter eingerückt `Features`, je registriertem Feature
   eine Zeile, und `Overlay`. Der Verlaufsgraph läuft.
3. `ImagespaceTint` im Overlay einschalten: eine Zeile kommt hinzu. Ausschalten: sie verschwindet,
   nachdem sie ausgemustert wurde, und nicht früher.
4. `F11` schreibt die Momentaufnahme im Format aus 4.7 ins Log.
5. Die Ecke umstellen wirkt sofort. `Performance/measure` aus hält die Messung an, und beide
   Anzeigen sagen das, statt eingefrorene Zahlen zu zeigen.
6. `pwsh tools/verify-plugin.ps1`, `ctest`, und `python tools/extract-i18n.py` ohne `--write` gehen
   durch.

## 8. Annahmen, die F1 bestätigen muss

Jede davon ist plausibel und keine ist belegt. Wenn eine bricht, gehört das in den Abschnitt „Aus
Teilprojekt F1 bestätigt" der Roadmap:

1. **Timestamp-Queries stehen auf Fallout 4s Gerät zur Verfügung.** Das Gerät meldet Feature Level
   `0xb000`; Timestamps sind seit D3D10 Kern. Erwartet, aber ungeprüft.
2. **Queries dürfen aus unserem `Present`-Hook heraus ausgestellt werden.** Wir zeichnen dort
   bereits ImGui auf denselben Kontext, also spricht nichts dagegen.
3. **Ein `NoInputs`-Fenster bei geschlossenem Overlay stört den Systemzeiger nicht.** Bei
   geschlossenem Overlay bekommt das Backend die Position des Systemzeigers, und `MousePointer`
   läuft nicht. Nach den Erfahrungen aus E2 ist das die Annahme, die ich am ehesten falsch
   erwarte — sie wird als Erstes geprüft.
4. **Die Ausgabe der Zeichendaten bei geschlossenem Overlay kostet nichts Nennenswertes.** Ein
   Fenster mit vier Zeilen ist ein Handvoll Dreiecke; die Zahl steht danach im eigenen HUD.
5. **`Guarded` läuft auf dem Render-Thread.** Es wird aus `Features::TickSystem` im `Present`-Hook
   gerufen — belegt durch den Aufrufweg, aber nicht gemessen.

## 9. Übergabe

Nach F1 liegt vor:

-   Eine Zeitmessung, die jedes Feature ohne Zutun erfasst, und eine Klammer, die ein Feature ab F2
    für eigene Pässe benutzen kann: `Render::PassScope scope{"MeinFeature/Blur"}`.
-   Dieselben Namen im RenderDoc-Capture wie im Overlay.
-   Ein Logformat für die Abnahme aller folgenden Teilprojekte.

F2 (Screen-Space Shadows) baut darauf den ersten eigenen Render-Pass und weist mit diesen Zahlen
nach, was er kostet.
