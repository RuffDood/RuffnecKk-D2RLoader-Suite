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
if ([string]::IsNullOrWhiteSpace($AllowlistPath)) {
    $AllowlistPath = Join-Path $repositoryRoot 'manifests\release-allowlist.json'
}
$AllowlistPath = [IO.Path]::GetFullPath($AllowlistPath)

if (-not (Test-Path -LiteralPath $AllowlistPath -PathType Leaf)) {
    throw "Release allowlist not found: $AllowlistPath"
}

$allowlist = Get-Content -LiteralPath $AllowlistPath -Raw | ConvertFrom-Json
$distribution = $allowlist.distribution
if ([string]$distribution.model -ne 'modular-catalog' -or
    [string]$distribution.primaryDownloads -ne 'individual-components' -or
    [string]$distribution.canonicalPackagingRuntime -ne 'PowerShell 7') {
    throw 'Release allowlist does not declare the approved modular catalog contract.'
}
$expectedAssets = $distribution.expectedGithubAssets
if ([int]$expectedAssets.individualPluginArchives -ne 17 -or
    [int]$expectedAssets.individualPatchFiles -ne 19 -or
    [int]$expectedAssets.optionalBundles -ne 2 -or
    [int]$expectedAssets.total -ne 38) {
    throw 'Release allowlist does not declare the approved 17/19/2 GitHub asset counts.'
}
if ([bool]$allowlist.policy.readmeIncluded -or
    [string]$allowlist.policy.readmeLocation -ne 'repository-only' -or
    @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-readme' }).Count -ne 0) {
    throw 'README files must remain repository-only and must not be release assets.'
}
$pluginEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-dll' })
$pluginIds = @($pluginEntries | ForEach-Object { [string]$_.componentId })
if ($pluginEntries.Count -ne 17 -or @($pluginIds | Sort-Object -Unique).Count -ne 17) {
    throw "Expected 17 unique plugin DLL entries in the release allowlist; found $($pluginEntries.Count)."
}
$ownedEntries = @($allowlist.entries | Where-Object { [string]$_.kind -ne 'memory-patch-json' })
$tomlConfigEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'plugin-config-toml' })
$jsonConfigEntries = @($allowlist.entries | Where-Object { [string]$_.kind -eq 'loose-config-json' })
if ($tomlConfigEntries.Count -ne 15 -or $jsonConfigEntries.Count -ne 2) {
    throw "Expected 15 TOML and 2 JSON plugin configuration entries; found $($tomlConfigEntries.Count) and $($jsonConfigEntries.Count)."
}
foreach ($entry in $ownedEntries) {
    if ([string]$entry.componentId -notin $pluginIds) {
        throw "Release entry '$($entry.destination)' is not assigned to an approved plugin archive."
    }
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
    if ($slug -eq 'remote-stash') {
        if ($readmes.Count -ne 1 -or $readmes[0].Name -ne 'README.md') {
            $errors.Add('remote-stash must contain exactly its approved button and migration README.md.')
        }
    }
    elseif ($readmes.Count -ne 0) {
        $errors.Add("$slug contains an unapproved per-plugin README; documentation must stay central.")
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

    $sourceFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -File -Recurse -Include '*.cpp','*.c','*.h','*.hpp','*.rc')
    if ($sourceFiles.Count -eq 0) {
        $errors.Add("$slug has no native source files.")
        continue
    }
    $sourceText = ($sourceFiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"

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
        if ($getProcAddressMatches.Count -ne 1 -or -not $approvedD3D12Lookup) {
            $errors.Add(
                'floating-damage may use GetProcAddress exactly once, only for the deferred system D3D12CreateDevice lookup.')
        }
    }
    elseif ($getProcAddressMatches.Count -ne 0) {
        $errors.Add("$slug contains forbidden dependency marker 'GetProcAddress'.")
    }
    $pluginEntry = @($pluginEntries | Where-Object { [string]$_.componentId -eq $pluginId })[0]
    $expectedPluginInfoId = if ($null -ne $pluginEntry.PSObject.Properties['pluginInfoId']) {
        [string]$pluginEntry.pluginInfoId
    }
    else {
        $pluginId
    }
    if ($expectedPluginInfoId -notmatch '^[a-z0-9-]+$') {
        $errors.Add("$slug declares invalid PluginInfo id $expectedPluginInfoId in the release allowlist.")
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

    $tomlFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -Filter '*.toml' -File -Recurse)
    $jsonFiles = @(Get-ChildItem -LiteralPath $pluginDirectory -Filter '*.json' -File -Recurse)
    $manifestConfigs = @($allowlist.entries | Where-Object {
        [string]$_.kind -in 'plugin-config-toml', 'loose-config-json' -and
        [string]$_.componentId -eq $pluginId
    })
    if ($manifestConfigs.Count -ne 1) {
        $errors.Add("$slug must have exactly one public configuration entry in the release allowlist.")
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
    else {
        $errors.Add("$slug exposes no TOML or JSON configuration contract.")
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
    DeclaredPlugins = $pluginEntries.Count
    PresentPlugins = $present
    RequireAll = [bool]$RequireAll
    FloatingDamageBinaryChecked = $floatingDamageBinaryChecked
    Result = 'VALID'
}
