# Teilprojekt C — Shader-Pipeline

Spec, Stand 2026-08-30. Teil der Portierung nach `docs/fallout4-port/ROADMAP.md`.
Setzt B1 (`2026-08-30-fallout4-render-anbindung-design.md`) und B2
(`2026-08-30-fallout4-target-inventar-design.md`) voraus.

## 1. Kontext und Ziel

Bis hierher beobachtet der Port nur: B1 hängt sich an `IDXGISwapChain::Present` und setzt einen
Marker, B2 beschriftet die Render-Targets. Nichts davon verändert ein einziges Pixel.

C ist der erste Abschnitt, der in das Rendern **eingreift**.

**Ziel:** Ein Untersystem, das eigenes HLSL zur Laufzeit übersetzt, damit einen laufenden
Post-Process-Pass von Fallout 4 ersetzt und die Übersetzung bei Dateiänderung im laufenden Spiel
wiederholt.

**Abnahmekriterium der Roadmap:** ein vorhandener FO4-Shader wird nachweislich durch einen eigenen
ersetzt.

Ein Punkt, der den Zuschnitt bestimmt: Fallout 4 liefert seine Shader als kompiliertes Bytecode in
`.fxp` aus. HLSL-Quellen der Engine haben wir nicht, und die 153 geerbten HLSL-Dateien unter
`package/Shaders/` sind _Skyrim_-Quellen. Einen Shader zu ersetzen heißt deshalb: für den
betroffenen Slot eigenes HLSL schreiben. Für einen Kopier-Pass ist das eine Handvoll Zeilen, für
den Lighting-Shader wäre es ein Forschungsprojekt für sich. C nimmt den Kopier-Pass.

## 2. Was die Vorerkundung ergeben hat

Alle folgenden Punkte sind im Quelltext von commonlibf4 nachgeschlagen, nicht aus dem
Skyrim-Gedächtnis abgeleitet.

| Sachverhalt                         | Befund                                                                                                                                                                                                |
| ----------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `BSShader`                          | vollständig definiert, `sizeof == 0x118`. Führt je Shader-Klasse ein `BSTSet` über Technik-IDs: `vertexShaders`, `hullShaders`, `domainShaders`, `pixelShaders`, `computeShaders`, dazu `fxpFilename` |
| `BSGraphics::PixelShader`           | `sizeof == 0x78`, Felder `id` (Technik-ID) und `REX::W32::ID3D11PixelShader* shader` bei `0x08`                                                                                                       |
| `BSShaderManager`                   | **kein** Singleton, **keine** Shader-Liste — nur Enums und ein `State`. Die 13 `BSShader`-Instanzen zu finden wäre eigene RE-Arbeit                                                                   |
| `ImageSpaceManager::GetSingleton()` | vorhanden, `ID::ImageSpaceManager::Singleton` ist `REL::VariantID{161743, 2712627}`. Führt `NiTPrimitiveArray<ImageSpaceEffect*> effectList` bei `0x010`                                              |
| `BSImagespaceShader`                | in `IDs_VTABLE.h` als `std::array<REL::ID, 3>` geführt — **drei** vtables. `BSShader` bringt zwei mit (`NiRefObject`, `BSReloadShaderI`), die dritte ist `ImageSpaceEffect`                           |
| Zahl der Klassen                    | 162 `BSImagespaceShader*`-Einträge in `IDs_RTTI.h`, darunter `…Copy`, `…GammaCorrect`, `…Blur3` bis `…Blur15`                                                                                         |
| `REX::W32::D3DCompile`              | vorhanden in `REX/W32/D3DCOMPILER.h`, samt `D3D_SHADER_MACRO`, `ID3DBlob`, `ID3DInclude` und den Flag-Enums. `commonlib-shared` linkt `d3dcompiler.lib` bereits `PUBLIC`                              |
| `NiTArray`, `BSTSet`                | beide mit `begin()` und `end()`, `BSTSet` zusätzlich mit `find()` und `size()`                                                                                                                        |

Daraus folgt der Kern der Architektur: **`BSImagespaceShader` erbt von `BSShader` und von
`ImageSpaceEffect` zugleich.** Die Einträge in `ImageSpaceManager::effectList` _sind_ die
Shader-Objekte. Wir kommen an einen konkreten `ID3D11PixelShader*` heran, ohne einen einzigen
Engine-Hook zu setzen.

## 3. Umfang

### In C enthalten

