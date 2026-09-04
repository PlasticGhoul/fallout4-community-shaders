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

# Said plainly, because the alternative is a confusing error from the file
# walk below about a directory that was never created.
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $stage)) {
    Write-Host "FAIL  staging failed, nothing to check"
    exit 1
}

$staged = @(Get-ChildItem $stage -Recurse -File |
    ForEach-Object { $_.FullName.Substring($stage.Length).TrimStart('\', '/').Replace('\', '/') })

$runtimeFiles = @(
    "F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-Regular.ttf",
    "F4SE/Plugins/CommunityShadersFO4/Fonts/IBMPlexSans-SemiBold.ttf",
    "F4SE/Plugins/CommunityShadersFO4/Translations/en.json",
    "F4SE/Plugins/CommunityShadersFO4/Translations/de.json"
)

Check ($staged -contains "F4SE/Plugins/CommunityShadersFO4.dll") "the tree carries the plugin"
Check ($staged -contains "Shaders/FO4/ImagespaceCopy.hlsl") "and the feature's shader"

foreach ($file in $runtimeFiles) {
    Check ($staged -contains $file) "and $file"
}

# The licence has to travel with the font, not merely near it: the SIL OFL
# requires it to accompany the files it covers.
Check (
    $staged -contains "F4SE/Plugins/CommunityShadersFO4/Fonts/OFL.txt"
) "and the font licence next to the fonts"

# Deliberately absent from a staged tree: a game install wants what the plugin
# needs at runtime, not the archive's paperwork. The archive checks below are
# where the licence has to turn up.
Check (-not ($staged -contains "COPYING")) "and not the licence"
Check (-not ($staged -contains "README.md")) "and not the readme"
Check (-not ($staged | Where-Object { $_ -match '(^|/)CORE$' })) "and no CORE marker"
Check (-not ($staged | Where-Object { $_ -like "SKSE/*" })) "no SKSE leftovers"
Check (-not ($staged | Where-Object { $_ -like "Interface/*" })) "no Interface leftovers"
Check (-not ($staged | Where-Object { $_ -match '^Shaders/[^/]+\.hlsl$' })) "no Skyrim shaders"
Check (-not ($staged | Where-Object { $_ -like "Data/*" })) "the root is Data, not a folder named Data"

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue

# --------------------------------------------------------------- the archives
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Entries([string]$Archive) {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        # Measured on 2026-08-31: cmake -E tar writes clean Data-relative
        # names with no ./ prefix, and directory entries of their own. The
        # prefix is stripped anyway because other archivers do add one, and a
        # mod manager would be handed a path it cannot place.
        return @($zip.Entries | ForEach-Object { $_.FullName -replace '^\./', '' } |
            Where-Object { $_ -ne "" -and -not $_.EndsWith("/") })
    } finally {
        $zip.Dispose()
    }
}

$dist = Join-Path $root $DistDir

# Sorted out in PowerShell rather than by -Filter: that one hands its pattern
# to the filesystem, which knows only * and ?, so a character class would be
# matched literally and find nothing.
$archives = @(Get-ChildItem $dist -Filter "*.zip" -ErrorAction SilentlyContinue)
$aio = @($archives | Where-Object { $_.Name -like "CommunityShadersFO4-AIO-*" })
$base = @($archives | Where-Object {
        $_.Name -like "CommunityShadersFO4-*" -and $_.Name -notlike "CommunityShadersFO4-AIO-*"
    })
$addon = @($archives | Where-Object { $_.Name -like "ImagespaceTint-*" })

# A stale archive would let a broken rule pass here: the tree half is rebuilt
# on every run, the archives are only ever whatever was packed last. Both the
# plugin and the rule that packs it count - changing the rule without repacking
# is exactly how a mutation slips through this half unnoticed.
$newest = @(
    (Get-Item $PluginFile),
    (Get-Item (Join-Path $root "tools/package.ps1"))
) | Sort-Object LastWriteTime -Descending | Select-Object -First 1

$stale = @($archives | Where-Object { $_.LastWriteTime -lt $newest.LastWriteTime })
Check ($stale.Count -eq 0) "the archives are newer than the plugin and the packing rule"

Check ($base.Count -eq 1) "exactly one base archive"
Check ($addon.Count -eq 1) "exactly one addon archive"
Check ($aio.Count -eq 1) "exactly one all-in-one archive"

if ($base.Count -eq 1 -and $addon.Count -eq 1 -and $aio.Count -eq 1) {
    # Wrapped at the call site as well: PowerShell unrolls a single element
    # array on assignment, and a lone entry would arrive as a bare string
    # whose .Count is empty.
    $baseEntries = @(Get-Entries $base[0].FullName)
    $addonEntries = @(Get-Entries $addon[0].FullName)
    $aioEntries = @(Get-Entries $aio[0].FullName)

    Check ($baseEntries -contains "F4SE/Plugins/CommunityShadersFO4.dll") "the base carries the plugin"
    Check ($baseEntries -contains "F4SE/Plugins/CommunityShadersFO4.pdb") "and the pdb"
    Check ($baseEntries -contains "COPYING") "and the licence"
    Check ($baseEntries -contains "README.md") "and the readme"

    # Base, not an addon: the overlay is what a feature is switched from, so
    # what the overlay reads cannot be optional.
    foreach ($file in $runtimeFiles) {
        Check ($baseEntries -contains $file) "and $file"
    }
    Check (-not ($baseEntries -contains "Shaders/FO4/ImagespaceCopy.hlsl")) "and not the addon's shader"

    Check (
        $addonEntries.Count -eq 1 -and $addonEntries[0] -eq "Shaders/FO4/ImagespaceCopy.hlsl"
    ) "the addon carries its shader and nothing else"

    $union = @($baseEntries + $addonEntries | Sort-Object -Unique)
    Check (
        $null -eq (Compare-Object $union (@($aioEntries) | Sort-Object -Unique))
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

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "$script:failures check(s) failed"
    exit 1
}

Write-Host "all checks passed"
exit 0
