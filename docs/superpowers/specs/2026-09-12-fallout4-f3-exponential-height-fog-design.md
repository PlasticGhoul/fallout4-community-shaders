# Teilprojekt F3 — Exponential Height Fog

Spec, Stand 2026-09-12. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt A bis F2 voraus, insbesondere `2026-09-06-fallout4-f2-screen-space-shadows-design.md` (die
Naht: Frame-Phase, Ziele, Ressourcen, Zustandswächter, Vollbildpass) und
`2026-09-05-fallout4-f1-performance-overlay-design.md` (die Zeitmessung, an der F3 abgenommen wird).

## 1. Kontext und Ziel

Fallout 4 nebelt mit einer Distanzrampe und einer Höhenrampe, gerechnet im Composite-Pixelshader,
mit vier Farben (nah/fern, tief/hoch), einer Potenz und einer Obergrenze. Was fehlt, ist Nebel als
Schicht: einer, der in Senken liegt, mit der Höhe dünner wird und um die Sonne glüht.

**Ziel:** Analytischer exponentieller Höhennebel nach der Unreal-Formel, die auch die Vorlage
benutzt, als eigener Vollbildpass hinter der opaken Szene — sichtbar im Spiel, meßbar in der Tafel
aus F1, mit einem Regler, der den Spielnebel zurücknimmt.

**Abnahmekriterium der Roadmap:** „Sichtbarer Effekt plus CPU-/GPU-Zahlen."

### Was F3 erbt

-   **Die Naht aus F2.** `Render::FramePhase`, `Render::Targets`, `Render::Resources`,
    `Render::StateGuard`, `Render::FullscreenPass` mit eigenem Rasterizer- und Depth-Stencil-State,
    `Shader::ShaderCompiler` mit drei Profilen und Defines, `Util::FileWatch` fürs Nachladen.
-   **Die Kamera aus F2.** `Render::Camera` weiß, daß `NiCamera::worldToCam` die vollständige
    Welt-nach-Clip-Matrix ist, daß die Kamerarotation vorwärts in Zeile 0, oben in Zeile 1 und
    rechts in Zeile 2 hält, daß die Skalen in `worldToCam[0]` und `[1]` stehen und die Nahebene in
    `worldToCam[2][3]`, und daß die Tiefe `z/w = 1 − near/d` ist, ohne Fernebene.
-   **Die Sonne aus F2.** `Sky::sun->light`, gelesen als `NiLight`: Richtung in Zeile 0 der
    Weltrotation, Farbe in `diff`.
-   **Den Census-Mechanismus aus dem Messspike.** `Shader::ShaderClasses()` kennt die zwölf
    Shader-Singletons mit aufgelöster vtable, `Render::VTablePatch` tauscht einen Eintrag,
    `Util::DescribeVTable` prüft die Tabelle vorher, Slot 09 buchstabiert Techniknamen.
-   **`Render::PassScope`** (F1) und das Einstellungsschema samt Panel (E2).

### Die Befunde, die den Entwurf bestimmen

1. **Der Spielnebel entsteht im Composite.** Seine Parameter liegen CPU-seitig offen in
   `BSGraphics::State::fogState` (`FogStateType`: `rangeData`, vier Farben, `power`, `clamp`,
   `highDensityScale`, `highLowRangeData`) und werden als `PerFrame`-Puffer hochgeladen. Die
   Quelle davor ist `Sky` (`fogDistances[8]`, `fogHeight`, `fogPower`, `fogClamp`), interpoliert
   aus `TESWeather::fogData[18]`.
2. **Die Vorlage wendet ihren Nebel in den Objekt-Shadern an**, der andere FO4-Port injiziert
   ihn per Define in einen nachgebauten Composite-Shader. Beides ist Permutations-Ersatz und
   damit F12. F3 muß seinen Nebel **als Pass hinter dem fertigen Bild** anbringen.
3. **Die Frame-Struktur hinter dem Composite ist nicht gemessen.** Der Spike hat je Klasse nur
   den ersten Aufruf festgehalten: Composite bindet `RT_058/059`, der Himmel zeichnet nach
   `RT_004` und liest beide Lichtziele. Ob `RT_004` danach die fertige Szene ist und welche Klasse
   als erste danach läuft, weiß nur ein Trace. Das ist die erste Aufgabe.