-   Übersetzen von HLSL zur Laufzeit über `REX::W32::D3DCompile`, mit `#include`-Auflösung.
-   Aufzählen der Post-Process-Pässe über `ImageSpaceManager::effectList`, mit Protokoll.
-   Ersetzen genau eines Pixel-Shaders einer Technik durch einen eigenen.
-   Neuübersetzung und erneuter Tausch bei Dateiänderung, ohne Spielneustart.
-   Ein Kopierschritt im Build, der unsere HLSL-Dateien neben der DLL ausliefert.
-   Ein Befunddokument über die Pässe, Gegenstück zu `render-targets.md`.

### Nicht in C enthalten

-   Descriptor- oder Defines-Schema, Varianten eines Shaders.
-   Thread-Pool und Platten-Cache für übersetzten Bytecode. Beides gehört zu D, siehe Abschnitt 4.
-   Vertex-, Hull-, Domain- und Compute-Shader. Der Mechanismus wird so geschnitten, dass sie
    später dazukommen können, aber C wickelt nur Pixel-Shader ab.
-   Die 13 `BSShader` der Engine, insbesondere `kLighting` und die Deferred-Kette.
-   Jedes Anfassen der 153 geerbten Skyrim-HLSL-Dateien.
-   Jeder Engine-Hook, jedes Trampolin, jede Änderung an commonlibf4.

## 4. Vorentscheidungen

| Frage                   | Entscheidung                                       | Begründung                                                                                                                                                                                          |
| ----------------------- | -------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Einschleusweg           | Zeiger-Tausch in `BSGraphics::PixelShader::shader` | Kein Hook, keine Kosten pro Draw, rückgängig machbar. Die Engine bindet unseren Shader danach selbst — mit ihren Konstantenpuffern, ihren Texturen und ihrem Input-Layout                           |
| Verworfen: Context-Hook | `PSSetShader` im Context-vtable abfangen           | Bräuchte die Traversierung trotzdem, kostet bei jedem der Tausenden Draws pro Frame, und der Immediate Context ist die Stelle, an der Overlays und Upscaler sich drängeln                           |
| Verworfen: Skyrim-Weg   | Hook auf die `.fxp`-Ladefunktion                   | Bräuchte einen Prolog-Detour, den `REL::THook` nicht beherrscht, also Microsoft Detours als neue Abhängigkeit, plus eine Adresse, die commonlibf4 nicht kennt                                       |
| Trampolin               | Bleibt **aus**                                     | C setzt keinen Hook. `InitInfo::trampoline` und `hook` bleiben auf `false`. Das korrigiert eine Annahme der Roadmap                                                                                 |
| commonlibf4             | Wird **nicht** geändert                            | Ein ordentlicher `BSImagespaceShader`-Header bräuchte die gemessene Größe, die wir nicht haben. Gebraucht wird er nicht: `ImageSpaceEffect` und `BSShader` sind definiert, die vtable-IDs stehen da |
| Platten-Cache           | Entfällt, verschoben nach D                        | C übersetzt einen kleinen Pixel-Shader, wenige Millisekunden beim Start. Ein Cache lohnt erst bei Hunderten Shadern und will dann nach dem dann bekannten Schlüsselschema gebaut werden             |
| Ziel-Pass               | `BSImagespaceShaderCopy`, mit Rückfallweg          | Der einzige Pass, dessen Original wir ohne Reverse Engineering nachbauen können: Textur lesen, Farbe schreiben. Ob er in einem Spielwelt-Frame läuft, sagt der Katalog                              |
| Shader-Verzeichnis      | `<Spiel>/Data/Shaders/FO4/`                        | Wie Skyrim CS. Der Build kopiert `package/Shaders/FO4/` dorthin, wie er schon die DLL kopiert. Funktioniert auch für Nutzer ohne Repo und passt zur Paketierung in D                                |
| Ein- und Ausschalter    | Die Datei selbst                                   | Ein Einstellungssystem gibt es erst mit D. Liegt keine Datei da, passiert nichts                                                                                                                    |

**Der Rückfallweg im Klartext.** Findet der Katalog `BSImagespaceShaderCopy` nicht in der
`effectList`, oder hat dieser Eintrag keine Technik mit Pixel-Shader, dann wird aus den gefundenen
Klassen die erste genommen, die alle drei Stufen des Sicherheitsnetzes besteht und genau **eine**
Technik-ID in `pixelShaders` führt. Mehr als eine Technik hieße, dass wir den Pass nicht eindeutig
treffen, und dann wird nicht getauscht, sondern protokolliert.

