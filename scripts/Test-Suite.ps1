[CmdletBinding()]
param(
    [switch]$RequireAll,

    [string]$AllowlistPath,

    [string]$FloatingDamageDll,

    [string]$DumpbinPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$pluginsRoot = Join-Path $repositoryRoot 'plugins'
$hasReleaseAllowlist = -not [string]::IsNullOrWhiteSpace($AllowlistPath)
$expectedSdkV4Components = @(
    'ruffneckk-vendor-stock-refresh'
)

if ($hasReleaseAllowlist) {
    $AllowlistPath = [IO.Path]::GetFullPath($AllowlistPath)
    if (-not (Test-Path -LiteralPath $AllowlistPath -PathType Leaf)) {
        throw "Release allowlist not found: $AllowlistPath"
    }
    $allowlist = Get-Content -LiteralPath $AllowlistPath -Raw | ConvertFrom-Json
    $sourcePolicyCatalog = $AllowlistPath
$compatibility = $allowlist.suite.compatibility
if ([string]$compatibility.policy -ne 'native-fingerprint-fail-closed' -or
    @($compatibility.runtimeQualified | Where-Object {
        [string]$_.version -eq '3.3' -and [int]$_.build -eq 93847
    }).Count -ne 1 -or
    @($compatibility.nativeEquivalent | Where-Object {
        [string]$_.version -eq '3.2' -and [int]$_.build -eq 92777
    }).Count -ne 1 -or
    $allowlist.suite.PSObject.Properties.Name -contains 'targetGameBuild' -or
    $allowlist.suite.PSObject.Properties.Name -contains 'validatedGameBuilds') {
    throw 'Suite compatibility must report qualified builds without a D2R build-name allowlist.'
}
$distribution = $allowlist.distribution
if ([string]$distribution.model -ne 'modular-catalog' -or
    [string]$distribution.primaryDownloads -ne 'individual-components' -or
    [string]$distribution.canonicalPackagingRuntime -ne 'PowerShell 7') {
    throw 'Release allowlist does not declare the approved modular catalog contract.'
}
$sdkV4Commit = '6eb8f8b6192868214706bd6d528c5294f2f551b7'
$sdkV4Overrides = @($allowlist.suite.pluginSdkOverrides)
$actualSdkV4Components = @($sdkV4Overrides | ForEach-Object { [string]$_.componentId })
if ($sdkV4Overrides.Count -ne $expectedSdkV4Components.Count -or
    @($actualSdkV4Components | Sort-Object -Unique).Count -ne $expectedSdkV4Components.Count -or
    @($expectedSdkV4Components | Where-Object { $_ -notin $actualSdkV4Components }).Count -ne 0 -or
    @($sdkV4Overrides | Where-Object {
        [string]$_.version -ne 'v4' -or [string]$_.commit -ne $sdkV4Commit
    }).Count -ne 0) {
    throw 'Suite PluginSDK v4 overrides must pin Vendor Stock Refresh.'
}
$expectedAssets = $distribution.expectedGithubAssets
$expectedCounts = $allowlist.policy.expectedCounts
$declaredPluginCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-dll' }).Count
$declaredPatchCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'memory-patch-json' }).Count
$declaredToolCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'standalone-tool' }).Count
$declaredPluginCompanionCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-companion-exe' }).Count
$declaredEmbeddedToolExecutableCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-exe' }).Count
$declaredEmbeddedToolReadmeCount = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-readme' }).Count
$expectedPluginCompanionCount = if ($null -ne $expectedCounts.PSObject.Properties['pluginCompanionExecutables']) {
    [int]$expectedCounts.pluginCompanionExecutables
} else { 0 }
$expectedEmbeddedToolExecutableCount = if ($null -ne $expectedCounts.PSObject.Properties['embeddedToolExecutables']) {
    [int]$expectedCounts.embeddedToolExecutables
} else { 0 }
$expectedEmbeddedToolReadmeCount = if ($null -ne $expectedCounts.PSObject.Properties['embeddedToolReadmes']) {
    [int]$expectedCounts.embeddedToolReadmes
} else { 0 }
$expectedToolCount = if ($null -ne $expectedAssets.PSObject.Properties['standaloneTools']) {
    [int]$expectedAssets.standaloneTools
} else { 0 }
$derivedAssetTotal = $declaredPluginCount + $declaredPatchCount + $declaredToolCount + [int]$expectedAssets.optionalBundles
if ([int]$expectedAssets.individualPluginArchives -ne $declaredPluginCount -or
    [int]$expectedAssets.individualPatchFiles -ne $declaredPatchCount -or
    $expectedToolCount -ne $declaredToolCount -or
    [int]$expectedAssets.optionalBundles -ne 2 -or
    [int]$expectedAssets.total -ne $derivedAssetTotal) {
    throw 'Release allowlist GitHub asset counts do not match its component entries.'
}
if ($declaredPluginCompanionCount -ne $expectedPluginCompanionCount -or
    $declaredEmbeddedToolExecutableCount -ne $expectedEmbeddedToolExecutableCount -or
    $declaredEmbeddedToolReadmeCount -ne $expectedEmbeddedToolReadmeCount -or
    [int]$expectedCounts.total -ne @($allowlist.entries).Count) {
    throw 'Release allowlist entry counts do not match its companion, embedded-tool and total entry contract.'
}
$seenDestinations = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in @($allowlist.entries)) {
    $kind = [string]$entry.kind
    $source = ([string]$entry.source).Replace('\', '/')
    $destination = ([string]$entry.destination).Replace('\', '/')
    $basename = [IO.Path]::GetFileName($destination)
    $archiveComponentId = if ($null -ne $entry.PSObject.Properties['archiveComponentId']) {
        [string]$entry.archiveComponentId
    }
    elseif ($null -ne $entry.PSObject.Properties['componentId']) {
        [string]$entry.componentId
    }
    else { '' }
    $destinationKey = if ($kind -in 'plugin-readme', 'embedded-tool-exe', 'embedded-tool-readme') {
        "$archiveComponentId|$destination"
    } else { $destination }
    if (-not $seenDestinations.Add($destinationKey)) {
        throw "Release allowlist contains duplicate destination '$destinationKey'."
    }
    switch ($kind) {
        'plugin-dll' {
            if ($destination -cne "plugins/$basename" -or $basename -notmatch '^d2rl-ruffneckk-[a-z0-9-]+\.dll$') {
                throw "Plugin DLL is not D2RMM-importable from '$destination'."
            }
        }
        'plugin-companion-exe' {
            if ($source -cne $destination -or $destination -cne "plugins/$basename" -or
                [IO.Path]::GetExtension($basename) -ine '.exe') {
                throw "Plugin companion is not D2RMM-importable from '$destination'."
            }
        }
        'plugin-readme' {
            if ($destination -cne 'README.md') { throw "Invalid plugin README path '$destination'." }
        }
        'embedded-tool-exe' {
            if ($source -notmatch '^tools/[A-Za-z0-9][A-Za-z0-9._-]*\.exe$' -or
                [IO.Path]::GetExtension($destination) -ine '.exe' -or
                [string]::IsNullOrWhiteSpace((Split-Path -Parent $destination))) {
                throw "Invalid embedded tool executable path '$source' -> '$destination'."
            }
        }
        'embedded-tool-readme' {
            if ($source -notmatch '^tools/[a-z0-9-]+/README\.md$' -or
                $basename -cne 'README.md' -or
                [string]::IsNullOrWhiteSpace((Split-Path -Parent $destination))) {
                throw "Invalid embedded tool README path '$source' -> '$destination'."
            }
        }
        'plugin-config-toml' {
            if ($destination -cne "config/$([string]$entry.componentId).toml") {
                throw "Plugin TOML is not D2RMM-importable from '$destination'."
            }
        }
        'loose-config-json' {
            if ($source -cne $destination -or $destination -cne "config/$basename" -or
                [IO.Path]::GetExtension($basename) -ine '.json') {
                throw "Plugin JSON is not D2RMM-importable from '$destination'."
            }
        }
        'memory-patch-json' {
            if ($source -cne $destination -or $destination -cne "patches/$basename" -or
                [IO.Path]::GetExtension($basename) -ine '.json') {
                throw "Memory patch is not D2RMM-importable from '$destination'."
            }
        }
        'standalone-tool' {
            if ($source -cne $destination -or $destination -cne $basename -or
                [IO.Path]::GetExtension($basename) -ine '.exe') {
                throw "Invalid standalone tool path '$destination'."
            }
        }
        default { throw "Unsupported release entry kind '$kind'." }
    }
}
$pluginReadmeEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-readme' })
$embeddedToolExecutableEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-exe' })
$embeddedToolReadmeEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'embedded-tool-readme' })
if (($pluginReadmeEntries.Count + $embeddedToolReadmeEntries.Count) -eq 0) {
    if ([bool]$allowlist.policy.readmeIncluded -or
        [string]$allowlist.policy.readmeLocation -ne 'repository-only') {
        throw 'A release without governed plugin READMEs must keep them repository-only.'
    }
}
elseif (-not [bool]$allowlist.policy.readmeIncluded -or
    [string]$allowlist.policy.readmeLocation -ne 'selected-plugin-archives' -or
    @($pluginReadmeEntries.componentId | Sort-Object -Unique).Count -ne $pluginReadmeEntries.Count) {
    throw 'Governed plugin READMEs require unique components and the selected-plugin-archives policy.'
}
$pluginEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-dll' })
$pluginIds = @($pluginEntries | ForEach-Object { [string]$_.componentId })
if ($pluginEntries.Count -ne $declaredPluginCount -or @($pluginIds | Sort-Object -Unique).Count -ne $pluginEntries.Count) {
    throw "Every plugin DLL entry in the release allowlist must have a unique component id; found $($pluginEntries.Count)."
}
$ownedEntries = @($allowlist.entries | Where-Object {
    [string]$_.kind -notin 'memory-patch-json', 'standalone-tool', 'embedded-tool-exe', 'embedded-tool-readme'
})
$tomlConfigEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-config-toml' })
$jsonConfigEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'loose-config-json' })
if (($tomlConfigEntries.Count + $jsonConfigEntries.Count) -gt $pluginEntries.Count) {
    throw "Plugins may have at most one justified public configuration; found $($tomlConfigEntries.Count + $jsonConfigEntries.Count) for $($pluginEntries.Count) plugins."
}
foreach ($entry in $ownedEntries) {
    if ([string]$entry.componentId -notin $pluginIds) {
        throw "Release entry '$($entry.destination)' is not assigned to an approved plugin archive."
    }
}
if ($embeddedToolExecutableEntries.Count -ne $embeddedToolReadmeEntries.Count) {
    throw 'Every embedded tool requires exactly one executable and one README.'
}
foreach ($executable in $embeddedToolExecutableEntries) {
    $componentId = [string]$executable.componentId
    $archiveComponentId = [string]$executable.archiveComponentId
    $readmes = @($embeddedToolReadmeEntries | Where-Object {
        [string]$_.componentId -eq $componentId -and
        [string]$_.archiveComponentId -eq $archiveComponentId
    })
    if ($readmes.Count -ne 1 -or $archiveComponentId -notin $pluginIds -or
        [string]::IsNullOrWhiteSpace([string]$executable.version)) {
        throw "Embedded tool '$componentId' must have one README, one semantic version and one approved plugin archive owner."
    }
}
}
else {
    $sourcePolicyCatalog = Join-Path $repositoryRoot 'tests\data\compatibility\native-writes-3.2.92777.json'
    if (-not (Test-Path -LiteralPath $sourcePolicyCatalog -PathType Leaf)) {
        throw "Public source-policy catalog not found: $sourcePolicyCatalog"
    }
    try {
        $sourcePolicyDocument = Get-Content -LiteralPath $sourcePolicyCatalog -Raw | ConvertFrom-Json
    }
    catch {
        throw "Invalid public source-policy catalog '$sourcePolicyCatalog': $($_.Exception.Message)"
    }
    $pluginEntries = @($sourcePolicyDocument.suitePlugins | ForEach-Object {
        [pscustomobject]@{ componentId = [string]$_.id }
    })
    $pluginIds = @($pluginEntries | ForEach-Object { [string]$_.componentId })
    if ($pluginIds.Count -eq 0 -or @($pluginIds | Sort-Object -Unique).Count -ne $pluginIds.Count) {
        throw 'The public source-policy catalog is empty or contains duplicate plugin IDs.'
    }
    $pluginReadmeEntries = @()
}