## 2. Umfang

### In F3 enthalten

1. Ein Diagnose-Feature `FrameTrace`, bleibend, standardmäßig aus.
2. `Render::FramePhase` mit zwei Phasen statt einer.
3. Der Nebelpass: ein Pixelshader auf dem Dreieck aus F2, mit Alpha-Mischung auf das HDR-Ziel.
4. Der Regler „Vanilla Fog" über `fogState.clamp`, mit Messung und Fallback.
5. Ein Feature `ExponentialHeightFog` mit sieben Reglern, Beschriftungen im Katalog, eigenem
   Addon-Archiv.
6. Ein neuer Host-Test, drei erweiterte.

### Nicht in F3 enthalten

-   **Der volumetrische Froxel-Nebel der Vorlage.** Vier Compute-Shader, zeitliche Historie, die
    Schattenkarte der Sonne — ein Vorhaben von der Größe von F2, das dieselben Zutaten braucht
    wie F6 (Volumetric Lighting). Zuschnitt nach F6. In der Roadmap als offene Erweiterung
    geführt.
-   **Eine eigene Nebelfarbe.** Unser Nebel nimmt die Nebelfarbe des Spiels, die je Wetter und
    Tageszeit interpoliert ist und zum Himmel paßt. Eine feste eigene Farbe wäre bei
    Wetterwechsel falsch. Damit entfällt auch eine Art `Color` im Einstellungsschema; sie kommt,
    wenn ein Feature sie wirklich braucht.
-   **Wetterabhängige Regler** (die `WeatherUI` der Vorlage).
-   **Abschwächung des Sonnenlichts durch Nebel** (`GetSunlightFogAttenuation`). Das ist ein
    Eingriff in die Beleuchtung, nicht ins fertige Bild — F12.
-   **Dynamische Cubemaps als Einstreuquelle** (F9 aufwärts).
-   **Wassernebel.** Unter Wasser bleibt der des Spiels.
-   **Ersatz des Spielnebels.** Bleibt F12; F3 nimmt ihn zurück, ersetzt ihn nicht.

## 3. Vorentscheidungen

Aus dem Brainstorming vom 2026-09-12, jeweils mit Begründung:

-   **Nur analytisch.** Siehe oben.
-   **Spielnebel per Regler zurücknehmen, mit „nur addieren" als Fallback.** Ein Wert in
    Engine-Speicher, der in `Shutdown` zurückgegeben wird — der Trick aus `ImagespaceTint`. Die
    Vorgabe des Reglers ist 1, also unverändert; F3 beginnt ohne Eingriff in den Spielnebel.
-   **Zweiter Anker in `FramePhase`, Nebel als Vollbildpass mit Alpha-Mischung.** Fortsetzung der
    Naht aus F2 statt eines zweiten Mechanismus. Verworfen: Ersatz eines Imagespace-Passes über
    den Zeigertausch aus C — C hat gezeigt, daß die Engine solche Pässe nicht in jedem Frame
    einplant. Verworfen: Injektion in den Composite — F12.
-   **Frame-Trace als bleibendes Feature.** F4 bis F11 brauchen dieselbe Antwort für ihre eigenen
    Anker: Cloud Shadows und Skylighting wollen vor die Beleuchtung, Volumetrics und SSGI
    dahinter. Wegwerfcode müßte viermal neu geschrieben werden.
-   **Kein Farbwähler.** Siehe oben.

## 4. Der Frame-Trace

### 4.1 Was es ist

`Features::FrameTrace` in `src/Features/`, mit einem Schalter (aus) und einer Taste (`Key`, Vorgabe
F10, `0x79`). Ein Tastendruck schärft es; der nächste vollständige Frame zwischen zwei `Present`
wird aufgezeichnet und im darauffolgenden `Frame()` als Block ins Log geschrieben.

### 4.2 Was es aufzeichnet

