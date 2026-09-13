# Teilprojekt F5 — Skylighting

Spec, Stand 2026-09-13. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`. Setzt A bis F4.5
voraus, insbesondere `2026-09-12-fallout4-f4-cloud-shadows-design.md` (Draw-Hook, Wiederholen
eines Engine-Zugs mit ersetzten Konstanten, eigene Zeichenziele, Sonde vor dem Bau) und
`2026-09-06-fallout4-f2-screen-space-shadows-design.md` (Multiplikation auf ein Engine-Ziel vor
dem Composite).

## 1. Kontext und Ziel

Fallout 4 beleuchtet die Welt im Composite aus direktem Licht (`RT_058/059`) und Umgebungslicht.
Das Umgebungslicht wird vom SSAO der Engine gedämpft, das nur sieht, was im Bild ist: ein paar
Meter um jede Kante. Was fehlt, ist die große Skala — unter einem Vordach, in einem Graben, unter
einem Baum, im Eingang eines Hauses ist der Himmel verdeckt, und das Umgebungslicht müßte dort
dunkler sein, als es ist.

**Ziel:** Himmelssichtbarkeit aus der Szene, die die Engine tatsächlich zeichnet, als Faktor auf
das Umgebungslicht — richtungsabhängig über die Normale, weich, über Frames akkumuliert, sichtbar
im Spiel, meßbar in der Tafel.

**Abnahmekriterium der Roadmap:** „Sichtbarer Effekt plus CPU-/GPU-Zahlen."

### Was F5 erbt

-   **Aus F4:** `Render::DrawHook` mit `DrawObserver`, `TechniqueFilter` (`kAnyTechnique` für eine
    ganze Klasse), `DrawCall::Repeat`; das Muster „Konstantenpuffer kopieren, Matrix ersetzen,
    alles zurückgeben"; die Sonde als Diagnose-Feature vor dem Bau; `Render::CubeTarget` als
    Vorbild für ein eigenes Zeichenziel; `Clouds::CloudProjection` als Vorbild für reine
    Matrixrechnung mit Host-Test.
-   **Aus F2 und F3:** `Render::FramePhase` mit `kBeforeComposite` und `kAfterOpaque`,
    `Render::FullscreenPass`, `Render::StateGuard`, `Render::Resources`, die Multiplikation per
    Blend `ZERO / SRC_COLOR`, `Render::FogCameraFromMatrix` (Weltpunkt aus Tiefe und Kamerabasis).
-   **Aus C:** `Shader::CompileComputeShader`, `Shader::LoadSource` mit Includes.
-   **Aus E2 und F4.5:** `Declare` als einzige Naht zum Overlay, `DeclareChoice` als Debug-Ansicht,
    die `Help` des Schalters als Beschreibung.

### Was die Vorlage tut

Skyrim CS 1.9 (`skyrim-base:src/Features/Skylighting.cpp`, `features/Skylighting/Shaders/`):

1. Je Frame rendert sie die Szene aus einer Richtung nahe dem Zenit (R2-Folge auf der Scheibe,
   `MaxZenith` 90°) in eine Tiefenkarte, mit dem Regen-Okklusionssystem der Engine
   (`Precipitation::RenderMask`, Würfel `occlusionDistance` = 10000 Einheiten).
2. Ein Compute-Shader (`UpdateProbesCS.hlsl`, `8×8×1`) trägt in ein 3D-Gitter von 256×256×128
   Zellen über 10000×10000×5000 Einheiten um die Kamera die Sichtbarkeit als SH2 ein: je Zelle
   Vergleichs-Sample der Zellmitte gegen die Tiefenkarte, SH der Richtung mit `Sichtbarkeit · 4π`,
   gleitender Mittelwert über die Frames, Einblenden über 15 Frames. Das Gitter ist ein Ringpuffer
   um die Kamerazelle (`ArrayOrigin`), beim Zellwechsel werden die neuen Zellen zurückgesetzt
   (`ValidMargin`). Dazu eine Schatten-Sichtbarkeit aus den Sonnenkaskaden, für andere Features.
3. Die Beleuchtungsshader lesen das Gitter (`Skylighting::Sample`: acht Zellen trilinear, mit
   Tangentengewicht und Empfänger-Normalenversatz), dämpfen das Umgebungslicht diffus mit der
   Kosinuskeule über die Normale (`EvaluateDiffuse`, `lerp(MinDiffuseVisibility, 1, v)`) und
   spekular über die Reflexionskeule.

### Die Befunde, die den Entwurf bestimmen

1. **`Precipitation` ist in commonlibf4 nur ein Name.** RTTI- und vtable-ID sind da, Klasse,
   `RenderMask`, Okklusionskamera und die drei Globalen der Vorlage nicht. Der Weg der Vorlage
   hieße RE mit mehreren Sondenläufen. Fallout 4 hat das System (`bPrecipitationOcclusion=1`,
   `Sky::precip`), aber F5 nutzt es nicht.
2. **Die Schattenkarte ist erreichbar.** `BSUtilityShader` zeichnet rund 40 Züge je Frame nach
   `DS_008` (4096², `R16_TYPELESS`), vor `BSDFLight`. Das sind die Schattenwerfer der Sonne mit
   `fShadowDistance` 20000 — mehr als das Gitter braucht. Der Griff aus F4 wiederholt sie in ein
   eigenes Tiefenziel mit eigener Projektion.
3. **Der Composite liest `RT_028` an SRV 9.** 1280×720, `R8G8B8A8_UNORM`, UAV. Die Engine hat
   `ISSAORawAO`, `ISSAOBlurH`, `ISSAOBlurV` als Imagespace-Pässe. Ist `RT_028` deren Ergebnis,
   das der Composite auf das Umgebungslicht multipliziert, dann bringt eine Multiplikation unserer
   Sichtbarkeit in `RT_028` sie exakt auf den Ambient-Anteil an — ohne Shader-Ersatz. Das ist
   die tragende Vermutung; die Sonde prüft sie.
4. **`RT_009` ist der spekulare Anteil.** `BSDFCompositeEnvmapSslrApplyAO` (`0x0B20`) schreibt es
   aus `RT_007/008` (halbe Auflösung, Reflexion) und der Composite liest es an SRV 14. Die
   spekulare Dämpfung der Vorlage hätte dort ihren Platz; F5 baut sie nicht.
5. **Die Normalen in `RT_020` (`R16G16_UNORM`) sind kodiert, die Kodierung ist unbekannt.**
   Die Kosinuskeule braucht eine Normale. Aus den Tiefenableitungen der Nachbarpixel läßt sich
   eine gewinnen; F8 wird dieselbe brauchen oder die Kodierung klären.
6. **`kBeforeComposite` feuert beim ersten Composite-Aufruf**, das ist `BSDFCompositeBase 0x0088`,
   der noch nach `RT_058/059` schreibt. Wo die SSAO-Pässe relativ dazu laufen, ist nicht
   gemessen — sie sind Imagespace und im `FrameTrace` unsichtbar. Ein Schreiben nach `RT_028`
   muß deshalb unmittelbar vor dem Zug stehen, der es liest: `BSDFComposite 0x0040`.

## 2. Umfang

### In F5 enthalten

-   **Sonde** (Lauf 1, Diagnose-Feature, danach entfernt): Konstantenpuffer des Utility-Shaders,
    Inhalt von `RT_028`, Okklusionskarte bei Regen.
-   **`Render::Phase::kComposite`** — die `SetupTechnique` von `BSDFCompositeShader` mit Technik
    `0x0040`, einmal je Frame.
-   **`Render::DepthTarget`** — ein Tiefenziel, das wir besitzen: DSV zum Zeichnen, SRV zum Lesen.
-   **`Render::Texture3D`** in `Resources` — SRV und UAV.
-   **`Render::Targets::DepthStencilView(slot)`** und der benannte Slot `kShadowMap = 8`.
-   **`Skylighting::OcclusionProjection`** — rein: Richtung aus dem Folgenindex, Orthomatrix,
    Gitterfenster (Zellursprung, Gitterursprung, Gültigkeitsrand). Host-Test.
-   **Feature `Skylighting`**: Beobachter (Erfassung), Compute-Dispatch (Gitter), Vollbildpass
    (Anbringen), Einstellungen, Debug-Ansichten, sechstes Feature-Archiv.
-   **Feature `TargetView`** (Diagnose, voreingestellt aus, `CORE`): legt ein wählbares Engine-Ziel
    über das Bild. Aus der Sonde hervorgegangen, bleibt als Werkzeug.
-   Shader: `Skylighting/UpdateProbes.hlsl` (Compute), `Skylighting/Skylighting.hlsl` (Pass),
    `Skylighting/SkylightingCommon.hlsli` (Abtastung, aus der Vorlage); im Basisarchiv
    `Common/SphericalHarmonics.hlsli` (Kopie der geerbten SH-Hilfen) und
    `Common/Reconstruct.hlsli` (Weltpunkt und Normale aus der Tiefe); `TargetView/TargetView.hlsl`.
-   Roadmap: Zeile F5 abgeschlossen, Abschnitt „Aus Teilprojekt F5 bestätigt".

### Nicht in F5 enthalten

-   Die spekulare Dämpfung (`RT_009`) und die Schatten-Sichtbarkeit aus den Sonnenkaskaden.
-   Der Weg über `Precipitation` (RE). Er bleibt als Folgeteilprojekt, falls die Schattenwerfer der
    Sonne die Sicht von oben nicht decken.
-   Decodieren von `RT_020`. Normalen aus der Tiefe genügen; ein Tausch ist eine Funktion.
-   Ein Regler für Gittergröße oder Auflösung; 10000 Einheiten und 256×256×128 sind Konstanten wie
    in der Vorlage.
-   Skylighting in Wasser, Gras oder Terrain-Shadern (F12 aufwärts).

## 3. Vorentscheidungen

-   **Sondengitter wie die Vorlage**, nicht feste Richtungen und nicht nur Zenit: der Nutzer will
    das bekannte Bild, und Compute-Shader und Abtastung lassen sich weitgehend übernehmen.
-   **Erfassung über die Schattenwerfer**, nicht über `Precipitation`: der Griff ist fertig, eine
    Sonde statt dreier, keine eigenen Adressen. Preis: die Objektauswahl ist die des Sonnenschattens.
-   **Anbringen über `RT_028`, in einer eigenen Phase.** Die Multiplikation ist der einzige Weg an
    den Ambient-Anteil ohne Shader-Ersatz, und sie muß unmittelbar vor dem Leser stehen.
-   **Rückfall auf `kAfterOpaque`**, falls `RT_028` kein SSAO ist: Multiplikation auf das fertige
    HDR-Bild, Himmelspixel ausgenommen (`depth ≥ 0.9999`, wie F4). Trifft auch das direkte Licht;
    wird dann als Zugeständnis bis F12 benannt, nicht kaschiert.
-   **Innenräume:** das Feature steht ab, wenn `Sky::mode != kFull`, wie F4. Beim Wiedereintritt
    werden Gitter und Zähler zurückgesetzt (ein `ClearUnorderedAccessView`), damit kein Stand aus
    der Zelle davor bleibt.
-   **Die Sonde entscheidet vor dem Bau der Erfassung**, wie in F4. Tasks 6 bis 9 des Plans
    (Phase, Ziele, Projektion, Shader) hängen nicht an ihr und werden gebaut, während der Nutzer
    Lauf 1 macht.

## 4. Die Sonde (Lauf 1)

Ein Diagnose-Feature `ShadowProbe` (voreingestellt aus, `CORE`), das drei Dinge ins Log schreibt,
**je Sekunde**, nicht einmalig:

1. **Utility-Züge.** Für den ersten `BSUtilityShader`-Zug je Frame, dessen DSV `DS_008` ist: Zahl
   der Züge je Frame nach `DS_008` und nach anderen Zielen; die gebundenen VS-Konstantenpuffer
   (Slot, Größe); von jedem Puffer bis 256 Byte einen Rohabzug als 16 Floats je Zeile, mit Hinweis
   „looks like a 4x4 with last row 0 0 0 1" bei jedem 64-Byte-Block, dessen letzte Zeile so lautet.
   Dazu die Sonnenrichtung des Frames (Zeile 0 der Lichtrotation, wie F2), damit sich die
   Sicht-Projektion der Schattenkarte an ihrer dritten Zeile erkennen läßt.
2. **Bewegung.** Dieselben Puffer eine Sekunde später: welche Bytes sich geändert haben. Eine
   Sicht-Projektion ändert sich mit der Sonne, eine Weltmatrix mit dem Objekt.
3. **Regen.** Zahl der `BSUtilityShader`-Züge je Frame nach `DS_005`, `DS_007`, `DS_009`; ihre
   Techniken. Der Nutzer setzt einmal `fw 1CA7E4`.

Dazu die `TargetView`-Ansicht auf `RT_028`: der Nutzer schaut das Bild an und sagt, was er sieht.

| Sondenbefund                                                                                              | Weg                                                                                                                   |
| --------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- |
| Ein Puffer je Sicht trägt eine 4×4 mit letzter Zeile `0 0 0 1`, deren dritte Zeile die Sonnenrichtung ist | **Erfassung wie Abschnitt 6**, dieser Puffer wird ersetzt                                                             |
| Die Matrix liegt in einem Puffer je Objekt (ändert sich je Zug)                                           | Ersetzen je Zug, wie F4 Slot 2: unsere Matrix mal der Weltmatrix des Objekts, deren Fundort die Sonde ebenfalls zeigt |
| Keine Orthomatrix in keinem Puffer                                                                        | F5 teilen: Erfassung als Forschung (RenderDoc), Gitter und Pass warten                                                |
| `RT_028` zeigt Verdunkelung in Ecken und unter Objekten (grau, nicht bunt)                                | **Anbringen wie Abschnitt 8**                                                                                         |
| `RT_028` zeigt etwas anderes                                                                              | Rückfall `kAfterOpaque`, Abschnitt 3                                                                                  |
| `DS_005` bei Regen gezeichnet                                                                             | Notiz für das Folgeteilprojekt „Erfassung über Precipitation"; für F5 ohne Folge                                      |

Nach dem Lauf: Sonde per `git revert` entfernt, `extract-i18n.py --write`. `TargetView` bleibt.

## 5. Die Bausteine

### 5.1 `Render::Phase::kComposite`

`FramePhase` bekommt eine dritte Phase. Sie hängt an derselben Klasse wie `kBeforeComposite`
(`BSDFCompositeShader`, derselbe Patch) und feuert, wenn die Technik der `SetupTechnique` `0x0040`
ist, einmal je Frame — vor dem Weiterreichen an die Engine, so daß der Zug danach liest, was der
Rückruf geschrieben hat. Der Patch weiß die Technik aus dem Aufruf selbst, nicht über den
`TechniqueTracker`. `FramePhaseHits` und `FramePhaseLastTechnique` gelten für sie wie für die
anderen; `PhaseDispatcherTests` bekommt keine Änderung (die Phase ist ein Enum-Wert mehr, die
Logik dieselbe), `FramePhase.cpp` einen Zweig.

### 5.2 `Render::DepthTarget`

`src/Render/DepthTarget.h/.cpp`, nach dem Muster von `CubeTarget`: eine quadratische Textur
`R32_TYPELESS` mit `DEPTH_STENCIL | SHADER_RESOURCE`, DSV `D32_FLOAT`, SRV `R32_FLOAT`, ein Mip.
`Create(size, debugName)`, `Release()`, `Clear(context, depth)`, `DSV()`, `SRV()`, `Size()`,
`Valid()`. Kein Farbziel, kein UAV.

### 5.3 `Render::Texture3D`

In `Resources.h/.cpp` neben `Texture`: `Create(width, height, depth, format, debugName)` mit
`SHADER_RESOURCE | UNORDERED_ACCESS`, SRV und UAV über das ganze Volumen, `ClearUAV(context,
uint4)` für den Reset. Dieselbe Idempotenz und derselbe Umgang mit Fehlern wie `Texture`.

### 5.4 `Render::Targets::DepthStencilView`

`kShadowMap = 8` als benannter Slot, `DepthStencilView(slot)` aus `DepthStencilTarget::dsView`,
und `LogSlots` nennt ihn. Der Beobachter vergleicht damit, ob ein Utility-Zug in die Schattenkarte
geht (`OMGetRenderTargets(0, nullptr, &dsv)`), wie F4 `RTV0 == kSceneHDR` prüft.

### 5.5 `Skylighting::OcclusionProjection` (rein)

`src/Features/Skylighting/OcclusionProjection.h/.cpp`, `double`, kein D3D, kein ImGui:

```cpp
namespace Skylighting
{
	inline constexpr double kOcclusionDistance = 10000.0;   // Würfelkante, Einheiten
	inline constexpr std::uint32_t kGridSize[3] = { 256, 256, 128 };
	inline constexpr double kGridExtent[3] = { 10000.0, 10000.0, 5000.0 };
	inline constexpr std::uint32_t kOcclusionMapSize = 2048;

