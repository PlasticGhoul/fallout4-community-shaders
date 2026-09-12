# Teilprojekt F4 — Cloud Shadows

Spec, Stand 2026-09-12. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`. Setzt A bis F3
voraus, insbesondere `2026-09-06-fallout4-f2-screen-space-shadows-design.md` (die Naht: Frame-Phase,
Ziele, Vollbildpass, Multiplikation auf die Lichtziele) und
`2026-09-12-fallout4-f3-exponential-height-fog-design.md` (Sichtstrahl aus der Kameramatrix,
`FrameTrace`, Debug-Ansicht als Einstellung).

## 1. Kontext und Ziel

Fallout 4 zeichnet Wolken als bis zu 32 Texturlagen auf eine Himmelskuppel, mit Zuggeschwindigkeit
je Lage aus dem Wetter, alpha-gemischt hinter der opaken Szene. Auf dem Boden ist davon nichts zu
sehen: das direkte Licht kennt nur die Schattenkarte der Sonne. Was fehlt, sind die Schatten der
Wolken, die mit ihnen über die Landschaft ziehen.

**Ziel:** Wolkenschatten aus den Wolken, die die Engine tatsächlich zeichnet — nicht aus einer
Näherung —, als Faktor auf das direkte Licht, sichtbar im Spiel, meßbar in der Tafel aus F1.

**Abnahmekriterium der Roadmap:** „Sichtbarer Effekt plus CPU-/GPU-Zahlen."

### Was F4 erbt

-   **Die Naht aus F2 und F3.** `Render::FramePhase` mit `kBeforeComposite` (vor der Beleuchtung,
    `RT_058/059` sind das direkte Licht) und dem Himmelsmarker, `Render::FullscreenPass`,
    `Render::StateGuard`, `Render::Resources`, `Shader::ShaderCompiler`, `Util::FileWatch`.
-   **Die Multiplikation auf die Lichtziele aus F2.** Blend-State `ZERO / SRC_COLOR`, ein Pixel
    gibt denselben Faktor an beide Ziele; kein Zwischenpuffer, weil die Ziele keinen UAV haben.
-   **Die Kamera aus F3.** `Render::FogCameraFromMatrix`: Sichtstrahl je Pixel aus `worldToCam`,
    `d = near / (1 − z)`, Nahebene je Frame gelesen. Die Sonne aus F2: Zeile 0 der Lichtrotation.
-   **`FrameTrace` und die Lehre aus F3:** zwei gemessene Frames belegen keinen Anker; jeder Anker
    bekommt einen Zähler über jeden Frame in der Sekundenzeile.
-   **`Render::VTablePatch`** für COM-Tabellen (der Present-Hook aus B1) wie für Engine-vtables.
-   **Die Debug-Ansicht als Einstellung** (`DeclareChoice`), nie als Shader-Define.

### Die Befunde, die den Entwurf bestimmen

1. **Die Vorlage ersetzt den Himmelsshader.** Skyrim CS läßt `Sky.hlsl` beim Rendern der
   Reflexions-Cubemap die Wolkendeckung in `SV_Target3` schreiben, in eine eigene R8-Cubemap je
   Lage, und tastet sie in den Objekt-Shadern in Sonnenrichtung ab. Beides ist Permutations-Ersatz
   und damit F12. F4 muß die Deckung **ohne** eigenen Himmelsshader gewinnen und den Schatten **als
   Pass** anbringen.
2. **Das Alpha des Engine-Shaders läßt sich ohne Shader-Ersatz abgreifen.** Die Wolken werden mit
   Quell-Alpha auf den Himmel gemischt; genau dieses Alpha ist die Deckung. Ein Ziel im Format
   `A8_UNORM` mit einem Blend-State, der nur den Alphakanal zusammensetzt
   (`SrcBlendAlpha = ONE`, `DestBlendAlpha = INV_SRC_ALPHA`), nimmt es auf, wie immer der Shader
   seine Farbe bildet. Voraussetzung ist, daß die Engine wirklich mit `SRC_ALPHA /