Je `SetupTechnique`-Aufruf einer der zwölf Klassen aus `Shader::ShaderClasses()` eine Zeile:

```
  #17  BSDFCompositeShader  0x0209 DFComposite_Base_ApplyAO   RTV FO4_RT_058 FO4_RT_059  DSV FO4_DS_002  SRV0 FO4_RT_022 SRV1 FO4_RT_020 …
```

Laufende Nummer, Klasse, Technik-ID, Name aus Slot 09 (nur für Klassen, deren RTTI-Prüfung
hält, wie im Census), die Ziele am Ausgabe-Merger (`OMGetRenderTargets`, acht Plätze plus
Tiefensicht) und die Pixel-SRVs 0 bis 15 (`PSGetShaderResources`), leere Plätze ausgelassen,
alles benannt über `Render::GetDebugName` und `GetViewTargetName`. Jeder `Get` liefert
Referenzen, jede wird freigegeben.

Am Blockanfang: Framenummer aus B1, `Sky::mode`, Kameraposition, `fogState` vollständig
(`rangeData`, vier Farben, `power`, `clamp`, `highDensityScale`, `highLowRangeData`) und
`Sky::fogDistances[8]`, `fogHeight`, `fogPower`, `fogClamp`. Am Blockende die Zahl der Aufrufe
je Klasse und die Gesamtzahl.

### 4.3 Wie es einhängt

`Render::VTablePatch::InstallAtTable` auf Slot 02 jeder Klasse, nach `Util::DescribeVTable`
(Klassenname und Subobjekt-Offset 0), genau wie `ShaderCensus`. Zwölf Thunks aus einer
`index_sequence`, damit jeder weiß, für welche Klasse er steht.

**Einmal installiert, nie zurückgenommen.** Der Grund steht in der F2-Spec: einen vtable-Eintrag
zurückzuschreiben, während ein anderer Thread darin steht, ist ein Rennen. Ungeschärft kostet
der Thunk das Lesen eines `std::atomic<bool>` und den Sprung zum Original. `Shutdown` entschärft
und läßt die Patches stehen; ein zweites `Setup` findet sie vor und installiert nicht erneut.

**Verhältnis zu `FramePhase`.** Das hält denselben Slot des Composite (künftig zweier Klassen)
und schreibt ebenfalls nie zurück. `FrameTrace` installiert später — aus `Setup`, das aus
`Present` läuft, nach `InstallFramePhase` aus `kGameDataReady` — und sitzt damit davor; sein
Original ist der Thunk von `FramePhase`, die Kette trägt.

**Verhältnis zu `ShaderCensus`.** Der schreibt zurück, sobald eine Klasse berichtet hat, und
würde damit unseren Eintrag überschreiben. `FrameTrace::Setup` lehnt ab, solange
`ShaderCensus/enabled` steht, mit Logzeile. Umgekehrt gilt dasselbe bereits für den Census gegen
`FramePhase` — die Reihenfolge-Notiz in `FramePhase.h`.

### 4.4 Grenzen

-   Aufzeichnung in ein festes Feld von 512 Einträgen auf dem Render-Thread, kein Heap im
    Thunk. Darüber wird abgeschnitten, und der Block sagt, wie viele fehlen.
-   Die 160 Imagespace-Unterklassen tragen eigene vtables und erscheinen nicht. Für den Anker von
    F3 reicht das: gesucht ist die erste der zwölf Klassen nach Composite und Himmel.
-   Ein Frame ist „zwischen zwei `Present`". Aufrufe außerhalb des Renderns (Ladebildschirm)
    sind ebenso ein Frame; der Blockkopf mit `Sky::mode` sagt, was man vor sich hat.

### 4.5 Host-Test

`FrameTraceTests`, neu: Zeilenaufbau aus einem synthetischen Eintrag (Klasse, ID, Name, drei
Zielnamen, zwei SRV-Namen), die Zählung je Klasse über eine Liste, das Verhalten bei 512 plus
eins. Der Zeilenaufbau und die Zählung liegen dafür in einer Klasse ohne D3D, `FrameTraceLog`,
neben dem Feature.

## 5. Zwei Phasen in `FramePhase`

