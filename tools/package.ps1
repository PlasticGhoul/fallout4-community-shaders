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