INV_SRC_ALPHA` mischt — die Sonde prüft das (Abschnitt 4).
3. **Fallout 4 hat eine Cubemap, aber niemand hat sie zeichnen sehen.** `FO4_CUBE_000` (512²,
   `R11G11B10_FLOAT`) liegt in `cubeMapRenderTargets[0]`, das Wasser liest sie
   (`BSWaterReflSSLRLOD`, SRV 3). In beiden Trace-Frames aus F3 zeichnete kein einziger Aufruf
   hinein. In allen Ini-Dateien steht `bReflectSky=0`; `bUseWaterReflections=1`. Ob und wie oft der
   Himmel dort landet, entscheidet, wie die Deckung erfaßt wird — das ist die erste Aufgabe.
4. **Die Wolken sind wenige Züge.** Eine Technik `BSSkyClouds` (`0x0005`) je Frame, dahinter ein
   Zeichenaufruf je aktiver Lage. Ein Hook, der jeden Zeichenaufruf des Spiels passieren läßt und
   nur diese wenigen behandelt, kostet pro Aufruf einen Vergleich.

## 2. Umfang

### In F4 enthalten

1. `Render::TechniqueTracker`: welche Klasse gerade welche Technik eingerichtet hat, aus Slot 02
   aller dreizehn Shader-Klassen — der Zustand, den ein Draw-Hook abfragt.
2. `Render::DrawHook`: Slot 12 (`DrawIndexed`) des unmittelbaren Gerätekontexts, mit einem
   Rückruf, der vor und nach dem Zeichenaufruf läuft, solange eine bestimmte Technik aktiv ist.
3. Die Sonde (Lauf 1), ein `diag`-Commit, der wieder verschwindet.
4. Die Erfassung der Wolkendeckung in eine eigene `A8_UNORM`-Cubemap, auf einem von drei Wegen
   nach Ausgang der Sonde (Abschnitt 6).
5. Der Schattenpass an `kBeforeComposite`.
6. Ein Feature `CloudShadows` mit vier Einstellungen, Beschriftungen im Katalog, eigenem
   Addon-Archiv, das sechste.
7. Zwei neue Host-Tests, zwei erweiterte.

### Nicht in F4 enthalten

-   **Deckung je Lage und Selbstschatten der Lagen** (die 32 Cubemaps der Vorlage). Der Schatten
    braucht die Summe der Lagen, und die entsteht durch das Zusammensetzen in einem Ziel.
-   **Wolkenschatten im Nebel** (F3s Sonneneinstreuung) und **auf dem Himmel**. Der Pass wirkt
    auf das direkte Licht der opaken Szene.
-   **Ein Ersatz des Himmelsshaders.** F12.
-   **Wetterabhängige Regler.**
-   **Wolkenschatten in Innenräumen mit Himmel** (Fensterlicht). Der Pass steht ab, wenn der
    Himmel nicht `kFull` ist, wie F2 und F3.

## 3. Vorentscheidungen

Aus dem Brainstorming vom 2026-09-12, jeweils mit Begründung:

-   **Exakte Deckung statt Näherung.** Die Wolkentexturen selbst zu projizieren wäre ohne Messlauf
    machbar, aber die Schatten liefen nicht deckungsgleich zu den Wolken am Himmel. Bei Schatten
    fällt ein Fake auf. Entschieden vom Nutzer.
-   **Beide Erfassungswege in der Spec, der Messlauf entscheidet.** Weg A (Umlenken an der
    Cubemap) ist billig, hängt aber davon ab, daß die Engine den Himmel dort zeichnet. Weg B
    (zweite Projektion der Wolkenzüge des Kamerabilds) funktioniert immer, kostet aber die
    Vermessung der Vertex-Konstanten. Erweist sich B als tiefe Forschung, wird F4 geteilt wie
    B1/B2. Entschieden vom Nutzer.
-   **Erfassung am Zeichenaufruf, nicht am `SetupTechnique`.** Ein am `SetupTechnique`
    umgelenktes Ziel bliebe gebunden, bis die Engine es selbst wechselt — und ob sie das tut oder
    ihren Zustand cacht, ist nicht bekannt. Um den Zeichenaufruf herum wird umgelenkt und
    zurückgegeben, ohne Zustand über den Aufruf hinaus. Dafür der Draw-Hook.
-   **Eine Cubemap für beide Wege.** Weg B zeichnet in fünf Flächen einer eigenen Cubemap statt in
    eine Halbkugeltextur, damit der Schattenpass in beiden Fällen dieselbe Abtastung hat und kein
    Define braucht. Die sechste Fläche (nach unten) bleibt schwarz.
-   **Direkt multiplizieren wie F2, kein Zwischenpuffer.** Entschieden vom Nutzer gegen die
    Alternative „Maske rendern, F2s Modulate mitbenutzen".
-   **`cloudHeight` als Regler.** Die Kugelprojektion hängt an der Wolkenhöhe, und Fallout 4s
    Kuppel muß nicht Skyrims 2 km entsprechen. Entschieden vom Nutzer.

## 4. Die Sonde (Lauf 1)

Ein `diag`-Commit wie die Clamp-Probe in F3, der nach dem Lauf mitsamt Code verschwindet; was
bleibt, ist der Befund in der Roadmap. Er benutzt `TechniqueTracker` und `DrawHook` (Abschnitt 5),
die bleiben, und schreibt je Sekunde:

1. **Eine Tabelle Technik × Ziel für `BSSkyShader`:** je Zeichenaufruf, der während einer
   `BSSkyShader`-Technik läuft, die Technik-ID, der Name von RTV0 (`Render::GetViewTargetName`,
   also `FO4_RT_004` oder `FO4_CUBE_000`), die Art des Aufrufs (`DrawIndexed`, andere) und die
   Anzahl in dieser Sekunde und seit Start. Damit ist beantwortet: zeichnet die Engine die Wolken
   in die Cubemap, wie oft, und alle sechs Flächen?
2. **Den Blend-State beim ersten Wolkenzug des Frames:** `OMGetBlendState`, daraus `srcBlend`,
   `destBlend`, `srcBlendAlpha`, `destBlendAlpha`, `renderTargetWriteMask` von Ziel 0. Erwartet
   `SRC_ALPHA / INV_SRC_ALPHA`; alles andere ändert Abschnitt 6.
3. **Einen Rohabzug der Vertex-Konstantenpuffer beim ersten Wolkenzug im Kamerabild**, einmal je
   Sekunde: `VSGetConstantBuffers(0, 14)`, je gebundenem Puffer eine Staging-Kopie, im nächsten
   Frame gelesen (`Map` mit `DO_NOT_WAIT`), und darin die Suche nach einer 4×4-Matrix, die
   `worldToCam` gleicht — als Zeilen oder als Spalten, auf drei Stellen, mit und ohne
   Translation. Gefunden: Slot, Byte-Offset, Anordnung. Nicht gefunden: die ersten 64 Floats je
   Puffer im Log, damit sich die Frage offline weiterverfolgen läßt. Das ist Vorarbeit für Weg B,
   damit kein zweiter Lauf nötig wird.
4. **`CheckFormatSupport(A8_UNORM)`** und `CheckFeatureSupport(D3D11_OPTIONS)` einmal beim Start:
   Renderziel und Mischung für A8, Teil-Update von Konstantenpuffern.

Für den Nutzer: Sanctuary draußen bei bewölktem Himmel, einmal F8 mit Wolken im Bild, dann eine
Minute spielen, Himmel und Fluß ansehen. Kein Eingriff in Dateien.

## 5. `TechniqueTracker` und `DrawHook`

### 5.1 `Render::TechniqueTracker`

Patcht Slot 02 aller dreizehn Shader-Klassen aus `Shader::ShaderClasses()` einmal für den Prozeß,
wie `FramePhase` seine drei (die Prüfung mit `Util::DescribeVTable` und die Regel „nie
zurücknehmen" gelten unverändert). Der Thunk schreibt `(Klassenindex, Technik)` in ein atomares
Paar und ruft das Original. Kosten: ein Store je `SetupTechnique`, rund 113 je Frame.

```cpp
namespace Render
{
	struct CurrentTechnique { std::size_t classIndex; std::uint32_t technique; bool valid; };