	/// Die Richtung des Frames aus der R2-Folge der Vorlage: Punkt auf der
	/// Einheitsscheibe, Radius mit sin(maxZenith) skaliert, auf die Kugel
	/// gehoben. Nach unten gerichtet (z < 0): der Blick von oben.
	void SkyDirection(std::uint32_t a_index, double a_seed, double a_maxZenithRadians, double (&a_out)[3]);

	/// Orthoprojektion entlang a_direction auf einen Würfel der Kante
	/// kOcclusionDistance um a_centre, Zeilenvektoren wie die Engine
	/// (clip_i = Σ_j M[i][j] p_j), Tiefe 0 am nahen Rand, 1 am fernen.
	void OcclusionMatrix(const double (&a_centre)[3], const double (&a_direction)[3], double (&a_out)[16]);

	struct GridWindow
	{
		double cellOrigin[3];     // Ursprung der Kamerazelle, relativ zur Kamera
		std::int32_t arrayOrigin[3];
		std::int32_t validMargin[3];
	};

	/// Die Fensterrechnung der Vorlage (GetCommonBufferData): Kamerazelle
	/// runden, Ringpuffer-Ursprung, Rand der neu hereingekommenen Zellen aus
	/// der Differenz zur letzten Zelle.
	GridWindow WindowFor(const double (&a_eye)[3], const std::int32_t (&a_previousCell)[3], std::int32_t (&a_outCell)[3]);
}
```

### 5.6 `Features::TargetView`

Ein Diagnose-Feature wie `FrameTrace`: `DeclareFeature("TargetView", false)`, `DeclareChoice
("TargetView/target", "RT_028", {"RT_028", "RT_009", "RT_020", "RT_003", "RT_022", "RT_057",
"RT_024", "RT_023", "RT_039"})`, `DeclareChoice("TargetView/channels", "rgb", {"rgb", "r", "g",
"b", "a"})`. In `kAfterOpaque` ein Vollbildpass auf `kSceneHDR`, der das gewählte Ziel als
`RenderTargetSRV(slot)` liest und ohne Blend über das Bild legt — nach dem Himmel, also auf jedem
Pixel lesbar, anders als F4s `cube`-Ansicht. Ein Ziel halber Auflösung wird gestreckt. Die Slots
sind Zahlen im Namen; `Targets::RenderTargetSRV` nimmt den Slot, kein neuer Name nötig.

## 6. Die Erfassung

Der Beobachter des Features (`Wants`: `TechniqueFilter{ Utility-Klassenindex, kAnyTechnique }`,
nicht während der eigenen Wiederholung) in `BeforeDraw`:

1. DSV holen; ist es nicht `Targets::DepthStencilView(kShadowMap)`, durchlassen.
2. Beim ersten Treffer des Frames: Richtung aus `SkyDirection(frame, seed, maxZenith)`,
   Kameraposition aus `Main::WorldRootCamera()`, Matrix aus `OcclusionMatrix`, in eine Kopie des
   Sicht-Puffers geschrieben (Slot und Offset aus der Sonde, als Konstanten im Quelltext mit
   Kommentar auf den Sondenlauf), `UpdateSubresource`; unser Tiefenziel auf 1.0 gelöscht.
3. Sichern: alle acht RTVs und die DSV, Viewports, Rasterizer (bleibt gebunden — Schattenwerfer
   tragen ihren Bias, und den wollen wir), den Sicht-Puffer des Slots.
4. Setzen: kein RTV, unsere DSV, Viewport `kOcclusionMapSize`, unser Sicht-Puffer.
5. `a_call.Repeat(context)`, Zähler.
6. Zurück: alles aus Schritt 3, in `AfterDraw`.

Das Ergebnis eines Frames ist eine Tiefenkarte aus einer Richtung; sie wird im selben Frame vom
Gitter gelesen (Abschnitt 7) und im nächsten überschrieben. Kein Staging, keine Latenz: die
Matrix wird aus Engine-Speicher gerechnet, nicht aus einem GPU-Puffer gelesen — anders als F4, wo
die Weltmatrix der Wolkenlage nur im Puffer stand.

Ist die Matrix je Objekt (zweite Zeile der Sondentabelle), ersetzt Schritt 2 je Zug: die
Weltmatrix aus dem Objektpuffer gelesen — dann doch über Staging wie F4, oder aus dem
`BSGeometry` des Zugs, falls der Hook es kennt (er kennt es nicht; Staging). Das steht dann im
Plan, nicht hier.

## 7. Das Gitter

Zwei `Texture3D`: Sonden `R16G16B16A16_FLOAT` (SH2), Zähler `R8_UINT`. Konstantenpuffer wie
`SkylightingSettings` der Vorlage (`row_major float4x4 OcclusionViewProj; float4 OcclusionDir;
float4 PosOffset; uint4 ArrayOrigin; int4 ValidMargin; float MinDiffuseVisibility; float pad[3]`),
gefüllt aus `GridWindow` und der Matrix des Frames.

`UpdateProbes.hlsl` ist `UpdateProbesCS.hlsl` der Vorlage ohne den Kaskadenteil: Zelle → Zellmitte
in Modellraum → durch `OcclusionViewProj` → UV und Tiefe → `SampleCmpLevelZero` gegen unsere
Tiefenkarte mit einem Vergleichssampler (`LESS_EQUAL`, linear) → SH der Richtung mal
`Sichtbarkeit · 4π` → gleitender Mittelwert mit `1 / accumFrames`, Einblenden über 15 Frames;
Zellen außerhalb der Karte oder neu hereingekommen (`ValidMargin`) werden auf die Einheits-SH
gesetzt. Dispatch `(256/8, 256/8, 128)` in **`kBeforeComposite`** (die Schattenkarte ist dann
gezeichnet, unsere Wiederholungen mit ihr). SRV: Tiefenkarte; UAV: beide Gitter; danach alles
wieder gelöst (`CSSetShaderResources`/`UAVs` auf null), weil die Engine ihre Compute-Bindungen
nicht neu setzt.

Konventionen der Vorlage, die wir übernehmen: `ARRAY_SIZE = 10000 · (1, 1, 0.5)`,
`CELL_SIZE = ARRAY_SIZE / ARRAY_DIM`, `UNIT_SH = (√(4π), 0, 0, 0)`, `cellCentreOS.y = -y` vor dem
UV (die Vorlage rendert mit umgekehrter y-Achse; ob unsere Orthomatrix das braucht, zeigt die
`occlusion`-Ansicht — eine Debug-Ansicht ist billiger als eine Vermutung).

## 8. Der Pass

In **`kComposite`** ein Vollbildpass auf `RT_028` (`RenderTargetView(28)`, 1280×720; Viewport
aus der Zielgröße, nicht aus dem Bildschirm), Blend `ZERO / SRC_COLOR`, Schreibmaske RGB — welche
Kanäle das SSAO trägt, sagt die Sonde; die Maske folgt ihr. Je Pixel (`Skylighting.hlsl`):

1. Tiefe aus `DS_002` an der Pixelmitte des halben Bildes (Load am 2×2-Block, oben links).
   `depth ≥ 0.9999` → Faktor 1 (Himmel, Ferne).
2. Weltpunkt relativ zur Kamera: Sichtstrahl aus `CameraForward/Right/Up` wie F3 und F4,
   `viewDepth = near / (1 − depth)`.
3. Normale: Weltpunkte der Nachbarn rechts und unten (dieselbe Rekonstruktion), Kreuzprodukt,
   normiert, nach der Kamera orientiert. Ein Baustein `NormalFromDepth` in
   `Common/Reconstruct.hlsli` unter `Shaders/FO4`, den F8 wiederverwendet.
4. `Skylighting::Sample(positionMS, normalWS)` aus `SkylightingCommon.hlsli` (die Vorlage,
   ohne `SKYLIGHTING_SHADOW_VIS`, mit unseren Konstanten statt `SharedData`), dann
   `EvaluateDiffuse(sh, normal, GetFadeOutFactor(positionMS))` → `lerp(minVisibility, 1, v)`.
5. Ausgabe: der Faktor in RGB. `debugView`: `visibility` schreibt den Faktor als Grauwert nach
   `kSceneHDR` in `kAfterOpaque` statt nach `RT_028` (damit man ihn sieht); `occlusion` legt die
   Tiefenkarte des Frames über das Bild; `grid` zeigt die Zenit-Sichtbarkeit der Zelle des Pixels
   (`EvaluateVisibility`), also das Gitter ohne Normale.

Konstanten des Passes (`cbuffer PerFrame : register(b1)`): Kamerabasis und `near` wie F4,
`SkylightingSettings` wie in Abschnitt 7 (derselbe Puffer, beide Shader lesen ihn), `debugView`
im freien `w`.

## 9. Das Feature

`Features::Skylighting` in `src/Features/Skylighting.h/.cpp`, `Name()` `"Skylighting"`.

| Methode    | Tut                                                                                                                              |
| ---------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `Declare`  | Schalter, `maxZenith`, `minVisibility`, `debugView`                                                                              |
| `Setup`    | Tiefenziel, zwei Gitter, Konstantenpuffer, Vergleichssampler, Shader übersetzen, Beobachter registrieren, zwei Phasen abonnieren |
| `Frame`    | Dateiwächter (Hot-Reload beider Shader); Innen/Außen-Wechsel erkennen und das Gitter zurücksetzen; Sekundenzeile                 |
| `Shutdown` | Beobachter austragen, Phasen austragen, alles freigeben                                                                          |

Sekundenzeile: Utility-Züge gesehen / wiederholt, Richtung des Frames, Kamerazelle, `validMargin`,
Dispatch-Zahl, Pass-Zahl, `FramePhaseLastTechnique(kComposite)` (muß `0x0040` sein).

### 9.1 Einstellungen

| Pfad                        | Art    | Vorgabe | Bereich                          | Hilfe                                                                                                           |
| --------------------------- | ------ | ------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `Skylighting/enabled`       | Bool   | true    |                                  | Darkens the ambient light where the sky is hidden — under overhangs, in trenches, beneath trees.                |
| `Skylighting/maxZenith`     | Slider | 90      | 30–90 (Grad)                     | How far from straight up the sky is sampled. Lower keeps light out of narrow gaps.                              |
| `Skylighting/minVisibility` | Slider | 0.1     | 0–1                              | How dark a fully hidden spot gets. 0 is black, 1 switches the effect off.                                       |
| `Skylighting/debugView`     | Choice | off     | off, visibility, occlusion, grid | Shows the pass instead of the picture: the factor, the depth map of this frame, or the grid as seen from above. |

### 9.2 Auslieferung

`package/Features/Skylighting/Shaders/FO4/Skylighting/{Skylighting.hlsl, UpdateProbes.hlsl,
SkylightingCommon.hlsli}`, kein `CORE` — eigenes Archiv, wie Cloud Shadows. Gemeinsame Bausteine
reisen im Basisarchiv unter `package/Shaders/FO4/Common/`, neben `Fullscreen.hlsl`:
`SphericalHarmonics.hlsli` ist eine **Kopie** der geerbten Datei aus
`package/Shaders/Common/Spherical Harmonics/` (nur `Shaders/FO4` wird ausgeliefert, und die
geerbten 153 Dateien bleiben unangetastet), `Reconstruct.hlsli` ist neu. Die Feature-Shader
binden sie über `#include "../Common/…"` ein; `Shader::LoadSource` löst relativ zur Wurzel
`Shaders/FO4` auf. `TargetView` mit `CORE` in `package/Features/TargetView/`.
`verify-package.ps1` bekommt beide Blöcke und prüft die zwei Common-Dateien im Basisarchiv.

