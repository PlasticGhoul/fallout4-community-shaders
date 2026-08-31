# Teilprojekt D2 — Paketierung, Implementierungsplan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drei installierbare Archive — Basis, Addon, All-in-One — die aus einem Bau entstehen und
deren Inhalt geprüft ist.

**Architecture:** Eine Regel, wie aus dem Repo ein Mod-Baum wird, liegt in `tools/package.ps1`.
CMake reicht ihr nur Pfade und die Version hinein und ruft sie an zwei Stellen: der Deploy-Schritt
mit `-Stage`, das neue Ziel `package` ohne. Damit ist die Spielinstallation Zeichen für Zeichen
das, was ausgeliefert wird. `tools/verify-package.ps1` prüft Baum und Archive.

**Tech Stack:** PowerShell 7 (`pwsh`), CMake 4.2+, `cmake -E tar` für ZIP,
`System.IO.Compression.ZipFile` zum Prüfen. Keine neue Abhängigkeit.

**Spec:** `docs/superpowers/specs/2026-08-31-fallout4-paketierung-design.md`

## Global Constraints

-   **Nur `.zip`**, erzeugt über `cmake -E tar cf <ziel> --format=zip -- .`. Kein 7-Zip.
-   **Archivwurzel ist `Data`.** Kein Archiv enthält einen Ordner dieses Namens.
-   **`package/` wird nie als Ganzes kopiert.** Nur `package/Shaders/FO4/**` und
    `package/Features/<Name>/**`. `package/Interface` und `package/SKSE` sind Skyrim-Erbe und
    bleiben draußen.
-   **`features/` wird nicht angefasst.** Die 40 geerbten Verzeichnisse sind Rohmaterial für F;
    kein Archiv enthält je etwas daraus.
-   **Archivname:** `<Name>-<Version>-g<SHA>[-dirty].zip`, Version aus `PROJECT_VERSION`.
-   **Der normale Bau schreibt keine Archive.** Ziel `package` hängt nicht in `ALL`.
-   **Konventionen:** Kommentare begründen statt zu beschreiben. Conventional Commits, Titel
    maximal 50 Zeichen, Rumpf bei 72 umgebrochen. Code und Commits auf Englisch, `docs/` auf
    Deutsch. Branch `port/d2-paketierung`, kein Push ohne Ansage.
-   **Bauen und Prüfen:**

    ```pwsh
    $env:VCPKG_ROOT = "C:\vcpkg"
    $cmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    & $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
    & $cmake --build --preset FO4
    ctest --test-dir build/FO4 -C Release --output-on-failure
    ```

-   **Testdisziplin:** Jede Prüfung wird nach dem Grünwerden absichtlich gebrochen, und der
    erwartete Fehlschlag wird **vorher benannt**.

---

## File Structure

| Datei                                                             | Verantwortung                                         |
| ----------------------------------------------------------------- | ----------------------------------------------------- |
| `tools/package.ps1`                                               | die eine Regel: Baum bauen, Archive packen            |
| `tools/verify-package.ps1`                                        | prüft Baum und Archive, `ok`/`FAIL` je Zeile          |
| `package/README.md`                                               | Liesmich für den Spieler, wandert in die Archivwurzel |
| `package/Features/FrameCounter/CORE`                              | Markerdatei: dieses Feature gehört in die Basis       |
| `package/Features/ImagespaceTint/Shaders/FO4/ImagespaceCopy.hlsl` | aus `package/Shaders/FO4/` umgezogen                  |
| `CMakeLists.txt`                                                  | Deploy-Schritt umgestellt, Ziel `package` neu         |

Entfällt: nichts. `package/Shaders/FO4/` bleibt als Ort für paketweite Shader bestehen.

---

## Task 1: Der Staging-Baum

Layout, Skript und Deploy-Schritt gehören in **eine** Aufgabe: sobald `ImagespaceCopy.hlsl` umzieht,
findet der bestehende Deploy-Schritt nichts mehr, und das Spiel liefe ohne Shader. Die drei Teile
sind nicht einzeln lauffähig.

**Files:**

-   Create: `tools/package.ps1`, `tools/verify-package.ps1`, `package/README.md`,
    `package/Features/FrameCounter/CORE`
-   Move: `package/Shaders/FO4/ImagespaceCopy.hlsl` →
    `package/Features/ImagespaceTint/Shaders/FO4/ImagespaceCopy.hlsl`
