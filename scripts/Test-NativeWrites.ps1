[CmdletBinding()]
# Detects overlapping executable writes across plugins and memory patches.
param(
    [string]$ManifestPath,
    [string]$AllowlistPath,
    [string]$ExternalCompatibilityPath,
    [string]$RepositoryRoot,
    [switch]$RuntimeSelection,
    [string[]]$SelectedPatchArtifacts,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Join-Path $PSScriptRoot '..'
}
$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $RepositoryRoot 'manifests\native-writes-3.2.92777.json'
}
if ([string]::IsNullOrWhiteSpace($AllowlistPath)) {
    $AllowlistPath = Join-Path $RepositoryRoot 'manifests\release-allowlist.json'
}
if ([string]::IsNullOrWhiteSpace($ExternalCompatibilityPath)) {
    $ExternalCompatibilityPath = Join-Path $RepositoryRoot 'manifests\external-compatibility.json'
}

function Read-JsonFile {
    param(
        [Parameter(Mandatory)]
        [string]$Path,
        [Parameter(Mandatory)]
        [string]$Label
    )

    $resolved = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "$Label not found: $resolved"
    }
    try {
        return Get-Content -LiteralPath $resolved -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid JSON in ${Label}: $($_.Exception.Message)"
    }
}

function Has-Property {
    param(
        [Parameter(Mandatory)]
        [object]$Object,
        [Parameter(Mandatory)]
        [string]$Name
    )
    return $null -ne $Object.PSObject.Properties[$Name]
}

function Convert-HexOrDecimal {
    param(
        [Parameter(Mandatory)]
        [object]$Value,
        [Parameter(Mandatory)]
        [string]$Field
    )

    $text = ([string]$Value).Trim()
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
    $tokens = @($text -split '\s+')
    foreach ($token in $tokens) {
        if ($token -notmatch '^[0-9A-Fa-f]{2}$') {
            throw "Invalid byte '$token' in $Field."
        }
    }
    return $tokens.Count
}

function Get-PatchOperationSize {
    param(
        [Parameter(Mandatory)]
        [object]$Operation,
        [Parameter(Mandatory)]
        [string]$Context
    )

    $operationName = [string]$Operation.op
    switch ($operationName) {
        'bytes' { $size = Get-ByteStringLength -Value $Operation.bytes -Field "$Context.bytes" }
        'nop' { $size = Convert-HexOrDecimal -Value $Operation.size -Field "$Context.size" }
        'write-u8' { $size = [UInt64]1 }
        'write-u16' { $size = [UInt64]2 }
        'write-u32' { $size = [UInt64]4 }
        'write-u64' { $size = [UInt64]8 }
        default { throw "Unsupported patch operation '$operationName' in $Context." }
    }

    $expectedSize = Get-ByteStringLength -Value $Operation.expected -Field "$Context.expected"
    if ([UInt64]$expectedSize -ne [UInt64]$size) {
        throw "$Context writes $size byte(s), but expected contains $expectedSize."
    }
    return [UInt64]$size
}