**Konkrete Namen.** Die Quelle heißt `package/Shaders/FO4/ImagespaceCopy.hlsl` im Repo und liegt
zur Laufzeit unter `<Spiel>/Data/Shaders/FO4/ImagespaceCopy.hlsl`. Eintrittspunkt ist `main`,
Zielprofil `ps_5_0`. Der Debug-Name des erzeugten Shader-Objekts lautet `FO4CS_PS_ImagespaceCopy`
und ist damit die Zeichenkette, nach der im Capture gesucht wird — passend zu den Präfixen
`FO4_RT_`, `FO4_DS_` und `FO4_CUBE_` aus B2.

**Auslieferung.** `FO4CS_DEPLOY_DIR` zeigt auf das `Data`-Verzeichnis des Spiels; die DLL geht
bereits nach `${FO4CS_DEPLOY_DIR}/F4SE/Plugins`. Der neue Kopierschritt legt `package/Shaders/FO4/`
nach `${FO4CS_DEPLOY_DIR}/Shaders/FO4/`. Die 153 geerbten Skyrim-Dateien eine Ebene darüber werden
**nicht** mitkopiert.

## 5. Architektur

Alles neu unter `src/Shader/`. Fünf Module, jedes mit einer Aufgabe und einer schmalen
Schnittstelle.

### 5.1 `ShaderSource`

Löst einen logischen Shader-Namen gegen `Data/Shaders/FO4/` auf, liest die Datei und löst
`#include "…"` **selbst und textuell** auf: die Zeile wird durch den Inhalt der eingebundenen Datei
ersetzt, eingerahmt von `#line`-Direktiven, damit Compilerfehler weiterhin auf Datei und Zeile der
_Quelle_ zeigen. Ergebnis ist ein einziger Übersetzungstext plus die Menge aller berührten Dateien
— diese Menge braucht der Watcher, damit eine Änderung an einer `.hlsli`-Datei ebenfalls eine
Neuübersetzung auslöst.

**Warum nicht `ID3DInclude`.** `REX::W32` deklariert `ID3DInclude` als `: public IUnknown`. Das
Windows SDK deklariert dieselbe Schnittstelle mit `DECLARE_INTERFACE(ID3DInclude)`, also **ohne**
Basis und mit genau zwei vtable-Einträgen (`d3dcommon.h:641` im SDK 10.0.26100.0). Wer die
REX-Fassung implementiert, schiebt drei `IUnknown`-Slots davor; `d3dcompiler` ruft dann
`QueryInterface`, wo es `Open` erwartet. Wir umgehen das, indem wir `nullptr` als Include-Handler
übergeben. Der Fehler in `REX::W32` wird an commonlib-shared zurückgemeldet.

Includes werden nur innerhalb von `Data/Shaders/` aufgelöst. Ein `..`, das aus dem Baum
herausführt, wird abgelehnt und protokolliert. Zyklen werden erkannt und ebenfalls abgelehnt.

### 5.2 `ShaderCompiler`

Dünne Hülle um `REX::W32::D3DCompile`. Eingabe: Quelltext, Name für Fehlermeldungen,
Eintrittspunkt, Zielprofil (`ps_5_0`), Defines. Ausgabe: entweder ein Bytecode-Blob oder der
Fehlertext des Compilers, unverändert.

Übersetzt wird mit `D3DCOMPILE_ENABLE_STRICTNESS` und `D3DCOMPILE_WARNINGS_ARE_ERRORS` — dieselbe
Haltung, die unser C++-Target mit `/W4 /WX` einnimmt. Optimierungsstufe 3.

Das Modul kennt weder Dateien noch die Engine und ist deshalb vollständig auf dem Host prüfbar.

### 5.3 `ImagespaceCatalog`

Der direkte Nachfahre von B2s Inventar: erst wissen, was da ist, dann eingreifen.

Läuft `ImageSpaceManager::GetSingleton()->effectList` ab und bestimmt für jeden Eintrag:

-   die Klasse, **aus der RTTI des Objekts selbst**,
-   den Offset des `ImageSpaceEffect`-Subobjekts innerhalb des Gesamtobjekts, ebenfalls aus der
    RTTI gelesen statt geschätzt,
-   daraus die Adresse des `BSShader`-Subobjekts,
-   dessen `shaderType`, `fxpFilename` und die Technik-IDs in `pixelShaders`.

