# Render-Target-Inventar von Fallout 4

Erhoben am 2026-08-30 mit dem Inventar aus Teilprojekt B2.

|                      |                                                                      |
| -------------------- | -------------------------------------------------------------------- |
| Spielversion         | Fallout 4 AE `1.11.240.0`                                            |
| F4SE                 | `0.7.9`                                                              |
| Auflösung            | 2560×1440, `R8G8B8A8_UNORM`, 2 Puffer                                |
| Feature-Level        | `0xb000` (D3D 11.0)                                                  |
| Szene                | Spielwelt, `coc SanctuaryExt`                                        |
| Belegt               | 65 von 101 Render-Targets, 10 von 13 Tiefenpuffern, 1 von 2 Cubemaps |
| Benannte D3D-Objekte | 267, keine Ablehnung                                                 |

Die Auflösungen gelten für 2560×1440. Vieles skaliert mit der Ausgabeauflösung; die halben
Auflösungen (1280×720) und die Kette darunter sind davon abgeleitet, die festen Größen wie
4096×4096 oder 512×512 dagegen nicht.

## Zwei geklärte Fragen

### Die beiden Arrays sind über `renderTargetID` verbunden — rückwärts

`RendererData` führt 101 Render-Targets, der `RenderTargetManager` nur 100 Eigenschaftszeilen.
Beide direkt mit demselben Index anzusprechen ergibt Unsinn: von 65 belegten Targets stimmte das
Format dabei nur in 22 Fällen, und das war Zufall, weil viele Targets dasselbe Format haben.

Die Verbindung läuft umgekehrt. `renderTargetID[j]` nennt den **Renderer-Slot**, den die
Manager-Zeile `j` beschreibt. Sucht man die Zeile `j` mit `renderTargetID[j] == i`, stimmt das
Format in **65 von 65** Fällen. Der Wert `0xFFFFFFFF` markiert Manager-Zeilen ohne Renderer-Slot.

Die Spalte „Manager-Zeile" unten führt diese aufgelöste Zuordnung.

### `BSGraphics::Format` ist `DXGI_FORMAT`

commonlibf4 deklariert `BSGraphics::Format` ohne Enumeratoren, der Wert galt damit als
bedeutungslos. Über die richtige Zeile gelesen stimmt er in allen 65 Fällen numerisch mit dem
DXGI-Format überein, das die Textur selbst meldet. Das Enum der Engine ist schlicht das
DXGI-Enum.

Beide Erkenntnisse ließen sich an commonlibf4 zurückgeben.

## Vermutungen zur Bedeutung

**Ausdrücklich Vermutungen.** Sie stützen sich allein auf Format, Auflösung und Bind-Flags; keine
davon ist durch Beobachtung des Renderings bestätigt.