### 5.1 Schnittstelle

```cpp
namespace Render
{
    enum class Phase : std::uint8_t
    {
        kBeforeComposite,  // BSDFCompositeShader, wie in F2
        kAfterOpaque,      // die Klasse, die der Trace als erste nach Composite und Himmel zeigt
        kCount
    };

    [[nodiscard]] bool InstallFramePhase() noexcept;                       // beide Patches
    PhaseDispatcher::Token SubscribeFramePhase(Phase, std::string_view a_name, std::function<void()>);
    void UnsubscribeFramePhase(Phase, PhaseDispatcher::Token) noexcept;
    [[nodiscard]] std::uint64_t FramePhaseHits(Phase) noexcept;
}
```

Eine feste Tabelle mit einem Eintrag je Phase: Klassenname, `RE::VTABLE::<Klasse>[0]`,
`VTablePatch`, Original, `PhaseDispatcher`, Trefferzähler. `ScreenSpaceShadows` übergibt
`kBeforeComposite` und ändert sonst nichts.

### 5.2 Was der Trace einträgt

Die Klasse hinter `kAfterOpaque` und der Slot des HDR-Ziels werden **nach dem Trace-Lauf**
eingetragen; der Plan führt das als eigene Aufgabe mit dem Trace als Voraussetzung. Erwartet
wird `BSEffectShader` und `FO4_RT_004`. Zeigt der Trace, daß diese Klasse nicht in jedem Frame
läuft — ohne Transparenzen kein Effect-Aufruf —, wird der nächste stabile Aufruf gewählt. Im
Extremfall ist der zweite Anker der Pixelshader-Tausch aus C an einem Pass, den der Trace als
stets vorhanden zeigt; das stünde dann als Befund in der Roadmap, und Abschnitt 6 bliebe bis auf
die Bindung des Ziels unverändert.

`Render::Targets` bekommt eine Konstante `kSceneHDR` für diesen Slot, und `LogSlots` nennt ihn
mit.

### 5.3 Ein Detail aus F2 gilt weiter

Beim Anker hängt das Ziel der kommenden Klasse noch nicht am Merger — `SetupTechnique` läuft vor
deren Bindung —, wohl aber, was die vorige Klasse hinterlassen hat. Der Nebelpass bindet sein Ziel
deshalb selbst über `Render::Targets::kSceneHDR` und löst vorher den Merger, weil `DS_002` dort
als Tiefensicht hängen kann, während wir es als SRV lesen.

### 5.4 Host-Test

`PhaseDispatcherTests`, erweitert: zwei Dispatcher zählen getrennt, ein Abonnent des einen läuft
nicht im anderen, `Unsubscribe` mit der falschen Phase ist folgenlos.

## 6. Der Nebelpass

### 6.1 Datenfluß

```
DS_002 (Tiefe, R24_UNORM_X8) ─────────────────────────────┐
Kamera: vorwärts, rechts/sx, oben/sy, Höhe, near ─────────┤
fogState: rangeData, highLowRangeData, vier Farben, power ┤──► Fog.hlsl (ps_5_0, Dreieck aus F2) ──► kSceneHDR
Sonne: Richtung (Zeile 0), Farbe (NiLight::diff) ─────────┤      SRC_ALPHA / INV_SRC_ALPHA
sieben Regler ────────────────────────────────────────────┘
```

Ein `PassScope` `ExponentialHeightFog/Fog`, geschachtelt in `ExponentialHeightFog/Draw`.

### 6.2 Strahl und Position

Ohne Matrixinversion, aus den in F2 vermessenen Größen:

```
strahl(ndc)  = vorwärts + ndc.x · rechts / sx + ndc.y · oben / sy     (unnormiert)
d            = near / (1 − z)                                           (Abstand entlang vorwärts)
relativ      = strahl · d                                               (kamerarelativ)
höhe         = kameraHöhe + relativ.z
länge        = |relativ|
```

