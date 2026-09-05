[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$PlanPath,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$SchemaPath,
    [string]$AllowlistPath,
    [switch]$RequirePackageReady,
    [string]$WriteReleaseNotesPath,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Read-JsonDocument {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Label)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label not found: $Path"
    }
    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid $Label JSON '$Path': $($_.Exception.Message)"
    }
}

function Get-PropertyValue {
    param(
        [Parameter(Mandatory)][object]$Object,
        [Parameter(Mandatory)][string]$Name,
        $Default = $null
    )

    if ($null -ne $Object.PSObject.Properties[$Name]) {
        return $Object.$Name
    }
    return $Default
}

function Add-SetDifferenceErrors {
    param(
        [Parameter(Mandatory)][AllowEmptyCollection()][Collections.Generic.List[string]]$Errors,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Expected,
        [Parameter(Mandatory)][AllowEmptyCollection()][string[]]$Actual
    )

    $missing = @($Expected | Where-Object { $_ -notin $Actual } | Sort-Object -Unique)
    $extra = @($Actual | Where-Object { $_ -notin $Expected } | Sort-Object -Unique)
    if ($missing.Count -ne 0) { $Errors.Add("$Label missing: $($missing -join ', ')") }
    if ($extra.Count -ne 0) { $Errors.Add("$Label unexpected: $($extra -join ', ')") }
}

$resolvedPlan = [IO.Path]::GetFullPath($PlanPath)
$resolvedSchema = [IO.Path]::GetFullPath($SchemaPath)
if ($RequirePackageReady -and [string]::IsNullOrWhiteSpace($AllowlistPath)) {
    throw 'RequirePackageReady requires an explicit external release allowlist path.'
}
$plan = Read-JsonDocument -Path $resolvedPlan -Label 'next-release registry'
$null = Read-JsonDocument -Path $resolvedSchema -Label 'next-release schema'
$errors = [Collections.Generic.List[string]]::new()
$semanticVersion = '^\d+\.\d+\.\d+$'
$componentIdPattern = '^[a-z0-9][a-z0-9.-]*$'
$allowedKinds = @('plugin', 'patch', 'tool', 'project')
$allowedDispositions = @('include', 'remove', 'defer')
$allowedChangeTypes = @('add', 'update', 'retain', 'remove', 'defer')
$allowedVersionStatuses = @('locked', 'pending', 'candidate', 'needs-reconciliation', 'not-applicable', 'not-scheduled')
$allowedGates = @('passed', 'pending', 'blocked', 'not-required')
$requiredGateNames = @('source', 'build', 'battleNetRuntime', 'steamRuntime', 'packaging')
$requiredPackagingGateNames = @('source', 'build', 'battleNetRuntime', 'packaging')
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$releaseAuthorityLocked = [bool]$plan.suite.releaseReady -or [string]$plan.suite.status -in 'package-ready', 'published'

if ([int](Get-PropertyValue $plan 'schemaVersion' 0) -ne 1) {
    $errors.Add('The next-release registry requires schemaVersion 1.')
}
if ([string]$plan.suite.id -ne 'ruffneckk-d2rloader-suite') {
    $errors.Add('The next-release registry has an unsupported Suite id.')
}
if ([string]$plan.suite.latestPublicVersion -notmatch $semanticVersion -or
    [string]$plan.suite.targetVersion -notmatch $semanticVersion) {
    $errors.Add('The latest public and target Suite versions must be semantic x.y.z versions.')
}
elseif ([version]$plan.suite.targetVersion -le [version]$plan.suite.latestPublicVersion) {
    $errors.Add('The target Suite version must be greater than the latest public version.')
}
if ([string]$plan.compatibility.policy -ne 'native-fingerprint-fail-closed') {
    $errors.Add('The compatibility policy must be native-fingerprint-fail-closed.')
}
if ($plan.suite.PSObject.Properties.Name -contains 'targetGameBuild' -or
    $plan.suite.PSObject.Properties.Name -contains 'validatedGameBuilds') {
    $errors.Add('D2R build identifiers are evidence, never a loading allowlist.')
}
$steamChannels = @($plan.compatibility.channels | Where-Object { [string]$_.name -eq 'Steam' })
if ($steamChannels.Count -ne 1) {
    $errors.Add('The registry must contain exactly one Steam compatibility channel.')
}

