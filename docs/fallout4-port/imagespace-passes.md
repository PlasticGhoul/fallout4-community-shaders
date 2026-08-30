# Image-Space-Pässe von Fallout 4

Befunddokument aus Teilprojekt C, erhoben am 2026-08-30 gegen Fallout 4 AE `1.11.240`.
Gegenstück zu `render-targets.md`. Alle Zahlen sind im laufenden Spiel gemessen, in Sanctuary.

## 1. Wie erhoben

`ImageSpaceManager::GetSingleton()->effectList` wird abgelaufen. Jeder Eintrag wird über die
**RTTI des Objekts selbst** bestimmt: MSVC legt vor jedem vtable-Eintrag einen
`RTTICompleteObjectLocator` ab, dessen `typeDescriptor` zum dekorierten Klassennamen führt und
dessen `offset` die Lage des Subobjekts nennt.

Das ist bewusst **kein** Vergleich gegen `RE::VTABLE`-IDs. `REL::ID::offset()` ruft bei einer der
Adressbibliothek unbekannten ID `REX::FAIL` und beendet den Prozess
(`lib/commonlib-shared/src/REL/IDDB.cpp:442`). Bei 162 Imagespace-Klassen mit je bis zu drei IDs
wäre eine einzige fehlende ID ein Absturz beim Spielstart. Der Compiler hat beide gesuchten
Angaben ohnehin ins Binary geschrieben.

## 2. Was in der Liste steht

| Kategorie                                                             | Anzahl  |
| --------------------------------------------------------------------- | ------- |
| Einträge in `effectList`                                              | 226     |
| davon `BSImagespaceShader*` mit Pixel-Techniken                       | **121** |
| `BSImagespaceShader*` ohne Pixel-Technik (Compute- oder Vertex-Pässe) | 39      |
| einfache `ImageSpaceEffect*` ohne `BSShader`-Hälfte                   | 65      |
| ohne lesbare RTTI                                                     | 1       |
| vom Sicherheitsnetz abgelehnt                                         | **0**   |

`effectList.size()` meldet 225, die Iteration findet 226 gefüllte Plätze. `NiTArray::size()` führt
`_size`, die Iteration läuft über `_capacity` und überspringt leere Plätze — bei diesem Array
gehen die beiden auseinander. Für uns zählt die Iteration.

## 3. Der wichtigste Befund: commonlibf4s `BSShader` beschreibt Fallout 4 nicht

Die Deklaration in `RE/B/BSShader.h` hat die **Reihenfolge** richtig — Vertex, Hull, Domain,
Pixel, Compute, dann der Dateiname — aber jeder Offset nach `shaderType` liegt **`0x78` zu
niedrig**, und die Klasse ist `0x190` groß statt der behaupteten `0x118`. Wer `pixelShaders`
darüber liest, bekommt Hälften von Zeigern, wo Technikzahlen stehen sollten.

| Offset          | Inhalt                                    | commonlibf4 sagt |
| --------------- | ----------------------------------------- | ---------------- |
| `0x000`         | vtable `NiRefObject`                      | `0x000`          |
| `0x010`         | vtable `BSReloadShaderI`                  | `0x010`          |
| `0x018`         | `shaderType`, liest `0xC` (`kImageSpace`) | `0x018`          |
| `0x020`–`0x097` | drei unbekannte Strukturen à `0x28`       | **fehlt**        |
| `0x098`         | `vertexShaders`                           | `0x020`          |
| `0x0C8`         | `hullShaders`                             | `0x050`          |
| `0x0F8`         | `domainShaders`                           | `0x080`          |
| `0x128`         | `pixelShaders`                            | `0x0B0`          |
| `0x158`         | `computeShaders`                          | `0x0E0`          |
| `0x188`         | `fxpFilename`                             | `0x110`          |
| `0x190`         | `sizeof`                                  | `0x118`          |

**Wie belegt.** Jedes der fünf Kartenoffsets ist durch seinen **eigenen Sentinel** bestätigt: die
Engine gibt jeder Template-Instanziierung eine eigene Vier-Byte-Konstante `DE AD BE EF`, und diese
liegen bei `0x0B0`, `0x0E0`, `0x110`, `0x140` und `0x170` — also exakt bei `Karte + 0x18`, wo
`BSTScatterTable::_sentinel` steht. Ein zweites Mal bestätigt durch die Einträge selbst: der
`next`-Zeiger jedes belegten Eintrags zeigt auf den Sentinel genau seiner eigenen Karte.