Kamerarelativ, damit Weltkoordinaten um 80.000 nicht in die Float-Genauigkeit geraten; nur die
Höhe braucht die Kamera-z, und die ist eine Zahl. `Render::Camera` bekommt eine Funktion
`FogCameraConstants`, die diese Größen aus `worldToCam` und der Kamerarotation füllt; sie ist
rein und wird gegen die Ladebildschirm-Matrix geprüft: Strahl der Bildmitte ist vorwärts, `near`
ist 15, `sx` und `sy` stehen im Verhältnis 16:9.

**Himmel.** `z = 1` heißt kein Objekt. Der Strahl bekommt eine feste Horizontweite von 500.000
Einheiten. Nach oben konvergiert das Integral von selbst, nach unten läuft es in volle Deckung —
was ein Horizont in einer Nebelschicht tun soll.

### 6.3 Das Integral

Die Unreal-Formel aus `ExponentialHeightFog.hlsli` der Vorlage, ohne Volumetrik und Cubemap:

```
falloff      = heightFalloff · 0.001
density      = density · 0.001
ursprung     = density · 2^(−falloff · max(kameraHöhe − height, 0))
startDistance als Ausschlußstrecke: Ursprung an deren Ende neu, Länge um sie gekürzt
steigung     = falloff · relativ.z
integral     = ursprung · länge · (|steigung| > 0.01 ? (1 − 2^−steigung) / steigung : ln2 − ½ln²2 · steigung)
deckkraft    = 1 − saturate(2^−integral)
```

Einstreuung der Sonne: Henyey-Greenstein mit `sunAnisotropy` über den Kosinus zwischen
Sichtstrahl und Sonnenrichtung, mal Sonnenfarbe, mal Deckkraft, mal `sunInscattering`.

### 6.4 Die Farbe

Die Nebelfarbe des Spiels für dieses Pixel, aus `fogState` nachgerechnet, so wie der Composite
sie bildet — die Formel steht im nachgebauten Composite-Shader des anderen Ports:

```
distanz    = saturate(länge · rangeData.x − rangeData.z)^power
höhenpaar  = saturate(höhe · highLowRangeData.xy − highLowRangeData.zw)
tief       = lerp(nearLowColor,  farLowColor,  distanz)
hoch       = lerp(nearHighColor, farHighColor, distanz)
farbe      = lerp(tief, hoch, höhenpaar)   — genaue Mischung nach dem Trace gegen den Spielnebel geprüft
```

Die Mischung wird im Spiel abgeglichen: ein Define `FOG_DEBUG_COLOR` am Kopf von `Fog.hlsl` läßt
den Pass nur diese Farbe mit voller Deckkraft ausgeben, und der Vergleich mit dem Spielnebel am
Horizont sagt, ob sie stimmt. Es wird für den Abgleich von Hand in die Datei gesetzt und über das
Nachladen wirksam; die Datei im Repo und im Archiv trägt es nicht.

Ausgabe: `float4(farbe · (1 + einstreuung), deckkraft)`. Die Karte mischt mit Quell-Alpha; das
Ziel ist `R11G11B10_FLOAT` ohne Alpha, was für `SRC_ALPHA / INV_SRC_ALPHA` nichts ausmacht.

### 6.5 Konstantenpuffer

Auf `b1`, 16-Byte-Ausrichtung ohne Lücken, je Feld ein `static_assert`-geprüfter Offset wie in F2:

| Feld                        | Typ        | Inhalt                                                |
| --------------------------- | ---------- | ----------------------------------------------------- |
| `cameraForward`             | float4     | xyz vorwärts, w Kamerahöhe                            |
| `cameraRight`               | float4     | xyz rechts / sx, w near                               |
| `cameraUp`                  | float4     | xyz oben / sy, w Horizontweite                        |
| `sunDirection`              | float4     | xyz Richtung zur Sonne, w `sunInscattering`           |
| `sunColor`                  | float4     | rgb `NiLight::diff`, w `sunAnisotropy`                |
| `fogRange`                  | float4     | `rangeData`                                           |
| `fogHeightRange`            | float4     | `highLowRangeData`                                    |
| `fogNearLow` … `fogFarHigh` | 4 × float4 | die vier Farben, w von `fogNearLow` ist `power`       |
| `params`                    | float4     | `density`, `height`, `heightFalloff`, `startDistance` |

