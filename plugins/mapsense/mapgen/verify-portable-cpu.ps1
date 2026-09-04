[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $false)]
    [string]$DumpbinPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-Dumpbin {
    param([string]$RequestedPath)

    if (-not [string]::IsNullOrWhiteSpace($RequestedPath)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $dumpbinMatches = @(& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find 'VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe')
        if ($LASTEXITCODE -eq 0 -and $dumpbinMatches.Count -gt 0) {
            return (Resolve-Path -LiteralPath $dumpbinMatches[0]).Path
        }
    }

    throw 'dumpbin.exe is required to verify the MapSense helper CPU baseline.'
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$resolvedDumpbin = Resolve-Dumpbin -RequestedPath $DumpbinPath

$headers = @(& $resolvedDumpbin /NOLOGO /HEADERS $resolvedExecutable 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed while reading PE headers from '$resolvedExecutable'."
}
$headerText = $headers -join "`n"
if ($headerText -notmatch '(?im)^\s*8664 machine \(x64\)\s*$' -or
    $headerText -notmatch '(?im)^\s*20B magic # \(PE32\+\)\s*$') {
    throw "MapSense helper is not an x86-64 PE32+ executable: '$resolvedExecutable'."
}

$disassembly = @(& $resolvedDumpbin /NOLOGO /DISASM $resolvedExecutable 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed while disassembling '$resolvedExecutable'."
}

$vectorPrefixLines = @($disassembly | Where-Object {
    $_ -match '(?i)^\s*[0-9A-F]+:\s+(?:62|C4|C5)\s'
})
$wideRegisterLines = @($disassembly | Where-Object {
    $_ -match '(?i)\b(?:ymm[0-9]+|zmm[0-9]+|k[0-7])\b'
})
if ($vectorPrefixLines.Count -ne 0 -or $wideRegisterLines.Count -ne 0) {
    $examples = @(
        @($vectorPrefixLines) + @($wideRegisterLines) |
            Select-Object -Unique -First 5
    ) -join [Environment]::NewLine
    throw @"
MapSense helper is not portable x86-64 baseline code. Found
$($vectorPrefixLines.Count) VEX/EVEX instruction(s) and
$($wideRegisterLines.Count) YMM/ZMM/opmask reference(s):
$examples
"@
}

Write-Output "PASS MapSense helper CPU baseline: x86-64 PE32+, VEX/EVEX=0, YMM/ZMM/opmask=0."