Was ich zwischenzeitlich für `fxpFilename` bei `0x110` hielt, war der Sentinel der dritten Karte.
Der echte Dateiname steht bei `0x188` und liest bei `BSImagespaceShaderCopy` die Zeichenkette
`ISCopy`.

Die Karte selbst beschreibt commonlibf4 dagegen **richtig**: `_capacity` bei `+0x0C`, `_free` bei
`+0x10`, `_sentinel` bei `+0x18`, die Einträge bei `+0x28`, je Eintrag `0x10` Bytes aus Wert und
Kettenzeiger. Nur wo die Karten sitzen, war falsch.

`BSGraphics::VertexShader` (`0x88`) stimmt ebenfalls Feld für Feld: gemessen wurden `id`, das
D3D11-Objekt bei `+0x08`, `byteCodeSize` `0x160`, drei `ConstantGroup` à `0x18` ab `+0x18`,
`shaderDesc` `0x00C0300000000FFF` bei `+0x60` und `constantTable[32]` ab `+0x68`.

**Offen:** was in den `0x78` Bytes zwischen `shaderType` und der ersten Karte steht. Drei
Strukturen à `0x28`, in jeder `0xFFFFFFFFFFFFFFFF`, `0xFFFFFFFF` und die Konstante `0x020007D0`.
Für Teilprojekt C nicht gebraucht, deshalb nicht weiter verfolgt.

## 4. Mehrfachvererbung

`BSImagespaceShader` erbt von `BSShader` **und** `ImageSpaceEffect`. Der `ImageSpaceEffect`-Anteil
liegt bei `+0x190`, bei allen 160 gefundenen Objekten identisch — und das ist zugleich
`sizeof(BSShader)`. `BSImagespaceShader` fügt vor seiner zweiten Basis also nichts hinzu.

Die Einträge in `effectList` **sind** damit die Shader-Objekte. Ein `ID3D11PixelShader*` ist ohne
einen einzigen Engine-Hook erreichbar.

## 5. Was ersetzt wurde

`BSImagespaceShaderCopy`, `fxp` `ISCopy`, Technik-ID `0`. Der Quervergleich hält: der Katalog
meldete `ps [0@0x20549579938]`, und der Tausch protokollierte
`installed 0x2054954cc38 in place of 0x20549579938`.

Der Farbstich erscheint **nicht in jedem Frame**. Das ist kein Fehler, sondern der eigentliche
Beweis: `ISCopy` ist ein Allzweck-Kopierpass, den die Engine nur einplant, wenn ein Target
umkopiert werden muss. Beim Blick in den Himmel läuft er verlässlich, beim Blick auf Terrain je
nach aktiver Effektkette. Ein konstanter Stich hätte bedeutet, dass wir bloß das Endbild einfärben.

## 5a. Was nicht geprüft wurde

Ein Abnahmekriterium der Spec ist **offen geblieben**: ein RenderDoc-Capture, in dem unser
Shader-Objekt unter `FO4CS_PS_ImagespaceCopy` erscheint. Bewusst ausgelassen, nicht vergessen.

Belegt ist die Wirkung bereits ohne Capture, und zwar zwingend: der Katalog meldete für
`BSImagespaceShaderCopy` den Zeiger `0x20549579938`, der Tausch protokollierte
`installed 0x2054954cc38 in place of 0x20549579938`, der Stich wurde sichtbar, eine Änderung an
der HLSL-Datei änderte die Farbe im laufenden Spiel, und ein Syntaxfehler ließ den letzten guten
Shader stehen. Damit steht fest, dass die Engine unseren übersetzten Shader bindet und ausführt.

Das Capture würde darüber hinaus nur zeigen, dass `SetDebugName` aus B2 auch an einem
`ID3D11PixelShader` haftet — Komfort für spätere Fehlersuche, nicht Teil des Mechanismus. Wer das
nächste Mal ohnehin ein Capture aufnimmt, kann es nebenbei mitprüfen: nach der Zeichenkette
`FO4CS_PS_ImagespaceCopy` in der `.rdc` suchen.

## 6. Die 121 Pässe mit Pixel-Technik

Alle führen genau **eine** Technik mit der ID `0`. Klassenname, dann der engine-eigene Name des
Shader-Pakets.