### 6.6 Abbruchbedingungen

In dieser Reihenfolge, jede beim ersten Auftreten mit eigener Logzeile:

1. `Sky::GetSingleton()` null oder `mode` nicht `kFull`, oder `sun` oder `sun->light` null.
2. `kSceneHDR` oder `kSceneDepth` fehlt.
3. `Main::WorldRootCamera()` null oder `worldToCam` nicht endlich.

Wird abgebrochen, wird nicht gezeichnet, und der Vanilla-Regler schreibt in diesem Frame nicht.

### 6.7 Nachladen

`Fog.hlsl` unter `Util::FileWatch`, abgefragt in `Frame()`, Neuübersetzung dort, mit dem
Zurücksetzen des Wächters auch bei Fehlschlag — wie `RaymarchCS.hlsl`.

## 7. Der Vanilla-Regler

### 7.1 Der Hebel

`fogState.clamp` ist die Obergrenze der Deckkraft des Spielnebels, im Composite
`FogNearHighColorAndClamp.w`. Der Regler `vanillaFog` von 0 bis 1 multipliziert ihn: bei 1
unverändert, bei 0 kein Spielnebel. Ein Wert statt vier Rampen, und er skaliert Distanz- und
Höhenanteil gemeinsam.

### 7.2 Wer hat zuletzt geschrieben

Multipliziert man jeden Frame den Wert, den man vorfindet, und die Engine schreibt ihn nicht
jedes Mal neu, fällt er in wenigen Frames auf null. Deshalb hält das Feature zwei Werte: das
zuletzt gesehene Original und das zuletzt Geschriebene. Steht beim nächsten Mal das Geschriebene
noch da, hat die Engine nicht aufgefrischt, und gerechnet wird mit dem Original. Steht etwas
anderes da, ist das das neue Original. `Shutdown` schreibt das Original zurück. Das ist das
Verfahren, mit dem `Settings::Save` die Dateiüberwachung rebasiert, und der Rückgabepfad aus
`ImagespaceTint`.

Steht der Regler auf 1, wird nicht geschrieben und nichts gemerkt.

### 7.3 Die Messung

Es ist nicht bekannt, wann die Engine `fogState` auffrischt und wann sie den Puffer hochlädt.
Der Trace-Lauf schreibt deshalb je Sekunde `fogState.clamp` und `Sky::fogClamp`, und eine
Probe im Feature schreibt in `Frame()` aus `Present` heraus `clamp · 0.5` und protokolliert in
`kBeforeComposite`, ob der Wert bis dorthin überlebt. Die Probe ist ein eigener `diag:`-Commit
wie in F2, der nach dem Lauf mitsamt Code wieder herausgenommen wird; was bleibt, ist der
Befund in der Roadmap. Drei Ausgänge:

1. Er überlebt: der Regler wirkt mit einem Frame Verzug. Fertig.
2. Er ist bis dorthin überschrieben: zweiter Versuch aus `kBeforeComposite` heraus, dazu
   `Sky::fogClamp` als zweite Quelle. Ein weiterer Lauf.
3. Nichts hält: der Regler entfällt ersatzlos, F3 bleibt bei „nur addieren", die Roadmap hält
   fest, daß der Spielnebel erst mit F12 weicht.

**Sichtprüfung** unabhängig vom Log: Regler auf 0, Blick in die Ferne, die Distanztrübung des
Spiels muß weg sein.

## 8. Das Feature

`src/Features/ExponentialHeightFog.{h,cpp}`, abgeleitet von `Features::Feature`, registriert in
`RegisterAll` nach `ScreenSpaceShadows`; `FrameTrace` davor, weil es ein Werkzeug ist.

| Methode    | Was sie tut                                                                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------ |
| `Name`     | `"ExponentialHeightFog"`                                                                                     |
| `Declare`  | Der Schalter über `DeclareFeature`, vorbelegt **an**, sieben Regler                                          |
| `Setup`    | Übersetzt den Pixelshader, legt Konstantenpuffer und Blend-State an, trägt den Rückruf in `kAfterOpaque` ein |
| `Frame`    | Dateiwächter, Vanilla-Regler nach 7.2                                                                        |
| `Shutdown` | Trägt aus, gibt Puffer, Blend-State und Shader zurück, schreibt `clamp` zurück                               |

