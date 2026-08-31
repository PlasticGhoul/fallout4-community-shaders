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
$OutDir = (Resolve-Path $OutDir).Path

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