**Warum RTTI statt vtable-IDs.** Der naheliegende Weg wäre, die vtable-Adresse gegen die
relozierten Werte aus `RE::VTABLE::BSImagespaceShader*` zu vergleichen. Das scheitert am
Fehlerverhalten der Adressbibliothek: `REL::ID::offset()` ruft bei einer unbekannten ID
`REX::FAIL` (`IDDB.cpp:442`), was den Prozess mit einem Dialog beendet und nicht abfangbar ist.
Bei 162 Klassen mit je bis zu drei IDs wäre eine einzige in der AE-Bibliothek fehlende ID ein
Absturz beim Spielstart.

MSVC legt vor jedem vtable-Eintrag einen `RTTICompleteObjectLocator` ab, den `REX::W32` bereits
deklariert. Aus ihm kommen beide gesuchten Angaben ohne jeden Bibliothekszugriff: das Feld
`typeDescriptor` führt zum dekorierten Klassennamen (`.?AVBSImagespaceShaderCopy@@`), und das Feld
`offset` **ist** der gesuchte Offset des Subobjekts — der Compiler hat ihn dort hinterlegt. Der
Katalog benennt damit auch Klassen, an die wir vorher nicht gedacht haben.

Ergebnis ist eine Tabelle im Log — Klassenname, Offset, Technik-IDs, Shader-Zeiger — und im
Speicher ein Verzeichnis, aus dem `ShaderOverride` seinen Ziel-Slot bezieht.

Der Katalog schreibt nichts. Er ist auch dann nützlich, wenn der Tausch scheitert.

### 5.4 `ShaderOverride`

Der eigentliche Eingriff, und das einzige Modul, das in fremden Speicher schreibt.

Erzeugt aus dem Bytecode über `Render::GetDevice()->CreatePixelShader` ein Shader-Objekt, hängt ihm
über `Render::SetDebugName` aus B2 einen Namen an, merkt sich den Original-Zeiger der Engine und
schreibt den eigenen in `BSGraphics::PixelShader::shader`.

Hält je Slot: den Original-Zeiger, unseren aktuellen Zeiger, und die Angabe, welcher gerade
installiert ist. Kann jederzeit zurücktauschen.

### 5.5 `ShaderWatcher`

Ein eigener Thread fragt alle 500 ms `std::filesystem::last_write_time` für **die Dateimenge ab,
die `ShaderSource` gemeldet hat** — die Hauptdatei und jedes eingebundene `.hlsli`. Ändert sich ein
Zeitstempel, gilt der Shader als veraltet.

**Warum kein `ReadDirectoryChangesW`.** `REX::W32::KERNEL32` deklariert weder diese Funktion noch
`FindFirstChangeNotification`. Sie zu benutzen hieße, `<Windows.h>` einzubinden — genau das, was
`REX::W32` vermeiden soll, und wofür B1 bereits die Regel aufgestellt hat, bei einem Typsystem zu
bleiben. Bei einer Handvoll beobachteter Dateien ist Abfragen ohnehin billiger als der Apparat
drumherum.

Die Abfrage entprellt von selbst: ein Editor, der eine Datei mehrfach schreibt, erzeugt zwischen
zwei Abfragen nur einen einzigen erkannten Zeitstempelwechsel. Eine Datei, die gerade nicht lesbar
ist, weil der Editor sie noch offen hält, wird beim nächsten Durchgang erneut versucht.

## 6. Ablauf und Zeitpunkte

| Zeitpunkt               | Was geschieht                                                                                   |
| ----------------------- | ----------------------------------------------------------------------------------------------- |
| `kGameDataReady`        | wie bisher: Present-Hook installieren, Marker, Target-Inventar. Neu: Watcher-Thread starten     |
| erstes `Present`        | Katalog aufnehmen, Ziel-Pass wählen, Quelle lesen, übersetzen, Shader erzeugen, Zeiger tauschen |
| jedes weitere `Present` | Warteschlange leeren (im Normalfall leer), Zeiger-Wächter prüfen                                |

Der Katalog läuft bewusst **nicht** bei `kGameDataReady`. Diese Nachricht trifft laut Teilprojekt A
auf einem fremden Thread ein, und ob `effectList` dort schon gefüllt ist, wissen wir nicht. Im
Present-Hook sind wir dagegen nachweislich auf dem Render-Thread und das Rendern läuft bereits —
beides ist genau das, was Traversierung und Zeiger-Tausch brauchen.

Ist `effectList` beim ersten Versuch leer, wird über die ersten 600 Frames erneut versucht und
danach mit einer Logzeile aufgegeben.

## 7. Nebenläufigkeit und Besitzverhältnisse