### 8.1 Die Einstellungen

| Pfad                                   | Bereich        | Vorgabe | Bedeutung                                                           |
| -------------------------------------- | -------------- | ------- | ------------------------------------------------------------------- |
| `ExponentialHeightFog/enabled`         | —              | an      | Schalter                                                            |
| `ExponentialHeightFog/density`         | 0 – 1          | 0,005   | Grunddichte, im Shader × 0,001                                      |
| `ExponentialHeightFog/height`          | −22000 – 22000 | 0       | Welt-z, ab der der Nebel dünner wird; Sanctuary liegt bei rund 7900 |
| `ExponentialHeightFog/heightFalloff`   | 0,001 – 2      | 0,2     | Abnahme mit der Höhe, im Shader × 0,001                             |
| `ExponentialHeightFog/startDistance`   | 0 – 100000     | 0       | nebelfreier Nahbereich                                              |
| `ExponentialHeightFog/sunInscattering` | 0 – 10         | 1       | Stärke des Sonnenglühens                                            |
| `ExponentialHeightFog/sunAnisotropy`   | −0,99 – 0,99   | 0,2     | Bündelung des Glühens um die Sonne                                  |
| `ExponentialHeightFog/vanillaFog`      | 0 – 1          | 1       | Anteil des Spielnebels, siehe 7                                     |
| `FrameTrace/enabled`                   | —              | aus     | Schalter                                                            |
| `FrameTrace/key`                       | Taste          | F10     | schärft die Aufzeichnung                                            |

Die Horizontweite von 500.000 ist kein Regler. Alle Regler werden bei jeder Verwendung frisch
gelesen; gemerkt werden nur die beiden Werte aus 7.2.

### 8.2 Auslieferung

```
package/Features/ExponentialHeightFog/Shaders/FO4/ExponentialHeightFog/Fog.hlsl
```

Ohne `CORE`-Marker, also ein eigenes Addon-Archiv, das fünfte. `FrameTrace` liefert keine Datei
aus und steckt in der Basis. `verify-package.ps1` wird um das Archiv erweitert.

## 9. Prüfung

### 9.1 Host-Tests

-   **`FrameTraceTests`**, neu — siehe 4.5.
-   **`CameraTests`**, erweitert — `FogCameraConstants` gegen die Ladebildschirm-Matrix, siehe 6.2,
    dazu gegen einen der drei F2-Frames: der Strahl der Bildmitte ist dessen Zeile 0.
-   **`PhaseDispatcherTests`**, erweitert — siehe 5.4.
-   **`SettingsSchemaTests`**, erweitert um beide neuen Blöcke.

Jeder grüne Test wird danach absichtlich gebrochen, die erwarteten Fehlschläge werden vorher
benannt, und **vor** dem Ausführen wird geprüft, daß der Bau geglückt ist. Eine Mutation, die
nicht fällt, ist ein Befund über den Test. Mutationen mit dem Edit-Werkzeug, nie `git checkout`.

### 9.2 Was ausdrücklich nicht host-getestet wird

Der Shader, die Bindung des HDR-Ziels, der Vanilla-Regler. Sie werden in den beiden Läufen
geprüft und in der Roadmap als so belegt geführt.

## 10. Risiken und Gabelungen

**Das tragende Risiko** ist der zweite Anker: daß die erste Klasse nach dem Himmel in jedem Frame
läuft und daß das Ziel, das sie vorfindet, die fertige HDR-Szene ist. Der Trace entscheidet das
vor der ersten Zeile des Nebelpasses; die Ausweichwege stehen in 5.2.

**Zwei kleinere Gabelungen:**

-   **Die Nebelfarbe.** Ob die Mischung aus 6.4 die des Composite trifft, sagt der Vergleich am
    Horizont mit `FOG_DEBUG_COLOR`. Trifft sie nicht, ist die Korrektur eine Zeile im Shader.
