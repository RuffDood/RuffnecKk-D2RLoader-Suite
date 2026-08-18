[CmdletBinding()]
# Validates every public memory-patch manifest and its configurable values.
param(
    [string]$PatchDirectory,
    [switch]$RuntimeSelection
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Windows PowerShell 5.1 evaluates parameter defaults before $PSScriptRoot is
# populated. Resolve the repository-relative default after parameter binding so
# the player-facing validator behaves identically in Windows PowerShell and
# modern PowerShell.
if ([string]::IsNullOrWhiteSpace($PatchDirectory)) {
    $PatchDirectory = Join-Path $PSScriptRoot '..\patches'
}

function Convert-HexOrDecimal {
    param(
        [Parameter(Mandatory)]
        [object]$Value,
        [Parameter(Mandatory)]
        [string]$Field
    )

    $text = [string]$Value
    if ($text -match '^0[xX]([0-9A-Fa-f]+)$') {
        return [Convert]::ToUInt64($Matches[1], 16)
    }
    if ($text -match '^[0-9]+$') {
        return [Convert]::ToUInt64($text, 10)
    }
    throw "Invalid numeric value for ${Field}: '$text'."
}

function Get-ByteStringLength {
    param(
        [Parameter(Mandatory)]
        [object]$Value,
        [Parameter(Mandatory)]
        [string]$Field
    )

    $text = ([string]$Value).Trim()
    if ($text.Length -eq 0) {
        throw "Empty byte string in $Field."
    }
    $tokens = $text -split '\s+'
    foreach ($token in $tokens) {
        if ($token -notmatch '^[0-9A-Fa-f]{2}$') {
            throw "Invalid byte '$token' in $Field."
        }
    }
    return $tokens.Count
}

function Get-OperationSize {
    param(
        [Parameter(Mandatory)]
        [object]$Operation,
        [Parameter(Mandatory)]
        [string]$Context
    )

    switch ([string]$Operation.op) {
        'bytes' {
            $size = Get-ByteStringLength -Value $Operation.bytes -Field "$Context.bytes"
        }
        'nop' {
            $size = [int](Convert-HexOrDecimal -Value $Operation.size -Field "$Context.size")
        }
        'write-u8' { $size = 1 }
        'write-u16' { $size = 2 }
        'write-u32' { $size = 4 }
        'write-u64' { $size = 8 }
        default { throw "Unsupported operation '$($Operation.op)' in $Context." }
    }

    $expectedSize = Get-ByteStringLength -Value $Operation.expected -Field "$Context.expected"
    if ($expectedSize -ne $size) {
        throw "$Context writes $size byte(s), but expected contains $expectedSize."
    }

    if ([string]$Operation.op -like 'write-u*') {
        $value = Convert-HexOrDecimal -Value $Operation.value -Field "$Context.value"
        $bits = $size * 8
        $maximum = if ($bits -eq 64) { [UInt64]::MaxValue } else { ([UInt64]1 -shl $bits) - 1 }
        if ($value -gt $maximum) {
            throw "$Context.value does not fit in $bits bits."
        }
    }

    return $size
}

function Get-OperationByRva {
    param(
        [Parameter(Mandatory)]
        [object]$Manifest,
        [Parameter(Mandatory)]
        [UInt64]$Rva,
        [Parameter(Mandatory)]
        [string]$ManifestName
    )

    $matches = @($Manifest.patches | Where-Object {
        (Convert-HexOrDecimal -Value $_.rva -Field "$ManifestName.rva") -eq $Rva
    })
    if ($matches.Count -ne 1) {
        throw "$ManifestName must contain exactly one operation at 0x$($Rva.ToString('X'))."
    }
    return $matches[0]
}

function Get-WriteValueAtRva {
    param(
        [Parameter(Mandatory)]
        [object]$Manifest,
        [Parameter(Mandatory)]
        [UInt64]$Rva,
        [Parameter(Mandatory)]
        [string]$ManifestName
    )

    $operation = Get-OperationByRva -Manifest $Manifest -Rva $Rva -ManifestName $ManifestName
    if ([string]$operation.op -notlike 'write-u*') {
        throw "$ManifestName operation at 0x$($Rva.ToString('X')) is not a documented value write."
    }
    return Convert-HexOrDecimal -Value $operation.value -Field "$ManifestName.value"
}

$resolvedPatchDirectory = [IO.Path]::GetFullPath($PatchDirectory)
if (-not (Test-Path -LiteralPath $resolvedPatchDirectory -PathType Container)) {
    throw "Patch directory not found: $resolvedPatchDirectory"
}

$files = @(Get-ChildItem -LiteralPath $resolvedPatchDirectory -Filter '*.json' -File | Sort-Object Name)
if ($files.Count -eq 0) {
    throw "No JSON patch manifests found in $resolvedPatchDirectory."
}

$records = [System.Collections.Generic.List[object]]::new()
$manifestsByName = @{}

foreach ($file in $files) {
    try {
        $manifest = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid JSON in $($file.Name): $($_.Exception.Message)"
    }

    if ([int]$manifest.version -ne 1) {
        throw "$($file.Name) must use memory patch schema version 1."
    }
    if ([string]::IsNullOrWhiteSpace([string]$manifest.name)) {
        throw "$($file.Name) has no name."
    }
    if ([string]::IsNullOrWhiteSpace([string]$manifest.description)) {
        throw "$($file.Name) has no description."
    }

    $operations = @($manifest.patches)
    if ($operations.Count -eq 0) {
        throw "$($file.Name) has no patch operations."
    }

    $manifestsByName[[string]$manifest.name] = $manifest
    for ($index = 0; $index -lt $operations.Count; $index++) {
        $operation = $operations[$index]
        $context = "$($file.Name).patches[$index]"
        if ([string]::IsNullOrWhiteSpace([string]$operation.description)) {
            throw "$context has no player-facing description."
        }
        $rva = Convert-HexOrDecimal -Value $operation.rva -Field "$context.rva"
        $size = Get-OperationSize -Operation $operation -Context $context
        if ($size -le 0) {
            throw "$context has a non-positive size."
        }
        $records.Add([pscustomobject]@{
            File = $file.Name
            Name = [string]$manifest.name
            Rva = [UInt64]$rva
            End = [UInt64]($rva + [UInt64]$size)
            Size = $size
        })
    }
}

$ordered = @($records | Sort-Object Rva, End, File)
for ($leftIndex = 0; $leftIndex -lt $ordered.Count; $leftIndex++) {
    $left = $ordered[$leftIndex]
    for ($rightIndex = $leftIndex + 1; $rightIndex -lt $ordered.Count; $rightIndex++) {
        $right = $ordered[$rightIndex]
        if ($right.Rva -ge $left.End) { break }
        if ($right.End -le $left.Rva) { continue }

        $groundPair =
            $left.File -like 'ruffneckk-ground-item-label-limit-*.json' -and
            $right.File -like 'ruffneckk-ground-item-label-limit-*.json'
        if (-not $groundPair) {
            throw "Overlapping writes: $($left.File) and $($right.File) at RVA 0x$($right.Rva.ToString('X'))."
        }
    }
}

$ground64 = @($files | Where-Object Name -eq 'ruffneckk-ground-item-label-limit-64.json').Count
$ground128 = @($files | Where-Object Name -eq 'ruffneckk-ground-item-label-limit-128.json').Count
if ($RuntimeSelection -and ($ground64 + $ground128) -gt 1) {
    throw 'Runtime selection contains both Ground Item Label Limit variants; install exactly one.'
}

if ($manifestsByName.ContainsKey('Extended Automatic Gold Pickup Range')) {
    $manifest = $manifestsByName['Extended Automatic Gold Pickup Range']
    $first = Get-WriteValueAtRva -Manifest $manifest -Rva 0x4BA0B3 -ManifestName 'Extended Gold Pickup'
    $second = Get-WriteValueAtRva -Manifest $manifest -Rva 0x4BA0DA -ManifestName 'Extended Gold Pickup'
    if ($first -ne $second -or $first -lt 1 -or $first -gt 0xFF) {
        throw 'Extended Gold Pickup values must match and remain between 0x01 and 0xFF.'
    }
}

if ($manifestsByName.ContainsKey('Gold Capacities')) {
    $manifest = $manifestsByName['Gold Capacities']
    $characterMultiplier = Get-WriteValueAtRva -Manifest $manifest -Rva 0x34B332 -ManifestName 'Gold Capacities'
    $stashValues = @(
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x133963 -ManifestName 'Gold Capacities'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x34B2FB -ManifestName 'Gold Capacities'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x34B311 -ManifestName 'Gold Capacities'
    )
    if ($characterMultiplier -lt 1 -or $characterMultiplier -gt 0x7FFFFFFF) {
        throw 'Gold Capacities character multiplier must be between 1 and 0x7FFFFFFF.'
    }
    if (@($stashValues | Select-Object -Unique).Count -ne 1 -or $stashValues[0] -lt 1 -or $stashValues[0] -gt 0x7FFFFFFF) {
        throw 'All three Gold Capacities stash values must match and remain between 1 and 0x7FFFFFFF.'
    }
}

if ($manifestsByName.ContainsKey('Hireling AI Tuning')) {
    $manifest = $manifestsByName['Hireling AI Tuning']
    $follow = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BEC27 -ManifestName 'Hireling AI'
    $catchUp = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BEC21 -ManifestName 'Hireling AI'
    $activityA = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BF734 -ManifestName 'Hireling AI'
    $activityB = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BF732 -ManifestName 'Hireling AI'
    $retreatChance = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BF83B -ManifestName 'Hireling AI'
    $retreatDistance = Get-WriteValueAtRva -Manifest $manifest -Rva 0x5BF7FD -ManifestName 'Hireling AI'
    if ($follow -lt 1 -or $catchUp -lt 1 -or $follow -gt $catchUp) {
        throw 'Hireling AI follow distance must be positive and not exceed forced catch-up distance.'
    }
    if ($activityA -ne $activityB -or $activityA -gt 100) {
        throw 'Both Hireling AI activity values must match and remain between 0 and 100.'
    }
    if ($retreatChance -gt 100 -or $retreatDistance -lt 1 -or $retreatDistance -gt 0xFF) {
        throw 'Hireling AI retreat chance must be 0..100 and retreat distance 1..255.'
    }
}

if ($manifestsByName.ContainsKey('Hit Chance Bounds')) {
    $manifest = $manifestsByName['Hit Chance Bounds']
    $lowerValues = @(
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x44BD57 -ManifestName 'Hit Chance Bounds'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x14E8068 -ManifestName 'Hit Chance Bounds'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x15149C2 -ManifestName 'Hit Chance Bounds'
    )
    $upperValues = @(
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x44BD45 -ManifestName 'Hit Chance Bounds'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x14E8073 -ManifestName 'Hit Chance Bounds'
        Get-WriteValueAtRva -Manifest $manifest -Rva 0x15149CD -ManifestName 'Hit Chance Bounds'
    )
    if (@($lowerValues | Select-Object -Unique).Count -ne 1 -or @($upperValues | Select-Object -Unique).Count -ne 1) {
        throw 'Hit Chance lower values must match each other, and upper values must match each other.'
    }
    if ($lowerValues[0] -gt 100 -or $upperValues[0] -gt 100 -or $lowerValues[0] -ge $upperValues[0]) {
        throw 'Hit Chance bounds must satisfy 0 <= lower < upper <= 100.'
    }
}

[pscustomobject]@{
    PatchDirectory = $resolvedPatchDirectory
    Manifests = $files.Count
    Operations = $records.Count
    RuntimeSelection = [bool]$RuntimeSelection
    Result = 'VALID'
}