$components = @($plan.components)
$componentIds = @($components | ForEach-Object { [string]$_.id })
if (@($componentIds | Sort-Object -Unique).Count -ne $components.Count) {
    $errors.Add('Every release registry entry requires a unique id.')
}

foreach ($component in $components) {
    $id = [string](Get-PropertyValue $component 'id' '')
    $kind = [string](Get-PropertyValue $component 'kind' '')
    $disposition = [string](Get-PropertyValue $component 'disposition' '')
    $changeType = [string](Get-PropertyValue $component 'changeType' '')
    $versionStatus = [string](Get-PropertyValue $component 'versionStatus' '')
    $targetVersion = Get-PropertyValue $component 'targetVersion'
    $asset = Get-PropertyValue $component 'asset'
    $archiveReadme = Get-PropertyValue $component 'archiveReadme'
    $archiveCompanions = @(Get-PropertyValue $component 'archiveCompanions' @())
    $archiveEmbedding = Get-PropertyValue $component 'archiveEmbedding'
    $sourceRef = [string](Get-PropertyValue $component 'sourceRef' '')

    if ($id -notmatch $componentIdPattern) { $errors.Add("Invalid release registry id '$id'.") }
    if ($kind -notin $allowedKinds) { $errors.Add("$id has unsupported kind '$kind'.") }
    if ($disposition -notin $allowedDispositions) { $errors.Add("$id has unsupported disposition '$disposition'.") }
    if ($changeType -notin $allowedChangeTypes) { $errors.Add("$id has unsupported changeType '$changeType'.") }
    if ($versionStatus -notin $allowedVersionStatuses) { $errors.Add("$id has unsupported versionStatus '$versionStatus'.") }
    foreach ($field in @('name', 'sourceRef', 'releaseNoteCategory', 'releaseNote', 'decision')) {
        if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $component $field ''))) {
            $errors.Add("$id requires a non-empty $field.")
        }
    }
    if ($kind -in 'plugin', 'tool' -and $null -ne $targetVersion -and
        [string]$targetVersion -notmatch $semanticVersion) {
        $errors.Add("$id requires a semantic targetVersion when one is declared.")
    }
    if ($kind -eq 'patch' -and $null -ne $targetVersion) {
        $errors.Add("Patch $id must not declare a component SemVer.")
    }
    $isEmbeddedTool = $kind -eq 'tool' -and $null -ne $archiveEmbedding
    if ($disposition -eq 'include' -and $kind -in 'plugin', 'patch', 'tool' -and
        $null -eq $asset -and -not $isEmbeddedTool -and $versionStatus -ne 'pending') {
        $errors.Add("Included $kind $id requires an asset name or an explicitly pending version.")
    }
    if ($null -ne $asset -and ([IO.Path]::GetFileName([string]$asset) -cne [string]$asset)) {
        $errors.Add("$id asset must be a basename, not a path: '$asset'.")
    }
    if ($disposition -eq 'include' -and $kind -eq 'plugin' -and $null -ne $asset -and
        $null -ne $targetVersion -and -not ([string]$asset).EndsWith("-v$targetVersion.zip", [StringComparison]::Ordinal)) {
        $errors.Add("$id asset '$asset' does not match targetVersion '$targetVersion'.")
    }
    if ($releaseAuthorityLocked -and $disposition -eq 'include' -and
        [string](Get-PropertyValue $component.gates 'packaging' '') -eq 'passed' -and
        -not $sourceRef.StartsWith('suite:', [StringComparison]::Ordinal)) {
        $errors.Add("$id cannot be release-ready while its authoritative source is '$sourceRef'.")
    }
    if ($disposition -eq 'include' -and $sourceRef.StartsWith('suite:', [StringComparison]::Ordinal)) {
        $relativeSource = $sourceRef.Substring('suite:'.Length).Replace('/', [IO.Path]::DirectorySeparatorChar)
        $resolvedSource = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $relativeSource))
        if (-not $resolvedSource.StartsWith($repositoryRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            $errors.Add("$id sourceRef escapes the public Suite repository: '$sourceRef'.")
        }
        elseif (-not (Test-Path -LiteralPath $resolvedSource)) {
            $errors.Add("$id sourceRef does not exist in the public Suite repository: '$sourceRef'.")
        }
    }
    if ($kind -eq 'patch' -and $null -ne $asset -and [string]$asset -notmatch '^ruffneckk-[a-z0-9-]+\.json$') {
        $errors.Add("Patch $id has an invalid asset name '$asset'.")
    }
    if ($kind -eq 'tool' -and $disposition -eq 'include') {
        if ($isEmbeddedTool -and $null -ne $asset) {
            $errors.Add("Embedded tool $id must not declare a separate GitHub asset.")
        }
        elseif (-not $isEmbeddedTool -and $null -ne $asset -and
            [IO.Path]::GetExtension([string]$asset) -ine '.exe') {
            $errors.Add("Standalone tool $id must be released as an EXE asset.")
        }
    }
    if ($null -ne $archiveReadme) {
        $expectedReadmeSource = if ($id -match '^ruffneckk-(?<slug>[a-z0-9-]+)$') {
            "plugins/$($Matches.slug)/README.md"
        } else { '' }
        if ($kind -ne 'plugin' -or $disposition -ne 'include') {
            $errors.Add("Only an included plugin may declare archiveReadme: $id.")
        }
        if (-not [bool](Get-PropertyValue $archiveReadme 'include' $false)) {
            $errors.Add("$id archiveReadme.include must be true when the contract is present.")
        }
        $readmeDestination = [string](Get-PropertyValue $archiveReadme 'destination' '')
        $validReadmeDestination = $readmeDestination -ceq 'README.md' -or
            $readmeDestination -cmatch '^[A-Za-z0-9][A-Za-z0-9._-]*-README\.md$'
        if ([string](Get-PropertyValue $archiveReadme 'source' '') -cne $expectedReadmeSource -or
            -not $validReadmeDestination) {
            $errors.Add("$id archiveReadme must map $expectedReadmeSource to README.md or a governed root README basename.")
        }
        if ([string](Get-PropertyValue $archiveReadme 'sha256' '') -notmatch '^[0-9A-F]{64}$') {
            $errors.Add("$id archiveReadme requires an uppercase SHA-256.")
        }
        foreach ($field in @('reviewedBy', 'reviewedOn', 'decision')) {
            if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $archiveReadme $field ''))) {
                $errors.Add("$id archiveReadme requires $field.")
            }
        }
        if ([string](Get-PropertyValue $archiveReadme 'reviewedOn' '') -notmatch '^\d{4}-\d{2}-\d{2}$') {
            $errors.Add("$id archiveReadme.reviewedOn must use YYYY-MM-DD.")
        }
    }
    if ($archiveCompanions.Count -ne 0) {
        if ($kind -ne 'plugin' -or $disposition -ne 'include') {
            $errors.Add("Only an included plugin may declare archiveCompanions: $id.")
        }
        $companionDestinations = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach ($companion in $archiveCompanions) {
            $companionKind = [string](Get-PropertyValue $companion 'kind' '')
            $source = ([string](Get-PropertyValue $companion 'source' '')).Replace('\', '/')
            $destination = ([string](Get-PropertyValue $companion 'destination' '')).Replace('\', '/')
            if ($companionKind -ne 'plugin-companion-exe') {
                $errors.Add("$id archive companion has unsupported kind '$companionKind'.")
            }
            if ($source -cne $destination -or $destination -notmatch '^plugins/[A-Za-z0-9][A-Za-z0-9._-]*\.exe$') {
                $errors.Add("$id archive companion must keep the same plugins/<name>.exe source and destination path.")
            }
            if (-not $companionDestinations.Add($destination)) {
                $errors.Add("$id declares duplicate archive companion destination '$destination'.")
            }
            if ([string](Get-PropertyValue $companion 'sha256' '') -notmatch '^[0-9A-F]{64}$') {
                $errors.Add("$id archive companion requires an uppercase SHA-256.")
            }
            if (-not [bool](Get-PropertyValue $companion 'requiredForRuntime' $false)) {
                $errors.Add("$id archive companion must be requiredForRuntime.")
            }
            if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $companion 'decision' ''))) {
                $errors.Add("$id archive companion requires a decision.")
            }
        }
    }
    if ($null -ne $archiveEmbedding) {
        if ($kind -ne 'tool' -or $disposition -ne 'include') {
            $errors.Add("Only an included tool may declare archiveEmbedding: $id.")
        }
        $ownerComponentId = [string](Get-PropertyValue $archiveEmbedding 'ownerComponentId' '')
        $destinationDirectory = [string](Get-PropertyValue $archiveEmbedding 'destinationDirectory' '')
        $embeddingExecutable = Get-PropertyValue $archiveEmbedding 'executable'
        $embeddingReadme = Get-PropertyValue $archiveEmbedding 'readme'
        if ($ownerComponentId -notmatch $componentIdPattern) {
            $errors.Add("$id archiveEmbedding requires a valid ownerComponentId.")
        }
        if ([bool](Get-PropertyValue $archiveEmbedding 'includeInBundles' $true)) {
            $errors.Add("$id archiveEmbedding must remain excluded from bundles.")
        }
        if ($destinationDirectory -notmatch '^[A-Za-z0-9][A-Za-z0-9 ._-]*$') {
            $errors.Add("$id archiveEmbedding requires a safe destinationDirectory.")
        }
        if ($null -eq $embeddingExecutable -or $null -eq $embeddingReadme) {
            $errors.Add("$id archiveEmbedding requires one executable and one README.")
        }
        else {
            $executableSource = ([string](Get-PropertyValue $embeddingExecutable 'source' '')).Replace('\', '/')
            $executableDestination = ([string](Get-PropertyValue $embeddingExecutable 'destination' '')).Replace('\', '/')
            $readmeSource = ([string](Get-PropertyValue $embeddingReadme 'source' '')).Replace('\', '/')
            $readmeDestination = ([string](Get-PropertyValue $embeddingReadme 'destination' '')).Replace('\', '/')
            if ($executableSource -notmatch '^tools/[A-Za-z0-9][A-Za-z0-9._-]*\.exe$' -or
                $executableDestination -cne "$destinationDirectory/$([IO.Path]::GetFileName($executableDestination))" -or
                [IO.Path]::GetExtension($executableDestination) -ine '.exe') {
                $errors.Add("$id archiveEmbedding executable must map a tools/*.exe source into its governed directory.")
            }
            $validEmbeddedReadmeDestination = $readmeDestination -ceq "$destinationDirectory/README.md" -or
                $readmeDestination -cmatch '^[A-Za-z0-9][A-Za-z0-9._-]*-README\.md$'
            if ($readmeSource -notmatch '^tools/[a-z0-9-]+/README\.md$' -or
                -not $validEmbeddedReadmeDestination) {
                $errors.Add("$id archiveEmbedding README must map a governed tool README into its directory or a governed root README basename.")
            }
            foreach ($file in @($embeddingExecutable, $embeddingReadme)) {
                if ([string](Get-PropertyValue $file 'sha256' '') -notmatch '^[0-9A-F]{64}$') {
                    $errors.Add("$id archiveEmbedding files require uppercase SHA-256 values.")
                }
            }
            foreach ($field in @('reviewedBy', 'reviewedOn')) {
                if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $embeddingReadme $field ''))) {
                    $errors.Add("$id archiveEmbedding README requires $field.")
                }
            }
            if ([string](Get-PropertyValue $embeddingReadme 'reviewedOn' '') -notmatch '^\d{4}-\d{2}-\d{2}$') {
                $errors.Add("$id archiveEmbedding README reviewedOn must use YYYY-MM-DD.")
            }
        }
        if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $archiveEmbedding 'decision' ''))) {
            $errors.Add("$id archiveEmbedding requires a decision.")
        }
    }

    $gates = Get-PropertyValue $component 'gates'
    if ($null -eq $gates) {
        $errors.Add("$id requires release gates.")
        continue
    }
    foreach ($gateName in $requiredGateNames) {
        $gateValue = [string](Get-PropertyValue $gates $gateName '')
        if ($gateValue -notin $allowedGates) {
            $errors.Add("$id gate $gateName has unsupported value '$gateValue'.")
        }
    }
}

