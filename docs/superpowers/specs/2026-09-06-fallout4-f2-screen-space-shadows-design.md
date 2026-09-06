# Teilprojekt F2 — Screen-Space Shadows

Spec, Stand 2026-09-06. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A bis F1 voraus, insbesondere `2026-08-30-fallout4-shader-pipeline-design.md` (Übersetzen und
Einschleusen), `2026-08-30-fallout4-target-inventar-design.md` (die Ziele der Engine) und
`2026-09-05-fallout4-f1-performance-overlay-design.md` (die Zeitmessung, an der F2 abgenommen wird).

## 1. Kontext und Ziel

Bis hierher hat der Port Werkzeuge gebaut: einen Present-Hook, ein benanntes Target-Inventar, einen
Weg, einen Engine-Shader zu ersetzen, ein Feature-Framework, ein Overlay und eine Zeitmessung.
Gezeichnet hat er nie etwas Eigenes.

**Ziel:** Der erste eigene Renderpass. Ein Kontaktschatten, der den Tiefenpuffer der Engine liest,
eine Maske erzeugt und sie in das Bild zurückgibt — sichtbar im laufenden Spiel, meßbar in der
Tafel aus F1.

**Abnahmekriterium der Roadmap:** „Sichtbarer Effekt plus CPU-/GPU-Zahlen."

F2 ist ausdrücklich als **Naht** zugeschnitten. Die Roadmap sagt: eigener Pass, G-Buffer lesen,
Ergebnis zurück in die Beleuchtung. Was hier entsteht, erben F3 bis F11 unverändert; das Feature
selbst ist der Beleg, daß die Naht trägt, nicht der eigentliche Ertrag.

### Was F2 erbt

-   **`Render::VTablePatch`** (B1) — Tausch eines vtable-Eintrags, auch an einer Tabelle, die über
    ihre eigene Adresse bekannt ist statt über eine Instanz. Auf dem Host gegen eine synthetische
    vtable geprüft.
-   **Das Target-Inventar** (B2) — die 101 Ziele der Engine sind vermessen und benannt. `FO4_DS_002`
    ist die Szenentiefe, `FO4_RT_058` und `FO4_RT_059` sind die beiden Lichtziele.
-   **`Shader::ShaderCompiler` und `Shader::ShaderSource`** (C) — Übersetzen mit `D3DCompile`, dazu
    ein Lader, der `#include` selbst spleißt, den Wurzelpfad nicht verlassen läßt und die Liste der
    beteiligten Dateien zurückgibt.
-   **`Util::FileWatch`** (C) — Dateiüberwachung über `last_write_time`, weil `REX::W32` weder
    `ReadDirectoryChangesW` noch `FindFirstChangeNotification` deklariert.
-   **`Features::Feature` und `Features::Registry`** (D1) — Lebenszyklus mit `Declare`, `Setup`,
    `Frame`, `Shutdown`, drei Zuständen und Abbau in umgekehrter Reihenfolge.
-   **Das Einstellungsschema und das Panel** (E2) — ein Feature bekommt seine Oberfläche allein
    dadurch, daß es Einstellungen deklariert. Kein ImGui in `src/Features/`.
-   **`Render::PassScope`** (F1) — mißt einen benannten Pass in CPU und GPU und benennt ihn zugleich
    im Capture.

### Der Befund, der den Entwurf bestimmt

Der Messspike vor F hat gezeigt, daß Fallout 4 seine Welt über `kDFPrepass` zeichnet, nicht über
`kLighting`. Die Vorlage der Skyrim Community Shaders bindet ihre Schattenmaske auf `t45` und liest
sie in gepatchten `BSLightingShader`-Permutationen — dieser Weg ist für Fallout 4 versperrt, und ihn
zu öffnen ist F12, nicht F2.

F2 muß die Maske deshalb **ohne** eine einzige ersetzte Engine-Permutation ins Bild bringen.

## 2. Umfang

### In F2 enthalten

1. Ein Einhängepunkt mitten im Frame, zwischen G-Buffer und Beleuchtungs-Composite.
2. Sieben Bausteine Render-Fundament: Compute- und Vertex-Übersetzung, eigene Texturen und
   Konstantenpuffer, Zugriff auf die Ziele der Engine, ein Zustandswächter, ein Vollbildpass, die
   Frame-Phase.
3. Der Bend-Raymarch als Compute-Shader, mit dem CPU-Teil, der seine Teilzeichnungen aufbaut.
4. Die Modulation der beiden Lichtziele durch die Maske.
5. Ein Feature `ScreenSpaceShadows` mit vier Reglern, Beschriftungen im Katalog und einem eigenen
   Addon-Archiv.
6. Zwei neue Host-Tests, ein erweiterter.

### Nicht in F2 enthalten