## 10. Prüfung

### 10.1 Host-Tests

**`SkylightingProjectionTests`** (`tests/`), ein `main` mit `Check`:

1. `SkyDirection`: 64 Indizes bei `maxZenith` 90° → alle `z < 0`, alle normiert, mindestens zwei
   verschieden; bei 30° → Winkel zur Senkrechten ≤ 30° für alle; Index 0 und 1 verschieden.
2. `OcclusionMatrix`: die Mitte landet auf `(0, 0, 0.5)`; ein Punkt `centre − direction · 5000`
   (zur Kamera hin, gegen die Blickrichtung) auf Tiefe 0, `+ 5000` auf Tiefe 1; ein Punkt an der
   Würfelkante quer zur Richtung auf `|x| = 1` oder `|y| = 1`; letzte Zeile `0 0 0 1`; die
   Richtung selbst bildet auf `(0, 0, >0, 0)` (Vektor, w = 0) ab.
3. `WindowFor`: stehende Kamera → `validMargin` 0 und `cellOrigin` innerhalb einer halben Zelle
   um die Kamera; Kamera eine Zelle in x weiter → `validMargin.x = ±1`, `arrayOrigin.x` um eins
   verschoben modulo 256; Sprung um 300 Zellen → `|validMargin.x| ≥ 256` (das Gitter wird ganz
   ungültig; der Shader klemmt).