-   Modify: `CMakeLists.txt:128-154` (der `FO4CS_DEPLOY_DIR`-Block)

**Interfaces:**

-   Consumes: nichts.
-   Produces: `tools/package.ps1` mit den Parametern `-SourceRoot`, `-PluginFile`, `-Version`,
    `-PdbFile`, `-CMake`, `-Stage`, `-WorkDir`, `-OutDir`. Aufgabe 2 ruft dasselbe Skript ohne
    `-Stage`.

-   [ ] **Step 1: Layout umstellen**

```bash
mkdir -p "package/Features/ImagespaceTint/Shaders/FO4"
mkdir -p "package/Features/FrameCounter"
git mv package/Shaders/FO4/ImagespaceCopy.hlsl \
       package/Features/ImagespaceTint/Shaders/FO4/ImagespaceCopy.hlsl
```

`package/Features/FrameCounter/CORE` anlegen, mit genau diesem Inhalt:

```
This feature ships inside the base archive rather than as its own addon.
The file's presence is the marker; its contents are never read.
```

`package/Shaders/FO4/` ist danach leer. Git verfolgt keine leeren Verzeichnisse — das ist in
Ordnung, das Skript prüft mit `Test-Path` und überspringt es.

-   [ ] **Step 2: `package/README.md` schreiben**

```markdown
# Community Shaders for Fallout 4

A port of Skyrim Community Shaders to Fallout 4, as an F4SE plugin.

## Requirements

-   Fallout 4 **1.11.240** exactly. The plugin refuses to load on any other version and says so in
    its log rather than risking a crash against relocated addresses.
-   F4SE 0.7.9 or newer, started through `f4se_loader.exe`. Launching the game through Steam loads
    no F4SE plugin at all.

## Installing

Install the archive with a mod manager, or unpack it into the game's `Data` folder. The archive is
already `Data`-relative: `F4SE/` and `Shaders/` belong directly under `Data`.

The all-in-one archive contains everything. The base archive is the plugin alone; each addon
archive carries one feature's shaders and needs the base.

## Settings

Settings live next to the log, in
`Documents/My Games/Fallout4/F4SE/CommunityShadersFO4.json`, with one block per feature. The file
is written with its defaults on first run. Edit it while the game runs and the change takes effect
within a second — there is no menu yet.

## Log

`Documents/My Games/Fallout4/F4SE/CommunityShadersFO4.log`, with the previous five runs kept
alongside it.

## Licence

GPL-3.0-or-later, with the modding exception in `EXCEPTIONS.md`. See `COPYING`.
```

-   [ ] **Step 3: Den fehlschlagenden Prüfer schreiben**

`tools/verify-package.ps1`. In dieser Aufgabe prüft er nur den Baum; Aufgabe 3 ergänzt die Archive.

```powershell
<#
.SYNOPSIS
    Verify the staged mod tree and the distributable archives.

.DESCRIPTION
    Stages the tree into a temporary directory and checks what landed there,
    then opens each archive in dist/ and checks its entries. The staging half
    needs no archives, so it is what catches a broken rule first.

.EXAMPLE
    pwsh tools/verify-package.ps1
#>
param(
    [string]$SourceRoot = ".",
    [string]$PluginFile = "build/FO4/Release/CommunityShadersFO4.dll",
    [string]$DistDir = "dist",
    [string]$CMake
)

$ErrorActionPreference = "Stop"
$script:failures = 0

function Check([bool]$Passed, [string]$What) {
    if ($Passed) {
        Write-Host "ok    $What"
    } else {
        Write-Host "FAIL  $What"
        $script:failures++
    }
}

$root = (Resolve-Path $SourceRoot).Path

if (-not (Test-Path $PluginFile)) {
    Write-Host "FAIL  plugin not built: $PluginFile"
    exit 1
}

# ------------------------------------------------------------------ the tree
$stage = Join-Path ([System.IO.Path]::GetTempPath()) "fo4cs-verify-stage"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue

& pwsh -NoProfile -File (Join-Path $root "tools/package.ps1") `
    -SourceRoot $root -PluginFile (Resolve-Path $PluginFile).Path `
    -Version "0.0.0" -Stage $stage | Out-Null

$staged = @(Get-ChildItem $stage -Recurse -File |
    ForEach-Object { $_.FullName.Substring($stage.Length).TrimStart('\', '/').Replace('\', '/') })

Check ($staged -contains "F4SE/Plugins/CommunityShadersFO4.dll") "the tree carries the plugin"
Check ($staged -contains "COPYING") "and the licence"
Check ($staged -contains "EXCEPTIONS.md") "and the modding exception"
Check ($staged -contains "README.md") "and the readme"
Check ($staged -contains "Shaders/FO4/ImagespaceCopy.hlsl") "and the feature's shader"
Check (-not ($staged | Where-Object { $_ -match '(^|/)CORE$' })) "and no CORE marker"
Check (-not ($staged | Where-Object { $_ -like "SKSE/*" })) "no SKSE leftovers"
Check (-not ($staged | Where-Object { $_ -like "Interface/*" })) "no Interface leftovers"
Check (-not ($staged | Where-Object { $_ -match '^Shaders/[^/]+\.hlsl$' })) "no Skyrim shaders"
Check (-not ($staged | Where-Object { $_ -like "Data/*" })) "the root is Data, not a folder named Data"

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "$script:failures check(s) failed"
    exit 1
}

Write-Host "all checks passed"
exit 0
```