	[[nodiscard]] bool InstallTechniqueTracker() noexcept;      // aus kGameDataReady, nach FramePhase
	[[nodiscard]] CurrentTechnique CurrentTechniqueOf() noexcept; // vom Render-Thread
	[[nodiscard]] std::size_t ClassIndexOf(std::string_view a_className) noexcept;
}
```

`FramePhase` und `FrameTrace` patchen dieselben Slots; jeder kettet an das Original, das er
vorfindet, deshalb ist die Reihenfolge der Installation die Reihenfolge der Kette und harmlos.
`ShaderCensus` bleibt ausgeschlossen, solange eines der beiden anderen läuft — die Regel aus F3
gilt hier mit.

### 5.2 `Render::DrawHook`

`VTablePatch::Install(context, 12, …)` auf dem unmittelbaren Gerätekontext, einmal für den Prozeß,
aus `Present` beim ersten Frame mit verifiziertem Kontext (wie der Present-Hook selbst). Der Thunk:

```cpp
void DrawIndexed(ID3D11DeviceContext* self, uint32_t count, uint32_t start, int32_t base)
{
	auto* const observer = g_observer.load(std::memory_order_acquire);
	if (observer == nullptr || !observer->Wants(CurrentTechniqueOf())) {
		original(self, count, start, base);
		return;
	}
	observer->Before(*self);          // darf Ziele, Blend-State, Konstanten tauschen
	original(self, count, start, base);
	observer->After(*self, count, start, base);  // gibt zurück; darf selbst zeichnen
}
```

Genau **ein** Beobachter, gesetzt und gelöscht vom Feature (`SetDrawObserver(DrawObserver*)`);
das Feature hält ihn statisch und trägt ihn in `Shutdown` aus, bevor es seine Ressourcen
freigibt. Ein zweites Feature, das einen Beobachter braucht, macht daraus eine Liste — nicht
vorher. `Draw`, `DrawIndexedInstanced` und `DrawInstanced` werden nicht gehakt, bis die Sonde
zeigt, daß die Wolken darüber laufen.

Gemessen wird der Hook selbst: die Sonde zählt Zeichenaufrufe je Frame, und die Tafel zeigt den
Frame vor und nach dem Hook.

### 5.3 Host-Test

`TechniqueTracker` und `DrawHook` sind engine- und D3D-gebunden. Testbar ist die
Beobachter-Auswahl: ein `DrawObserver`-Filter aus `(Klasse, Technik)` gegen `CurrentTechnique`
— trifft nur bei gleichem Paar und gültigem Zustand. Das ist klein und steht in
`tests/DrawObserverTests.cpp`, gebaut aus `src/Render/DrawObserver.cpp` ohne D3D.

## 6. Die Erfassung

Das Ziel ist in jedem Fall dieselbe Ressource: eine Cubemap 512², sechs Flächen, `A8_UNORM`,
`RENDER_TARGET | SHADER_RESOURCE`, eine RTV je Fläche, eine Cube-SRV, benannt
`FO4CS_CloudCoverage`. Der Blend-State zum Zusammensetzen: Farbe `ZERO / ONE` (bleibt
unberührt), Alpha `ONE / INV_SRC_ALPHA`, Schreibmaske `ALPHA`. Eine Fläche wird geleert (Alpha 0),
bevor der erste Wolkenzug eines Frames in sie geht — je Fläche der Frame der letzten Leerung, wie
der `FrameChecker` der Vorlage.

### 6.1 Weg A — Umlenken an der Cubemap der Engine

Vorbedingung aus der Sonde: `BSSkyClouds` zeichnet in Flächen von `FO4_CUBE_000`, und **jede
Fläche wird mindestens alle zwei Sekunden aufgefrischt**. Seltener hieße Schatten, die stehen,
während die Wolken ziehen — dann ist A nicht gangbar, auch wenn gezeichnet wird.

Der Beobachter will `(BSSkyShader, 0x0005)`. `Before`: `OMGetRenderTargets(1)`; ist RTV0 die
Fläche `i` der Engine-Cubemap (`cubeMapRenderTargets[0].rtView[i]`), dann Fläche `i` unserer
Cubemap leeren, falls in diesem Frame noch nicht geschehen, RTV0 gegen sie tauschen, DSV
behalten, unseren Blend-State setzen. `After`: den gesicherten Zustand zurückgeben (RTV, DSV,
Blend-State samt Faktor und Maske). Der Wolkenzug der Engine landet damit **nicht** in ihrer
Cubemap — was dem Wasser eine Reflexion ohne Wolken beschert. Das ist ein Zugeständnis, das der
Nutzer im Lauf beurteilt; die Alternative, den Zug zweimal auszuführen (einmal für die Engine,
einmal für uns), ist eine Zeile mehr in `After` und wird gewählt, wenn die Reflexion sichtbar
leidet.

### 6.2 Weg A′ — `bReflectSky`

Zeichnet die Engine nie in die Cubemap, ist `bReflectSky:Water = 0` der erste Verdächtige. Das
Plugin setzt die Einstellung dann beim Start über `RE::GetINISetting("bReflectSky:Water")` und
`SetBinary(true)` — im Speicher der Engine, nicht in der Datei — und ein zweiter Messlauf mit
derselben Sonde prüft, ob die Cubemap jetzt bedient wird. Trägt das, wird das Setzen Teil von
`Setup` (mit Rückgabe des alten Werts in `Shutdown`) und A gilt. Trägt es nicht, Weg B.

### 6.3 Weg B — zweite Projektion der Wolkenzüge des Kamerabilds

Vorbedingung aus der Sonde: die Sicht-Projektions-Matrix der Wolken liegt in einem gebundenen
Vertex-Konstantenpuffer an bekannter Stelle.

Der Beobachter will dasselbe Paar. `Before` sichert RTV/DSV, Blend-State und den betroffenen
Konstantenpuffer-Slot. `After` läßt den Zug der Engine stehen und zeichnet ihn **fünfmal** neu:
für die Flächen +X, −X, +Y, −Y, +Z unserer Cubemap je eine Sicht-Projektion mit 90° Öffnung aus
der Kameraposition, gebaut wie `worldToCam` (Zeile 3 vorwärts, Nahebene aus der Matrix), mit
unserer Fläche als RTV0, **ohne** DSV (der Tiefentest entfällt damit), unserem Blend-State und
einem eigenen Konstantenpuffer, der den der Engine mit getauschter Matrix wiedergibt.

Den Inhalt des Engine-Puffers kennen wir nur mit einem Frame Verzug: je Frame `CopyResource`
in eine Staging-Kopie, im nächsten Frame gelesen, Matrix an der bekannten Stelle ersetzt,
`UpdateSubresource` in unseren Puffer. Die Wolken sind dadurch **einen Frame alt**, was bei
Wolkenzug unsichtbar ist. Das Teil-Update von D3D11.1 wird nicht gebraucht; die Sonde notiert
trotzdem, ob es ginge.

Fünf zusätzliche Zeichenaufrufe je Lage, höchstens 160 je Frame, jeder ein Kuppelstück in 512².

### 6.4 Was in jedem Fall gilt

Kein Weg berührt den Pixelshader der Engine. Die Kamera der Cubemap ist die Kameraposition; die
Kuppel folgt ihr, deshalb ist die Cubemap an jedem Ort gültig. Der Schattenpass liest die
Cubemap eines **anderen** Frames als den, in dem sie beschrieben wurde — bei Weg A ohnehin, bei
Weg B wird sie nach dem Composite beschrieben und vor dem nächsten gelesen. Eine Kopie ist nicht
nötig, weil nie gleichzeitig gelesen und geschrieben wird.

## 7. Der Schattenpass

Ein Vollbildpass an `kBeforeComposite`, Rückruf `CloudShadows/Draw`, Passname `CloudShadows/Shadow`,
angemeldet vor `ScreenSpaceShadows` (Registrierungsreihenfolge), Blend-State `ZERO / SRC_COLOR`
auf `RT_058` und `RT_059` wie F2s Modulate.

### 7.1 Je Pixel

```
tiefe        = DS_002 an dieser Stelle; tiefe ≥ 0,9999 → Faktor 1 (Himmel)
strahl       = forward + ndc.x · rightOverScaleX + ndc.y · upOverScaleY   (FogCamera)
d            = near / (1 − tiefe)
relativ      = strahl · d                                                   (kamerarelativ)
richtung     = KugelProjektion(relativ, zurSonne, R, H)
deckung      = Coverage.SampleLevel(linear, richtung, 0).a
faktor       = saturate(1 − deckung · opacity)
```

Die Kugelprojektion ist die der Vorlage: Punkt `p = (relativ + (0,0,R)) / (R+H)` auf der Einheits-
kugel um den Erdmittelpunkt, Schnitt des Strahls `p + s·t` mit der Kugel vom Radius 1 (der
Wolkenschale), zurück in Kameraeinheiten `v = (p + s·t)(R+H) − (0,0,R)`. `R` ist der Erdradius
6371 km, `H` der Regler `cloudHeight`, beide in Spieleinheiten (1 m = 70 Einheiten, wie die
Vorlage rechnet). Die Richtung `v` geht ohne Normierung in die Cubemap.

Der Pass steht ab, wenn der Himmel nicht `kFull` ist, keine Sonne existiert oder die Sonne unter
dem Horizont steht (`zurSonne.z ≤ 0`); Ablehnungen einmal im Log, mit Grund.

### 7.2 Konstantenpuffer

Ein `PerFrame` auf Slot 1 (den `StateGuard` sichert): `CameraForward` (xyz, w Kamerahöhe —
ungenutzt, für Gleichlauf mit F3), `CameraRight` (xyz, w near), `CameraUp` (xyz, w debugView),
`SunDirection` (xyz zur Sonne, w opacity), `Params` (x cloudHeight in Einheiten, y Erdradius in
Einheiten, zw frei). Dazu ein linearer Sampler auf Slot 0 mit `CLAMP` — `StateGuard` sichert
zusätzlich PS-Sampler 0, wenn er es nicht schon tut.

### 7.3 Debug-Ansicht

`debugView` (`CameraUp.w`): `off`; `coverage` gibt die abgetastete Deckung als Grau mit Alpha 1
und Blend-State ohne Multiplikation aus — die Wolkenkarte, wie der Boden sie sieht; `direction`
gibt `normalize(richtung) · 0,5 + 0,5` aus, um die Kugelprojektion zu prüfen. Beide schreiben
auf `RT_058` allein.

## 8. Das Feature

`src/Features/CloudShadows.{h,cpp}`, abgeleitet von `Features::Feature`, registriert in
`RegisterAll` vor `ScreenSpaceShadows`. Die reine Rechnung in
`src/Features/CloudShadows/CloudProjection.{h,cpp}`.

| Methode    | Was sie tut                                                                                                                                                                        |
| ---------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Name`     | `"CloudShadows"`                                                                                                                                                                   |
| `Declare`  | Schalter über `DeclareFeature`, vorbelegt **an**, drei Regler, eine Auswahl                                                                                                        |
| `Setup`    | Prüft `A8_UNORM` als Renderziel, legt Cubemap, RTVs, SRV, beide Blend-States, Sampler, Konstantenpuffer an, übersetzt den Shader, setzt den Draw-Beobachter, trägt den Rückruf ein |
| `Frame`    | Dateiwächter; Weg B: Staging-Kopie lesen und eigenen Puffer schreiben                                                                                                              |
| `Shutdown` | Beobachter austragen, Rückruf austragen, alles freigeben; Weg A′: `bReflectSky` zurückschreiben                                                                                    |