-   **Ersetzen einer Engine-Permutation.** Das ist F12, mitsamt dem Permutations-Cache. F2 faßt
    weder `kDFLight` noch `kDFComposite` inhaltlich an, sondern schreibt nur auf deren Ziele.
-   **Ein benanntes `RENDER_TARGET`-Enum.** Die Lücke in CommonLibF4 bleibt bestehen. F2 legt
    benannte Konstanten für die vier Slots an, die es braucht, und keine mehr.
-   **Zeitliche Filterung, Entrauschen, Nachbarschaftsglättung.** Bend liefert eine brauchbare Maske
    in einem Durchgang. Ein Filter ist ein eigenes Vorhaben mit eigenem Nutzen-Nachweis.
-   **Schatten von Punkt- und Spotlichtern.** Bend arbeitet je Lichtquelle; die Sonne ist eine. Die
    Vorlage kann nicht mehr, und der Bedarf ist unbelegt.
-   **Dynamische Auflösung.** Fallout 4 kann sie, wir setzen `1,1` ein und halten das fest.
-   **Ein eigener Wächterthread.** `ImagespaceTint` hat einen und trägt damit den in D2 gefundenen
    Fehler; F2 fragt im Frame ab.

## 3. Vorentscheidungen

Aus dem Brainstorming vom 2026-09-06, jeweils mit Begründung:

-   **F2 bleibt ungeteilt.** Fundament und Feature stecken in einer Zeile, aber ein Teilprojekt
    „Fundament" hätte kein Abnahmekriterium außer „übersetzt". Das Fundament wird am Feature
    abgenommen; die Spec gliedert statt dessen in Abschnitte.
-   **Der Einhängepunkt ist ein vtable-Patch auf `SetupTechnique`.** Slot 02 der `BSShader`-vtable,
    im Spiel belegt. Die Alternative — ein Hook auf `ID3D11DeviceContext::OMSetRenderTargets` — ist
    engine-unabhängig, läuft aber tausendfach je Frame durch unseren Code. Ein Imagespace-Pass als
    Taktgeber scheidet aus, weil diese Pässe fast alle hinter der Beleuchtung liegen.
-   **Der Rückweg ist die Modulation von `RT_058` und `RT_059`.** Beide sind `R11G11B10_FLOAT`, beide
    werden von `kDFLight` beschrieben und danach von `kDFComposite` weiterverwendet — das übliche
    Paar Diffus/Spekular. Gedämpft wird damit genau das direkte Licht. Die AO-Kette der Engine
    (`RT_031`, halbe Auflösung, über die `ApplyAO`-Permutation angewandt) wäre der risikoärmere
    Mechanismus, dämpft aber das Ambiente statt der Sonne; eine Multiplikation hinter dem Composite
    wäre am sichersten sichtbar und am gröbsten falsch.
-   **Das Verfahren ist Bend SSS.** Sonys Raymarch, Apache-2.0, mit GPL-3.0 verträglich, 355 Zeilen
    HLSL liegen bereits unter `features/Screen-Space Shadows/`, 245 Zeilen CPU-Teil im Tag
    `skyrim-base`. Er fächert die Strahlen in Lichtrichtung als Wellenfronten auf, statt je Pixel
    einzeln zu marschieren, und sein CPU-Teil ist ohne Spiel testbar.
-   **Moduliert wird durch eine Zeichnung mit Multiplikationsmischung.** `RT_058` und `RT_059` tragen
    laut Inventar `RENDER_TARGET | SHADER_RESOURCE` und **kein** UAV; ein Compute-Shader kann nicht
    hineinschreiben. Kopieren, rechnen und zurückkopieren wären vier Vollbildkopien je Frame.
-   **Der vtable-Patch gehört nicht dem Feature.** Das Overlay kann ein Feature mitten im Spiel
    abschalten. Einen vtable-Eintrag zurückzunehmen, während ein anderer Thread darin steht, ist ein
    Rennen. Der Patch gehört deshalb einem eigenen Baustein, wird einmal installiert und bleibt
    stehen — wie der Present-Hook aus B1. Features tragen sich ein und aus.

## 4. Die Naht im Frame

### 4.1 Wo wir laufen

`Render::FramePhase` patcht Slot 02 in der Haupt-vtable von `BSDFCompositeShader`. Beim **ersten**
Aufruf je Frame gilt:

-   `kDFPrepass` hat den G-Buffer gefüllt — `RT_022 RT_020 RT_057 RT_024 RT_023 RT_029` auf `DS_002`.
-   `kDFLight` hat sein direktes Licht nach `RT_058` und `RT_059` geschrieben.
-   `kDFComposite` hat noch nichts gezeichnet.

Wir laufen dort, dann rufen wir den gemerkten Originalzeiger und geben dessen Ergebnis weiter.

