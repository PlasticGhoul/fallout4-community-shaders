<#
.SYNOPSIS
    Verify the built plugin is a loadable x64 F4SE plugin.

.DESCRIPTION
    Checks the PE header, the machine type, the DLL characteristic bit, the
    exported entry points and the version resource. Exits non-zero if any check
    fails, so it can gate a build.

    The export table is parsed straight out of the PE image rather than shelled
    out to dumpbin, so this runs from any shell, not just a Developer prompt.

.EXAMPLE
    pwsh tools/verify-plugin.ps1
#>
param(
    [string]$Dll = "build/FO4/Release/CommunityShadersFO4.dll",
    [string]$ExpectedName = "CommunityShadersFO4",
    [string]$ExpectedVersion = "0.1.0.0"
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

function Get-PeExportName {
    param([byte[]]$Bytes)

    $pe = [BitConverter]::ToInt32($Bytes, 0x3C)
    $sectionCount = [BitConverter]::ToUInt16($Bytes, $pe + 6)
    $optionalSize = [BitConverter]::ToUInt16($Bytes, $pe + 20)
    $optional = $pe + 24
    $magic = [BitConverter]::ToUInt16($Bytes, $optional)

    # Data directories sit after the optional header's fixed part: 112 bytes for
    # PE32+, 96 for PE32. The export directory is the first entry.
    $dirOffset = $optional + $(if ($magic -eq 0x20B) { 112 } else { 96 })
    $exportRva = [BitConverter]::ToUInt32($Bytes, $dirOffset)
    if ($exportRva -eq 0) { return @() }

    $sections = @()
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $s = $optional + $optionalSize + ($i * 40)
        $sections += [pscustomobject]@{
            VirtualSize    = [BitConverter]::ToUInt32($Bytes, $s + 8)
            VirtualAddress = [BitConverter]::ToUInt32($Bytes, $s + 12)
            RawSize        = [BitConverter]::ToUInt32($Bytes, $s + 16)
            RawPointer     = [BitConverter]::ToUInt32($Bytes, $s + 20)
        }
    }

    function Convert-RvaToOffset([uint32]$Rva) {
        foreach ($s in $sections) {
            $span = [Math]::Max($s.VirtualSize, $s.RawSize)
            if ($Rva -ge $s.VirtualAddress -and $Rva -lt ($s.VirtualAddress + $span)) {
                return $s.RawPointer + ($Rva - $s.VirtualAddress)
            }
        }
        return -1
    }

    $exportOffset = Convert-RvaToOffset $exportRva
    if ($exportOffset -lt 0) { return @() }

    $nameCount = [BitConverter]::ToUInt32($Bytes, $exportOffset + 24)
    $namesRva = [BitConverter]::ToUInt32($Bytes, $exportOffset + 32)
    $namesOffset = Convert-RvaToOffset $namesRva
    if ($namesOffset -lt 0) { return @() }

    $names = @()
    for ($i = 0; $i -lt $nameCount; $i++) {
        $rva = [BitConverter]::ToUInt32($Bytes, $namesOffset + ($i * 4))
        $start = Convert-RvaToOffset $rva
        if ($start -lt 0) { continue }
        $end = $start
        while ($Bytes[$end] -ne 0) { $end++ }
        $names += [System.Text.Encoding]::ASCII.GetString($Bytes, $start, $end - $start)
    }
    return $names
}

if (-not (Test-Path $Dll)) {
    Write-Host "FAIL  artefact not found: $Dll"
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($Dll)
$pe = [BitConverter]::ToInt32($bytes, 0x3C)

$signature = [System.Text.Encoding]::ASCII.GetString($bytes, $pe, 2)
Check ($signature -eq "PE") "PE signature present"

$machine = [BitConverter]::ToUInt16($bytes, $pe + 4)
Check ($machine -eq 0x8664) ("machine type is x64 (found 0x{0:X4})" -f $machine)

$characteristics = [BitConverter]::ToUInt16($bytes, $pe + 22)
Check (($characteristics -band 0x2000) -ne 0) "DLL characteristic bit set"

$exports = Get-PeExportName -Bytes $bytes
foreach ($symbol in @("F4SEPlugin_Load", "F4SEPlugin_Query", "F4SEPlugin_Version")) {
    Check ($exports -contains $symbol) "exports $symbol"
}

$info = (Get-Item $Dll).VersionInfo
Check ($info.ProductName -eq $ExpectedName) "version resource names $ExpectedName (found '$($info.ProductName)')"
Check ($info.FileVersion -eq $ExpectedVersion) "file version is $ExpectedVersion (found '$($info.FileVersion)')"

Write-Host ""
if ($script:failures -gt 0) {
    Write-Host "$script:failures check(s) failed"
    exit 1
}

Write-Host "all checks passed"
exit 0