### 8.1 Die Einstellungen

| Pfad                       | Bereich     | Vorgabe | Bedeutung                                                                      |
| -------------------------- | ----------- | ------- | ------------------------------------------------------------------------------ |
| `CloudShadows/enabled`     | —           | an      | Schalter                                                                       |
| `CloudShadows/opacity`     | 0 – 4       | 0,5     | Dunkelheit der Schatten, wie die Vorlage                                       |
| `CloudShadows/cloudHeight` | 500 – 20000 | 2000    | Höhe der Wolkenschale in Metern; bestimmt, wie weit die Schatten versetzt sind |
| `CloudShadows/debugView`   | Auswahl     | off     | `off`, `coverage`, `direction`, siehe 7.3                                      |

Alle Regler werden bei jeder Verwendung frisch gelesen.

### 8.2 Auslieferung

```
package/Features/CloudShadows/Shaders/FO4/CloudShadows/CloudShadows.hlsl
```

Ohne `CORE`-Marker, also ein eigenes Addon-Archiv, das sechste. `verify-package.ps1` wird um das
Archiv erweitert. `TechniqueTracker` und `DrawHook` liefern keine Datei aus und stecken in der
Basis.

## 9. Prüfung

### 9.1 Host-Tests

-   **`CloudProjectionTests`**, neu — die Kugelprojektion als reine Funktion: Sonne im Zenit
    liefert eine Richtung senkrecht über dem Punkt (x, y des Punkts, z = H); Sonne 45° hoch
    versetzt die Richtung um `H` in der Waagerechten; ein Punkt hinter der Kamera rechnet wie
    einer davor; `H = 0` liefert den Punkt selbst. Dazu der Faktor: Deckung 1 bei `opacity` 0,5
    gibt 0,5, `opacity` 4 klemmt bei 0.