| Klasse                                               | `fxp`                                |
| ---------------------------------------------------- | ------------------------------------ |
| `BSImagespaceShaderGammaCorrect`                     | `ISGamma`                            |
| `BSImagespaceShaderGammaCorrectLUT`                  | `ISGammaLUT`                         |
| `BSImagespaceShaderGammaCorrectResize`               | `ISGamma`                            |
| `BSImagespaceShaderFXAA`                             | `ISFXAA`                             |
| `BSImagespaceShaderCopy`                             | `ISCopy`                             |
| `BSImagespaceShaderCopyScaleBias`                    | `ISCopyScaleBias`                    |
| `BSImagespaceShaderCopyVisAlpha`                     | `ISCopyVisAlpha`                     |
| `BSImagespaceShaderGreyScale`                        | `ISCopyGreyScale`                    |
| `BSImagespaceShaderDownsampleDepth`                  | `ISCopyDownsampleDepth`              |
| `BSImagespaceShaderCopyStencil`                      | `ISCopyStencil`                      |
| `BSImagespaceShaderCopyWaterMask`                    | `ISCopyWaterMask`                    |
| `BSImagespaceShaderCopyShadowMapToArray`             | `ISCopyShadowMapToArray`             |
| `BSImagespaceShaderCopyNormals`                      | `ISCopyNormals`                      |
| `BSImagespaceShaderRefraction`                       | `ISRefraction`                       |
| `BSImagespaceShaderDoubleVision`                     | `ISDoubleVision`                     |
| `BSImagespaceShaderTextureMask`                      | `ISCopyTextureMask`                  |
| `BSImagespaceShaderMap`                              | `ISMap`                              |
| `BSImagespaceShaderDepthOfField`                     | `ISDepthOfField`                     |
| `BSImagespaceShaderDepthOfFieldFogged`               | `ISDepthOfFieldFogged`               |
| `BSImagespaceShaderDepthOfFieldSplitScreen`          | `ISDepthOfFieldSplitScreen`          |
| `BSImagespaceShaderBokehDepthOfFieldPass1`           | `ISBokehBlur`                        |
| `BSImagespaceShaderBokehDepthOfFieldPass2`           | `ISBokehBlurHoriz`                   |
| `BSImagespaceShaderBokehDepthOfFieldPass3`           | `ISBokehBlurVert`                    |
| `BSImagespaceShaderBokehDepthOfFieldPass4`           | `ISBokehBlurComposite`               |
| `BSImagespaceShaderBokehDepthOfFieldPass4Fogged`     | `ISBokehBlurCompositeFogged`         |
| `BSImagespaceShaderDistantBlur`                      | `ISDistantBlur`                      |
| `BSImagespaceShaderDistantBlurFogged`                | `ISDistantBlurFogged`                |
| `BSImagespaceShaderRadialBlur`                       | `ISRadialBlur`                       |
| `BSImagespaceShaderRadialBlurMedium`                 | `ISRadialBlurMedium`                 |
| `BSImagespaceShaderRadialBlurHigh`                   | `ISRadialBlurHigh`                   |
| `BSImagespaceShaderHDRTonemapBlendCinematic`         | `ISHDRTonemapBlendCinematic`         |
| `BSImagespaceShaderHDRTonemapBlendCinematicFade`     | `ISHDRTonemapBlendCinematicFade`     |
| `BSImagespaceShaderHDRDownSample16`                  | `ISHDRDownSample16`                  |
| `BSImagespaceShaderHDRDownSample4`                   | `ISHDRDownSample4`                   |
| `BSImagespaceShaderHDRDownSample16Lum`               | `ISHDRDownSample16Lum`               |
| `BSImagespaceShaderHDRDownSample4RGB2Lum`            | `ISHDRDownSample4RGB2Lum`            |
| `BSImagespaceShaderHDRDownSample4LumClamp`           | `ISHDRDownSample4LumClamp`           |
| `BSImagespaceShaderHDRDownSample4LightAdapt`         | `ISHDRDownSample4LightAdapt`         |
| `BSImagespaceShaderHDRDownSample16LumClamp`          | `ISHDRDownSample16LumClamp`          |
| `BSImagespaceShaderHDRDownSample16LightAdapt`        | `ISHDRDownSample16LightAdapt`        |
| `BSImagespaceShaderBlur3`                            | `ISBlur3`                            |
| `BSImagespaceShaderBlur5`                            | `ISBlur5`                            |
| `BSImagespaceShaderBlur7`                            | `ISBlur7`                            |
| `BSImagespaceShaderBlur9`                            | `ISBlur9`                            |
| `BSImagespaceShaderBlur11`                           | `ISBlur11`                           |
| `BSImagespaceShaderBlur13`                           | `ISBlur13`                           |
| `BSImagespaceShaderBlur15`                           | `ISBlur15`                           |
| `BSImagespaceShaderNonHDRBlur3`                      | `ISNonHDRBlur3`                      |
| `BSImagespaceShaderNonHDRBlur5`                      | `ISNonHDRBlur5`                      |
| `BSImagespaceShaderNonHDRBlur7`                      | `ISNonHDRBlur7`                      |
| `BSImagespaceShaderNonHDRBlur9`                      | `ISNonHDRBlur9`                      |
| `BSImagespaceShaderNonHDRBlur11`                     | `ISNonHDRBlur11`                     |
| `BSImagespaceShaderNonHDRBlur13`                     | `ISNonHDRBlur13`                     |
| `BSImagespaceShaderNonHDRBlur15`                     | `ISNonHDRBlur15`                     |
| `BSImagespaceShaderBrightPassBlur3`                  | `ISBrightPassBlur3`                  |
| `BSImagespaceShaderBrightPassBlur5`                  | `ISBrightPassBlur5`                  |
| `BSImagespaceShaderBrightPassBlur7`                  | `ISBrightPassBlur7`                  |
| `BSImagespaceShaderBrightPassBlur9`                  | `ISBrightPassBlur9`                  |
| `BSImagespaceShaderBrightPassBlur11`                 | `ISBrightPassBlur11`                 |
| `BSImagespaceShaderBrightPassBlur13`                 | `ISBrightPassBlur13`                 |
| `BSImagespaceShaderBrightPassBlur15`                 | `ISBrightPassBlur15`                 |
| `BSImagespaceShaderWaterDisplacementClearSimulation` | `ISWaterDisplacementClearSimulation` |
| `BSImagespaceShaderWaterDisplacementTexOffset`       | `ISWaterDisplacementTexOffset`       |
| `BSImagespaceShaderWaterDisplacementWadingRipple`    | `ISWaterDisplacementWadingRipple`    |
| `BSImagespaceShaderWaterDisplacementRainRipple`      | `ISWaterDisplacementRainRipple`      |
| `BSImagespaceShaderWaterWadingHeightmap`             | `ISWaterWadingHeightmap`             |
| `BSImagespaceShaderWaterRainHeightmap`               | `ISWaterRainHeightmap`               |
| `BSImagespaceShaderWaterBlendHeightmaps`             | `ISWaterBlendHeightmaps`             |
| `BSImagespaceShaderWaterSmoothHeightmap`             | `ISWaterSmoothHeightmap`             |
| `BSImagespaceShaderWaterDisplacementNormals`         | `ISWaterDisplacementNormals`         |
| `BSImagespaceShaderNoiseScrollAndBlend`              | `ISNoiseScrollAndBlend`              |
| `BSImagespaceShaderNoiseNormalmap`                   | `ISNoiseNormalmap`                   |
| `BSImagespaceShaderLocalMap`                         | `ISLocalMap`                         |
| `BSImagespaceShaderLocalMapCompanion`                | `ISLocalMapCompanion`                |
| `BSImagespaceShaderAlphaBlend`                       | `ISAlphaBlend`                       |
| `BSImagespaceShaderPipboyScreen`                     | `ISPipboyScreen`                     |
| `BSImagespaceShaderHUDGlass`                         | `ISHUDGlass`                         |
| `BSImagespaceShaderHUDGlassDropShadow`               | `ISHUDGlassDS`                       |
| `BSImagespaceShaderHUDGlassBlurY`                    | `ISHUDGlassBY`                       |
| `BSImagespaceShaderHUDGlassBlurX`                    | `ISHUDGlassBX`                       |
| `BSImagespaceShaderHUDGlassMarkers`                  | `ISHUDGlassMarkers`                  |
| `BSImagespaceShaderVatsTargetDebug`                  | `ISVatsTargetDebug`                  |
| `BSImagespaceShaderVatsTarget`                       | `ISVatsTarget`                       |
| `BSImagespaceShaderModMenuEffect`                    | `ISModMenu`                          |
| `BSImagespaceShaderModMenuGlowComposite`             | `ISModMenuGlow`                      |
| `BSImagespaceShaderAmbientOcclusion`                 | `ISAO`                               |
| `BSImagespaceShaderAmbientOcclusionBlur`             | `ISAOGauss`                          |
| `BSImagespaceShaderVLSSpotLight`                     | `ISVLSSpotLight`                     |
| `BSImagespaceShaderVLSApplication`                   | `ISVLSApplication`                   |
| `BSImagespaceShaderVLSComposite`                     | `ISVLSComposite`                     |
| `BSImagespaceShaderVLSSliceCoord`                    | `ISVLSCoord`                         |
| `BSImagespaceShaderVLSSliceInterp`                   | `ISVLSSliceInterp`                   |
| `BSImagespaceShaderVLSSliceStencil`                  | `ISVLSSliceStencil`                  |
| `BSImagespaceShaderVLSSliceScatterRay`               | `ISVLSSliceScatterRay`               |
| `BSImagespaceShaderVLSSliceScatterInterp`            | `ISVLSSliceScatterInterp`            |
| `BSImagespaceShaderVLSScatterAccum`                  | `ISVLSScatterAccum`                  |
| `BSImagespaceShaderSAOCameraZ`                       | `ISSAOCameraZ`                       |
| `BSImagespaceShaderSAOMinify`                        | `ISSAOMinify`                        |
| `BSImagespaceShaderSAORawAO`                         | `ISSAORawAO`                         |
| `BSImagespaceShaderSAOBlurH`                         | `ISSAOBlurH`                         |
| `BSImagespaceShaderSAOBlurV`                         | `ISSAOBlurV`                         |
| `BSImagespaceShaderSAORawAOEditor`                   | `ISSAORawAOEditor`                   |
| `BSImagespaceShaderMotionBlur`                       | `ISMotionBlur`                       |
| `BSImagespaceShaderTemporalAA`                       | `ISTemporalAA`                       |
| `BSImagespaceShaderTemporalAAPipboy`                 | `ISTemporalAAPipboy`                 |
| `BSImagespaceShaderTemporalAAPowerArmorPipboy`       | `ISTemporalAAPowerArmorPipboy`       |
| `BSImagespaceShaderGammaLinearize`                   | `ISGammaLinearize`                   |
| `BSImagespaceShaderSunbeams`                         | `ISSunbeams`                         |
| `BSImagespaceShaderSSLRPrepass`                      | `ISSSLRPrepass`                      |
| `BSImagespaceShaderSSLRRaytracing`                   | `ISSSLRRaytracing`                   |
| `BSImagespaceShaderSSLRBlurH`                        | `ISSSLRBlurH`                        |
| `BSImagespaceShaderSSLRBlurV`                        | `ISSSLRBlurV`                        |
| `BSImagespaceShaderLensFlare`                        | `LensFlare`                          |
| `BSImagespaceShaderRainSplash`                       | `RainSplash`                         |
| `BSImagespaceShaderRainSplashUpdate`                 | `RainSplashUpdate`                   |
| `BSImagespaceShaderRainSplashDraw`                   | `RainSplashDraw`                     |
| `BSImagespaceShaderLensFlareVisibility`              | `LensFlareVis`                       |
| `BSImagespaceShaderUpsampleDynamicResolution`        | `ISUpsampleDynamicResolution`        |
| `BSImagespaceShaderFullScreenColor`                  | `ISFullScreenColor`                  |
| `BSImagespaceShaderHUDGlassClear`                    | `ISHUDGlassClear`                    |
| `BSImagespaceShaderHUDGlassCopy`                     | `ISHUDGlassCopy`                     |

Zwei Klassen teilen sich ein Paket: `BSImagespaceShaderGammaCorrect` und
`BSImagespaceShaderGammaCorrectResize` lesen beide `ISGamma`. Sonst ist die Zuordnung eindeutig.

## 7. Was daraus zurückgehen kann

Drei Befunde für commonlibf4 beziehungsweise commonlib-shared:

1.  `RE::BSShader` beschreibt nicht das Laufzeitobjekt — Offsets und Größe wie in Abschnitt 3.
2.  `REX::W32::ID3DInclude` erbt fälschlich von `IUnknown`. Das Windows SDK deklariert die
    Schnittstelle mit `DECLARE_INTERFACE`, also ohne Basis und mit genau zwei vtable-Einträgen
    (`d3dcommon.h:641`, SDK `10.0.26100.0`). Eine daraus abgeleitete Implementierung lässt
    `d3dcompiler` `QueryInterface` aufrufen, wo es `Open` meint.
3.  `BSImagespaceShader` und seine 161 Geschwister fehlen als Header, obwohl RTTI- und
    vtable-IDs vorhanden sind. Mit `sizeof(BSShader) == 0x190` und dem Subobjekt-Offset `0x190`
    ließen sie sich jetzt sauber deklarieren.