function Normalize-RelativePath {
    param([Parameter(Mandatory)][string]$Path)
    return $Path.Replace('\', '/').TrimStart('./')
}

function New-OwnershipRange {
    param(
        [Parameter(Mandatory)][string]$Domain,
        [Parameter(Mandatory)][string]$Owner,
        [Parameter(Mandatory)][object]$Write,
        [string]$Artifact,
        [string]$SelectionGroup
    )

    $rva = Convert-HexOrDecimal -Value $Write.rva -Field "$Owner.rva"
    $size = Convert-HexOrDecimal -Value $Write.size -Field "$Owner.size"
    if ($size -eq 0) {
        throw "$Owner contains a zero-length fixed write at 0x$($rva.ToString('X'))."
    }
    if ($rva -gt ([UInt64]::MaxValue - $size)) {
        throw "$Owner fixed write overflows UInt64 at 0x$($rva.ToString('X'))."
    }
    if ([string]::IsNullOrWhiteSpace([string]$Write.kind)) {
        throw "$Owner fixed write at 0x$($rva.ToString('X')) has no kind."
    }

    return [pscustomobject]@{
        Domain = $Domain
        Owner = $Owner
        Artifact = $Artifact
        SelectionGroup = $SelectionGroup
        Rva = [UInt64]$rva
        End = [UInt64]($rva + $size)
        Size = [UInt64]$size
        Kind = [string]$Write.kind
    }
}

function Test-RangesOverlap {
    param(
        [Parameter(Mandatory)][object]$Left,
        [Parameter(Mandatory)][object]$Right
    )
    return $Left.Rva -lt $Right.End -and $Right.Rva -lt $Left.End
}

function Test-AllowedGroundOverlap {
    param(
        [Parameter(Mandatory)][object]$Left,
        [Parameter(Mandatory)][object]$Right
    )

    if ($Left.Domain -ne 'patch' -or $Right.Domain -ne 'patch') {
        return $false
    }
    if ($Left.SelectionGroup -ne 'ground-item-label-limit' -or
        $Right.SelectionGroup -ne 'ground-item-label-limit') {
        return $false
    }
    $allowed = @(
        'patches/ruffneckk-ground-item-label-limit-64.json',
        'patches/ruffneckk-ground-item-label-limit-128.json'
    )
    return $Left.Artifact -ne $Right.Artifact -and
        $allowed -contains $Left.Artifact -and
        $allowed -contains $Right.Artifact
}

function Assert-NoOverlaps {
    param(
        [Parameter(Mandatory)][object[]]$Ranges,
        [Parameter(Mandatory)][string]$Label,
        [switch]$AllowGroundPair
    )

    $ordered = @($Ranges | Sort-Object Rva, End, Domain, Owner)
    for ($leftIndex = 0; $leftIndex -lt $ordered.Count; $leftIndex++) {
        $left = $ordered[$leftIndex]
        for ($rightIndex = $leftIndex + 1; $rightIndex -lt $ordered.Count; $rightIndex++) {
            $right = $ordered[$rightIndex]
            if ($right.Rva -ge $left.End) { break }
            if (-not (Test-RangesOverlap -Left $left -Right $right)) { continue }
            if ($AllowGroundPair -and (Test-AllowedGroundOverlap -Left $left -Right $right)) {
                continue
            }
            $start = if ($left.Rva -gt $right.Rva) { $left.Rva } else { $right.Rva }
            throw "$Label overlap at 0x$($start.ToString('X')): $($left.Owner) and $($right.Owner)."
        }
    }
}

function Assert-NoCrossOverlaps {
    param(
        [Parameter(Mandatory)][object[]]$LeftRanges,
        [Parameter(Mandatory)][object[]]$RightRanges,
        [Parameter(Mandatory)][string]$Label
    )

    foreach ($left in $LeftRanges) {
        foreach ($right in $RightRanges) {
            if (-not (Test-RangesOverlap -Left $left -Right $right)) { continue }
            $start = if ($left.Rva -gt $right.Rva) { $left.Rva } else { $right.Rva }
            throw "$Label overlap at 0x$($start.ToString('X')): $($left.Owner) and $($right.Owner)."
        }
    }
}

function Assert-SameStringSet {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Expected,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Actual,
        [Parameter(Mandatory)][string]$Label
    )

    $expectedSorted = @($Expected | Sort-Object -Unique)
    $actualSorted = @($Actual | Sort-Object -Unique)
    if ($expectedSorted.Count -ne $Expected.Count) {
        throw "$Label expected set contains duplicates."
    }
    if ($actualSorted.Count -ne $Actual.Count) {
        throw "$Label actual set contains duplicates."
    }
    if ($expectedSorted.Count -ne $actualSorted.Count -or
        @(Compare-Object -ReferenceObject $expectedSorted -DifferenceObject $actualSorted).Count -ne 0) {
        throw "$Label mismatch. Expected [$($expectedSorted -join ', ')], actual [$($actualSorted -join ', ')]."
    }
}

function Assert-SourceMentionsRva {
    param(
        [Parameter(Mandatory)][string]$SourcePath,
        [Parameter(Mandatory)][UInt64]$Rva,
        [Parameter(Mandatory)][string]$Context
    )

    $text = Get-Content -LiteralPath $SourcePath -Raw
    $hex = $Rva.ToString('X')
    if ($text -notmatch "(?i)0x0*$hex(?![0-9A-F])") {
        throw "$Context source does not mention RVA 0x$hex."
    }
}

