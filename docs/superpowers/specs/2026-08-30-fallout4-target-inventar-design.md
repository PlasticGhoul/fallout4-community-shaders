# Teilprojekt B2 — Render-Target-Inventar

Spec, Stand 2026-08-30. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt Teilprojekt B1 voraus (`2026-08-30-fallout4-render-anbindung-design.md`).

## 1. Kontext und Ziel

Fallout 4 hält seine Render-Targets anonym. `BSGraphics::RendererData` führt
`RenderTarget renderTargets[101]` (Offset `0x0A58`), `DepthStencilTarget depthStencilTargets[13]`
(`0x1D48`) und `CubeMapRenderTarget cubeMapRenderTargets[2]` (`0x2500`) — ohne benanntes Enum, wie
es Skyrim mit `RENDER_TARGET::kMAIN` kennt.

**Ziel:** Ein RenderDoc-Capture, in dem diese Targets **beschriftet** erscheinen statt anonym,
plus ein Dokument, das die erhobene Tabelle festhält.

### Warum nicht die vollständige Inventur

Die ursprüngliche Roadmap-Fassung verlangte, alle Targets zu identifizieren und ein benanntes Enum
im commonlibf4-Fork abzuliefern. Diese Fassung wurde am 2026-08-30 verworfen, aus zwei Gründen.

**Vollständigkeit ist kein sinnvolles Ziel.** Von hundert Targets sind die meisten selten oder nie
in Gebrauch. Was die späteren Teilprojekte anfassen werden, ist eine Handvoll: Hauptfarbe, Tiefe,
Bewegungsvektoren, die G-Buffer der Deferred-Kette. Jedes weitere zu benennen kostet dieselbe
mühsame Korrelationsarbeit bei stark fallendem Nutzen.

**Der bleibende Wert liegt im Instrument, nicht in der Liste.** Ein Werkzeug, das die Targets zur
Laufzeit beschriftet, macht _jede_ künftige Identifikation billig. Eine Liste beantwortet nur die
Fragen, die man beim Erstellen schon hatte.

### Was die Vorerkundung ergeben hat

Drei Befunde vom 2026-08-30, die den Zuschnitt bestimmt haben:

-   **Die Engine benennt ihre Ressourcen nicht.** Eine Volltextsuche über zwei RenderDoc-Captures
    fand keinen einzigen Bethesda-Ressourcennamen — nur `Swap Chain Backbuffer` (RenderDocs eigene
    Beschriftung) und Shader-Semantiken wie `SV_Target`. Die Shader sind ohne Debug-Info übersetzt;
    es überlebt allein die Compiler-Versionszeichenkette. Es gibt also keine Abkürzung.
-   **`RenderTargetProperties` trägt keinen Namen**, nur Breite, Höhe, Format und Flags.
-   **Aber die Texturen kennen sich selbst.** `ID3D11Texture2D::GetDesc` liefert Breite, Höhe,
    DXGI-Format, Mip-Level, Sample-Beschreibung, Usage und Bind-Flags. Das ist die maßgebliche
    Quelle, und `REX::W32` kennt 122 DXGI-Formate namentlich.

## 2. Umfang

### In B2 enthalten

-   Ein Inventar, das über die drei Target-Arrays läuft und für jedes nicht-leere Element die
    D3D-Beschreibung ausliest.
-   Debug-Namen auf allen nicht-leeren D3D-Objekten, sodass RenderDoc sie beschriftet anzeigt.
-   Eine protokollierte Tabelle, inklusive der rohen `BSGraphics::Format`-Zahl der Engine.
-   Host-getestete Umsetzung von DXGI-Format und Bind-Flags in lesbaren Text.
-   Ein Befunddokument unter `docs/fallout4-port/`.

### Nicht in B2 enthalten

-   **Kein Enum im commonlibf4-Fork.** Es entsteht sinnvoll erst, wenn ein Teilprojekt ein
    konkretes Target braucht — dann weiß man auch, welches.
-   Keine Bindungsverfolgung über einen Hook auf `OMSetRenderTargets`. Das wäre ein weiterer
    vtable-Eingriff auf einem sehr heißen Pfad; der Gegenwert rechtfertigt das Risiko in B2 nicht.
-   Kein Eingriff in das Rendern. Wir lesen die Engine und beschriften ihre Objekte, wir verändern
    ihr Verhalten nicht.

## 3. Vorentscheidungen