`BSDFLightShader` wäre der falsche Anker: dessen erster Aufruf liegt vor der Beleuchtung, die
Lichtziele sind dann leer.

### 4.2 Die Adressen

Offline gegen `version-1-11-240-0.bin` geprüft, 652.306 Einträge, ohne Spielstart:

| Klasse                | ID `[0]`  | Offset      | ID `[1]`  | Offset      |
| --------------------- | --------- | ----------- | --------- | ----------- |
| `BSDFCompositeShader` | `435278`  | `0x29161a8` | `342198`  | `0x2916218` |
| `BSDFLightShader`     | `65824`   | `0x29191b8` | `1555006` | `0x2919228` |
| `BSDFPrePassShader`   | `1091304` | `0x29157a0` | `997876`  | `0x2915810` |

`[1]` liegt bei allen dreien genau `0x70` hinter `[0]`: das ist die vtable des zweiten
Basis-Subobjekts (`BSShader` erbt von `NiRefObject` und `BSReloadShaderI`). Wir patchen **`[0]`**.

`IDs_VTABLE.h` führt einwertige `REL::ID`, weil Fallout 4s Adressbibliothek stabile vtable-IDs
vergibt, die in og, ng und ae auf verschiedene Offsets derselben Tabelle auflösen.

### 4.3 Die Signatur

```cpp
virtual bool SetupTechnique(std::uint32_t a_currentPass);  // Slot 02
```

Unser Ersatz ist eine freie Funktion `bool(RE::BSShader*, std::uint32_t)`.

**Die vtable-Slotnummern von `BSShader` stimmen** — im Spiel belegt, Slot 02 ist `SetupTechnique`,
Slot 09 ist `GetTechniqueName`. **Die Datenoffsets derselben Klasse stimmen nicht**: jedes Feld nach
`shaderType` liegt `0x78` zu niedrig, `sizeof` ist `0x190` statt `0x118`. F2 liest **kein** Feld von
`BSShader`; wer es doch tut, nimmt `src/Shader/BSShaderLayout.h`.

### 4.4 Einmal je Frame

`FramePhase` vergleicht gegen die Framenummer aus B1. Der zweite Aufruf im selben Frame löst nicht
aus, der erste im nächsten schon. Läuft das Composite in einem Frame gar nicht — Ladebildschirm,
Pip-Boy, Hauptmenü —, läuft kein Rückruf, und das ist richtig.

Vor den Rückrufen prüft `FramePhase` nichts Inhaltliches. Was ein Rückruf für sich braucht, prüft er
selbst.

## 5. Das Render-Fundament

Sieben Bausteine, alle so geschnitten, daß F3 bis F11 sie unverändert erben. Keiner weiß etwas von
Schatten.

### 5.1 `Shader::ShaderCompiler`, erweitert

Heute: `CompilePixelShader(quelle, name, einstieg)`. Daraus wird

```cpp
CompileResult Compile(
    std::string_view a_source,
    const std::string& a_sourceName,
    const std::string& a_entryPoint,
    const std::string& a_profile,
    std::span<const std::pair<std::string, std::string>> a_defines);
```

mit drei dünnen Hüllen für `ps_5_0`, `vs_5_0` und `cs_5_0`. Die bestehende Pixel-Hülle behält ihre
Signatur, damit `ImagespaceTint` unverändert bleibt.

Die Defines sind nicht Bequemlichkeit: Bend trägt `SAMPLE_COUNT` als Übersetzungskonstante, nicht
in einem Puffer — der Shader baut daraus `READ_COUNT` und daraus die Größe seines
`groupshared`-Feldes. (`WAVE_SIZE` ist dagegen kein Define von außen: `bend_sss_gpu.hlsli` setzt es
selbst auf `64`, und der CPU-Teil muß mit demselben Wert gerufen werden.) `D3DCompile` erwartet ein
`REX::W32::D3D_SHADER_MACRO`-Feld mit einem Nullabschluß; die Zeichenketten müssen den Aufruf
überleben. Beide Typen sind in `REX::W32` deklariert (`D3D.h`, `D3D11.h`); `<d3d11.h>` bleibt wie
im ganzen Projekt draußen.

Es wird weiterhin **kein** Include-Handler übergeben: `REX::W32::ID3DInclude` erbt fälschlich von
`IUnknown`, während die echte Schnittstelle keine Basis und zwei vtable-Einträge hat. `ShaderSource`
spleißt vorher.

### 5.2 `Render::Resources`

```cpp
class Texture {          // Texture2D + optional SRV + optional UAV
    bool Create(const REX::W32::D3D11_TEXTURE2D_DESC&, std::string_view a_debugName);
    void Release() noexcept;      // idempotent
};

class ConstantBuffer {   // dynamisch, D3D11_MAP_WRITE_DISCARD
    template <class T> void Update(const T&) noexcept;
};
```

