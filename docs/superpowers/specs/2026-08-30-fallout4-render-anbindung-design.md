# Teilprojekt B1 — Render-Anbindung

Spec, Stand 2026-08-30. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt Teilprojekt A voraus (`2026-08-30-fallout4-fundament-design.md`).

## 1. Kontext und Ziel

Teilprojekt A hat bewiesen, dass unser Plugin geladen wird, protokollieren kann und den
F4SE-Nachrichtenweg erreicht. Es fasst bewusst nichts von der Engine an.

B1 stellt die erste Verbindung zum Renderer her: Zugriff auf Device, Context und SwapChain, ein
Einhängepunkt pro Frame, und Marker, die in einem Capture-Werkzeug sichtbar sind.

**Ziel:** Ein RenderDoc-Capture von Fallout 4, in dem ein von uns gesetzter Marker sichtbar ist.

Die ursprüngliche Roadmap-Fassung von B enthielt zusätzlich das Inventar der Render-Targets. Das
ist offene Reverse-Engineering-Arbeit ohne abschätzbares Ende und wurde als **B2** abgetrennt. Die
Marker aus B1 sind das Werkzeug, mit dem man in B2 überhaupt sinnvoll suchen kann; die Reihenfolge
ergibt sich daraus von selbst.

## 2. Umfang

### In B1 enthalten

-   Zugriffsschicht auf Device, Context und SwapChain über commonlibf4.
-   Present-Hook per Patch der COM-vtable.
-   Frame-Zähler, mit einer Logzeile alle 600 Frames auf Debug-Ebene.
-   Marker über `ID3DUserDefinedAnnotation` mit RAII-Scope, ein Marker pro Frame.
-   Kreuzvergleich zur Absicherung der Adressauflösung.
-   Ein Host-Test der vtable-Manipulation gegen eine synthetische vtable.

### Nicht in B1 enthalten

-   Das Inventar der Render-Targets und ein benanntes `RENDER_TARGET`-Enum (**B2**).
-   Hooks auf Engine-Pässe, Shader-Ersetzung, `BSRenderPass` (**C**).
-   Overlay, ImGui (**E**).
-   Ein Frame-Begin-Hook, eine Callback-Registry und ein Per-Frame-Zustandsschnappschuss. Alle
    drei wurden erwogen und gestrichen: sie hätten in B1 keinen Nutzer, und was C und F wirklich
    brauchen, steht erst fest, wenn man dort ist.
-   Ergänzungen an unserem commonlibf4-Fork. B1 kommt mit dem aus, was die Bibliothek liefert.

## 3. Vorentscheidungen

| Thema                       | Entscheidung                                                         | Begründung                                                                                                                                                                                                                                                                                                             |
| --------------------------- | -------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Einhängepunkt               | `IDXGISwapChain::Present`                                            | Semantisch wäre `BSGraphics::Renderer::End` schöner, ist aber teurer: `REL::THook` beherrscht nur Call- und Jump-Stellen, keine Prolog-Detours, und `THookVFT` löst nur spielmodul-relative vtables auf. Ein Engine-Hook bräuchte also Microsoft Detours als neue Abhängigkeit oder eine erst zu findende Aufrufstelle |
| Hook-Technik                | Selbstgeschriebener Patch der COM-vtable                             | Rund dreißig Zeilen, keine neue Abhängigkeit, kein Eingriff in Engine-Code, unabhängig von Spielversionen                                                                                                                                                                                                              |
| Trampolin                   | Bleibt **aus**                                                       | Ohne `REL::THook` wird keines gebraucht. `InitInfo::trampoline` und `hook` bleiben auf `false` bis Teilprojekt C                                                                                                                                                                                                       |
| Verkettung                  | Der gemerkte Zeiger wird aufgerufen, nicht die DXGI-Originalfunktion | Sonst zerreißt die Kette, sobald ENB, ein Overlay oder ein Upscaler ebenfalls auf Present sitzt                                                                                                                                                                                                                        |
| Hinterlassene Schnittstelle | Nur Zugriff, Frame-Zähler, Marker                                    | Alles darüber hinaus wäre Vorratsbau ohne Nutzer                                                                                                                                                                                                                                                                       |