-   **`DrawObserverTests`**, neu — siehe 5.3.
-   **`PhaseDispatcherTests`** unverändert; **`SettingsSchemaTests`**, erweitert um den Block.
-   **`CameraTests`**, erweitert — die fünf Flächenmatrizen aus Weg B gegen `worldToCam` der drei
    F2-Frames: Zeile 3 ist die Flächenrichtung, die Nahebene stimmt, ein Punkt auf der Achse
    landet in der Bildmitte. Nur wenn Weg B gebaut wird.

Jeder grüne Test wird danach absichtlich gebrochen, die erwarteten Fehlschläge werden vorher
benannt, und **vor** dem Ausführen wird geprüft, daß der Bau geglückt ist. Mutationen mit dem
Edit-Werkzeug, nie `git checkout`.

### 9.2 Was ausdrücklich nicht host-getestet wird

Der Draw-Hook, das Umlenken, der Shader, die Cubemap-Orientierung. Sie werden in den Läufen
geprüft, mit der Debug-Ansicht `coverage` als Sichtprobe, und in der Roadmap als so belegt
geführt.

## 10. Risiken und Gabelungen

**Das tragende Risiko** ist die Erfassung: ob die Engine in die Cubemap zeichnet (Weg A), ob
`bReflectSky` das ändert (A′), ob die Matrix im Konstantenpuffer zu finden ist (B). Die Sonde
entscheidet vor der ersten Zeile des Schattenpasses. Findet sie für keinen Weg die Vorbedingung,
wird F4 geteilt: die Erfassung als Forschung mit RenderDoc, der Pass wartet.