| Slots                        | Vermutung                        | Begründung                                                                                                                                                                                  |
| ---------------------------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RT_064` bis `RT_070`        | Belichtungsanpassung             | Eine Reduktionskette 640×360 → 160×90 → 40×23 → 10×6 → 3×2 → 1×1 → 1×1, durchgehend `R11G11B10_FLOAT` mit UAV. Das Lehrbuchmuster einer Herunterrechnung auf einen einzigen Helligkeitswert |
| `RT_039`                     | Hierarchische Tiefe (Hi-Z)       | Volle Auflösung, `R32_FLOAT`, **12 Mip-Stufen**, UAV. Ein Tiefenformat mit vollständiger Mip-Kette hat kaum eine andere Verwendung                                                          |
| `DS_007`, `DS_008`           | Schattenkarten                   | 4096×4096 `R16_TYPELESS`, unabhängig von der Bildschirmauflösung. Zwei davon deutet auf Kaskaden oder getrennte Licht-Typen                                                                 |
| `RT_001` bis `RT_006`        | HDR-Hauptziele                   | Volle Auflösung, `R11G11B10_FLOAT` und `R16G16B16A16_FLOAT` — Formate, die man für Farbe mit hohem Dynamikumfang wählt                                                                      |
| `RT_020`, `RT_021`           | Kodierte Normalen                | Volle Auflösung, `R16G16_UNORM`. Zwei Kanäle bei voller Präzision ist die übliche Kodierung für Normalenvektoren im G-Buffer                                                                |
| `RT_029`                     | Bewegungsvektoren                | Volle Auflösung, `R16G16_FLOAT`. Zwei vorzeichenbehaftete Kanäle in Bildschirmauflösung                                                                                                     |
| `RT_052`, `RT_053`, `DS_006` | Pip-Boy                          | 876×700 fällt aus jeder Auflösungsreihe heraus, und Farb- wie Tiefenpuffer teilen dieselbe ungewöhnliche Größe                                                                              |
| `RT_054` bis `RT_056`        | Reflexions- oder Umgebungskarten | 1024×1024 mit 11 Mip-Stufen, quadratisch, auflösungsunabhängig                                                                                                                              |
| `RT_051`                     | Nachschlagetabelle               | 16×1 — eine Zeile mit sechzehn Werten ist kein Bild                                                                                                                                         |

## Render-Targets

| Slot         | Auflösung | DXGI-Format           | Mips | Bind-Flags     | Manager-Zeile    |
| ------------ | --------- | --------------------- | ---- | -------------- | ---------------- | ----------------- | --- |
| `FO4_RT_001` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 14                |
| `FO4_RT_002` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 13                |
| `FO4_RT_003` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 2   |
| `FO4_RT_004` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 1   |
| `FO4_RT_005` | 2560x1440 | `R16G16B16A16_FLOAT`  | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 84                |
| `FO4_RT_006` | 2560x1440 | `R16G16B16A16_FLOAT`  | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 85                |
| `FO4_RT_007` | 1280x720  | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 5                 |
| `FO4_RT_008` | 1280x720  | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 6                 |
| `FO4_RT_009` | 1280x720  | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 7                 |
| `FO4_RT_010` | 1280x720  | `R16G16_FLOAT`        | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 4                 |
| `FO4_RT_011` | 1280x720  | `R32_FLOAT`           | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 8                 |
| `FO4_RT_012` | 640x384   | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 3                 |
| `FO4_RT_013` | 128x102   | `B8G8R8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 99                |
| `FO4_RT_014` | 640x360   | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 68  |
| `FO4_RT_015` | 640x360   | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 69  |
| `FO4_RT_016` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 15                |
| `FO4_RT_017` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 62                |
| `FO4_RT_018` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 63                |
| `FO4_RT_019` | 1024x1024 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 64                |
| `FO4_RT_020` | 2560x1440 | `R16G16_UNORM`        | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 27                |
| `FO4_RT_021` | 2560x1440 | `R16G16_UNORM`        | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 28                |
| `FO4_RT_022` | 2560x1440 | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 26                |
| `FO4_RT_023` | 2560x1440 | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 31                |
| `FO4_RT_024` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 30                |
| `FO4_RT_025` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 37  |
| `FO4_RT_026` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 58                |
| `FO4_RT_027` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 59                |
| `FO4_RT_028` | 1280x720  | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 39  |
| `FO4_RT_029` | 2560x1440 | `R16G16_FLOAT`        | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 32                |
| `FO4_RT_030` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 19                |
| `FO4_RT_031` | 1280x720  | `R8_UNORM`            | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 20                |
| `FO4_RT_032` | 1280x720  | `R16G16_UNORM`        | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 21                |
| `FO4_RT_033` | 1280x720  | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 23                |
| `FO4_RT_034` | 2560x1440 | `R8G8B8A8_UNORM_SRGB` | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 22                |
| `FO4_RT_035` | 512x512   | `R8_UNORM`            | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 86                |
| `FO4_RT_036` | 640x360   | `R10G10B10A2_UNORM`   | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 24  |
| `FO4_RT_037` | 640x360   | `R10G10B10A2_UNORM`   | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 25  |
| `FO4_RT_038` | 1280x720  | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 38  |
| `FO4_RT_039` | 2560x1440 | `R32_FLOAT`           | 12   | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 40  |
| `FO4_RT_045` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 52  |
| `FO4_RT_046` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 53  |
| `FO4_RT_047` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 54  |
| `FO4_RT_048` | 1280x720  | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 55  |
| `FO4_RT_049` | 1280x720  | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 56  |
| `FO4_RT_050` | 1280x720  | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 57  |
| `FO4_RT_051` | 16x1      | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 65                |
| `FO4_RT_052` | 876x700   | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 60                |
| `FO4_RT_053` | 876x700   | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 61                |
| `FO4_RT_054` | 1024x1024 | `R8G8B8A8_UNORM`      | 11   | `RENDER_TARGET | SHADER_RESOURCE` | 16                |
| `FO4_RT_055` | 1024x1024 | `R8G8B8A8_UNORM`      | 11   | `RENDER_TARGET | SHADER_RESOURCE` | 17                |
| `FO4_RT_056` | 1024x1024 | `R8G8B8A8_UNORM`      | 11   | `RENDER_TARGET | SHADER_RESOURCE` | 18                |
| `FO4_RT_057` | 2560x1440 | `R8G8B8A8_UNORM`      | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 29                |
| `FO4_RT_058` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 33                |
| `FO4_RT_059` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 34                |
| `FO4_RT_060` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 35  |
| `FO4_RT_061` | 2560x1440 | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 36  |
| `FO4_RT_062` | 512x512   | `R16G16B16A16_FLOAT`  | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 10                |
| `FO4_RT_063` | 512x512   | `R16G16B16A16_FLOAT`  | 1    | `RENDER_TARGET | SHADER_RESOURCE` | 11                |
| `FO4_RT_064` | 640x360   | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 70  |
| `FO4_RT_065` | 160x90    | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 71  |
| `FO4_RT_066` | 40x23     | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 72  |
| `FO4_RT_067` | 10x6      | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 73  |
| `FO4_RT_068` | 3x2       | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 74  |
| `FO4_RT_069` | 1x1       | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 75  |
| `FO4_RT_070` | 1x1       | `R11G11B10_FLOAT`     | 1    | `RENDER_TARGET | SHADER_RESOURCE  | UNORDERED_ACCESS` | 76  |