-   [ ] **Step 4: Laufen lassen und den Fehlschlag sehen**

```pwsh
pwsh tools/verify-package.ps1
```

Erwartet: **Abbruch**, `tools/package.ps1` existiert nicht.

-   [ ] **Step 5: `tools/package.ps1` schreiben**

```powershell
<#
.SYNOPSIS
    Assemble the Fallout 4 mod tree, and pack it into distributable archives.

.DESCRIPTION
    One rule, two callers. With -Stage the tree is assembled at the given path
    and nothing is packed - that is what a game install wants, and what the
    build's deploy step uses. Without -Stage three archives are written: the
    base, one per feature that carries no CORE marker, and an all-in-one.

    package/ is never copied wholesale. Only package/Shaders/FO4 and
    package/Features/<Name> travel; package/Interface and package/SKSE are
    inherited Skyrim content and must not ship.

.EXAMPLE
    pwsh tools/package.ps1 -SourceRoot . -PluginFile build/FO4/Release/CommunityShadersFO4.dll `
        -Version 0.1.0 -CMake cmake
#>
param(
    [Parameter(Mandatory)] [string]$SourceRoot,
    [Parameter(Mandatory)] [string]$PluginFile,
    [Parameter(Mandatory)] [string]$Version,
    [string]$PdbFile,
    [string]$CMake,
    [string]$Stage,
    [string]$WorkDir,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"

$root = (Resolve-Path $SourceRoot).Path

function Copy-Tree([string]$From, [string]$To) {
    New-Item -ItemType Directory -Force -Path $To | Out-Null
    if (Get-ChildItem $From -Force -ErrorAction SilentlyContinue) {
        Copy-Item (Join-Path $From "*") $To -Recurse -Force
    }
}

function Get-Features {
    $dir = Join-Path $root "package/Features"
    if (-not (Test-Path $dir)) {
        return @()
    }
    Get-ChildItem $dir -Directory | ForEach-Object {
        [pscustomobject]@{
            Name   = $_.Name
            Path   = $_.FullName
            IsCore = Test-Path (Join-Path $_.FullName "CORE")
        }
    }
}

# The marker is a decision, not content: it says where the feature ships, and
# it has no business inside an archive.
function Copy-FeatureTree([string]$From, [string]$To) {
    $from = (Resolve-Path $From).Path
    New-Item -ItemType Directory -Force -Path $To | Out-Null

    foreach ($file in Get-ChildItem $from -Recurse -File) {
        $relative = $file.FullName.Substring($from.Length).TrimStart('\', '/')
        if ($relative -eq "CORE") {
            continue
        }
        $target = Join-Path $To $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item $file.FullName $target -Force
    }
}

function New-BaseTree([string]$To) {
    $plugins = Join-Path $To "F4SE/Plugins"
    New-Item -ItemType Directory -Force -Path $plugins | Out-Null
    Copy-Item $PluginFile $plugins -Force

    if ($PdbFile -and (Test-Path $PdbFile)) {
        Copy-Item $PdbFile $plugins -Force
    } else {
        # Not fatal: a generator that produces no pdb must still be able to
        # package. The archive is simply harder to read a crash log against.
        Write-Host "warn  no pdb found, the archive will not carry one"
    }

    Copy-Item (Join-Path $root "COPYING") $To -Force
    Copy-Item (Join-Path $root "EXCEPTIONS.md") $To -Force
    Copy-Item (Join-Path $root "package/README.md") (Join-Path $To "README.md") -Force

    $shaders = Join-Path $root "package/Shaders/FO4"
    if (Test-Path $shaders) {
        Copy-Tree $shaders (Join-Path $To "Shaders/FO4")
    }

    foreach ($feature in Get-Features | Where-Object { $_.IsCore }) {
        Copy-FeatureTree $feature.Path $To
    }
}

if (-not (Test-Path $PluginFile)) {
    throw "plugin not built: $PluginFile"
}

if ($Stage) {
    # The staged tree is the all-in-one tree: an install wants everything, and
    # what gets played should be what gets shipped.
    New-BaseTree $Stage
    foreach ($feature in Get-Features | Where-Object { -not $_.IsCore }) {
        Copy-FeatureTree $feature.Path $Stage
    }
    Write-Host "staged to $Stage"
    exit 0
}

throw "packing is not implemented yet"
```

-   [ ] **Step 6: Prüfer grün sehen**

```pwsh
pwsh tools/verify-package.ps1
```

Erwartet: zehn Zeilen `ok`, `all checks passed`.

-   [ ] **Step 7: Prüfung absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `and no CORE marker` schlägt fehl, die übrigen neun
bleiben grün.

Mutation in `Copy-FeatureTree`: die drei Zeilen entfernen, die `CORE` überspringen.

```powershell
        if ($relative -eq "CORE") {
            continue
        }
```

Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 8: Deploy-Schritt umstellen**

In `CMakeLists.txt` den Block unter `if(FO4CS_DEPLOY_DIR)` durch diesen ersetzen. Beide bisherigen
`add_custom_command`-Blöcke entfallen — auch der, der `package/Shaders/FO4` kopiert.

```cmake
if(FO4CS_DEPLOY_DIR)
    # The same script the package target uses, so the installation that gets
    # played is the tree that gets shipped. Two copy commands here would be a
    # second rule, and rules in two places drift.
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND
            pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/package.ps1" -SourceRoot
            "${CMAKE_SOURCE_DIR}" -PluginFile "$<TARGET_FILE:${PROJECT_NAME}>" -PdbFile
            "$<TARGET_PDB_FILE:${PROJECT_NAME}>" -Version "${PROJECT_VERSION}" -Stage
            "${FO4CS_DEPLOY_DIR}"
        COMMENT "Staging into ${FO4CS_DEPLOY_DIR}"
        VERBATIM
    )
endif()
```

-   [ ] **Step 9: Bauen und die Bauzeit messen**

Die Spec verlangt, den `pwsh`-Start zu **messen** statt zu schätzen. Zweimal ohne Änderung bauen,
damit nur der POST_BUILD-Schritt zählt:

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4
Measure-Command { & $cmake --build --preset FO4 } | Select-Object TotalSeconds
```

Erwartet: die Spielinstallation enthält `Data/Shaders/FO4/ImagespaceCopy.hlsl` und die frische DLL.
Die gemessene Zeit gehört ins Übergabedokument. Liegt der Aufschlag über einer Sekunde, wird der
Deploy-Schritt auf die alten `copy`-Aufrufe zurückgedreht und die doppelte Regel in Kauf genommen —
so steht es in der Spec.

```pwsh
Get-ChildItem "F:/SteamLibrary/steamapps/common/Fallout 4/Data/Shaders/FO4"
```

-   [ ] **Step 10: Alle Host-Tests laufen lassen**

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
```

Erwartet: acht Tests grün, Plugin-Prüfung ohne Beanstandung.

-   [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "feat: stage the mod tree from one rule"
```

---

## Task 2: Die Archive

**Files:**

-   Modify: `tools/package.ps1`, `tools/verify-package.ps1`, `CMakeLists.txt`

**Interfaces:**

-   Consumes: `Copy-Tree`, `Copy-FeatureTree`, `New-BaseTree`, `Get-Features` aus Aufgabe 1.
-   Produces: das CMake-Ziel `package`; drei Archive unter `dist/`.

-   [ ] **Step 1: Die fehlschlagenden Archivprüfungen ergänzen**

In `tools/verify-package.ps1`, **vor** dem abschließenden `Write-Host ""`:

```powershell
# --------------------------------------------------------------- the archives
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Entries([string]$Archive) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        # cmake -E tar writes its entries relative to the working directory, so
        # they arrive with a ./ prefix that means nothing to a mod manager.
        return @($zip.Entries | ForEach-Object { $_.FullName -replace '^\./', '' } |
            Where-Object { $_ -ne "" -and -not $_.EndsWith("/") })
    } finally {
        $zip.Dispose()
    }
}

$dist = Join-Path $root $DistDir
$base = @(Get-ChildItem $dist -Filter "CommunityShadersFO4-[0-9]*.zip" -ErrorAction SilentlyContinue)
$aio = @(Get-ChildItem $dist -Filter "CommunityShadersFO4-AIO-*.zip" -ErrorAction SilentlyContinue)
$addon = @(Get-ChildItem $dist -Filter "ImagespaceTint-*.zip" -ErrorAction SilentlyContinue)

Check ($base.Count -eq 1) "exactly one base archive"
Check ($addon.Count -eq 1) "exactly one addon archive"
Check ($aio.Count -eq 1) "exactly one all-in-one archive"

if ($base.Count -eq 1 -and $addon.Count -eq 1 -and $aio.Count -eq 1) {
    $baseEntries = Get-Entries $base[0].FullName
    $addonEntries = Get-Entries $addon[0].FullName
    $aioEntries = Get-Entries $aio[0].FullName

    Check ($baseEntries -contains "F4SE/Plugins/CommunityShadersFO4.dll") "the base carries the plugin"
    Check ($baseEntries -contains "F4SE/Plugins/CommunityShadersFO4.pdb") "and the pdb"
    Check ($baseEntries -contains "COPYING") "and the licence"
    Check ($baseEntries -contains "README.md") "and the readme"
    Check (-not ($baseEntries -contains "Shaders/FO4/ImagespaceCopy.hlsl")) "and not the addon's shader"

    Check (
        $addonEntries.Count -eq 1 -and $addonEntries[0] -eq "Shaders/FO4/ImagespaceCopy.hlsl"
    ) "the addon carries its shader and nothing else"

    $union = @($baseEntries + $addonEntries | Sort-Object -Unique)
    Check (
        (Compare-Object $union (@($aioEntries) | Sort-Object -Unique)) -eq $null
    ) "the all-in-one is the union of base and addons"

    foreach ($pair in @(
            @{ Name = "base"; Entries = $baseEntries },
            @{ Name = "addon"; Entries = $addonEntries },
            @{ Name = "all-in-one"; Entries = $aioEntries })) {
        $e = $pair.Entries
        Check (-not ($e | Where-Object { $_ -match '(^|/)CORE$' })) "no CORE marker in the $($pair.Name)"
        Check (-not ($e | Where-Object { $_ -like "SKSE/*" })) "no SKSE leftovers in the $($pair.Name)"
        Check (-not ($e | Where-Object { $_ -like "Interface/*" })) "no Interface leftovers in the $($pair.Name)"
        Check (-not ($e | Where-Object { $_ -match '^Shaders/[^/]+\.hlsl$' })) "no Skyrim shaders in the $($pair.Name)"
        Check (-not ($e | Where-Object { $_ -like "Data/*" })) "the $($pair.Name) root is Data itself"
    }
}
```

-   [ ] **Step 2: Laufen lassen und den Fehlschlag sehen**

```pwsh
pwsh tools/verify-package.ps1
```

Erwartet: die zehn Baum-Prüfungen bleiben grün, die drei Vollzähligkeitsprüfungen schlagen fehl —
`dist/` ist leer, weil noch nichts packt.

-   [ ] **Step 3: Das Packen in `tools/package.ps1` schreiben**

Die Zeile `throw "packing is not implemented yet"` durch Folgendes ersetzen:

```powershell
if (-not $CMake) {
    throw "-CMake is required when building archives; it is what packs them"
}
if (-not $WorkDir) {
    $WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) "fo4cs-package"
}
if (-not $OutDir) {
    $OutDir = Join-Path $root "dist"
}

# A stale archive from an earlier commit would otherwise sit here and travel
# with the next upload.
Remove-Item $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item $OutDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Get-NameSuffix {
    $sha = & git -C $root rev-parse --short HEAD 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $sha) {
        # No git, no commit: the version alone still names the artefact.
        return $Version
    }
    if (& git -C $root status --porcelain) {
        return "$Version-g$sha-dirty"
    }
    return "$Version-g$sha"
}

function New-Archive([string]$TreeDir, [string]$ArchivePath) {
    Push-Location $TreeDir
    try {
        & $CMake -E tar cf $ArchivePath --format=zip -- .
        if ($LASTEXITCODE -ne 0) {
            throw "packing $ArchivePath failed"
        }
    } finally {
        Pop-Location
    }
    Write-Host "wrote $ArchivePath"
}

$suffix = Get-NameSuffix

$baseTree = Join-Path $WorkDir "base"
New-BaseTree $baseTree
New-Archive $baseTree (Join-Path $OutDir "CommunityShadersFO4-$suffix.zip")

# The all-in-one grows out of the base rather than being assembled twice.
$aioTree = Join-Path $WorkDir "aio"
Copy-Tree $baseTree $aioTree

foreach ($feature in Get-Features | Where-Object { -not $_.IsCore }) {
    $tree = Join-Path $WorkDir $feature.Name
    Copy-FeatureTree $feature.Path $tree

    if (-not (Get-ChildItem $tree -Recurse -File -ErrorAction SilentlyContinue)) {
        # An empty addon archive would be a nuisance to whoever downloaded it.
        Write-Host "skip  $($feature.Name) has no assets, no addon archive"
        continue
    }

    New-Archive $tree (Join-Path $OutDir "$($feature.Name)-$suffix.zip")
    Copy-Tree $tree $aioTree
}

New-Archive $aioTree (Join-Path $OutDir "CommunityShadersFO4-AIO-$suffix.zip")
exit 0
```

-   [ ] **Step 4: Das CMake-Ziel anlegen**

In `CMakeLists.txt`, unmittelbar **nach** dem `if(FO4CS_DEPLOY_DIR)`-Block:

```cmake
# Deliberately outside ALL: the iteration build is the fast way into the game
# and has no business writing three archives every time.
add_custom_target(
    package
    COMMAND
        pwsh -NoProfile -File "${CMAKE_SOURCE_DIR}/tools/package.ps1" -SourceRoot
        "${CMAKE_SOURCE_DIR}" -PluginFile "$<TARGET_FILE:${PROJECT_NAME}>" -PdbFile
        "$<TARGET_PDB_FILE:${PROJECT_NAME}>" -Version "${PROJECT_VERSION}" -CMake
        "${CMAKE_COMMAND}" -WorkDir "${CMAKE_CURRENT_BINARY_DIR}/package"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Packing dist/"
    VERBATIM
)
```

-   [ ] **Step 5: Packen und die rohen Einträge ansehen**

```pwsh
& $cmake -S . --preset FO4 -D "FO4CS_DEPLOY_DIR=F:/SteamLibrary/steamapps/common/Fallout 4/Data"
& $cmake --build --preset FO4 --target package
Get-ChildItem dist
```

Dann **einmal ungefiltert nachsehen**, wie `cmake -E tar` seine Einträge benennt — der Prüfer
entfernt ein führendes `./`, aber ob es da ist, ist eine Annahme, die zu belegen ist:

```pwsh
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path dist/CommunityShadersFO4-AIO-*.zip))
$z.Entries | Select-Object -First 10 FullName
$z.Dispose()
```

Trägt jeder Eintrag ein `./`, ist das im Übergabedokument festzuhalten: Vortex muss damit
zurechtkommen, und Aufgabe 3 prüft es im Spiel.

-   [ ] **Step 6: Prüfer grün sehen**

```pwsh
pwsh tools/verify-package.ps1
```

Erwartet: alle Prüfungen `ok`, `all checks passed`.

-   [ ] **Step 7: Prüfung absichtlich brechen und den Bruch belegen**

Erwarteter Fehlschlag, **vorher benannt**: `no Skyrim shaders in the base`, `no SKSE leftovers in
the base`, `no Interface leftovers in the base` und dieselben drei für `all-in-one` schlagen fehl,
dazu `the all-in-one is the union of base and addons` bleibt grün, weil beide gleich falsch sind.
Die Baum-Prüfungen bleiben grün, weil sie den Staging-Pfad nehmen.

Mutation in `New-BaseTree`: die vier Zeilen, die `package/Shaders/FO4` bedingt kopieren, durch
einen Rundumschlag ersetzen — genau der Fehler, den Skyrims Fassung gemacht hätte:

```powershell
    Copy-Tree (Join-Path $root "package") $To
```

Danach zurücknehmen und erneut grün sehen.

-   [ ] **Step 8: Alle Host-Tests laufen lassen**

```pwsh
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
pwsh tools/verify-package.ps1
```

Erwartet: acht Tests grün, beide Prüfskripte ohne Beanstandung.

-   [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "feat: pack the mod tree into archives"
```

---

## Task 3: Abnahme im Spiel

**Files:** keine Änderung erwartet.

Diese Aufgabe braucht den Nutzer. Anweisungen konkret halten: welche Datei, welcher Knopf, was
danach passieren soll. Jeder Spielstart kostet ihn Zeit, also müssen die Läufe zählen — die beiden
Installationen unten sind zusammen einer.

-   [ ] **Step 1: Aufräumen, damit die Abnahme etwas belegt**

Die entwickelte Fassung liegt bereits im Spiel, weil der Deploy-Schritt sie dorthin schreibt. Ein
Archiv, das über eine bestehende Installation gelegt wird, belegt nichts. Vor der Abnahme also aus
`F:/SteamLibrary/steamapps/common/Fallout 4/Data` entfernen:

```
Data/F4SE/Plugins/CommunityShadersFO4.dll
Data/F4SE/Plugins/CommunityShadersFO4.pdb
Data/Shaders/FO4/
```

-   [ ] **Step 2: Basis allein installieren** — Abnahmekriterium 5

`dist/CommunityShadersFO4-*.zip` (nicht das AIO) über Vortex installieren, Spiel über
`f4se_loader.exe` starten, `coc SanctuaryExt`.

Erwartet: Das Spiel startet, der Farbstich **fehlt**, und das Log zeigt `ImagespaceTint: running`
gefolgt von einer Warnung, dass `ImagespaceCopy.hlsl` nicht gefunden wurde. Kein Absturz.

Das ist der Prüfstein der Aufteilung. Fällt er anders aus, ist der Befund aus Abschnitt 8 der Spec
zu prüfen: `loadedOnce` wird in `WatcherLoop` auch bei fehlender Datei gesetzt, und `_watch` bleibt
dann leer.

-   [ ] **Step 3: Addon nachinstallieren** — der Befund aus der Spec

Bei **laufendem** Spiel `dist/ImagespaceTint-*.zip` über Vortex installieren, Alt-Tab.

Erwartet nach der Lesart des Quelltextes: der Stich kommt **nicht**, weil der Watcher leer ist.
Nach einem Neustart des Spiels ist er da. Bestätigt sich das, ist es ein Befund für die Übergabe —
ob D2 ihn repariert oder an F weitergibt, wird danach entschieden.

-   [ ] **Step 4: All-in-One installieren** — Abnahmekriterium 4

Basis und Addon deinstallieren, `dist/CommunityShadersFO4-AIO-*.zip` installieren, starten.

Erwartet: Das Spiel startet, der Stich ist da, das Log meldet `installed … in place of …`.

-   [ ] **Step 5: Befunde notieren**

Alles Beobachtete roh festhalten, bevor etwas geändert wird. Rohmaterial für Aufgabe 4.

Kein Commit in dieser Aufgabe.

---

## Task 4: Dokumente und Abschluss

**Files:**

-   Modify: `docs/fallout4-port/ROADMAP.md`, `.claude/CLAUDE.md`

-   [ ] **Step 1: Roadmap nachziehen**

-   Zeile D2 auf **abgeschlossen**.
-   Den Absatz zur Teilung von D ergänzen: D2 wurde am 2026-08-31 vor E gezogen, mit der
    Begründung, dass eine funktionierende Auslieferung früh dafür sorgt, dass jedes Feature ab F
    automatisch mitfährt.
-   Abschnitt „Aus Teilprojekt D2 bestätigt" nach dem Muster der vorherigen anlegen, mit den
    gemessenen Werten aus Aufgabe 2 und 3: der Bauzeit-Aufschlag durch den `pwsh`-Start, ob
    `cmake -E tar` ein `./` voranstellt, ob Vortex ein Data-relatives Archiv erkennt, und wie sich
    die Basis ohne ihr Addon verhält.

-   [ ] **Step 2: `CLAUDE.md` nachziehen**

-   In der Tabelle „Temporarily moot" die Zeile
    `| Packaging, AIO archives, dist/, feature .ini version audit | Subproject D2 |` ersetzen durch
    `| Feature .ini versions and their audit | not before F |` — die Paketierung selbst ist zurück.
-   Einen Abschnitt „Packaging" ergänzen: das Ziel `package`, die drei Archive, `package.ps1` als
    die eine Regel, `verify-package.ps1`, und wo Feature-Assets liegen
    (`package/Features/<Name>/`, `CORE` bedeutet Basis). Dazu der Satz, dass `package/Interface`
    und `package/SKSE` Skyrim-Erbe sind und nicht ausgeliefert werden.
-   Im Abschnitt „Features" den Verweis ergänzen, dass ein neues Feature seine Assets unter
    `package/Features/<Name>/` ablegt.

-   [ ] **Step 3: Voller Lauf**

```pwsh
& $cmake --build --preset FO4
ctest --test-dir build/FO4 -C Release --output-on-failure
pwsh tools/verify-plugin.ps1
& $cmake --build --preset FO4 --target package
pwsh tools/verify-package.ps1
```

Erwartet: acht Tests grün, beide Prüfskripte ohne Beanstandung.

-   [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "docs: record subproject d2 acceptance"
```

-   [ ] **Step 5: Abschluss**

`superpowers:finishing-a-development-branch` aufrufen. Erwartete Wahl nach bisherigem Muster: lokal
nach `dev` mergen (Fast-Forward), Feature-Branch löschen, Push auf Ansage.

---

## Self-Review

**Spec-Abdeckung**

| Spec-Abschnitt                    | Aufgabe                                                                              |
| --------------------------------- | ------------------------------------------------------------------------------------ |
| 4.1 Wo Feature-Assets liegen      | 1 (Step 1)                                                                           |
| 4.2 Der Staging-Baum              | 1 (Step 5)                                                                           |
| 4.3 `tools/package.ps1`           | 1 (Staging), 2 (Packen)                                                              |
| 4.4 CMake-Ziel und Deploy-Schritt | 1 (Step 8), 2 (Step 4)                                                               |
| 4.5 `tools/verify-package.ps1`    | 1 (Step 3), 2 (Step 1)                                                               |
| 5 Was in welches Archiv geht      | 2 (Step 6)                                                                           |
| 6 Fehlerbehandlung, alle sieben   | 1 (PDB, fehlende DLL), 2 (leeres Feature, `dist/`, kein `-CMake`, kein Git)          |
| 7.1 Ohne Spiel prüfbar            | 1, 2                                                                                 |
| 7.3 Abnahmekriterien 1–3, 6       | 2 (Step 5, 6, 8)                                                                     |
| 7.3 Abnahmekriterien 4–5          | 3                                                                                    |
| 8 Annahmen, alle vier             | 1 (Step 9 misst die Bauzeit), 2 (Step 5 belegt das `./`), 3 (Vortex, fehlende Datei) |
| 9 Übergabe                        | 4                                                                                    |

**Platzhalter:** keine. Jeder Codeschritt enthält den Code, jeder Prüfschritt das erwartete
Ergebnis, jeder Mutationsschritt den vorher benannten Fehlschlag.

**Typkonsistenz geprüft:** `Copy-Tree`, `Copy-FeatureTree`, `New-BaseTree` und `Get-Features`
werden in Aufgabe 1 definiert und in Aufgabe 2 unter genau diesen Namen benutzt. `Get-Features`
liefert Objekte mit `Name`, `Path` und `IsCore`; alle drei Felder werden in beiden Aufgaben so
gelesen. Die Parameter von `package.ps1` stimmen zwischen Skript, Deploy-Schritt, `package`-Ziel
und `verify-package.ps1` überein — `verify-package.ps1` ruft es ohne `-CMake`, was zulässig ist,
weil es nur `-Stage` benutzt.

**Bewusste Abweichung von der Spec:** Die Spec nennt `-WorkDir` ohne Angabe „ein temporäres
Verzeichnis"; der Plan legt es auf `<Temp>/fo4cs-package` fest, damit zwei Läufe sich nicht
gegenseitig überschreiben und der Ort beim Nachsehen bekannt ist.
