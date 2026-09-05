# Die Shader-Klassen von Fallout 4 und ihre Techniken

Befunddokument aus dem Messspike vor Teilprojekt F, erhoben am 2026-09-05 gegen Fallout 4 AE
`1.11.240`. Drittes Dokument neben `render-targets.md` (B2) und `imagespace-passes.md` (C).

Drei Spielläufe, alle in Sanctuary, einer davon zusätzlich im Root Cellar. Alle Zahlen sind im
laufenden Spiel gemessen.

Die Frage, die den Spike ausgelöst hat: Der Zeiger-Tausch aus Teilprojekt C ersetzt einen
Imagespace-Pass, indem er den einen Techniken-Eintrag der Klasse überschreibt. Trägt dieses
Vorgehen über Imagespace hinaus, also auch für die Shader, die die Welt zeichnen?

**Die Antwort ist nein.** Ein Imagespace-Pass hat eine Technik mit der ID 0. Die Weltshader haben
zusammen **3.445**, und sie sind Permutations-Bitmasken. Ein Shader-Cache mit Permutationen ist
damit für alles jenseits von Imagespace unumgänglich.

## 1. Wie erhoben

`Features::ShaderCensus` patcht Slot 02 der vtable jeder der 13 Klassen. Slot 02 ist
`SetupTechnique`; der Thunk hält das erste `this` fest, das durchkommt, und leitet weiter. Aus dem
festgehaltenen Objekt werden die fünf Technikkarten gelesen, ihre IDs über Slot 09
(`GetTechniqueName`) von der Engine selbst ausbuchstabiert, und danach wird der Patch
zurückgenommen.

Zwei Vorsichtsmaßnahmen, die beide angeschlagen haben:

-   **Die gefangene Klasse wird gegen ihre eigene RTTI geprüft**, bevor Slot 09 gerufen wird. Erst
    wenn das Objekt sich selbst so nennt, wie die vtable-ID versprochen hat, ist ein Aufruf ins Spiel
    vertretbar.
-   **Eine Technikkarte wird abgelehnt, wenn sie sich nicht selbst beweist**: Sentinel, Kettenzeiger
    und `capacity - free` müssen zusammenpassen. Eine Karte, die die Engine gerade auf einem anderen
    Thread vergrößert, sieht aus wie ein alter Kopf über einem halb gefüllten neuen Feld — wer ihr
    folgt, verliert den Prozess. Das ist zweimal passiert, bevor die Prüfung stand.

### 1.1 Vier Klassen kamen nie von selbst

Nach zwei Läufen hatten `kLighting`, `kDistantTree`, `kParticle` und `kImageSpace` nie gezeichnet.
Bei `kImageSpace` kann ein vtable-Patch grundsätzlich nicht greifen: die Klasse ist die Basis von
Unterklassen, die jede ihre eigene Tabelle tragen, siehe `imagespace-passes.md`. Bei den anderen
drei half längeres Warten nicht.

Der Ausweg war, die Engine nach ihren eigenen Shadern zu fragen. `Util::FindPointerInModuleData`
sucht einen bereits gefangenen Shader-Zeiger in den beschreibbaren Abschnitten von `Fallout4.exe`
und liest die Nachbarplätze; jeder Fund wird über seine eigene RTTI benannt. Es wird nichts
dereferenziert, was nicht vorher als lesbar erwiesen wurde, und nichts geschrieben.

## 2. Die Engine hält ihre Shader in einer Tabelle

Der Fund, der den Spike bezahlt hat. Im Datenbereich von `Fallout4.exe` stehen zwölf
Shader-Singletons unmittelbar nebeneinander, in fester Reihenfolge:

| Platz | Klasse                | Platz | Klasse                      |
| ----- | --------------------- | ----- | --------------------------- |
| `+0`  | `BSEffectShader`      | `+6`  | `BSDFCompositeShader`       |
| `+1`  | `BSUtilityShader`     | `+7`  | `BSSkyShader`               |
| `+2`  | `BSDistantTreeShader` | `+8`  | `BSLightingShader`          |
| `+3`  | `BSParticleShader`    | `+9`  | `BSBloodSplatterShader`     |
| `+4`  | `BSDFPrePassShader`   | `+10` | `BSWaterShader`             |
| `+5`  | `BSDFLightShader`     | `+11` | `BSFaceCustomizationShader` |