**Vier kleinere Gabelungen:**

-   **Das Alpha ist nicht die Deckung** — die Engine mischt vormultipliziert oder additiv. Der
    Blend-State aus der Sonde sagt es vorher. Additiv hieße: Alpha aus der Farbhelligkeit, ein
    eigener kleiner Pixelshader statt des Blend-Tricks, was Weg B voraussetzt.
-   **Die Cubemap der Engine ist nicht weltachsig** — dann stimmt die Kugelprojektion nur mit
    einer Drehung. Die Debug-Ansicht `coverage` zeigt es; die Korrektur ist eine Matrix im Puffer.
-   **`A8_UNORM` ist kein Renderziel auf dieser Karte** — dann `R8G8B8A8_UNORM` mit Schreibmaske
    `ALPHA`, viermal so groß, sonst gleich.
-   **Die Wolken laufen über `DrawIndexedInstanced`** — dann Slot 20 zusätzlich, derselbe Thunk.

**Zugeständnisse:** Die Deckungskarte ist einen bis mehrere Frames alt. Bei Weg A verliert die
Wasserreflexion ihre Wolken, solange der Zug nicht doppelt ausgeführt wird. Der Draw-Hook kostet
jeden Zeichenaufruf des Spiels einen Vergleich; die Tafel mißt es.