## 4. Architektur

Drei Einheiten unter `src/Render/`, jede mit einer Aufgabe.

### 4.1 `Renderer` — Zugriffsschicht

Zustandslos. Kapselt `BSGraphics::GetRendererData()` und `BSGraphics::GetCurrentRendererWindow()`.

Ihre Aufgabe ist nicht Abstraktion um ihrer selbst willen, sondern zwei konkrete Dinge:

1.  **Ein Ort für die Herkunft der Zeiger.** commonlibf4 hält sie als `REX::W32::ID3D11Device*`,
    `REX::W32::ID3D11DeviceContext*` und `REX::W32::IDXGISwapChain*`. Entgegen der ersten Annahme
    sind das **keine** bloßen Stubs: `REX::W32` deklariert D3D11 und DXGI vollständig, inklusive
    `ID3DUserDefinedAnnotation` und der zugehörigen IIDs. Wir bleiben deshalb durchgehend in
    diesem Namensraum und binden `<d3d11.h>` gar nicht erst ein — eine Umdeutung zwischen zwei
    Typsystemen entfällt damit ersatzlos.
2.  **Null-Prüfung an einem Ort.** `GetRendererData()` kann vor der Renderer-Initialisierung
    einen Nullzeiger liefern.

Die Zeiger werden bei jedem Aufruf frisch gelesen und **nicht** zwischengespeichert: das ist
billig und schließt aus, dass wir nach einem Geräteverlust oder einer Fensteränderung auf einem
veralteten Zeiger arbeiten. Die Objekte gehören der Engine; wir rufen kein `AddRef` und kein
`Release`.

Schnittstelle:

```cpp
namespace Render
{
	[[nodiscard]] ID3D11Device*        Device() noexcept;         // nullptr wenn nicht bereit
	[[nodiscard]] ID3D11DeviceContext* Context() noexcept;        // nullptr wenn nicht bereit
	[[nodiscard]] IDXGISwapChain*      SwapChain() noexcept;      // nullptr wenn nicht bereit
}
```

### 4.2 `SwapChainHook` — der Eingriff

Besitzt den Patch und sonst nichts.

**Installation:** vtable des SwapChain lesen (`*reinterpret_cast<void***>(swapChain)`), Slot **8**
(`IDXGISwapChain::Present`) merken, `VirtualProtect` auf `PAGE_READWRITE`, Zeiger tauschen, Schutz
auf den vorherigen Wert zurücksetzen.

Slot 8 ergibt sich aus der Vererbungskette: `IUnknown` belegt 0 bis 2, `IDXGIObject` 3 bis 6,
`IDXGIDeviceSubObject` 7, und `IDXGISwapChain::Present` folgt als erste eigene Methode.

**Der Thunk** erhöht den Frame-Zähler, öffnet einen Marker-Scope, ruft darin den gemerkten Zeiger
auf und schließt den Scope beim Verlassen. Der Aufruf liegt also **innerhalb** des Markers, nicht
davor. Er ruft **nicht** die DXGI-Originalfunktion — läge ein anderer Hook darunter, wäre der
sonst übersprungen.

**Kein Deinstallieren.** F4SE entlädt Plugins nicht; ein Rückbau hätte keinen Aufrufer. Der
gemerkte Originalzeiger existiert für die Verkettung, nicht für eine Rücknahme.

### 4.3 `Markers` — Beschriftung

Holt einmalig `ID3DUserDefinedAnnotation` per `QueryInterface` vom Context und bietet einen
RAII-Scope, der im Konstruktor `BeginEvent` und im Destruktor `EndEvent` ruft.

Fehlt die Schnittstelle, werden die Marker zu No-Ops statt zu einem Fehler. Hängt kein
Capture-Werkzeug am Prozess, sind die Aufrufe ohnehin nahezu kostenlos — der Marker bleibt deshalb
dauerhaft im Code und wird nicht hinter einen Schalter gelegt.

`BeginEvent` erwartet `LPCWSTR`; die Frame-Nummer wird entsprechend als `std::wstring` formatiert.