function Test-RuntimePatchSelection {
    param(
        [Parameter(Mandatory)][object]$Manifest,
        [Parameter(Mandatory)][string[]]$Artifacts
    )

    $selected = @(foreach ($artifact in $Artifacts) {
        Normalize-RelativePath -Path ([string]$artifact)
    })
    $known = @($Manifest.memoryPatchArtifacts | ForEach-Object {
        Normalize-RelativePath -Path ([string]$_.artifact)
    })
    foreach ($artifact in $selected) {
        if ($known -notcontains $artifact) {
            throw "RuntimeSelection contains an unknown patch artifact: $artifact"
        }
    }
    if (@($selected | Sort-Object -Unique).Count -ne $selected.Count) {
        throw 'RuntimeSelection contains duplicate patch artifacts.'
    }

    foreach ($group in @($Manifest.selectionGroups)) {
        $members = @($group.artifacts | ForEach-Object {
            Normalize-RelativePath -Path ([string]$_)
        })
        $matches = @($selected | Where-Object { $members -contains $_ })
        if ($matches.Count -ne 1) {
            throw "RuntimeSelection must contain exactly one artifact from '$($group.id)'; found $($matches.Count)."
        }
    }

    $behaviors = @($Manifest.memoryPatchArtifacts | Where-Object {
        $selected -contains (Normalize-RelativePath -Path ([string]$_.artifact))
    } | ForEach-Object { [string]$_.behaviorId } | Sort-Object -Unique)
    if ($selected.Count -ne 18 -or $behaviors.Count -ne 18) {
        throw "RuntimeSelection must contain 18 artifacts implementing 18 behaviors; found $($selected.Count) artifact(s) and $($behaviors.Count) behavior(s)."
    }
}