| Thema                   | Entscheidung                                     | Begründung                                                                                                                                                                                                                                      |
| ----------------------- | ------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Maßgebliche Datenquelle | `ID3D11Texture2D::GetDesc`                       | `RenderTargetProperties::format` ist ein `BSGraphics::Format`, und dieses Enum ist in commonlibf4 nur vorwärtsdeklariert — die Zahl wäre für uns bedeutungslos. Die D3D-Beschreibung ist Wahrheit aus erster Hand, in standardisierten Formaten |
| Engine-Tabelle          | wird zusätzlich roh protokolliert                | Kostet nichts und liefert `textureTarget`, `copyable`, `enableFastClear` — Angaben, die D3D nicht kennt. Siehe Abschnitt 6 zum Nebengewinn                                                                                                      |
| Beschriftung            | `SetPrivateData` mit `WKPDID_D3DDebugObjectName` | Verwandelt jedes künftige Capture in ein beschriftetes. Die GUID fehlt in `REX::W32` und wird von uns definiert                                                                                                                                 |
| Zeitpunkt               | einmal, nach dem Present-Hook aus B1             | Kein Wiederholen: wann welche Targets entstehen, ist eine der Fragen, die das Inventar beantworten soll — keine, die es voraussetzen darf                                                                                                       |
| Abnahme                 | Instrument plus Befunddokument                   | Der Identifikationsteil ist der unkalkulierbare; ihn aus der Abnahme herauszuhalten macht B2 abschätzbar                                                                                                                                        |

## 4. Architektur

Drei Einheiten unter `src/Render/`.

### 4.1 `DebugName`

Eine Funktion:

```cpp
namespace Render
{
	/// Attaches a name D3D debugging tools display, RenderDoc included.
	/// Silently does nothing for a null object.
	void SetDebugName(REX::W32::ID3D11DeviceChild* a_object, std::string_view a_name) noexcept;
}
```

Setzt `WKPDID_D3DDebugObjectName`, GUID `{429b8c22-9188-4b0c-8742-acb0bf85c200}`. Die Konstante
fehlt in `REX::W32` und wird hier einmal definiert.

Bewusst eigenständig statt im Inventar vergraben: ab Teilprojekt C benennen wir damit auch unsere
**eigenen** Ressourcen. Das geerbte Skyrim-Projekt schrieb genau diese Praxis vor — dort als
`Util::SetResourceName`, mit der ausdrücklichen Regel, die GUID nirgends zu duplizieren.

### 4.2 `FormatNames`

Reine Funktionen ohne Zustand:

```cpp
namespace Render
{
	[[nodiscard]] std::string_view FormatName(REX::W32::DXGI_FORMAT a_format) noexcept;
	[[nodiscard]] std::string      BindFlagsString(std::uint32_t a_bindFlags);
}
```

Der **einzige host-testbare Teil** von B2, und er ist es wert. Eine Tabelle mit 122 Einträgen und
eine Handvoll Bit-Tests sind genau die Art Code, in der sich Fehler lautlos einnisten — und ein
falsch zugeordnetes Format würde die gesamte spätere Identifikationsarbeit in die Irre führen.

### 4.3 `TargetInventory`

```cpp
namespace Render
{
	/// Walks the engine's render target arrays once: reads each texture's D3D
	/// description, gives every non-null object a debug name, and logs a table.
	void RunTargetInventory() noexcept;
}
```

Läuft über `renderTargets[101]`, `depthStencilTargets[13]` und `cubeMapRenderTargets[2]`. Für jedes
nicht-leere Element werden alle vorhandenen Objekte benannt — bei `RenderTarget` sind das sechs:
`texture`, `copyTexture`, `rtView`, `srView`, `copySRView`, `uaView`.

Namensschema, ein eigener Name je Objekt, damit im Capture nichts zusammenfällt:

| Feld          | Name                  |
| ------------- | --------------------- |
| `texture`     | `FO4_RT_042`          |
| `copyTexture` | `FO4_RT_042_COPY`     |
| `rtView`      | `FO4_RT_042_RTV`      |
| `srView`      | `FO4_RT_042_SRV`      |
| `copySRView`  | `FO4_RT_042_COPY_SRV` |
| `uaView`      | `FO4_RT_042_UAV`      |

Entsprechend `FO4_DS_003` und `FO4_CUBE_000` mit ihren jeweiligen Ansichten. Der Index wird
dreistellig mit führenden Nullen geschrieben, damit die Namen sortierbar bleiben. Der Aufbau folgt
der Konvention des geerbten Projekts, das Ansichten mit dem Suffix der Ansichtsart kennzeichnete.

## 5. Ablauf und Zeitpunkt

Einmal, direkt nachdem `InstallSwapChainHook` aus B1 erfolgreich war. Fällt der Kreuzvergleich dort
durch, läuft auch das Inventar nicht — ohne gesicherten Renderer-Zugriff wäre jede gelesene Zahl
wertlos.

Protokolliert wird auf Info-Ebene. Bei erwarteten dreißig bis vierzig belegten Einträgen ist das
eine überschaubare Menge, und die Tabelle ist der eigentliche Ertrag des Teilprojekts — sie gehört
in das Log, das man ohnehin anschaut, nicht hinter einen Debug-Schalter.

## 6. Datenquelle und der Nebengewinn

Maßgeblich ist `ID3D11Texture2D::GetDesc`. Zusätzlich wird die Engine-Tabelle aus
`RenderTargetManager::GetSingleton()` mitprotokolliert:

```
RenderTargetProperties        renderTargetData[100];       // 000
DepthStencilTargetProperties  depthStencilTargetData[12];  // C80
CubeMapRenderTargetProperties cubeMapRenderTargetData[1];  // DA0
std::uint32_t                 renderTargetID[100];         // DC4
```