Nach Grün je Prüfung eine Mutation, vorher benannt.

**`PhaseDispatcherTests`** unverändert; die neue Phase ist ein Enum-Wert.

### 10.2 Was nicht host-getestet wird

Der Beobachter, der Dispatch, der Pass, die Shader. Der Spiellauf, die Debug-Ansichten und die
Sekundenzeile decken es ab.

### 10.3 Abnahme (Lauf 2)

| #   | Schritt                                                                                         | Geht es schief, heißt das                                                 |
| --- | ----------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| 1   | Overlay: Seite `Skylighting` mit Schalter, zwei Reglern, Auswahl                                | `Declare` lief nicht                                                      |
| 2   | Red Rocket, unter dem Vordach stehen, Blick auf den Boden darunter: Schalter aus und an         | Kein Unterschied: Gitter leer, oder `RT_028` ist nicht der Ambient-Faktor |
| 3   | `debugView` `occlusion`: eine Höhenkarte der Umgebung, die je Frame die Richtung wechselt       | Schwarz oder weiß: Erfassung leer, Matrix falsch, Tiefe verkehrt          |
| 4   | `debugView` `grid`: hell auf freiem Feld, dunkel unter dem Vordach, weich, ohne Kacheln         | Kacheln: Fensterrechnung; alles hell: Vergleichs-Sample immer 1           |
| 5   | `debugView` `visibility`: wie `grid`, aber Wände zur offenen Seite hell, zur Wand dunkel        | Wie `grid`: Normale bleibt Zenit                                          |
| 6   | Schnell 200 m laufen: das Gitter baut sich in etwa einer Sekunde nach, ohne Sprung in der Mitte | Sprung: `ValidMargin` oder `ArrayOrigin`                                  |
| 7   | `maxZenith` auf 30: flachere Verdunkelung, Wände heller                                         | Nichts: Richtung kommt nicht an                                           |
| 8   | `minVisibility` auf 0 und auf 1                                                                 | Bei 1 muß das Bild dem Schalter-aus-Bild gleichen                         |
| 9   | Tafel: `Skylighting/Capture`, `Skylighting/Probes`, `Skylighting/Draw`; **F11 an und aus**      | Zeile fehlt: Pass läuft nicht                                             |
| 10  | Root Cellar rein: Sekundenzeile „standing down"; raus: Gitter baut sich neu                     | Stand aus der Zelle davor: Reset fehlt                                    |
| 11  | Pip-Boy, Alt-Tab, Feature aus und an im Overlay                                                 | Absturz beim Austragen: Reihenfolge in `Shutdown`                         |
| 12  | Hot-Reload: ich frage vorher, dann berühre ich beide Shader; im Bild ändert sich nichts         | Keine „recompiling"-Zeile: Wächter sieht die Dateien nicht                |