Beide sind RAII, geben in `Release()` alles zurück und vertragen einen zweiten Aufruf. Jede erzeugte
Ressource bekommt sofort über `Render::DebugName` einen Namen, damit sie im Capture nicht namenlos
auftaucht.

`ConstantBuffer` rundet die Größe auf 16 auf und prüft das Ausrichten des übergebenen Typs zur
Übersetzungszeit.

### 5.3 `Render::Targets`

Zugriff auf die Ziele der Engine über ihren Slot. Es ist nichts zu erzeugen:

| Was             | Woher                                              |
| --------------- | -------------------------------------------------- |
| Farbziel-RTV    | `RendererData::renderTargets[i].rtView`            |
| Farbziel-SRV    | `RendererData::renderTargets[i].srView`            |
| Tiefensicht-SRV | `RendererData::depthStencilTargets[i].srViewDepth` |

Die Nummer im Namen `FO4_RT_058` **ist** der Feldindex; das Inventar aus B2 beschriftet mit
`std::format("FO4_RT_{:03}", i)` über denselben Index.

Benannte Konstanten legt F2 für vier Slots an und keinen mehr:

```cpp
inline constexpr std::size_t kSceneDepth   = 2;   // FO4_DS_002
inline constexpr std::size_t kLightDiffuse = 58;  // FO4_RT_058
inline constexpr std::size_t kLightSpecular= 59;  // FO4_RT_059
inline constexpr std::size_t kGBufferNormal= 20;  // FO4_RT_020, für F3 aufwärts
```

Jeder Zugriff prüft den Index gegen die Feldgröße und den Zeiger gegen `nullptr` und meldet einen
Fehlschlag mit dem Slot im Text — eine Diagnose, die nicht sagt, welche Prüfung riß, ist keine.

### 5.4 `Render::StateGuard`

Sichert im Konstruktor und gibt im Destruktor zurück:

-   Ausgabeziele samt Tiefensicht (`OMGetRenderTargets`)
-   Mischzustand mit Faktor und Abtastmaske (`OMGetBlendState`)
-   Viewport (`RSGetViewports`)
-   Vertex-, Pixel- und Compute-Shader
-   Die Plätze, die wir anfassen: `PSSetShaderResources(0..1)`, `CSSetShaderResources(0)`,
    `CSSetUnorderedAccessViews(0)`, `CSSetSamplers(0)`, `CSSetConstantBuffers(1)`

Alles, was `OMGet*` und `*Get*` zurückgeben, kommt mit einer erhöhten Referenz; der Wächter gibt
jede davon wieder frei. Das ist der einzige Ort im Projekt, an dem wir COM-Referenzen halten — die
Regel aus B1, Engine-Objekte weder zu referenzieren noch freizugeben, gilt für Zeiger, die wir uns
holen, nicht für die, die D3D uns mit einer Referenz übergibt.

**Ein Detail, das zählt.** `DS_002` hängt zum Zeitpunkt unseres Einstiegs noch als Tiefensicht am
Ausgabe-Merger. Es zugleich als SRV zu binden brächte D3D11 dazu, es stillschweigend zu lösen. Der
Pass löst deshalb zuerst die Ziele (`OMSetRenderTargets(0, nullptr, nullptr)`), rechnet, bindet dann
die beiden Lichtziele **ohne** Tiefensicht für die Zeichnung, und der Wächter stellt danach her, was
war.

### 5.5 `Render::FullscreenPass`

Ein Dreieck ohne Vertexpuffer, aus `SV_VertexID` gerechnet, Topologie `TRIANGLELIST`, drei
Eckpunkte, kein Eingabelayout. Der Vertex-Shader wird einmal übersetzt und von allen folgenden
Features geteilt; sein Quelltext liegt in der Basis, nicht im Addon, weil er nicht zu einem Feature
gehört — siehe 7.2.

### 5.6 `Render::FramePhase`

Zwei Klassen, nicht eine — aus demselben Grund, aus dem F1 die Zeitmessung als zwei Funktionszeiger
in `Features::Registry` reicht, statt dort nach dem Profiler zu greifen:

-   **`Render::PhaseDispatcher`** hält, wer einmal je Frame gerufen wird, und kennt weder D3D noch
    die Engine. Damit ist er ohne Spiel prüfbar.
-   **`Render::FramePhase`** besitzt den vtable-Patch aus Abschnitt 4, wird einmal installiert und
    bleibt für die Prozeßlaufzeit stehen. Er reicht die Framenummer aus B1 an den Dispatcher.

Schnittstelle:

```cpp
using Token = std::uint32_t;
Token Subscribe(std::string_view a_name, std::function<void()> a_callback);
void  Unsubscribe(Token) noexcept;
```