Konkret setzt der Thunk **einen** Scope mit dem Namen `CommunityShadersFO4 Frame <n>`, der den
Aufruf des gemerkten Present-Zeigers umschließt. Damit erscheint im Capture ein benannter Block
pro Frame statt eines punktuellen Ereignisses -- das ist die Struktur, an der sich B2 später
orientieren kann.

## 5. Installation und Lebenszyklus

Installiert wird bei **`kGameDataReady`**.

`kPostPostLoad` ist zu früh: der Renderer steht dort nicht zwingend. Im Abnahmelauf von A lagen
zwischen beiden Nachrichten rund neun Sekunden, in denen die Engine hochfährt.

Ist der SwapChain zu diesem Zeitpunkt dennoch null, wird das deutlich protokolliert und **nicht**
installiert. Das Plugin bleibt dann passiv. Es gibt keinen Wiederholungsversuch und kein Pollen:
schlägt der Zeitpunkt fehl, ist das eine Erkenntnis, die in B1 dokumentiert und behoben gehört,
kein Zustand, den man umgeht.

**Threading.** Present läuft auf dem Render-Thread. Der Frame-Zähler ist ein `std::atomic` mit
`memory_order_relaxed`, weil ihn niemand zur Synchronisation benutzt. Marker werden ausschließlich
innerhalb des Thunks gesetzt, also auf demselben Thread, dem der Context gehört; darüber hinaus
ist kein Schutz nötig.

## 6. Validierung der Adressauflösung

B1 steht und fällt damit, dass `REL::VariantID{og, ng}` für unsere AE-Runtime den NG-Wert liefert.
`REL::ID` füllt fehlende Slots mit dem letzten Wert auf, bei `COMMONLIB_RUNTIMECOUNT = 3` wird aus
`{og, ng}` also `{og, ng, ng}`. Ein falsch aufgelöster Zeiger, der zufällig nicht null ist, ist
gefährlicher als einer, der es ist.

**Gegenprobe durch Kreuzvergleich.** Drei unabhängig gelesene Wege müssen dasselbe Device
ergeben:

1.  `GetRendererData()->device`
2.  `ID3D11DeviceContext::GetDevice` auf dem Context aus `GetRendererData()`
3.  `IDXGISwapChain::GetDevice` auf dem SwapChain aus `GetCurrentRendererWindow()`

Stimmen alle drei überein, halten wir echte, zusammengehörige COM-Objekte in der Hand. Damit das
zufällig gelänge, müsste eine falsch aufgelöste Adresse auf eine in sich konsistente
COM-Objektfamilie zeigen.

Zusätzlich plausibilisiert und protokolliert werden `GetFeatureLevel()` — muss ein D3D11-Level
sein — sowie Breite, Höhe und Format aus `IDXGISwapChain::GetDesc()`. Beides sind Angaben, die
spätere Teilprojekte ohnehin brauchen.

**Schlägt der Kreuzvergleich fehl, wird nicht installiert**, und es wird eine Logzeile mit allen
drei Zeigern geschrieben. Das ist derselbe Grundsatz wie die Versionsprüfung in A: lieber passiv
bleiben als raten.

## 7. Fehlerbehandlung

| Fall                                        | Verhalten                                                                     |
| ------------------------------------------- | ----------------------------------------------------------------------------- |
| `GetRendererData()` oder SwapChain null     | Logzeile, keine Installation, Plugin bleibt passiv                            |
| Kreuzvergleich schlägt fehl                 | Logzeile mit allen drei Zeigern, keine Installation                           |
| `VirtualProtect` schlägt fehl               | Logzeile, keine Installation, Speicherschutz bleibt unverändert               |
| `ID3DUserDefinedAnnotation` nicht verfügbar | Logzeile auf Info-Ebene, Marker werden No-Ops, Hook wird trotzdem installiert |

In keinem Fall wird das Laden des Plugins abgebrochen und in keinem Fall eine Ausnahme geworfen:
B1 ist ein Zusatz, kein Fundament, und darf ein laufendes Spiel nicht gefährden.

## 8. Tests und Abnahme

### 8.1 Ohne Spiel prüfbar

B1 hat, anders als A, fast keine host-testbare Fläche — alles hängt an D3D und der laufenden
Engine. Eine Ausnahme gibt es, und sie trifft das Riskanteste.