Log: keine `[W]`- und `[E]`-Zeilen; Sekundenzeile mit `0x0040` als Technik von `kComposite` in
jedem Intervall; Wiederholungen je Frame ≈ Utility-Züge je Frame nach `DS_008`.

## 11. Risiken und Gabelungen

**Das tragende Risiko** hat zwei Hälften, beide von der Sonde entschieden: ob die Matrix der
Schattenkarte in einem Puffer je Sicht zu finden ist (sonst je Zug oder Forschung), und ob `RT_028`
der Ambient-Faktor ist (sonst `kAfterOpaque`).

**Vier kleinere Gabelungen:**

-   **Die Schattenwerfer der Sonne decken die Sicht von oben nicht.** Bei flacher Sonne wählt die
    Engine Objekte für ein Frustum, das schräg liegt; ein Dach genau über der Kamera kann fehlen.
    Sichtbar in `occlusion`. Abhilfe ist der `Precipitation`-Weg als Folgeteilprojekt; F5 benennt
    es dann als Zugeständnis.
-   **Terrain ist kein Schattenwerfer.** Zeichnet die Engine das Gelände nicht in `DS_008` (dann
    fehlen Gräben und Hänge in der Sicht von oben), bleibt Skylighting auf Objekte beschränkt. Die
    Sonde zählt die Utility-Techniken; `BSUtilityTNBT…` mit `Sm` sind Meshes, Terrain hätte
    andere.