| Thread                    | Aufgabe                                                                                                      |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ |
| Watcher                   | Änderung erkennen, entprellen, Quelle lesen, `D3DCompile`, `CreatePixelShader`. Übergibt ein fertiges Objekt |
| Render-Thread (`Present`) | Warteschlange leeren, Zeiger tauschen, den abgelösten eigenen Shader freigeben, Wächter prüfen               |

Die Aufteilung geht auf, weil `ID3D11Device` per Vorgabe frei threadsicher ist. Nur der
Zeiger-Tausch gehört auf den Render-Thread; er ist ein einzelner ausgerichteter
8-Byte-Schreibvorgang. Auf dem heißen Pfad liegt kein Lock: ein Atomic-Flag sagt `Present`, ob
überhaupt etwas abzuholen ist, und nur dann wird die Mutex angefasst.

**Besitz.** Der Original-Zeiger der Engine wird gemerkt, nie referenziert und nie freigegeben —
dieselbe Regel, die B1 für Device, Context und SwapChain aufgestellt hat. Unser eigener Shader
gehört uns allein und wird freigegeben, wenn ein neuer ihn ablöst oder wir zurücktauschen.

## 8. Das Sicherheitsnetz vor dem ersten Schreibzugriff

Das ist die Stelle, an der eine falsche Annahme nicht abstürzt, sondern in fremden Speicher
schreibt. Der Aufbau wird deshalb **belegt, bevor irgendetwas geschrieben wird.** Drei Stufen, alle
drei müssen halten:

1.  **RTTI-Identität.** Der Locator bei `vtable[-1]` muss `signature == 1` führen, und die aus
    seinem Feld `self` zurückgerechnete Modulbasis muss der von
    `REX::FModule::GetExecutingModule().GetBaseAddress()` entsprechen. Damit ist belegt, dass die
    vtable zum Spielmodul gehört und der Locator echt ist. Der Klassenname muss mit `.?AV`
    beginnen und `BSImagespaceShader` enthalten.
2.  **Offset aus der RTTI.** `offset` des Locators nennt die Lage des Subobjekts. Der daraus
    berechnete `BSShader`-Anfang muss seinerseits einen Locator derselben Klasse tragen, dessen
    `offset` null ist. Damit ist die Rechnung von beiden Seiten belegt.
3.  **Plausibilität.** `shaderType` im gültigen Bereich, `fxpFilename` ein lesbarer String,
    `pixelShaders.size()` klein und ungleich null.

Fällt eine Stufe durch: protokollieren, das gesamte Untersystem abschalten, Spiel läuft unverändert
weiter. Derselbe Umgang wie mit `ValidateAndLog` in B1 und der Rückwärts-Zuordnung in B2 — lieber
nichts tun als etwas Falsches.

## 9. Fehlerbehandlung

| Fall                             | Verhalten                                                                                                                                                            |
| -------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Datei fehlt                      | Nichts geschieht, eine Logzeile. Das ist zugleich der Ausschalter                                                                                                    |
| Übersetzungsfehler               | Fehlertext des Compilers wortgetreu ins Log, samt Datei und Zeile. Installiert bleibt, was installiert ist; ein Tippfehler kann kein schwarzes Bild erzeugen         |
| `CreatePixelShader` schlägt fehl | Logzeile mit `HRESULT`, kein Tausch                                                                                                                                  |
| Sicherheitsnetz reißt            | Untersystem abschalten, laut protokollieren                                                                                                                          |
| Engine lädt Shader neu           | Der Wächter in `Present` bemerkt es: steht im Slot weder unser Zeiger noch der gemerkte Original-Zeiger, gilt das Original als erneuert — merken und erneut tauschen |
| Ziel-Pass läuft nicht            | Der Katalog entscheidet über den Rückfallweg, siehe Abschnitt 4                                                                                                      |

## 10. Tests und Abnahme

### 10.1 Ohne Spiel prüfbar

Nach dem Muster im Repo: je Modul ein Executable, handgeschriebenes `Check`, per `add_test`
registriert.

| Test                  | Prüft                                                                                                                                                                                                                          |
| --------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `ShaderCompilerTests` | Gültiges HLSL ergibt nicht-leeren Bytecode; ungültiges ergibt Fehlschlag mit nicht-leerem Fehlertext, der die Zeilennummer nennt; eine Warnung wird zum Fehler. Läuft auf dem Host, weil `d3dcompiler.lib` ohnehin gelinkt ist |
| `ShaderSourceTests`   | Pfadauflösung, `#include` inklusive Verschachtelung, gesammelte Dateimenge, fehlende Datei, ein `..` das aus dem Baum führt                                                                                                    |
| `ShaderWatcherTests`  | Temporäres Verzeichnis, Datei anfassen, Änderung wird beim nächsten Durchgang gemeldet; ein Durchgang ohne Änderung meldet nichts; eine gelöschte Datei führt nicht zu einer Ausnahme                                          |