## 11. Abnahme

Zwei Spielstarts, drei bei Weg A′ oder B.

**Lauf 1, Sonde.** Sanctuary draußen, bewölkt: einmal F8 mit Wolken im Bild, dann eine Minute
spielen. Ergebnis: Tabelle Technik × Ziel, Blend-State, Fundort der Matrix, Formatunterstützung.
Danach wird der Weg gewählt, die Sonde entfernt, die Erfassung und der Pass gebaut.

**Lauf 2, Abnahme.**

| #   | Schritt                                                                | Geht es schief, heißt das                                          |
| --- | ---------------------------------------------------------------------- | ------------------------------------------------------------------ |
| 1   | Overlay: Block `Cloud Shadows` mit Schalter, drei Reglern, Auswahl     | `Declare` lief nicht                                               |
| 2   | Bewölkt, Blick über die Landschaft: Schalter aus und an                | Kein Unterschied: Erfassung leer, oder der Pass steht ab           |
| 3   | `debugView` auf `coverage`: die Wolkenkarte, sie zieht mit dem Himmel  | Schwarz: nichts erfaßt; steht: Cubemap wird nicht aufgefrischt     |
| 4   | `debugView` auf `direction`: gleichmäßiger Verlauf, kein Sprung        | Sprünge: Kugelprojektion oder Kameravorzeichen                     |
| 5   | `opacity` auf 4: harte Schatten, die in Zugrichtung der Wolken wandern | Falsche Richtung: Cubemap-Orientierung                             |
| 6   | `cloudHeight` schieben: die Schatten versetzen sich gegen die Wolken   | Nichts: `Params` kommt nicht an                                    |
| 7   | Tafel: `CloudShadows/Draw` und `/Shadow`, dann **F11**                 | Fehlen sie: `PassScope` greift nicht                               |
| 8   | Root Cellar, Pip-Boy, Alt-Tab                                          | Artefakte: der Beobachter oder der Wächter gibt nicht alles zurück |
| 9   | Feature im laufenden Spiel aus und an                                  | Absturz: Beobachter ausgetragen, bevor der Hook fertig war         |
| 10  | Hot-Reload, **von mir angestoßen** durch Berühren der deployten Datei  | Keine Zeile „recompiling": der Wächter sieht die Datei nicht       |