Ein Rückruf läuft in einem `Render::PassScope` mit seinem Namen, den `FramePhase` beim Eintragen
darumlegt — damit F1 jeden Abnehmer mißt und nicht nur die, die daran gedacht haben.

Aus- und Eintragen aus einem laufenden Rückruf heraus ist erlaubt. Die Abnehmer liegen deshalb in
einem **festen Feld** statt in einem `std::vector`: nichts zieht um, während ein `std::function`
gerade ausgeführt wird. Ein während des Durchlaufs Ausgetragener läuft nicht mehr, seine Funktion
wird aber erst nach dem Durchlauf freigegeben — sonst zerstörte man das Aufrufziel unter dem
laufenden Aufruf. Ein während des Durchlaufs Eingetragener wartet auf den nächsten Frame, weil die
Laufzahl vor dem ersten Rückruf feststeht.

### 5.7 Nachladen bei Dateiänderung

`Util::FileWatch` wird in `Frame()` abgefragt und gleich dort neu übersetzt — auf dem Render-Thread,
ohne Thread und ohne Sperre. Übersetzen kostet Millisekunden und passiert nur bei einer Änderung.
Dasselbe Verfahren trägt die Neuübersetzung, wenn sich die Abtastzahl ändert.

Der in D2 gefundene Fehler von `ImagespaceTint` wird dabei nicht mitgeerbt: der Wächter wird auch
dann zurückgesetzt, wenn das Übersetzen scheiterte, sonst bliebe er nach einer fehlenden Datei für
die ganze Sitzung leer.

## 6. Der Pass

### 6.1 Datenfluß

```
DS_002.srViewDepth ──► Bend-Raymarch (cs_5_0) ──► SSS_Mask (R8_UNORM, voll, SRV+UAV)
                                                        │
Sky::sun->light ──┐                                     ▼
cameraData.viewProjMat ──► BuildDispatchList (CPU)   Vollbilddreieck, ZERO/SRC_COLOR
                                                        │
                                                        ▼
                                              RT_058 und RT_059
```

Zwei `PassScope`-Bereiche: `ScreenSpaceShadows/RayMarch` und `ScreenSpaceShadows/Modulate`,
geschachtelt in den Bereich des Rückrufs.

### 6.2 Die Eingaben

**Sonnenrichtung.** `RE::Sky::GetSingleton()->sun->light` ist ein `NiDirectionalLight`. Das ist der
Weg, den die Vorlage über `ShadowSceneNode` nimmt — eine der bekannten Lücken in CommonLibF4 —, und
er ist hier nicht nötig: `Sun::light` liegt bei `0x38` und ist deklariert.

**Kameramatrix.** `RendererShadowState::cameraData.viewProjMat` bei `0x6D0`, deklariert und
`static_assert`-geprüft. `BuildDispatchList` will die Lichtrichtung in Clip-Koordinaten: die
negierte, normierte Weltrichtung mit `w = 0` durch die transponierte View-Projektion.

**Tiefe.** `FO4_DS_002` ist `R24G8_TYPELESS`, `srViewDepth` liefert sie als
`R24_UNORM_X8_TYPELESS`. Das trifft genau den Nicht-`TERRAIN_BLENDING`-Zweig des geerbten Shaders,
in dem `DepthTexture` als `Texture2D<unorm float>` deklariert ist — der Zweig, den wir behalten und
dessen Alternative ersatzlos entfällt.

### 6.3 Abbruchbedingungen

Vor allem anderen, in dieser Reihenfolge, jede mit eigener Logzeile beim ersten Auftreten:

1. `Sky::GetSingleton()` ist null, oder `mode` ist nicht `Sky::Mode::kFull` (`0x3`; die übrigen
   sind `kNone`, `kInterior`, `kSkyDomeOnly`). `mode` ist ein `REX::TEnumSet<Mode, std::uint32_t>`
   bei `0x36C`. Ohne Himmel gibt es keine Sonne und damit keinen Sonnenschatten.
2. `sun` oder `sun->light` ist null.
3. Ein Ziel aus `Render::Targets` fehlt.
4. Die Maske hat nicht die Größe des Bildes — die Auflösung hat sich geändert, die Maske wird neu
   angelegt und dieser Frame ausgelassen.

Wird abgebrochen, wird auch nicht moduliert. Die Maske bleibt unbenutzt statt veraltet angewandt.

### 6.4 Der Raymarch

Der Shader bleibt Bends, mit drei Änderungen: der `TERRAIN_BLENDING`-Zweig fällt weg, `SharedData`
der Vorlage wird durch unseren eigenen Konstantenpuffer ersetzt, und `DynamicRes` steht auf `1,1`.