Ein einziger bekannter Shader-Zeiger erschließt damit **alle** übrigen — ohne vtable-Patch, ohne
Adressbibliothek und ohne darauf zu warten, dass eine Klasse zufällig etwas zeichnet. Vier Klassen
sind auf diesem Weg vermessen worden, die sonst gar nicht in der Messung aufgetaucht wären.

`BSImagespaceShader` steht **nicht** in dieser Tabelle. Das ist zu erwarten: es ist eine
Basisklasse, ihre konkreten Vertreter hängen in `ImageSpaceManager::effectList`.

Zwei Einschränkungen, die beim Weiterverwenden zählen:

-   Der Zensus protokolliert die **Laufzeitadresse** der Fundstelle, nicht ihren modulrelativen
    Offset. Wer die Tabelle direkt ansprechen will, statt sie über einen Anker zu finden, muss den
    Offset erst noch bestimmen — ASLR macht die protokollierte Zahl von Lauf zu Lauf wertlos.
-   Die Reihenfolge ist über drei Läufe stabil, aber nur auf `1.11.240` beobachtet.

## 3. Die Technikzahlen

Über drei Läufe und über Innen- wie Außenzelle **bitgleich**, und in keinem Lauf ist eine einzige
Karte nachträglich gewachsen. Die Karten werden also beim Laden aus der `.fxp` gefüllt und nicht
bei Bedarf; die Permutationsmenge steht vollständig im Voraus fest.

| Klasse                      | Enum                 | `shaderType` |    Vertex |     Pixel |
| --------------------------- | -------------------- | -----------: | --------: | --------: |
| `BSEffectShader`            | `kEffect`            |            0 |       474 |   **968** |
| `BSDFPrePassShader`         | `kDFPrepass`         |            4 |   **315** |   **470** |
| `BSDFLightShader`           | `kDFLight`           |         4 ⚠ |         1 |       306 |
| `BSUtilityShader`           | `kUtility`           |            1 |       259 |       177 |
| `BSDFCompositeShader`       | `kDFComposite`       |            6 |        23 |       180 |
| `BSWaterShader`             | `kWater`             |           10 |       102 |        94 |
| `BSLightingShader`          | `kLighting`          |            8 |        18 |        18 |
| `BSSkyShader`               | `kSky`               |            7 |         9 |         9 |
| `BSParticleShader`          | `kParticle`          |            3 |         6 |         6 |
| `BSDistantTreeShader`       | `kDistantTree`       |            2 |         2 |         2 |
| `BSBloodSplatterShader`     | `kBloodSpatter`      |            9 |         2 |         2 |
| `BSFaceCustomizationShader` | `kFaceCustomization` |           11 |         1 |         1 |
| **Summe**                   |                      |              | **1.212** | **2.233** |

Hull, Domain und Compute sind bei **allen** Klassen leer — mit Ausnahme von `kDFPrepass`, dessen
Hull- und Domain-Karten einen gefüllten Kopf tragen, aber kein gültiges Feld mehr; siehe
Abschnitt 7.

⚠ `BSDFLightShader` meldet `shaderType` **4**, nicht die aus der Reihenfolge erwartete 5 — obwohl
`fxpFilename` „DFLight" sagt und die RTTI die Klasse bestätigt. Es teilt sich den Wert damit mit
`BSDFPrePassShader`. `BSShaderManager::ShaderEnum` und das Feld im Objekt gehen hier auseinander;
wer über `shaderType` unterscheidet, unterscheidet diese beiden nicht.

**Technik-IDs sind Bitmasken, keine laufenden Nummern.** Aus `kDFComposite` abgelesen: `0x1` Tx,
`0x8` Base, `0x20` Envmap, `0x200` ApplyAO, `0x800` Sslr, `0x1000` Decal, `0x10000` Tilelight,
`0x20000` Wetness. Die Engine buchstabiert jede ID auf Wunsch selbst aus, was die Namen in dieser
Messung sind.

## 4. Fallout 4 zeichnet seine Welt über `kDFPrepass`, nicht über `kLighting`

Der Befund mit den weitesten Folgen, und er ist doppelt belegt.

`BSLightingShader` hat in **keinem** der drei Läufe gezeichnet, weder außen in Sanctuary noch
innen im Root Cellar. Es wurde am Ende nur noch über die Shader-Tabelle aus Abschnitt 2 gefunden,
nicht dadurch, dass es lief. Und seine Karte ist mit 18 Techniken winzig; die Namen sind reines
Skyrim-Vokabular: `BSLightingVcHair`, `BSLightingSkinTint`, `BSLightingFace`, `BSLightingVcTree`,
und bezeichnenderweise `BSLightingVcMenu`.