Dazu nebenbei die beiden offenen Punkte aus F3: der Farbabgleich des Nebels über dessen
Debug-Ansicht `color`, und der Hot-Reload von `Fog.hlsl` auf demselben Weg.

**Abgenommen ist F4, wenn beide Läufe stehen** und die Passzeile samt der Kosten des Draw-Hooks
in der Roadmap unter „Aus Teilprojekt F4 bestätigt" steht. Was ungeprüft bleibt, wird dort
benannt.

## 12. Was F4 für F5 aufwärts hinterläßt

-   **`Render::TechniqueTracker`** — welche Technik gerade eingerichtet ist, für jeden, der einen
    Zeichenaufruf einer bestimmten Technik abpassen will.
-   **`Render::DrawHook`** mit `DrawObserver` — der Weg, einen Zeichenaufruf der Engine zu
    umgeben: Ziele tauschen, nachzeichnen, zurückgeben. Dynamic Cubemaps (F9) und IBL (F10) werden
    denselben Griff brauchen.
-   **Eine Cubemap als Ziel eigener Zeichenaufrufe**, samt der fünf Flächenmatrizen, falls Weg B.
-   **Der Befund, ob und wann Fallout 4 seine Reflexions-Cubemap zeichnet** — für das Wasser (F+)
    und die Cubemaps von F9.