Daraus fällt etwas ab, das nicht geplant war: Protokolliert man die undurchsichtige
`BSGraphics::Format`-Zahl **neben** dem echten DXGI-Format derselben Textur, ergibt sich die
Zuordnung der beiden Enums von selbst. Das ist Wissen, das derzeit niemand hat und das sich später
an commonlibf4 zurückgeben ließe. Es ist kein Abnahmekriterium — aber die Daten dafür kosten nichts
und werden deshalb erhoben.

**Zu klären ist außerdem eine Unstimmigkeit**, die bei der Vorerkundung auffiel: die Arraygrößen
weichen zwischen den beiden Quellen ab. `RendererData` führt 101, 13 und 2 Einträge, der
`RenderTargetManager` dagegen 100, 12 und 1. Je einer mehr auf der Renderer-Seite. Naheliegend ist
der Backbuffer, aber das ist eine Vermutung. Das Inventar soll die Frage beantworten, nicht
umgehen: protokolliert werden beide Reihen vollständig, sodass sich der überzählige Eintrag
identifizieren lässt.

## 7. Fehlerbehandlung

| Fall                                                | Verhalten                                                                                    |
| --------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| Eintrag leer (Textur null)                          | überspringen, **nicht** melden — bei sechzig bis siebzig Leereinträgen wäre das nur Rauschen |
| `RenderTargetManager::GetSingleton()` liefert null  | Engine-Tabelle auslassen, D3D-Teil trotzdem durchführen, einmal protokollieren               |
| `SetDebugName` schlägt fehl                         | **einmal** protokollieren, nicht pro Objekt; das Inventar läuft weiter                       |
| `GetDesc` nicht aufrufbar, weil die Textur null ist | fällt unter „Eintrag leer"                                                                   |

Nichts davon bricht das Laden ab oder wirft eine Ausnahme. Das Inventar ist ein Diagnosewerkzeug;
es darf ein laufendes Spiel unter keinen Umständen gefährden.

## 8. Tests und Abnahme

### 8.1 Ohne Spiel prüfbar

**Host-Test für `FormatNames`.** Geprüft werden: eine Auswahl gängiger Formate liefert den
erwarteten Namen (`R8G8B8A8_UNORM`, `R16G16B16A16_FLOAT`, `D24_UNORM_S8_UINT`, `R11G11B10_FLOAT`);
ein unbekannter Zahlenwert liefert `UNKNOWN` statt eines leeren Strings oder eines Absturzes;
`BindFlagsString` setzt einzelne Flags korrekt um und verbindet mehrere Flags in stabiler
Reihenfolge.

Zur Rückfallzeichenkette: `FormatName` gibt `std::string_view` zurück und kann deshalb keinen
Wert einbetten. Der Aufrufer protokolliert die rohe Zahl ohnehin daneben, sodass ein unbekanntes
Format am Zahlenwert erkennbar bleibt.

Dazu wie gehabt: Kaltbau aus frischem Klon, Warmbau als No-Op, `tools/verify-plugin.ps1`,
bestehende Tests aus A und B1 weiterhin grün.

### 8.2 Nur im Spiel prüfbar

1. Das Log enthält die Tabelle: Index, Breite, Höhe, DXGI-Format im Klartext, Bind-Flags, dazu die
   rohe Engine-Formatzahl.
2. Die Zahl der belegten Einträge ist plausibel und die Auflösungen passen zu 2560×1440
   beziehungsweise dessen Teilern.
3. Ein RenderDoc-Capture zeigt bei den Ressourcen unsere Namen (`FO4_RT_…`).
4. Das Spiel läuft stabil, kein neues Crashlog.

### 8.3 Abnahmekriterien

B2 gilt als abgeschlossen, wenn 8.1 und 8.2 erfüllt sind, das Befunddokument unter
`docs/fallout4-port/` liegt und die Roadmap fortgeschrieben ist.

## 9. Annahmen, die B2 bestätigen muss

-   Die Textur-Zeiger in `RendererData::renderTargets[i].texture` sind zum Zeitpunkt des Inventars
    gültig, und ein erheblicher Teil der 101 Einträge ist belegt.
-   `SetPrivateData` mit `WKPDID_D3DDebugObjectName` wirkt sich auf die Anzeige in RenderDoc aus,
    auch wenn die Namen erst nach der Erzeugung der Ressourcen gesetzt werden.
-   Die GUID `{429b8c22-9188-4b0c-8742-acb0bf85c200}` ist die richtige. Sie ist weithin
    dokumentiert; bestätigt ist sie erst, wenn RenderDoc die Namen zeigt.
-   Der überzählige Eintrag auf der Renderer-Seite ist der Backbuffer.

## 10. Übergabe

B2 hinterlässt ein Befunddokument mit der Tabelle, beschriftete Captures für alle künftige Arbeit,
eine wiederverwendbare `SetDebugName`-Funktion für Teilprojekt C, und — sofern die Korrelation
aufgeht — die Zuordnung von `BSGraphics::Format` zu `DXGI_FORMAT`.

Das benannte `RENDER_TARGET`-Enum entsteht später, wenn ein Teilprojekt ein konkretes Target
braucht. Dann ist auch klar, welches.
