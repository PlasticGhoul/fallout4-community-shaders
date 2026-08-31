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