function Invoke-NativeWriteValidation {
    param(
        [Parameter(Mandatory)][object]$Manifest,
        [Parameter(Mandatory)][object]$Allowlist,
        [Parameter(Mandatory)][object]$ExternalCompatibility,
        [Parameter(Mandatory)][string]$Root
    )

    if ([int]$Manifest.schemaVersion -ne 1) {
        throw 'Native-write manifest must use schemaVersion 1.'
    }
    if ([UInt64](Convert-HexOrDecimal -Value $Manifest.target.build -Field 'target.build') -ne 92777) {
        throw 'Native-write manifest must target D2R build 92777.'
    }
    $expectedTarget = @{
        canonicalSha256 = 'CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715'
        analysisSha256 = '673E8C0B2E89563E75525B24D137098EFD07B2DB4ED42ADEC56AA1ADDF0E63AB'
        pluginSdkCommit = '4933e2c42cb2592958cd0df3b6dc5003102252d1'
        loaderArchiveSha256 = '923B4933476A8649B7D7C4B50286C0E7FC64CE013B561A446C152A41868C4EDB'
        d2rCoreSha256 = '013B047612BFF0EB564891037508FD43D03AFDFB20BFEB9B5BC683B36559FFC6'
    }
    foreach ($field in @($expectedTarget.Keys)) {
        if (-not (Has-Property -Object $Manifest.target -Name $field) -or
            [string]$Manifest.target.$field -ne [string]$expectedTarget[$field]) {
            throw "Native-write target pin '$field' is missing or incorrect."
        }
    }
    $sdkPinPath = Join-Path $Root 'third_party\PluginSDK\UPSTREAM.md'
    if (-not (Test-Path -LiteralPath $sdkPinPath -PathType Leaf) -or
        (Get-Content -LiteralPath $sdkPinPath -Raw) -notmatch [regex]::Escape([string]$Manifest.target.pluginSdkCommit)) {
        throw 'The vendored PluginSDK pin does not match the native-write manifest.'
    }
    $expectedPluginIds = @($Allowlist.entries | Where-Object {
        [string]$_.kind -eq 'plugin-dll'
    } | ForEach-Object { [string]$_.componentId })
    $manifestPluginIds = @($Manifest.suitePlugins | ForEach-Object { [string]$_.id })
    Assert-SameStringSet -Expected $expectedPluginIds -Actual $manifestPluginIds -Label 'Suite plugin IDs'
    if ($manifestPluginIds.Count -ne 17) {
        throw "Native-write manifest must contain 17 Suite plugin IDs; found $($manifestPluginIds.Count)."
    }

    $suiteRanges = [System.Collections.Generic.List[object]]::new()
    $pluginsById = @{}
    foreach ($plugin in @($Manifest.suitePlugins)) {
        $id = [string]$plugin.id
        $pluginsById[$id] = $plugin
        $sourceRelative = Normalize-RelativePath -Path ([string]$plugin.source)
        $sourcePath = Join-Path $Root ($sourceRelative.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Suite component '$id' source is missing: $sourceRelative"
        }
        $writes = @($plugin.fixedWrites)
        if ($writes.Count -eq 0) {
            throw "Suite component '$id' has no audited fixed writes."
        }
        foreach ($write in $writes) {
            $range = New-OwnershipRange -Domain 'suite' -Owner $id -Write $write
            if ($range.Kind -eq 'sdk-inline-hook' -and
                $range.Size -ne [UInt64]$Manifest.policy.inlineHookEntryWriteBytes) {
                throw "$id inline-hook ownership span at 0x$($range.Rva.ToString('X')) must be $($Manifest.policy.inlineHookEntryWriteBytes) bytes."
            }
            $suiteRanges.Add($range)
        }
        foreach ($allocation in @($plugin.dynamicAllocations)) {
            if ((Has-Property -Object $allocation -Name 'rva') -or
                (Has-Property -Object $allocation -Name 'fixedRva') -or
                (Has-Property -Object $allocation -Name 'address')) {
                throw "$id dynamic allocation must not declare a fixed image RVA/address."
            }
        }
    }

    $expectedPatchArtifacts = @(Get-ChildItem -LiteralPath (Join-Path $Root 'patches') -Filter '*.json' -File |
        ForEach-Object { "patches/$($_.Name)" })
    $manifestPatchArtifacts = @($Manifest.memoryPatchArtifacts)
    $manifestPatchNames = @($manifestPatchArtifacts | ForEach-Object {
        Normalize-RelativePath -Path ([string]$_.artifact)
    })
    Assert-SameStringSet `
        -Expected $expectedPatchArtifacts `
        -Actual $manifestPatchNames `
        -Label 'Memory patch artifacts'
    if ($manifestPatchArtifacts.Count -ne 19) {
        throw "Native-write manifest must contain 19 memory patch artifacts; found $($manifestPatchArtifacts.Count)."
    }
    $behaviorIds = @($manifestPatchArtifacts | ForEach-Object {
        [string]$_.behaviorId
    } | Sort-Object -Unique)
    if ($behaviorIds.Count -ne 18) {
        throw "Native-write manifest must contain 18 memory patch behaviors; found $($behaviorIds.Count)."
    }

    $patchRanges = [System.Collections.Generic.List[object]]::new()
    $patchOperationCount = 0
    foreach ($record in $manifestPatchArtifacts) {
        $artifact = Normalize-RelativePath -Path ([string]$record.artifact)
        $selectionGroup = ''
        if (Has-Property -Object $record -Name 'selectionGroup') {
            $selectionGroup = [string]$record.selectionGroup
        }
        $artifactPath = Join-Path $Root ($artifact.Replace('/', '\'))
        $patch = Read-JsonFile -Path $artifactPath -Label $artifact
        $actualOperations = @($patch.patches)
        $declaredWrites = @($record.fixedWrites)
        if ($actualOperations.Count -ne $declaredWrites.Count) {
            throw "$artifact declares $($declaredWrites.Count) ownership range(s), but contains $($actualOperations.Count) patch operation(s)."
        }
        for ($index = 0; $index -lt $actualOperations.Count; $index++) {
            $operation = $actualOperations[$index]
            $context = "$artifact.patches[$index]"
            $actualRva = Convert-HexOrDecimal -Value $operation.rva -Field "$context.rva"
            $actualSize = Get-PatchOperationSize -Operation $operation -Context $context
            $actualKind = "memory-patch-$([string]$operation.op)"
            $declared = New-OwnershipRange `
                -Domain 'patch' `
                -Owner $artifact `
                -Artifact $artifact `
                -SelectionGroup $selectionGroup `
                -Write $declaredWrites[$index]
            if ($declared.Rva -ne $actualRva -or
                $declared.Size -ne $actualSize -or
                $declared.Kind -ne $actualKind) {
                throw "$context ownership range does not match the patch artifact."
            }
            $patchRanges.Add($declared)
            $patchOperationCount++
        }
    }
    if ($patchOperationCount -ne 62) {
        throw "Expected 62 memory patch operations; found $patchOperationCount."
    }

    $expectedExternalIds = @($ExternalCompatibility.plugins | ForEach-Object { [string]$_.id })
    $manifestExternalIds = @($Manifest.externalPlugins | ForEach-Object { [string]$_.id })
    Assert-SameStringSet -Expected $expectedExternalIds -Actual $manifestExternalIds -Label 'External plugin IDs'
    if ($manifestExternalIds.Count -ne 3) {
        throw "Native-write manifest must contain three pinned external plugins; found $($manifestExternalIds.Count)."
    }

    $externalRanges = [System.Collections.Generic.List[object]]::new()
    foreach ($external in @($Manifest.externalPlugins)) {
        $id = [string]$external.id
        $source = @($ExternalCompatibility.plugins | Where-Object { [string]$_.id -eq $id })
        if ($source.Count -ne 1) {
            throw "External plugin '$id' is missing or duplicated in external-compatibility.json."
        }
        if ([string]$external.sha256 -ne [string]$source[0].sha256) {
            throw "$id SHA-256 does not match external-compatibility.json."
        }
        $expectedRvas = @($source[0].inlineHookRvas | ForEach-Object {
            (Convert-HexOrDecimal -Value $_ -Field "$id.inlineHookRvas").ToString('X')
        })
        $writes = @($external.fixedWrites)
        $actualRvas = @($writes | ForEach-Object {
            (Convert-HexOrDecimal -Value $_.rva -Field "$id.fixedWrites.rva").ToString('X')
        })
        Assert-SameStringSet -Expected $expectedRvas -Actual $actualRvas -Label "$id hook RVAs"
        foreach ($write in $writes) {
            $range = New-OwnershipRange -Domain 'external' -Owner $id -Write $write
            if ($range.Kind -ne 'external-inline-hook' -or
                $range.Size -ne [UInt64]$Manifest.policy.inlineHookEntryWriteBytes) {
                throw "$id fixed writes must be five-byte external inline-hook entries."
            }
            $externalRanges.Add($range)
        }
        $expectedCalls = @()
        if (Has-Property -Object $source[0] -Name 'validatedCallThroughRvas') {
            $expectedCalls = @($source[0].validatedCallThroughRvas | ForEach-Object {
                (Convert-HexOrDecimal -Value $_ -Field "$id.validatedCallThroughRvas").ToString('X')
            })
        }
        $actualCalls = @()
        if (Has-Property -Object $external -Name 'validatedCallThroughRvas') {
            $actualCalls = @($external.validatedCallThroughRvas | ForEach-Object {
                (Convert-HexOrDecimal -Value $_ -Field "$id.validatedCallThroughRvas").ToString('X')
            })
        }
        Assert-SameStringSet -Expected $expectedCalls -Actual $actualCalls -Label "$id validated call-through RVAs"
    }

    Assert-NoOverlaps -Ranges @($suiteRanges) -Label 'Suite-to-Suite'
    Assert-NoCrossOverlaps -LeftRanges @($suiteRanges) -RightRanges @($patchRanges) -Label 'Suite-to-patch'
    Assert-NoCrossOverlaps -LeftRanges @($suiteRanges) -RightRanges @($externalRanges) -Label 'Suite-to-yinyin'
    Assert-NoOverlaps -Ranges @($patchRanges) -Label 'Patch-to-patch' -AllowGroundPair
    Assert-NoCrossOverlaps -LeftRanges @($patchRanges) -RightRanges @($externalRanges) -Label 'Patch-to-yinyin'
    Assert-NoOverlaps -Ranges @($externalRanges) -Label 'Yinyin-to-yinyin'

    $requiredCallThroughs = @{
        'EE2A0' = @(
            'ruffneckk-equipped-item-to-cube',
            'ruffneckk-mass-identify',
            'ruffneckk-remote-stash'
        )
        '373890' = @(
            'ruffneckk-ethereal-item-rules',
            'ruffneckk-charm-aura-trigger-fix',
            'ruffneckk-enhanced-damage-min-max-fix',
            'ruffneckk-progressive-affixes'
        )
        '2F48C0' = @(
            'ruffneckk-item-durability',
            'ruffneckk-repair-costs-cap',
            'ruffneckk-prevent-merc-death-in-town'
        )
    }
    $callThroughs = @($Manifest.composableCallThroughs)
    if ($callThroughs.Count -ne $requiredCallThroughs.Count) {
        throw "Expected $($requiredCallThroughs.Count) composable call-through entries; found $($callThroughs.Count)."
    }
    $seenCallThroughs = @{}
    foreach ($contract in $callThroughs) {
        $entry = Convert-HexOrDecimal -Value $contract.entryRva -Field 'composableCallThroughs.entryRva'
        $key = $entry.ToString('X')
        if ($seenCallThroughs.ContainsKey($key)) {
            throw "Composable call-through entry 0x$key is duplicated."
        }
        $seenCallThroughs[$key] = $true
        if (-not $requiredCallThroughs.ContainsKey($key)) {
            throw "Unproved composable call-through entry 0x$key."
        }
        if ([bool]$contract.requiresVanillaEntry) {
            throw "Composable call-through entry 0x$key must not require the vanilla entry."
        }
        if ([string]$contract.consumerContract -ne 'call-current-live-entry') {
            throw "Composable call-through entry 0x$key must call the current live entry."
        }
        $required = @($requiredCallThroughs[$key])
        $owner = [string]$contract.ownerPluginId
        if ($owner -ne $required[0]) {
            throw "Composable call-through entry 0x$key has the wrong owner '$owner'."
        }
        $consumers = @($contract.consumerPluginIds | ForEach-Object { [string]$_ })
        Assert-SameStringSet -Expected @($required[1..($required.Count - 1)]) -Actual $consumers -Label "0x$key consumers"
        $ownerWrites = @($suiteRanges | Where-Object {
            $_.Owner -eq $owner -and $_.Rva -eq $entry -and $_.Kind -eq 'sdk-inline-hook'
        })
        if ($ownerWrites.Count -ne 1) {
            throw "Composable call-through owner '$owner' must own exactly one inline-hook entry at 0x$key."
        }
        foreach ($consumer in $consumers) {
            $consumerWrites = @($suiteRanges | Where-Object {
                $_.Owner -eq $consumer -and $_.Rva -le $entry -and $_.End -gt $entry
            })
            if ($consumerWrites.Count -ne 0) {
                throw "Composable consumer '$consumer' also writes the owner entry 0x$key."
            }
        }
        foreach ($participant in @($owner) + $consumers) {
            if (-not $pluginsById.ContainsKey($participant)) {
                throw "Composable participant '$participant' is not a Suite component."
            }
            $sourceRelative = Normalize-RelativePath -Path ([string]$pluginsById[$participant].source)
            $sourcePath = Join-Path $Root ($sourceRelative.Replace('/', '\'))
            Assert-SourceMentionsRva -SourcePath $sourcePath -Rva $entry -Context $participant
        }
    }

    return [pscustomobject]@{
        Plugins = $manifestPluginIds.Count
        SuiteWrites = $suiteRanges.Count
        PatchBehaviors = $behaviorIds.Count
        PatchArtifacts = $manifestPatchArtifacts.Count
        PatchWrites = $patchRanges.Count
        ExternalPlugins = $manifestExternalIds.Count
        ExternalWrites = $externalRanges.Count
        ComposableCallThroughs = $callThroughs.Count
    }
}

function Copy-JsonObject {
    param([Parameter(Mandatory)][object]$Object)
    return $Object | ConvertTo-Json -Depth 100 | ConvertFrom-Json
}

function Assert-Throws {
    param(
        [Parameter(Mandatory)][scriptblock]$Action,
        [Parameter(Mandatory)][string]$Label
    )
    $failed = $false
    try {
        & $Action | Out-Null
    }
    catch {
        $failed = $true
    }
    if (-not $failed) {
        throw "Negative self-test did not fail: $Label"
    }
}

$ManifestPath = [IO.Path]::GetFullPath($ManifestPath)
$AllowlistPath = [IO.Path]::GetFullPath($AllowlistPath)
$ExternalCompatibilityPath = [IO.Path]::GetFullPath($ExternalCompatibilityPath)
$manifest = Read-JsonFile -Path $ManifestPath -Label 'native-write manifest'
$allowlist = Read-JsonFile -Path $AllowlistPath -Label 'release allowlist'
$externalCompatibility = Read-JsonFile `
    -Path $ExternalCompatibilityPath `
    -Label 'external compatibility manifest'

$summary = Invoke-NativeWriteValidation `
    -Manifest $manifest `
    -Allowlist $allowlist `
    -ExternalCompatibility $externalCompatibility `
    -Root $RepositoryRoot

if ($RuntimeSelection) {
    $selection = @($SelectedPatchArtifacts | Where-Object {
        -not [string]::IsNullOrWhiteSpace([string]$_)
    })
    if ($selection.Count -eq 0) {
        $selection = @(Get-ChildItem -LiteralPath (Join-Path $RepositoryRoot 'patches') -Filter '*.json' -File | ForEach-Object {
            "patches/$($_.Name)"
        })
    }
    Test-RuntimePatchSelection -Manifest $manifest -Artifacts $selection
}

$selfTestCount = 0
if ($SelfTest) {
    if ((Convert-HexOrDecimal -Value '4660' -Field 'self-test.decimal') -ne 4660 -or
        (Convert-HexOrDecimal -Value '0x1234' -Field 'self-test.hex') -ne 4660) {
        throw 'Numeric parser self-test failed.'
    }
    $selfTestCount++
    Assert-Throws -Label 'malformed numeric value' -Action {
        Convert-HexOrDecimal -Value '0xGG' -Field 'self-test.invalid'
    }
    $selfTestCount++

    $suiteCollision = Copy-JsonObject -Object $manifest
    $suiteCollision.suitePlugins[1].fixedWrites[0].rva = $suiteCollision.suitePlugins[0].fixedWrites[0].rva
    Assert-Throws -Label 'Suite-to-Suite collision' -Action {
        Invoke-NativeWriteValidation -Manifest $suiteCollision -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $externalCollision = Copy-JsonObject -Object $manifest
    $externalCollision.suitePlugins[0].fixedWrites[0].rva = $externalCollision.externalPlugins[0].fixedWrites[0].rva
    Assert-Throws -Label 'Suite-to-yinyin collision' -Action {
        Invoke-NativeWriteValidation -Manifest $externalCollision -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $patchCollision = Copy-JsonObject -Object $manifest
    $patchCollision.suitePlugins[0].fixedWrites[0].rva = $patchCollision.memoryPatchArtifacts[0].fixedWrites[0].rva
    Assert-Throws -Label 'Suite-to-patch collision' -Action {
        Invoke-NativeWriteValidation -Manifest $patchCollision -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $missingComponent = Copy-JsonObject -Object $manifest
    $missingComponent.suitePlugins = @($missingComponent.suitePlugins | Select-Object -Skip 1)
    Assert-Throws -Label 'missing Suite component' -Action {
        Invoke-NativeWriteValidation -Manifest $missingComponent -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $vanillaDependency = Copy-JsonObject -Object $manifest
    $vanillaDependency.composableCallThroughs[0].requiresVanillaEntry = $true
    Assert-Throws -Label 'call-through requiring vanilla entry' -Action {
        Invoke-NativeWriteValidation -Manifest $vanillaDependency -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $patchDrift = Copy-JsonObject -Object $manifest
    $patchDrift.memoryPatchArtifacts[0].fixedWrites[0].size = 1
    Assert-Throws -Label 'patch artifact ownership drift' -Action {
        Invoke-NativeWriteValidation -Manifest $patchDrift -Allowlist $allowlist -ExternalCompatibility $externalCompatibility -Root $RepositoryRoot
    }
    $selfTestCount++

    $allArtifacts = @($manifest.memoryPatchArtifacts | ForEach-Object { [string]$_.artifact })
    Assert-Throws -Label 'both Ground Item Label variants in RuntimeSelection' -Action {
        Test-RuntimePatchSelection -Manifest $manifest -Artifacts $allArtifacts
    }
    $selfTestCount++
    $oneGroundVariant = @($allArtifacts | Where-Object {
        $_ -ne 'patches/ruffneckk-ground-item-label-limit-128.json'
    })
    Test-RuntimePatchSelection -Manifest $manifest -Artifacts $oneGroundVariant
    $selfTestCount++
}

[pscustomobject]@{
    Manifest = $ManifestPath
    RepositoryRoot = $RepositoryRoot
    Plugins = $summary.Plugins
    SuiteWrites = $summary.SuiteWrites
    PatchBehaviors = $summary.PatchBehaviors
    PatchArtifacts = $summary.PatchArtifacts
    PatchWrites = $summary.PatchWrites
    ExternalPlugins = $summary.ExternalPlugins
    ExternalWrites = $summary.ExternalWrites
    ComposableCallThroughs = $summary.ComposableCallThroughs
    RuntimeSelection = [bool]$RuntimeSelection
    SelfTests = $selfTestCount
    Result = 'VALID'
}