$included = @($components | Where-Object { [string]$_.disposition -eq 'include' })
$includedIds = @($included | ForEach-Object { [string]$_.id })
$requiredIncluded = @($plan.policy.requiredIncludedIds | ForEach-Object { [string]$_ })
$deniedIncluded = @($plan.policy.deniedIncludedIds | ForEach-Object { [string]$_ })
foreach ($id in $requiredIncluded) {
    if ($id -notin $includedIds) { $errors.Add("Required release component '$id' is not included.") }
}
foreach ($id in $deniedIncluded) {
    if ($id -in $includedIds) { $errors.Add("Denied release component '$id' is included.") }
}

$includedPlugins = @($included | Where-Object { [string]$_.kind -eq 'plugin' })
$includedPatches = @($included | Where-Object { [string]$_.kind -eq 'patch' })
$includedTools = @($included | Where-Object { [string]$_.kind -eq 'tool' })
$embeddedTools = @($includedTools | Where-Object { $null -ne (Get-PropertyValue $_ 'archiveEmbedding') })
$standaloneTools = @($includedTools | Where-Object { $null -eq (Get-PropertyValue $_ 'archiveEmbedding') })
$plannedArchiveReadmes = @($includedPlugins | Where-Object {
    $null -ne (Get-PropertyValue $_ 'archiveReadme') -and
    [bool](Get-PropertyValue $_.archiveReadme 'include' $false)
})
$plannedArchiveCompanions = @(
    foreach ($component in $includedPlugins) {
        foreach ($companion in @(Get-PropertyValue $component 'archiveCompanions' @())) {
            [pscustomobject]@{
                ComponentId = [string]$component.id
                Kind = [string]$companion.kind
                Source = ([string]$companion.source).Replace('\', '/')
                Destination = ([string]$companion.destination).Replace('\', '/')
                SHA256 = ([string]$companion.sha256).ToUpperInvariant()
            }
        }
    }
)
$plannedEmbeddedFiles = @(
    foreach ($tool in $embeddedTools) {
        $embedding = $tool.archiveEmbedding
        [pscustomobject]@{
            ComponentId = [string]$tool.id
            ArchiveComponentId = [string]$embedding.ownerComponentId
            Kind = 'embedded-tool-exe'
            Source = ([string]$embedding.executable.source).Replace('\', '/')
            Destination = ([string]$embedding.executable.destination).Replace('\', '/')
            SHA256 = ([string]$embedding.executable.sha256).ToUpperInvariant()
            Version = [string]$tool.targetVersion
        }
        [pscustomobject]@{
            ComponentId = [string]$tool.id
            ArchiveComponentId = [string]$embedding.ownerComponentId
            Kind = 'embedded-tool-readme'
            Source = ([string]$embedding.readme.source).Replace('\', '/')
            Destination = ([string]$embedding.readme.destination).Replace('\', '/')
            SHA256 = ([string]$embedding.readme.sha256).ToUpperInvariant()
            Version = ''
        }
    }
)
foreach ($tool in $embeddedTools) {
    $ownerComponentId = [string]$tool.archiveEmbedding.ownerComponentId
    if (@($includedPlugins | Where-Object { [string]$_.id -eq $ownerComponentId }).Count -ne 1) {
        $errors.Add("Embedded tool $($tool.id) requires one included plugin owner; found '$ownerComponentId'.")
    }
}
$duplicateCompanionDestinations = @($plannedArchiveCompanions | Group-Object Destination | Where-Object Count -gt 1)
foreach ($duplicate in $duplicateCompanionDestinations) {
    $errors.Add("Archive companion destination is shared by multiple plugins: $($duplicate.Name).")
}
$expectedAssets = $plan.distribution.expectedGithubAssets
$derivedTotal = $includedPlugins.Count + $includedPatches.Count + $standaloneTools.Count + [int]$plan.distribution.bundles
if ([int]$expectedAssets.individualPluginArchives -ne $includedPlugins.Count -or
    [int]$expectedAssets.individualPatchFiles -ne $includedPatches.Count -or
    [int]$expectedAssets.standaloneTools -ne $standaloneTools.Count -or
    [int]$expectedAssets.optionalBundles -ne [int]$plan.distribution.bundles -or
    [int]$expectedAssets.total -ne $derivedTotal) {
    $errors.Add("Expected GitHub asset counts do not match the derived $($includedPlugins.Count)/$($includedPatches.Count)/$($includedTools.Count)/$($plan.distribution.bundles) catalog ($derivedTotal total).")
}

if ([bool]$plan.suite.releaseReady -or $RequirePackageReady) {
    if (-not [bool]$plan.suite.releaseReady) {
        $errors.Add('Packaging is blocked because suite.releaseReady is false.')
    }
    if ([string]$plan.suite.status -notin 'package-ready', 'published') {
        $errors.Add("Packaging requires suite.status 'package-ready' or 'published'.")
    }
    foreach ($component in $included) {
        $id = [string]$component.id
        if ($component.kind -in 'plugin', 'tool' -and
            ([string](Get-PropertyValue $component 'targetVersion' '') -notmatch $semanticVersion)) {
            $errors.Add("Packaging requires a locked semantic version for $id.")
        }
        $componentEmbedding = Get-PropertyValue $component 'archiveEmbedding'
        $requiresOwnAsset = $component.kind -in 'plugin', 'patch' -or
            ($component.kind -eq 'tool' -and $null -eq $componentEmbedding)
        if ($requiresOwnAsset -and [string]::IsNullOrWhiteSpace([string](Get-PropertyValue $component 'asset' ''))) {
            $errors.Add("Packaging requires an asset name for $id.")
        }
        foreach ($gateName in $requiredPackagingGateNames) {
            $gateValue = [string](Get-PropertyValue $component.gates $gateName '')
            if ($gateValue -notin 'passed', 'not-required') {
                $errors.Add("Packaging requires $id gate $gateName to pass or be not-required; found $gateValue.")
            }
        }
    }
}

if (-not [string]::IsNullOrWhiteSpace($AllowlistPath)) {
    $resolvedAllowlist = [IO.Path]::GetFullPath($AllowlistPath)
    $allowlist = Read-JsonDocument -Path $resolvedAllowlist -Label 'release allowlist'
    if ([string]$allowlist.suite.version -ne [string]$plan.suite.targetVersion) {
        $errors.Add("Allowlist Suite version $($allowlist.suite.version) does not match registry target $($plan.suite.targetVersion).")
    }

    $allowlistPlugins = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-dll' })
    $allowlistReadmes = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-readme' })
    $allowlistCompanions = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-companion-exe' })
    $allowlistEmbeddedExecutables = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-exe' })
    $allowlistEmbeddedReadmes = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-readme' })
    $allowlistPatches = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'memory-patch-json' })
    $allowlistTools = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'standalone-tool' })
    Add-SetDifferenceErrors -Errors $errors -Label 'Plugin allowlist' `
        -Expected @($includedPlugins | ForEach-Object { [string]$_.id }) `
        -Actual @($allowlistPlugins | ForEach-Object { [string]$_.componentId })
    Add-SetDifferenceErrors -Errors $errors -Label 'Patch allowlist' `
        -Expected @($includedPatches | ForEach-Object { [string]$_.asset }) `
        -Actual @($allowlistPatches | ForEach-Object { [IO.Path]::GetFileName([string]$_.destination) })
    Add-SetDifferenceErrors -Errors $errors -Label 'Standalone tool allowlist' `
        -Expected @($standaloneTools | ForEach-Object { [string]$_.id }) `
        -Actual @($allowlistTools | ForEach-Object { [string]$_.componentId })
    Add-SetDifferenceErrors -Errors $errors -Label 'Plugin README allowlist' `
        -Expected @($plannedArchiveReadmes | ForEach-Object { [string]$_.id }) `
        -Actual @($allowlistReadmes | ForEach-Object { [string]$_.componentId })
    Add-SetDifferenceErrors -Errors $errors -Label 'Plugin companion allowlist' `
        -Expected @($plannedArchiveCompanions | ForEach-Object { "$($_.ComponentId)|$($_.Destination)" }) `
        -Actual @($allowlistCompanions | ForEach-Object {
            "$([string]$_.componentId)|$(([string]$_.destination).Replace('\', '/'))"
        })
    Add-SetDifferenceErrors -Errors $errors -Label 'Embedded tool executable allowlist' `
        -Expected @($plannedEmbeddedFiles | Where-Object Kind -eq 'embedded-tool-exe' | ForEach-Object {
            "$($_.ComponentId)|$($_.ArchiveComponentId)|$($_.Destination)"
        }) `
        -Actual @($allowlistEmbeddedExecutables | ForEach-Object {
            "$([string]$_.componentId)|$([string]$_.archiveComponentId)|$(([string]$_.destination).Replace('\', '/'))"
        })
    Add-SetDifferenceErrors -Errors $errors -Label 'Embedded tool README allowlist' `
        -Expected @($plannedEmbeddedFiles | Where-Object Kind -eq 'embedded-tool-readme' | ForEach-Object {
            "$($_.ComponentId)|$($_.ArchiveComponentId)|$($_.Destination)"
        }) `
        -Actual @($allowlistEmbeddedReadmes | ForEach-Object {
            "$([string]$_.componentId)|$([string]$_.archiveComponentId)|$(([string]$_.destination).Replace('\', '/'))"
        })

    foreach ($component in $includedPlugins) {
        $matches = @($allowlistPlugins | Where-Object { [string]$_.componentId -eq [string]$component.id })
        if ($matches.Count -eq 1 -and [string]$matches[0].version -ne [string]$component.targetVersion) {
            $errors.Add("Plugin $($component.id) allowlist version $($matches[0].version) does not match registry version $($component.targetVersion).")
        }
    }
    foreach ($component in $standaloneTools) {
        $matches = @($allowlistTools | Where-Object { [string]$_.componentId -eq [string]$component.id })
        if ($matches.Count -eq 1 -and
            ([string]$matches[0].version -ne [string]$component.targetVersion -or
             [IO.Path]::GetFileName([string]$matches[0].destination) -cne [string]$component.asset)) {
            $errors.Add("Standalone tool $($component.id) does not match its governed version and asset name.")
        }
    }
    foreach ($component in $plannedArchiveReadmes) {
        $matches = @($allowlistReadmes | Where-Object { [string]$_.componentId -eq [string]$component.id })
        if ($matches.Count -eq 1 -and
            ([string]$matches[0].source -cne [string]$component.archiveReadme.source -or
             [string]$matches[0].destination -cne [string]$component.archiveReadme.destination -or
             ([string]$matches[0].sha256).ToUpperInvariant() -cne [string]$component.archiveReadme.sha256)) {
            $errors.Add("Plugin $($component.id) README allowlist entry does not match its reviewed archive contract.")
        }
    }
    foreach ($companion in $plannedArchiveCompanions) {
        $matches = @($allowlistCompanions | Where-Object {
            [string]$_.componentId -eq $companion.ComponentId -and
            ([string]$_.destination).Replace('\', '/') -ceq $companion.Destination
        })
        if ($matches.Count -eq 1 -and
            (([string]$matches[0].source).Replace('\', '/') -cne $companion.Source -or
             ([string]$matches[0].sha256).ToUpperInvariant() -cne $companion.SHA256)) {
            $errors.Add("Plugin $($companion.ComponentId) companion allowlist entry does not match its governed archive contract.")
        }
    }
    foreach ($embeddedFile in $plannedEmbeddedFiles) {
        $matches = @($allowlist.entries | Where-Object {
            [string]$_.kind -eq $embeddedFile.Kind -and
            [string]$_.componentId -eq $embeddedFile.ComponentId -and
            [string]$_.archiveComponentId -eq $embeddedFile.ArchiveComponentId -and
            ([string]$_.destination).Replace('\', '/') -ceq $embeddedFile.Destination
        })
        if ($matches.Count -eq 1 -and
            (([string]$matches[0].source).Replace('\', '/') -cne $embeddedFile.Source -or
             ([string]$matches[0].sha256).ToUpperInvariant() -cne $embeddedFile.SHA256 -or
             ($embeddedFile.Kind -eq 'embedded-tool-exe' -and
              [string]$matches[0].version -ne $embeddedFile.Version))) {
            $errors.Add("Embedded tool file $($embeddedFile.ComponentId) does not match its governed archive contract.")
        }
    }

    if (($plannedArchiveReadmes.Count + $allowlistEmbeddedReadmes.Count) -eq 0) {
        if ([bool](Get-PropertyValue $allowlist.policy 'readmeIncluded' $false) -or
            [string](Get-PropertyValue $allowlist.policy 'readmeLocation' '') -ne 'repository-only') {
            $errors.Add('An allowlist without governed plugin READMEs must keep them repository-only.')
        }
    }
    elseif (-not [bool](Get-PropertyValue $allowlist.policy 'readmeIncluded' $false) -or
        [string](Get-PropertyValue $allowlist.policy 'readmeLocation' '') -ne 'selected-plugin-archives') {
        $errors.Add('Governed plugin READMEs require the selected-plugin-archives allowlist policy.')
    }

    $allowlistExpected = $allowlist.distribution.expectedGithubAssets
    $allowlistToolCount = [int](Get-PropertyValue $allowlistExpected 'standaloneTools' 0)
    if ([int]$allowlistExpected.individualPluginArchives -ne $includedPlugins.Count -or
        [int]$allowlistExpected.individualPatchFiles -ne $includedPatches.Count -or
        $allowlistToolCount -ne $standaloneTools.Count -or
        [int]$allowlistExpected.optionalBundles -ne [int]$plan.distribution.bundles -or
        [int]$allowlistExpected.total -ne $derivedTotal) {
        $errors.Add('Release allowlist asset counts do not match the next-release registry.')
    }
}

if ($errors.Count -ne 0) {
    throw ($errors -join [Environment]::NewLine)
}

if (-not [string]::IsNullOrWhiteSpace($WriteReleaseNotesPath)) {
    $resolvedNotes = [IO.Path]::GetFullPath($WriteReleaseNotesPath)
    if ((Test-Path -LiteralPath $resolvedNotes) -and -not $Force) {
        throw "Release notes output already exists; use -Force to replace it: $resolvedNotes"
    }
    $parent = Split-Path -Parent $resolvedNotes
    if (-not [string]::IsNullOrWhiteSpace($parent) -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent | Out-Null
    }
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("# RuffnecKk D2RLoader Suite $($plan.suite.targetVersion)")
    $lines.Add('')
    $lines.Add("Status: $($plan.suite.status); releaseReady=$([bool]$plan.suite.releaseReady).")
    foreach ($category in @('Added', 'Changed', 'Fixed', 'Removed', 'Deferred')) {
        $entries = @($components | Where-Object { [string]$_.releaseNoteCategory -eq $category } | Sort-Object name)
        if ($entries.Count -eq 0) { continue }
        $lines.Add('')
        $lines.Add("## $category")
        $lines.Add('')
        foreach ($entry in $entries) {
            $versionSuffix = if ($null -ne $entry.targetVersion) { " $($entry.targetVersion)" } else { '' }
            $lines.Add(('- {0}{1}: {2}' -f $entry.name, $versionSuffix, $entry.releaseNote))
        }
    }
    $lines.Add('')
    $lines.Add('## Release gates')
    $lines.Add('')
    $lines.Add("- $($plan.compatibility.releaseGate)")
    $lines.Add("- Expected GitHub assets: $derivedTotal ($($includedPlugins.Count) plugins, $($includedPatches.Count) patches, $($standaloneTools.Count) standalone tools, $($plan.distribution.bundles) bundles).")
    if ($embeddedTools.Count -ne 0) {
        $lines.Add("- Embedded tools: $($embeddedTools.Count), packaged only inside their governed plugin archives.")
    }
    $utf8NoBom = [Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText($resolvedNotes, ($lines -join "`n") + "`n", $utf8NoBom)
}

[pscustomobject]@{
    Plan = $resolvedPlan
    TargetVersion = [string]$plan.suite.targetVersion
    Status = [string]$plan.suite.status
    ReleaseReady = [bool]$plan.suite.releaseReady
    Plugins = $includedPlugins.Count
    Patches = $includedPatches.Count
    StandaloneTools = $standaloneTools.Count
    EmbeddedTools = $embeddedTools.Count
    Bundles = [int]$plan.distribution.bundles
    Assets = $derivedTotal
    AllowlistCompared = -not [string]::IsNullOrWhiteSpace($AllowlistPath)
    Result = 'VALID'
}