**Host-Test der vtable-Manipulation.** Gegen eine _synthetische_ vtable im eigenen Speicher: ein
Array von Funktionszeigern anlegen, ein Objekt konstruieren, dessen erstes Feld darauf zeigt, den
Patch anwenden und prüfen, dass

-   der Originalzeiger aus Slot 8 korrekt gemerkt wurde,
-   ausschließlich Slot 8 sich geändert hat und alle anderen Slots unverändert sind,
-   das Zurücksetzen den Ausgangszustand exakt herstellt.

Das fängt die drei realistischen Fehler: falscher Index, nicht gemerktes Original, vergessener
Schutzwechsel. Kein D3D nötig.

Dazu wie in A: Kaltbau aus frischem Klon, Warmbau als No-Op, `tools/verify-plugin.ps1`.

### 8.2 Nur im Spiel prüfbar

1.  Das Plugin lädt wie in A, das Log enthält die bekannten Zeilen ohne Regression.
2.  Das Log nennt Device, Context und SwapChain; alle drei sind nicht null und der Kreuzvergleich
    ist bestanden. Feature-Level und Auflösung sind plausibel.
3.  Der Frame-Zähler läuft. Belegt durch eine Logzeile alle 600 Frames auf Debug-Ebene, also
    rund alle zehn Sekunden bei 60 Bildern je Sekunde.
4.  Ein RenderDoc-Capture zeigt unseren Marker.
5.  Das Spiel läuft über mehrere Minuten stabil, im Hauptmenü und in der Spielwelt.

### 8.3 Abnahmekriterien

B1 gilt als abgeschlossen, wenn die Punkte aus 8.1 und 8.2 erfüllt sind und
`docs/fallout4-port/ROADMAP.md` den Status fortgeschrieben hat.

## 9. Voraussetzung, die ein Mensch erledigen muss

**RenderDoc installieren.** Es ist auf der Entwicklungsmaschine nicht vorhanden. Für das Capture
muss **Capture Child Processes** aktiviert sein: `f4se_loader.exe` startet das eigentliche Spiel
als Kindprozess, ohne die Option hängt RenderDoc am Loader und sieht keinen einzigen Frame.

## 10. Annahmen, die B1 bestätigen muss

Begründet, aber nicht bewiesen. Werden im Verlauf verifiziert und in der Roadmap festgehalten.

-   `REL::VariantID{og, ng}` liefert für die AE-Runtime den NG-Wert, und die so aufgelösten
    Adressen sind für 1.11.240 korrekt. Prüfung durch den Kreuzvergleich aus Abschnitt 6.
-   Bei `kGameDataReady` ist der SwapChain vorhanden.
-   ~~`<d3d11.h>` und die `REX::W32`-Deklarationen lassen sich gemeinsam einbinden.~~ **Erledigt
    vor Umsetzungsbeginn:** die Frage stellt sich nicht. `REX::W32` deklariert D3D11 und DXGI
    vollständig, einschließlich `ID3DUserDefinedAnnotation` und `IID_ID3D11Device`. Wir bleiben
    in diesem Namensraum, `<d3d11.h>` wird nicht eingebunden.
-   `IDXGISwapChain::Present` liegt in dieser Umgebung auf vtable-Slot 8. Ergibt sich aus der
    COM-Vererbungskette und wird beim ersten Lauf durch einen laufenden Frame-Zähler bestätigt.

## 11. Übergabe an B2 und C

B1 hinterlässt: gesicherten Zugriff auf Device, Context und SwapChain; einen bestätigten Beleg,
dass die Adressauflösung für AE trägt; einen Einhängepunkt pro Frame; und Marker, die in einem
Capture sichtbar sind.

**B2** beginnt damit, die 101 anonymen Render-Targets aus `BSGraphics::RendererData` zu
identifizieren — mit den Markern aus B1 als Orientierung im Capture.

**C** schaltet als Erstes das Trampolin ein, weil dort echte Engine-Hooks über `REL::THook`
gebraucht werden, und ergänzt `BSRenderPass` in unserem commonlibf4-Fork.