Das ist der übriggebliebene Vorwärtspfad. Die Welt zeichnet der Prepass: 315 Vertex- und 470
Pixel-Techniken, und er bindet den G-Buffer.

**Folge für Teilprojekt F.** In den Skyrim Community Shaders hängt die gesamte Gruppe der
Objekt-Shader-Features an `BSLightingShader` — TruePBR, Extended Materials, Extended Translucency,
Hair Specular, Skin, Inverse Square Lighting, Linear Lighting, Wetness Effects, Terrain Blending.
Keines davon lässt sich übersetzen. Sie müssen gegen `kDFPrepass` und den G-Buffer neu gedacht
werden. Das bestätigt die bereits getroffene Entscheidung, der Gruppe ein eigenes
Forschungs-Teilprojekt voranzustellen, und macht es zugleich teurer.

Ebenfalls bestätigt: Fallout 4 hat **keinen** eigenen Gras- und keinen eigenen Terrain-Shader.
Es gibt keine Klasse dafür, und in `kLighting` steht kein einziger Grasname.

## 5. Was jede Klasse bindet

Aufgezeichnet ist, was beim ersten Aufruf jeder Klasse an der Pipeline hing. Namen wie
`FO4_RT_022` sind unsere eigenen, per `SetPrivateData` vergebenen; die Engine benennt nichts
selbst, siehe `render-targets.md`.

| Klasse                | Ziele                                                    |
| --------------------- | -------------------------------------------------------- |
| `BSDFPrePassShader`   | `RT_022 RT_020 RT_057 RT_024 RT_023 RT_029` auf `DS_002` |
| `BSDFLightShader`     | `RT_058 RT_059` auf `DS_002`                             |
| `BSDFCompositeShader` | `RT_058 RT_059` auf `DS_002`                             |
| `BSUtilityShader`     | nur `DS_008`, die 4096²-Schattenkarte                    |
| `BSSkyShader`         | `RT_004` auf `DS_002`, dazu 14 SRVs (siehe unten)        |
| `BSEffectShader`      | ein unbenanntes Ziel, SRVs `RT_017` und `RT_036`         |
| `BSWaterShader`       | ein unbenanntes Ziel, 16 unbenannte SRVs                 |

### 5.1 Der G-Buffer

Die sechs Ziele des Prepass, mit den Formaten aus `render-targets.md`. Zwei Vermutungen aus B2
sind damit bestätigt:

| Slot   | Target       | Format                | Befund                    |
| ------ | ------------ | --------------------- | ------------------------- |
| `RTV0` | `FO4_RT_022` | `R8G8B8A8_UNORM_SRGB` |                           |
| `RTV1` | `FO4_RT_020` | `R16G16_UNORM`        | Normalen ✔ **bestätigt** |
| `RTV2` | `FO4_RT_057` | `R8G8B8A8_UNORM`      |                           |
| `RTV3` | `FO4_RT_024` | `R8G8B8A8_UNORM`      |                           |
| `RTV4` | `FO4_RT_023` | `R8G8B8A8_UNORM_SRGB` |                           |
| `RTV5` | `FO4_RT_029` | `R16G16_FLOAT`        | Bewegung ✔ **bestätigt** |
| `DSV`  | `FO4_DS_002` | Tiefe                 |                           |

Diese Bindung hat sich über zwei Läufe und über Außen- wie Innenzelle Slot für Slot wiederholt.

`kDFLight` und `kDFComposite` schreiben beide nach `FO4_RT_058` / `FO4_RT_059`
(`R11G11B10_FLOAT`) — das HDR-Ergebnis.

`kSky` liest beim Zeichnen den kompletten G-Buffer, beide Lichtziele, `FO4_DS_002`, dazu
`FO4_RT_028`, `FO4_RT_003`, `FO4_RT_009` und `FO4_RT_039` (Hi-Z). Es ist damit die Klasse mit der
größten Leseabhängigkeit und der beste Ort, um zu sehen, was die Pipeline zu diesem Zeitpunkt
bereitstellt.

## 6. Was ein Shader-Cache daraus zu tragen hat

-   **3.445 Techniken** über zwölf Klassen, mit `kEffect` (1.442) und `kDFPrepass` (785) als den
    beiden großen Brocken.