Der Konstantenpuffer liegt auf `b1` und trägt, in dieser Reihenfolge und auf 16 ausgerichtet:
`LightCoordinate` (4 × float), `WaveOffset` (2 × int), `FarDepthValue`, `NearDepthValue`,
`InvDepthTextureSize` (2 × float), `DynamicRes` (2 × float), dann die vier Regler.

`BuildDispatchList` liefert **bis zu acht** Teilzeichnungen — `DispatchList::Dispatch` ist ein Feld
von acht, typisch sind ein bis zwei bei einer Sonne außerhalb des Bildes und vier bis sechs bei
einer im Bild. Je Teilzeichnung wird der Puffer neu
beschrieben und einmal `Dispatch` gerufen — die Aufteilung ist Bends Wellenfront-Schema, nicht eine
Schleife, die man zusammenfassen könnte.

**Die Abtastzahl** wird aus der Auflösung gegen 1920×1080 skaliert und auf Achterschritte gerundet,
damit eine schwankende Auflösung nicht ständig neu übersetzt. Ändert sie sich, wird der
Compute-Shader mit neuem `SAMPLE_COUNT` neu übersetzt; das geschieht in `Frame`, nicht im Rückruf.

### 6.5 Die Modulation

`RT_058` und `RT_059` werden gemeinsam als Ziele gebunden, ohne Tiefensicht. Der Mischzustand ist

```
SrcBlend = ZERO, DestBlend = SRC_COLOR, BlendOp = ADD
```

für Farbe und Alpha, `RenderTargetWriteMask = ALL` für beide Ziele. Die Grafikkarte rechnet damit
`dest = dest × src`. Der Pixel-Shader gibt schlicht den Maskenwert auf beide Ausgänge, gelesen mit
Punktabtastung an der Pixelmitte.

Eine Zeichnung, drei Eckpunkte, beide Ziele. Nichts wird kopiert.

## 7. Das Feature

`src/Features/ScreenSpaceShadows.{h,cpp}`, abgeleitet von `Features::Feature`, registriert in
`RegisterAll`.