-   **Der Vanilla-Regler.** Drei Ausgänge in 7.3, jeder mit Folge.

**Ein bekanntes Zugeständnis.** Der Pass liegt vor den Transparenzen; Wasser, Glas und Effekte
sehen unseren Nebel nur, soweit er auf dem liegt, was hinter ihnen ist. Der Spielnebel auf ihnen
bleibt. Das ist der Preis eines Passes gegenüber F12 und steht so in der Roadmap.

## 11. Abnahme

Zwei Spielstarts, gebündelt.

**Lauf 1, Trace und Probe.** Sanctuary draußen, Blick über den Ort: F10. Ein zweites Mal mit
Wasser im Bild: F10. Dazu läuft die Probe aus 7.3. Ergebnis: Anker-Klasse, HDR-Ziel, Ausgang der
Probe. Danach werden `kAfterOpaque` und `kSceneHDR` eingetragen und der Nebelpass gebaut.

**Lauf 2, Abnahme.**

| #   | Schritt                                                                                      | Geht es schief, heißt das                                                          |
| --- | -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| 1   | Overlay: Block `Exponential Height Fog` mit Schalter und sieben Reglern, Block `Frame Trace` | `Declare` lief nicht                                                               |
| 2   | Draußen, Blick in die Senke am Fluß: Schalter aus und an                                     | Kein Unterschied: der Anker feuert nicht, oder das Ziel ist falsch                 |
| 3   | `height` hoch- und runterschieben                                                            | Die Schicht wandert nicht mit: Höhe oder Strahl falsch                             |
| 4   | Blick zur Sonne, `sunInscattering` auf 5                                                     | Kein Glühen: Sonnenrichtung oder -farbe falsch                                     |
| 5   | `vanillaFog` auf 0, Blick in die Ferne                                                       | Distanztrübung bleibt: Ausgang 3 aus 7.3                                           |
| 6   | Performance-Tafel: `ExponentialHeightFog/Draw` und `/Fog`, dann **F11**                      | Fehlen sie, greift `PassScope` nicht; ohne F11 keine Zahl                          |
| 7   | Root Cellar                                                                                  | Läuft der Pass, greift die Himmelsprüfung nicht                                    |
| 8   | Pip-Boy, Alt-Tab und zurück                                                                  | Artefakte: der Wächter gibt nicht alles zurück                                     |
| 9   | Feature im laufenden Spiel abschalten, wieder an                                             | Absturz: Rennen beim Austragen; Spielnebel nicht zurück: `clamp` nicht restauriert |
| 10  | `Fog.hlsl` speichern, während das Spiel läuft — **diesmal ausgeführt**                       | Keine Reaktion binnen einer Sekunde: der Wächter sieht die Datei nicht             |

**Abgenommen ist F3, wenn beide Läufe stehen** und die Passzeile gegen die F1-Grundlinie
(6,598 ms GPU / 6,503 ms CPU je Frame in Sanctuary) in der Roadmap festgehalten ist, unter „Aus
Teilprojekt F3 bestätigt". Was ungeprüft bleibt, wird dort ausdrücklich benannt.

## 12. Was F3 für F4 aufwärts hinterläßt

-   **`FrameTrace`** — ein Frame der Engine als Liste, auf Tastendruck. Der Anker jedes weiteren
    Bildschirmraum-Features wird damit gemessen statt vermutet.
-   **Zwei Phasen in `FramePhase`** — vor der Beleuchtung und hinter der opaken Szene, mit einer
    Tabelle, die eine dritte trägt, sollte F6 eine brauchen.
-   **`Render::Targets::kSceneHDR`** — das fertige Bild als benannter Slot.
-   **Der Sichtstrahl je Pixel** aus `Render::Camera`, ohne Matrixinversion — die Grundlage jeder
    Rekonstruktion von Weltpositionen aus der Tiefe, die SSGI und Skylighting brauchen werden.
-   **Ein belegter Weg, einen Engine-Wert je Frame zu überstimmen und zurückzugeben**, oder der
    Befund, daß es nicht geht.