-   **Die y-Achse der Orthoprojektion ist verkehrt** (Vorlage: `cellCentreOS.y = -y`). Zeigt
    `grid` das Bild gespiegelt gegen `occlusion`, ist es ein Vorzeichen in `OcclusionMatrix`, das
    der Host-Test dann festhält.
-   **Normalen aus der Tiefe sind an Kanten falsch** (ein Pixel breit). Für eine Dämpfung des
    Umgebungslichts in halber Auflösung ist das unsichtbar; wird es sichtbar, nimmt der Baustein
    den kleineren der beiden Nachbarn je Achse.

**Zugeständnisse:** Die Sichtbarkeit ist bis zu 15 Frames alt und in neu betretenen Zellen erst
nach einer Sekunde da. Die spekulare Dämpfung fehlt. 72 MB GPU-Speicher für die Gitter. Die Kosten
liegen zum Teil im Frame-Wert (Wiederholungen), wie F4; gemessen mit zwei Schnappschüssen.

## 12. Was F5 für F6 aufwärts hinterläßt

-   **`Render::Phase::kComposite`** — der Punkt unmittelbar vor dem Zug, der die Composite-Eingaben
    liest. Wer `RT_028`, `RT_009` oder den G-Buffer verändern will, tut es hier.
-   **`Render::DepthTarget`** und **`Render::Texture3D`** — ein eigenes Tiefenziel und ein Volumen
    mit UAV. F6 (Volumetric Lighting) braucht beides.
-   **`TargetView`** — jedes Engine-Ziel über das Bild gelegt. Die Sonde jedes weiteren Features.
-   **`NormalFromDepth`** — bis F8 die Kodierung von `RT_020` klärt.
-   **Der Befund zu `RT_028`** und zum Konstantenpuffer des Utility-Shaders — für F7 (Volumetric
    Shadows), das dieselbe Schattenkarte lesen wird.