-   Die Menge steht beim Laden fest und wächst zur Laufzeit nicht. Ein Cache muss also nicht
    nachziehen, sondern kann vollständig aufgebaut werden.
-   IDs sind Bitmasken. Eine Ersetzung, die eine Permutation meint, muss über die Maske adressieren,
    nicht über einen Index.
-   Der Eintrag, den eine Ersetzung überschreibt, ist die `BSGraphics::<Stage>Shader` der Engine,
    nicht die D3D-Schnittstelle darin. `Shader::PixelShaderTechniques` liefert genau diese
    schreibbaren Plätze.

## 7. Zwei Karten, die nichts mehr enthalten

`BSDFPrePassShader` ist die einzige Klasse mit nicht-leeren Hull- und Domain-Karten. Beide melden
über alle drei Läufe hinweg bitgleich `capacity 128, free 59, good 63` mit gültigem Sentinel — und
beide werden abgelehnt, mit zwei verschiedenen Gründen.

Ein Rohabzug hat die Frage entschieden. Der Speichermanager schreibt vor jedes Feld einen Kopf mit
der Kennung `DADAFEAD` und der Blockgröße. Bei der Vertex-Karte derselben Klasse, die sauber
durchläuft, steht dort `0x2000` — genau `512 × 0x10`, ohne Rundung. **Blockgröße ist Kapazität mal
Eintragsgröße.** Bei Hull und Domain steht `0xC00`, also 3072 Byte; für 128 Einträge zu 16 Byte
wären 2048 nötig.

Die Kettenzeiger schließen die naheliegende Erklärung aus. Als Abstand zur Feldbasis gemessen
lauten sie `0xF0`, `0xD0`, `0x430`, `0xB0`, `0x350`, `0x80` — allesamt Vielfache von `0x10`, kein
einziger ein Vielfaches von `0x18` oder `0x30`. Größere Einträge, die die Blockgröße erklären
würden, würden keinen dieser Zeiger erklären.

Was bleibt, ist ein Feld, das diesen Karten nicht mehr gehört:

-   Wertzeiger `0x0` neben gesetzten Kettenzeigern, unregelmäßig verteilt. In der Vertex-Karte kommt
    das kein einziges Mal vor.
-   Die Domain-Karte hat ihre ersten 32 Plätze vollständig auf null, während ihr Kopf 69 belegte
    behauptet.
-   Zeiger, die ohne Struktur an 16-Byte-Grenzen in denselben Poolbereich zeigen — das Muster einer
    Freiliste.
-   Bitgleich über drei Läufe, weil der Startvorgang deterministisch ist.

Die Lesart, die alles davon trägt: Die Engine legt die Tessellierungskarten des Prepass an, gibt
den Speicher wieder frei und lässt den Kopf mit seinen Zahlen stehen.

**Unsere Offsets sind damit nicht widerlegt, sondern bestätigt.** Am selben Objekt, über dieselben
Offsets gelesen, läuft die Vertex-Karte bei `+0x098` sauber durch, und die drei Sentinels liegen
bei `…bb8`, `…bb4`, `…bb0` — vier Byte auseinander, absteigend, einer je Instanziierung. Die
Ablehnung ist die richtige Antwort, nur aus einem anderen Grund als zunächst vermutet: nicht „wir
lesen falsch", sondern „dort steht nichts mehr".

## 8. Was ungeprüft geblieben ist

-   **Der modulrelative Offset der Shader-Tabelle** ist nicht bestimmt worden, nur ihre Existenz und
    ihre Reihenfolge. Solange sie über einen Anker gefunden wird, braucht es ihn nicht.
-   **Die Freilisten-Lesart aus Abschnitt 7 ist eine Schlussfolgerung, kein Beweis.** Ein
    fortlaufender Hex-Abzug über mehrere hundert Byte würde sie erhärten. Er ist bewusst unterblieben:
    kein geplantes Feature hängt an Tessellierungstechniken des Prepass, und der Leser verhält sich
    bereits richtig.
-   **Die Zahlen stammen aus Sanctuary und dem Root Cellar.** Dass sie an beiden Orten und über drei
    Läufe gleich sind, spricht dafür, dass sie ortsunabhängig sind — bewiesen ist es für diese beiden
    Orte.
-   **`kImageSpace` ist hier nicht vermessen**, sondern in `imagespace-passes.md`. Der Zensus meldet
    es dauerhaft als fehlend; das ist erwartet und kein Mangel.