## Tiefenpuffer

Für Tiefenpuffer und Cubemaps wurde die Manager-Zuordnung nicht ausgewertet: `renderTargetID`
bezieht sich auf die Render-Target-Reihe. Ob `depthStencilTargetData` und
`cubeMapRenderTargetData` einer eigenen Zuordnung folgen, ist offen.

| Slot         | Auflösung | DXGI-Format      | Mips | Bind-Flags       |
| ------------ | --------- | ---------------- | ---- | ---------------- | -------------- |
| `FO4_DS_001` | 2560x1440 | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_002` | 2560x1440 | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_003` | 1280x720  | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_004` | 1280x720  | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_005` | 512x512   | `R16_TYPELESS`   | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_006` | 876x700   | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_007` | 4096x4096 | `R16_TYPELESS`   | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_008` | 4096x4096 | `R16_TYPELESS`   | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_009` | 512x512   | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |
| `FO4_DS_010` | 1x1       | `R24G8_TYPELESS` | 1    | `SHADER_RESOURCE | DEPTH_STENCIL` |

## Cubemaps

| Slot           | Auflösung | DXGI-Format       | Mips | Bind-Flags     |
| -------------- | --------- | ----------------- | ---- | -------------- | ---------------- |
| `FO4_CUBE_000` | 512x512   | `R11G11B10_FLOAT` | 1    | `RENDER_TARGET | SHADER_RESOURCE` |

## Offene Fragen

-   Der überzählige 101. Render-Target-Slot ist nicht geklärt. `RT_000` ist unbelegt, ebenso 40
    weitere; welcher Slot dem Backbuffer entspricht, geht aus den Daten nicht hervor.
-   Ob `depthStencilTargetData` und `cubeMapRenderTargetData` eine eigene Zuordnungstabelle haben.
-   Das Inventar läuft einmal bei `kGameDataReady`. Ob später weitere Targets entstehen — etwa
    beim Betreten eines Innenraums oder bei einem Auflösungswechsel — ist nicht untersucht.

## Wie dieses Dokument entstand

Die Tabelle stammt aus dem Log des Plugins, nicht aus einem Capture. Die Beschriftung wurde
daneben in einem RenderDoc-Capture der Spielwelt bestätigt: 59 unserer Namen sind dort sichtbar.
Geprüft wurde durch Suche in der `.rdc`-Datei, nicht über die Oberfläche.