| Methode    | Was sie tut                                                                                                                                         |
| ---------- | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Name`     | `"ScreenSpaceShadows"` — zugleich der Präfix der Einstellungspfade, deshalb unveränderlich                                                          |
| `Declare`  | Der Schalter über `DeclareFeature`, vorbelegt **an**, dazu vier Regler. Faßt nichts an außer `Settings`                                             |
| `Setup`    | Übersetzt Compute und Vertex, legt Maske, Konstantenpuffer und Punkt-Rand-Sampler an, trägt den Rückruf in `FramePhase` ein                         |
| `Frame`    | Fragt den Dateiwächter, liest die Regler frisch, übersetzt bei geänderter Abtastzahl neu                                                            |
| `Shutdown` | Trägt den Rückruf aus, gibt Maske, Puffer, Sampler und beide Shader zurück. Nach einem halb gescheiterten `Setup` aufrufbar, und im laufenden Spiel |

`Setup` schlägt nur an Dingen fehl, die wieder fehlschlügen: ein Shader, der sich nicht übersetzen
läßt, eine Textur, die das Gerät ablehnt. Auf ein noch nicht bereites Spiel wartet es nicht — das
tut `Frame`, und die Abbruchbedingungen aus 6.3 tun es je Frame.

### 7.1 Die Einstellungen

| Pfad                                   | Art      | Bereich    | Vorgabe | Bedeutung                                 |
| -------------------------------------- | -------- | ---------- | ------- | ----------------------------------------- |
| `ScreenSpaceShadows/enabled`           | Schalter | —          | an      | Der Feature-Schalter aus `DeclareFeature` |
| `ScreenSpaceShadows/sampleCount`       | Regler   | 1–4        | 1       | Vervielfacher der Abtastungen             |
| `ScreenSpaceShadows/surfaceThickness`  | Regler   | 0,005–0,05 | 0,02    | Angenommene Dicke der Oberflächen         |
| `ScreenSpaceShadows/bilinearThreshold` | Regler   | 0,02–1,0   | 0,02    | Kantenschwelle beim Zwischenrechnen       |
| `ScreenSpaceShadows/shadowContrast`    | Regler   | 0–4        | 1,0     | Härte des Schattenübergangs               |

Skyrims `Enable` innerhalb der Bend-Einstellungen entfällt: der Feature-Schalter sagt dasselbe.

Jeder Eintrag bekommt `.Label(…)` und `.Help(…)`; `tools/extract-i18n.py --write` erweitert `en.json`
und muß danach ohne `--write` grün melden.

Die Regler werden nach der Regel aus `CLAUDE.md` **bei jeder Verwendung** frisch gelesen. Der einzige
gespeicherte Wert ist die zuletzt übersetzte Abtastzahl, weil sie die Neuübersetzung auslöst.

### 7.2 Auslieferung

Unsere Shader liegen sämtlich unter `Data/Shaders/FO4/` — das ist die Wurzel, die
`ImagespaceTint` benutzt (`Util::DataDirectory() / "Shaders" / "FO4"`), und `tools/package.ps1`
kopiert `package/Shaders/FO4` genau dorthin. Ein Feature legt seinen Baum `Data`-relativ an und
trifft damit dieselbe Wurzel:

```
package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/RaymarchCS.hlsl
package/Features/ScreenSpaceShadows/Shaders/FO4/ScreenSpaceShadows/bend_sss_gpu.hlsli
package/Shaders/FO4/Fullscreen.hlsl
```

Das Feature-Verzeichnis bekommt **keinen** `CORE`-Marker, wird also ein eigenes Addon-Archiv. F2 ist
das erste echte Feature und damit die erste Gelegenheit, D2s Aufteilung an etwas zu prüfen, das
nicht `ImagespaceTint` heißt.

`package/Shaders/FO4` **gibt es bisher nicht** — die Kopierregel führt den Pfad seit D2, aber
niemand hat je etwas hineingelegt, weil `ImagespaceTint` seinen Shader als Addon ausliefert.
`Fullscreen.hlsl` ist sein erster Bewohner, und es gehört dorthin, weil es keinem Feature gehört:
ein Spieler, der nur die Basis installiert, muß es trotzdem haben, sobald das erste Addon kommt.
`verify-package.ps1` ist entsprechend zu erweitern.

Bends Apache-2.0-Kopfzeilen bleiben in beiden übernommenen Dateien unangetastet.

## 8. Prüfung

### 8.1 Host-Tests

**`BendDispatchTests`** — `Bend::BuildDispatchList` ist reine CPU-Mathematik ohne eine Zeile D3D.
Geprüft an bekannten Eingaben: Sonne im Zenit, Sonne hinter der Kamera, Sonne am Bildrand, und ein
entartetes Sichtfeld der Breite null. Erwartet werden eine Teilzeichnungszahl zwischen eins und
**acht**, Wellenzahlen größer null in jeder Teilzeichnung, und Wellenversätze, die zusammen das
ganze Sichtfeld abdecken. Das ist der Teil des Verfahrens, in dem sich ein Vorzeichenfehler
versteckt.

Der übernommene Kopf definiert `BuildDispatchList` im Header **ohne `inline`**. In zwei
Übersetzungseinheiten eingebunden — Test und Feature — gäbe das ein doppeltes Symbol. Er wird
deshalb in genau einer Einheit eingebunden, hinter `src/Features/ScreenSpaceShadows/BendDispatch.h`,
die nur deklariert.

**`PhaseDispatcherTests`** — der Zustandsautomat hinter der Frame-Phase, ohne Spiel, wie
`Menu::Gate` in E1. Er sitzt als eigene Klasse `Render::PhaseDispatcher` neben dem Patch, genau
damit er ohne Engine prüfbar bleibt. Geprüft: der zweite
Aufruf im selben Frame löst nicht aus; der erste im nächsten schon; ein ausgetragener Rückruf läuft
nicht mehr; Austragen aus dem laufenden Rückruf heraus wirkt erst danach und stürzt nicht ab; zwei
Rückrufe laufen in Eintragungsreihenfolge.

**`ShaderCompilerTests`, erweitert** — `cs_5_0` und `vs_5_0` übersetzen; ein Define erreicht den
Shader, belegt an einem Quelltext, der sich ohne das Define nicht übersetzen läßt; ein unbekanntes
Profil wird abgelehnt statt durchgereicht.

Jeder grüne Test wird danach absichtlich gebrochen, die erwarteten Fehlschläge werden vorher
benannt, und **vor** dem Ausführen wird geprüft, daß der Bau geglückt ist — eine Mutation, die nicht
übersetzt, hinterläßt das alte Executable, und dessen Lauf sieht aus wie ein bestandener Test. Eine
Mutation, die nicht fällt, ist ein Befund über den Test.

Mutationen werden mit dem Edit-Werkzeug gesetzt und mit dem Edit-Werkzeug zurückgenommen, nie mit
`git checkout`.

### 8.2 Was ausdrücklich nicht host-getestet wird

`Render::StateGuard`, `Render::Targets`, `Render::Resources` und `Render::FullscreenPass` brauchen
ein D3D-Gerät und eine laufende Engine. Eine Attrappe dafür wäre mehr Gerüst als Gegenstand. Sie
werden im Abnahmelauf geprüft und in der Roadmap als so belegt geführt, nicht stillschweigend
abgehakt.

## 9. Risiken und Gabelungen

**Das tragende Risiko** ist die Annahme, `RT_058` und `RT_059` trügen direktes Licht getrennt vom
Rest. Sie ist begründet — beide sind `R11G11B10_FLOAT`, beide werden von `kDFLight` beschrieben —
aber unbelegt.

Entscheidend ist ihre Reichweite: sie betrifft **allein Schritt 3 des Datenflusses**. Maske,
Raymarch, Fundament und Naht bleiben richtig, auch wenn sie fällt. Sichtbar würde ihr Fallen daran,
daß Himmel und Leuchtreklamen mit dunkler werden. Dann bleiben zwei Wege:

-   Die AO-Kette: den Pixel-Shader des letzten SAO-Passes über den Zeigertausch aus C so ersetzen,
    daß sein Ergebnis `engineAO × Maske` ist. Die Engine wendet es über `ApplyAO` selbst an.
-   Hinter dem Composite multiplizieren, mit den bekannten Nachteilen.

Beides ist eine Änderung an einem Pass, keine am Entwurf.

**Zwei kleinere Gabelungen derselben Art:**

-   **Die Tiefenkonvention.** `FarDepthValue` und `NearDepthValue` stehen auf `1` und `0`. Ist es
    umgekehrt, ist die Maske leer oder der Schatten zeigt in die falsche Richtung. Der Tausch ist
    eine Zeile.
-   **Die Richtung der Sonne.** Die Vorlage negiert die Weltrichtung des Lichts. Ob Fallout 4s
    `NiDirectionalLight` dieselbe Konvention führt, ist unbelegt; ein Schatten, der zur falschen
    Seite fällt, sagt es sofort.

## 10. Abnahme

Ein Spielstart, Sanctuary bei tiefer Sonne, dazu ein Abstecher in den Root Cellar. Läufe werden
gebündelt, weil jeder Start Zeit kostet.

| #   | Schritt                                                                     | Geht es schief, heißt das                                              |
| --- | --------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| 1   | Overlay öffnen, Block `Screen-Space Shadows` mit Schalter und vier Reglern  | `Declare` lief nicht, oder die Einstellungsdatei wurde nicht ergänzt   |
| 2   | Draußen: Schalter aus und an, Boden unter Zaun, Treppe, Autowrack           | Kein Unterschied: der Rückruf feuert nicht, oder die Maske ist leer    |
| 3   | Dabei auf Himmel und Leuchtreklamen achten                                  | Werden die mit dunkler, fällt die tragende Annahme aus Abschnitt 9     |
| 4   | `shadowContrast` auf 4, dann `surfaceThickness` ans obere Ende              | Keine Wirkung: die Regler erreichen den Konstantenpuffer nicht         |
| 5   | Performance-Tafel: `ScreenSpaceShadows/RayMarch` und `/Modulate` mit Zahlen | Fehlen sie, greift `PassScope` im Rückruf nicht                        |
| 6   | Root Cellar betreten                                                        | Läuft der Pass dort, greift die Sonnenprüfung aus 6.3 nicht            |
| 7   | Pip-Boy öffnen, Ladebildschirm, Alt-Tab und zurück                          | Artefakte oder Absturz: der Zustandswächter gibt nicht alles zurück    |
| 8   | Feature im laufenden Spiel abschalten, Schatten verschwinden                | Absturz: das Austragen aus `FramePhase` hat ein Rennen                 |
| 9   | `RaymarchCS.hlsl` speichern, während das Spiel läuft                        | Keine Reaktion binnen einer Sekunde: der Wächter sieht die Datei nicht |

**Abgenommen ist F2, wenn 1 bis 9 stehen** und die Zahlen aus Schritt 5 gegen die Grundlinie aus F1
(6,598 ms GPU / 6,503 ms CPU je Frame in Sanctuary) in der Roadmap festgehalten sind, unter „Aus
Teilprojekt F2 bestätigt".

Was ungeprüft bleibt, wird dort ausdrücklich als ungeprüft benannt.

## 11. Was F2 für F3 aufwärts hinterläßt

-   **`Render::FramePhase`** — der Platz im Frame, an dem ein Bildschirmraum-Feature arbeitet.
-   **`Render::Targets`** — der Zugriff auf G-Buffer, Tiefe und Lichtziele.
-   **`Render::Resources`, `Render::StateGuard`, `Render::FullscreenPass`** — eigene Ressourcen
    anlegen, den Zustand der Engine unversehrt lassen, ein Vollbild zeichnen.
-   **`Shader::ShaderCompiler` mit drei Profilen und Defines** — die Grundlage jeder Permutation,
    lange bevor F12 den Cache baut.
-   **Ein belegter Rückweg ins Bild**, ohne eine einzige ersetzte Engine-Permutation.

Ein Feature ab F3 sollte damit aus einer Datei in `src/Features/`, einem Shader in
`package/Features/` und einem Eintrag in `RegisterAll` bestehen.