Jeder Test wird nach dem Grünwerden absichtlich gebrochen, mit vorher benanntem erwartetem
Fehlschlag. Dabei ist darauf zu achten, dass die Mutation auch übersetzt — sonst prüft man das alte
Executable, wie in B1 einmal geschehen.

### 10.2 Nur im Spiel prüfbar

Katalog-Traversierung, Offset-Messung und Zeiger-Tausch brauchen die laufende Engine. Sie werden
über Log, Screenshot und Capture belegt.

### 10.3 Abnahmekriterien

1.  Das Log zeigt: Katalog mit Anzahl der Effekte, gewählter Pass mit Technik-ID, gemessener
    Offset, Übersetzung erfolgreich, Tausch mit altem und neuem Zeiger.
2.  Screenshot mit sichtbarem Farbstich. Datei entfernt, Spiel neu gestartet — Bild wieder normal.
3.  RenderDoc-Capture, in dem unser Shader-Objekt unter unserem Namen auftaucht, gefunden über
    Zeichenkettensuche in der `.rdc` wie in B1 und B2.
4.  Hot-Reload: Tönungsfaktor in der Datei ändern, Alt-Tab, Farbe ändert sich ohne Neustart.
5.  Robustheit: Syntaxfehler ins HLSL, Alt-Tab — Log zeigt den Compilerfehler, das Bild behält den
    letzten guten Shader, das Spiel läuft weiter.

## 11. Annahmen, die C bestätigen muss

Diese Punkte sind hergeleitet, aber nicht gemessen. Jeder von ihnen kann die Umsetzung ändern.

-   Dass `BSImagespaceShader` tatsächlich von `BSShader` **und** `ImageSpaceEffect` erbt. Die drei
    vtable-IDs sind ein starkes Indiz, kein Beweis. Stufe 2 des Sicherheitsnetzes ist genau die
    Messung dieser Annahme.
-   Dass `effectList` beim ersten `Present` gefüllt ist.
-   Dass `BSImagespaceShaderCopy` in einem Spielwelt-Frame vorkommt.
-   Dass ein Ersatz-Shader, der nur `t0` liest und schreibt, ohne Kenntnis der übrigen Bindungen
    ein brauchbares Bild ergibt.
-   Dass die Engine ihre Imagespace-Shader im laufenden Betrieb nicht ohnehin regelmäßig neu lädt.
    Trifft das doch zu, wird aus dem Wächter mehr als eine Randabsicherung.

## 11a. Befund für commonlib-shared

`REX::W32::ID3DInclude` erbt in `REX/W32/D3D.h` von `IUnknown`. Das Windows SDK deklariert die
Schnittstelle mit `DECLARE_INTERFACE(ID3DInclude)`, also ohne Basis und mit genau zwei
vtable-Einträgen. Die REX-Fassung ist damit unbrauchbar: eine daraus abgeleitete Implementierung
bekommt drei `IUnknown`-Slots vorangestellt, und `d3dcompiler` ruft `QueryInterface`, wo es `Open`
erwartet. Der dritte Befund, der sich zurückgeben lässt — nach den beiden aus B2.

## 12. Übergabe

**Befunddokument:** `docs/fallout4-port/imagespace-passes.md` — welche Effekte in einem
Spielwelt-Frame vorkommen, mit Klassennamen, Technik-IDs und dem gemessenen Offset. Das Gegenstück
zu `render-targets.md` und der bleibende Wert von C über den Mechanismus hinaus.

**Korrekturen an der Roadmap**, fällig beim Abschluss von C:

-   C schaltet das Trampolin **nicht** ein und braucht `REL::THook` nicht.
-   `BSRenderPass` wird für C **nicht** gebraucht. Die Lücke in commonlibf4 bleibt bestehen, sie
    wird nur später fällig als angenommen.

**Für D und F offen geblieben:** Platten-Cache und Defines-Schema, die übrigen Shader-Klassen, ein
gemessener `BSImagespaceShader`-Header samt Rückgabe an commonlibf4, und die 13 `BSShader` der
Engine, für die es weiterhin keinen Singleton gibt.