$errors = [System.Collections.Generic.List[string]]::new()
$present = 0

foreach ($entry in $pluginEntries) {
    $pluginId = [string]$entry.componentId
    if ($pluginId -notmatch '^ruffneckk-([a-z0-9-]+)$') {
        $errors.Add("Invalid plugin id in manifest: $pluginId")
        continue
    }

    $slug = $Matches[1]
    $pluginDirectory = Join-Path $pluginsRoot $slug
    if (-not (Test-Path -LiteralPath $pluginDirectory -PathType Container)) {
        if ($RequireAll) {
            $errors.Add("Missing plugin directory: plugins/$slug")
        }
        continue
    }
    $present++

    $readmes = @(Get-ChildItem -LiteralPath $pluginDirectory -File -Recurse |
        Where-Object Name -Match '^README(?:\..+)?$')
    if ($hasReleaseAllowlist) {
        $releaseReadmes = @($pluginReadmeEntries | Where-Object { [string]$_.componentId -eq $pluginId })
        if ($releaseReadmes.Count -eq 1) {
            $expectedReadmeSource = "plugins/$slug/README.md"
            if ($readmes.Count -ne 1 -or $readmes[0].Name -cne 'README.md' -or
                [string]$releaseReadmes[0].source -cne $expectedReadmeSource -or
                [string]$releaseReadmes[0].destination -cne 'README.md' -or
                ([string]$releaseReadmes[0].sha256).ToUpperInvariant() -cne
                    (Get-FileHash -Algorithm SHA256 -LiteralPath $readmes[0].FullName).Hash) {
                $errors.Add("$slug must contain exactly the reviewed README.md pinned by its release entry.")
            }
        }
        elseif ($releaseReadmes.Count -gt 1) {
            $errors.Add("$slug has more than one release README entry.")
        }
        elseif ($slug -eq 'remote-stash') {
            if ($readmes.Count -ne 1 -or $readmes[0].Name -ne 'README.md') {
                $errors.Add('remote-stash must contain exactly its approved button and migration README.md.')
            }
        }
        elseif ($readmes.Count -ne 0) {
            $errors.Add("$slug contains an unapproved per-plugin README; documentation must stay central.")
        }
    }

    $nestedBuilds = @(Get-ChildItem -LiteralPath $pluginDirectory -Directory -Recurse |
        Where-Object Name -Match '^build(?:-.+)?$')
    if ($nestedBuilds.Count -ne 0) {
        $errors.Add("$slug contains a nested build directory.")
    }

    $cmakePath = Join-Path $pluginDirectory 'CMakeLists.txt'
    if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
        $errors.Add("$slug has no CMakeLists.txt.")
        continue
    }
    $cmakeText = Get-Content -LiteralPath $cmakePath -Raw
    $expectedOutput = "d2rl-$pluginId"
    if ($cmakeText -notmatch [regex]::Escape("OUTPUT_NAME `"$expectedOutput`"")) {
        $errors.Add("$slug does not declare OUTPUT_NAME $expectedOutput.")
    }
    if ($pluginId -in $expectedSdkV4Components) {
        if ($cmakeText -notmatch [regex]::Escape('RuffnecKk::CommonV4')) {
            $errors.Add("$slug is pinned to PluginSDK v4 but does not link RuffnecKk::CommonV4.")
        }
    }
    elseif ($cmakeText -match [regex]::Escape('RuffnecKk::CommonV4')) {
        $errors.Add("$slug links PluginSDK v4 without a governed release override.")
    }

    $sourceFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -File -Recurse -Include '*.cpp','*.c','*.h','*.hpp','*.rc' |
        Where-Object {
            $_.Name -notmatch '(?i)test' -and
            $_.FullName -notmatch '(?i)[\\/]tests[\\/]' -and
            $_.FullName -notmatch '(?i)[\\/]build[^\\/]*[\\/]'
        })
    if ($sourceFiles.Count -eq 0) {
        $errors.Add("$slug has no native source files.")
        continue
    }
    $sourceText = ($sourceFiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    $runtimeSourceRoot = Join-Path $pluginDirectory 'src'
    $runtimeSourceFiles = @(Get-ChildItem -LiteralPath $runtimeSourceRoot -File -Recurse -Include '*.cpp','*.c','*.h','*.hpp','*.rc' |
        Where-Object {
            $_.Name -notmatch '(?i)test' -and
            $_.FullName -notmatch '(?i)[\\/]tests[\\/]' -and
            $_.FullName -notmatch '(?i)[\\/]build[^\\/]*[\\/]'
        })
    $runtimeSourceText = ($runtimeSourceFiles | ForEach-Object {
        Get-Content -LiteralPath $_.FullName -Raw
    }) -join "`n"

    foreach ($forbidden in @('ModScopedOnly', 'plugin-items.dll', 'plugin-levels.dll', 'plugin-misc.dll', 'plugin-quests.dll', 'plugin-skills.dll')) {
        if ($sourceText.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            $errors.Add("$slug contains forbidden dependency marker '$forbidden'.")
        }
    }
    $getProcAddressMatches = @([regex]::Matches(
        $sourceText,
        'GetProcAddress\s*\('))
    if ($slug -eq 'floating-damage') {
        $approvedD3D12Lookup =
            $sourceText -match 'GetProcAddress\s*\(\s*d3d12Module\s*,\s*"D3D12CreateDevice"\s*\)'
        $approvedMapSenseLookup =
            $sourceText -match 'GetProcAddress\s*\(\s*mapSense\s*,\s*"RuffnecKkMapSenseGetOverlayHostApi"\s*\)'
        if (($getProcAddressMatches.Count -ne 2) -or
            (-not $approvedD3D12Lookup) -or
            (-not $approvedMapSenseLookup)) {
            $errors.Add(
                'floating-damage may resolve only D3D12CreateDevice and the optional versioned MapSense overlay-host API.')
        }
    }
    elseif ($getProcAddressMatches.Count -ne 0) {
        $errors.Add("$slug contains forbidden dependency marker 'GetProcAddress'.")
    }
    $pluginEntry = @($pluginEntries | Where-Object { [string]$_.componentId -eq $pluginId })[0]
    $expectedPluginInfoId = if ($hasReleaseAllowlist -and $null -ne $pluginEntry.PSObject.Properties['pluginInfoId']) {
        [string]$pluginEntry.pluginInfoId
    }
    elseif (-not $hasReleaseAllowlist -and $pluginId -in @(
        'ruffneckk-bulk-currency-deposit',
        'ruffneckk-resistance-floor'
    )) {
        $pluginId.Substring('ruffneckk-'.Length)
    }
    else {
        $pluginId
    }
    if ($expectedPluginInfoId -notmatch '^[a-z0-9-]+$') {
        $errors.Add("$slug declares invalid PluginInfo id $expectedPluginInfoId.")
    }
    elseif ($sourceText -notmatch [regex]::Escape(".id = `"$expectedPluginInfoId`"")) {
        $errors.Add("$slug does not expose PluginInfo id $expectedPluginInfoId.")
    }
    if ($sourceText -notmatch '\.author\s*=\s*"RuffnecKk"') {
        $errors.Add("$slug does not expose author RuffnecKk.")
    }
    if ($sourceText -notmatch '\.apiVersion\s*=\s*D2RL_PLUGIN_API_VERSION') {
        $errors.Add("$slug does not expose the SDK API version macro.")
    }
    $forbiddenBuildPolicyPatterns = @(
        '(?i)(?:strcmp|strncmp)\s*\(\s*(?:D2RL::)?GetBuildName\s*\(',
        '(?i)(?:strcmp|strncmp)\s*\(\s*[A-Za-z_][A-Za-z0-9_]*build[A-Za-z0-9_]*',
        '(?i)(?:strcmp|strncmp)\s*\([^,\r\n]+,\s*[A-Za-z_][A-Za-z0-9_]*build[A-Za-z0-9_]*',
        '(?i)(?:string|string_view)\s*\(\s*[A-Za-z_][A-Za-z0-9_]*build[A-Za-z0-9_]*\s*\)\s*(?:==|!=|<=|>=|<|>)',
        '(?i)\b[A-Za-z_][A-Za-z0-9_]*build(?:name)?\b\s*(?:==|!=|<=|>=|<|>)',
        '(?i)\b(?:atoi|atol|strtol|strtoul|from_chars)\s*\([^;\r\n]*\b[A-Za-z_][A-Za-z0-9_]*build[A-Za-z0-9_]*',
        '(?i)\bonly\s+(?:governed\s+)?D2R\s+builds?\b',
        '(?i)\bunsupported\s+D2R\s+build(?:\s+identity)?\b',
        '(?i)\b(?:Is)?(?:Allowed|Supported)Build\b'
    )
    if (@($forbiddenBuildPolicyPatterns | Where-Object {
        $runtimeSourceText -match $_
    }).Count -ne 0) {
        $errors.Add("$slug contains a forbidden D2R build-name allowlist.")
    }
    $hasBuildDiagnostic = $runtimeSourceText -match 'GetBuildName\s*\('
    $hasFingerprintDiagnostic = $runtimeSourceText -match 'validating (?:the complete )?native fingerprint' -or
        $runtimeSourceText -match 'validating the native foundation' -or
        $runtimeSourceText -match 'native fingerprint accepted' -or
        $runtimeSourceText -match 'complete fail-closed fingerprint'
    if (-not $hasBuildDiagnostic -or -not $hasFingerprintDiagnostic) {
        $errors.Add("$slug must log the observed build name and gate native work through its complete fingerprint.")
    }

    $tomlFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -Filter '*.toml' -File -Recurse)
    $jsonFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -Filter '*.json' -File -Recurse)
    $manifestConfigs = @()
    if ($hasReleaseAllowlist) {
        $manifestConfigs = @($allowlist.entries | Where-Object {
            [string]$_.kind -in 'plugin-config-toml', 'loose-config-json' -and
            [string]$_.componentId -eq $pluginId
        })
    }
    if ($manifestConfigs.Count -gt 1) {
        $errors.Add("$slug may have at most one justified public configuration entry in the release allowlist.")
    }
    if ($tomlFiles.Count -gt 0) {
        if ($tomlFiles.Count -ne 1 -or $tomlFiles[0].Name -ne "$pluginId.toml") {
            $errors.Add("$slug must contain exactly config/$pluginId.toml when it uses TOML.")
        }
        if ($cmakeText -notmatch 'd2rlplugin_embed_config') {
            $errors.Add("$slug does not embed its TOML through the SDK helper.")
        }
        if ($manifestConfigs.Count -eq 1) {
            $manifestConfig = $manifestConfigs[0]
            $expectedSource = "plugins/$slug/config/$pluginId.toml"
            $expectedDestination = "config/$pluginId.toml"
            $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $tomlFiles[0].FullName).Hash
            if ([string]$manifestConfig.kind -ne 'plugin-config-toml' -or
                [string]$manifestConfig.source -cne $expectedSource -or
                [string]$manifestConfig.destination -cne $expectedDestination -or
                ([string]$manifestConfig.sha256).ToUpperInvariant() -cne $actualHash) {
                $errors.Add("$slug TOML is not pinned to its canonical source, destination, and current SHA-256.")
            }
        }
    }
    elseif ($jsonFiles.Count -gt 0) {
        if ($cmakeText -match 'd2rlplugin_embed_config') {
            $errors.Add("$slug uses JSON but also invokes the TOML embed helper.")
        }
        if ($manifestConfigs.Count -eq 1 -and [string]$manifestConfigs[0].kind -ne 'loose-config-json') {
            $errors.Add("$slug JSON configuration is not declared as loose-config-json.")
        }
    }
    elseif ($manifestConfigs.Count -ne 0) {
        $errors.Add("$slug has a release configuration entry but exposes no governed TOML or JSON configuration.")
    }
}

if ($errors.Count -ne 0) {
    throw ($errors -join [Environment]::NewLine)
}

$floatingDamageBinaryChecked = $false
if (-not [string]::IsNullOrWhiteSpace($FloatingDamageDll) -or
    -not [string]::IsNullOrWhiteSpace($DumpbinPath)) {
    if ([string]::IsNullOrWhiteSpace($FloatingDamageDll) -or
        [string]::IsNullOrWhiteSpace($DumpbinPath)) {
        throw 'FloatingDamageDll and DumpbinPath must be supplied together.'
    }

    $resolvedDll = (Resolve-Path -LiteralPath $FloatingDamageDll).Path
    $resolvedDumpbin = (Resolve-Path -LiteralPath $DumpbinPath).Path
    $dumpbinOutput = @(
        & $resolvedDumpbin /NOLOGO /DEPENDENTS /IMPORTS $resolvedDll 2>&1
    )
    if ($LASTEXITCODE -ne 0) {
        throw "dumpbin failed while inspecting '$resolvedDll'."
    }
    if (($dumpbinOutput -join "`n") -match '(?i)\bd3d12\.dll\b') {
        throw @"
Floating Damage must not import d3d12.dll statically. D2R ships its own D3D12
loader chain, so the plugin must keep its deferred runtime lookup.
"@
    }
    $floatingDamageBinaryChecked = $true
}

[pscustomobject]@{
    Repository = $repositoryRoot
    SourcePolicyCatalog = $sourcePolicyCatalog
    DeclaredPlugins = $pluginEntries.Count
    PresentPlugins = $present
    RequireAll = [bool]$RequireAll
    ReleaseAllowlistCompared = $hasReleaseAllowlist
    FloatingDamageBinaryChecked = $floatingDamageBinaryChecked
    Result = 'VALID'
}
