[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SourceRoot,
    [Parameter(Mandatory = $true)][string]$AllowlistPath,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Normalize-ReleasePath {
    param([Parameter(Mandatory)][string]$Path)
    return $Path.Replace('\', '/')
}

function Assert-RelativeReleasePath {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Label)
    if ([string]::IsNullOrWhiteSpace($Path) -or [IO.Path]::IsPathRooted($Path) -or $Path.Contains(':')) {
        throw "$Label must be a non-empty relative path: '$Path'."
    }
    $segments = (Normalize-ReleasePath $Path).Split('/')
    if ($segments -contains '' -or $segments -contains '.' -or $segments -contains '..') {
        throw "$Label contains an unsafe path segment: '$Path'."
    }
}

function Resolve-ReleaseSource {
    param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$RelativePath)
    $rootPrefix = $Root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $absolute = [IO.Path]::GetFullPath((Join-Path $Root $RelativePath))
    if (-not $absolute.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source path escapes SourceRoot: '$RelativePath'."
    }
    if (-not (Test-Path -LiteralPath $absolute -PathType Leaf)) {
        throw "Allowlisted source file does not exist: '$RelativePath'."
    }
    return $absolute
}

function Get-ZipEntrySha256 {
    param([Parameter(Mandatory)][System.IO.Compression.ZipArchiveEntry]$Entry)
    $stream = $Entry.Open()
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-', '')
    }
    finally {
        $algorithm.Dispose()
        $stream.Dispose()
    }
}

function New-VerifiedZip {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][object[]]$Items)
    if ($Items.Count -eq 0) { throw "Archive '$Path' has no entries." }
    if (Test-Path -LiteralPath $Path) { throw "Output already exists: '$Path'." }

    $fileStream = $null
    $archive = $null
    try {
        $fileStream = [IO.File]::Open($Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $archive = [IO.Compression.ZipArchive]::new($fileStream, [IO.Compression.ZipArchiveMode]::Create, $true)
        foreach ($item in @($Items | Sort-Object Destination)) {
            # Stored entries keep release ZIP bytes identical across the .NET
            # Framework and modern .NET compression implementations.
            $entry = $archive.CreateEntry([string]$item.Destination, [IO.Compression.CompressionLevel]::NoCompression)
            $entry.LastWriteTime = [DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [TimeSpan]::Zero)
            $inputStream = [IO.File]::OpenRead([string]$item.Source)
            $entryStream = $entry.Open()
            try { $inputStream.CopyTo($entryStream) }
            finally {
                $entryStream.Dispose()
                $inputStream.Dispose()
            }
        }
    }
    catch {
        if ($null -ne $archive) { $archive.Dispose(); $archive = $null }
        if ($null -ne $fileStream) { $fileStream.Dispose(); $fileStream = $null }
        if (Test-Path -LiteralPath $Path) { Remove-Item -LiteralPath $Path -Force }
        throw
    }
    finally {
        if ($null -ne $archive) { $archive.Dispose() }
        if ($null -ne $fileStream) { $fileStream.Dispose() }
    }

    $readArchive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $actualEntries = @($readArchive.Entries)
        if ($actualEntries.Count -ne $Items.Count -or @($actualEntries | Where-Object { [string]::IsNullOrEmpty($_.Name) }).Count -ne 0) {
            throw "Archive '$Path' entry count or directory policy mismatch."
        }
        foreach ($archiveEntry in $actualEntries) {
            $entryPath = Normalize-ReleasePath $archiveEntry.FullName
            $expected = @($Items | Where-Object Destination -eq $entryPath)
            if ($expected.Count -ne 1) { throw "Unexpected archive entry '$entryPath' in '$Path'." }
            if ((Get-ZipEntrySha256 -Entry $archiveEntry) -ne [string]$expected[0].SHA256) {
                throw "Archive SHA-256 mismatch for '$entryPath' in '$Path'."
            }
        }
    }
    finally { $readArchive.Dispose() }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$resolvedSourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedAllowlist = (Resolve-Path -LiteralPath $AllowlistPath).Path
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$absoluteOutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)

try { $document = Get-Content -LiteralPath $resolvedAllowlist -Raw | ConvertFrom-Json }
catch { throw "Invalid release allowlist JSON '$resolvedAllowlist': $($_.Exception.Message)" }

if ([int]$document.schemaVersion -ne 1 -or [string]$document.suite.id -ne 'ruffneckk-d2rloader-suite') {
    throw 'Unsupported release allowlist.'
}
if ([string]$document.suite.version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'The Suite release requires a semantic x.y.z version.'
}
if ([string]$document.distribution.model -ne 'modular-catalog' -or [string]$document.distribution.primaryDownloads -ne 'individual-components') {
    throw 'The release manifest must use the approved modular catalog model.'
}
if ([bool]$document.policy.readmeIncluded -or
    [string]$document.policy.readmeLocation -ne 'repository-only') {
    throw 'README files must remain repository-only and must not be release assets.'
}
if (-not [bool]$document.policy.requireSha256) { throw 'Every public release entry must have a pinned SHA-256.' }

$entries = @($document.entries)
$expectedCounts = $document.policy.expectedCounts
if ($entries.Count -ne [int]$expectedCounts.total) {
    throw "Expected $($expectedCounts.total) allowlist entries; found $($entries.Count)."
}
$kindCounts = @{
    'plugin-dll' = [int]$expectedCounts.pluginDlls
    'plugin-readme' = [int]$expectedCounts.pluginReadmes
    'loose-config-json' = [int]$expectedCounts.looseConfigJson
    'plugin-config-toml' = [int]$expectedCounts.pluginConfigToml
    'memory-patch-json' = [int]$expectedCounts.memoryPatchJson
}
$deniedBasenames = @($document.policy.deniedBasenames | ForEach-Object { ([string]$_).ToLowerInvariant() })
$seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$validated = [Collections.Generic.List[object]]::new()

foreach ($entry in $entries) {
    $kind = [string]$entry.kind
    if (-not $kindCounts.ContainsKey($kind)) { throw "Unsupported release entry kind '$kind'." }
    $source = Normalize-ReleasePath ([string]$entry.source)
    $destination = Normalize-ReleasePath ([string]$entry.destination)
    Assert-RelativeReleasePath -Path $source -Label 'Source path'
    Assert-RelativeReleasePath -Path $destination -Label 'Archive destination'
    if ($kind -notin 'plugin-readme', 'plugin-config-toml' -and
        -not $source.Equals($destination, [StringComparison]::Ordinal)) {
        throw "Source and destination must match: '$source' vs '$destination'."
    }
    if (-not $seen.Add($destination)) { throw "Duplicate archive destination '$destination'." }
    $basename = [IO.Path]::GetFileName($destination)
    if ($deniedBasenames -contains $basename.ToLowerInvariant()) { throw "Release entry '$destination' is forbidden." }
    if ($kind -ne 'plugin-readme' -and $destination -match '(?i)(^|/)readme(?:\.[^/]+)?$') {
        throw "README requires the plugin-readme release kind: '$destination'."
    }

    $componentId = ''
    if ($kind -ne 'memory-patch-json') {
        $componentId = [string]$entry.componentId
        if ($componentId -notmatch '^ruffneckk-[a-z0-9-]+$') {
            throw "Entry '$destination' requires a valid plugin componentId."
        }
    }

    $extension = [IO.Path]::GetExtension($destination)
    switch ($kind) {
        'plugin-dll' {
            if ($extension -ine '.dll' -or $basename -notmatch '^d2rl-ruffneckk-[a-z0-9-]+\.dll$') {
                throw "Invalid plugin release path '$destination'."
            }
        }
        'plugin-readme' {
            if ($destination -cne 'README.md' -or [IO.Path]::GetFileName($source) -cne 'README.md') {
                throw "Invalid plugin README release path '$source' -> '$destination'."
            }
        }
        'plugin-config-toml' {
            if ($extension -ine '.toml' -or $destination -cne "config/$componentId.toml") {
                throw "Invalid plugin TOML release destination '$destination'."
            }
            $expectedSource = "plugins/$($componentId.Substring('ruffneckk-'.Length))/config/$componentId.toml"
            if ($source -cne $expectedSource) {
                throw "Invalid plugin TOML source '$source'; expected '$expectedSource'."
            }
        }
        { $_ -in 'loose-config-json', 'memory-patch-json' } {
            if ($extension -ine '.json') { throw "Invalid JSON release path '$destination'." }
        }
    }

    $version = ''
    if ($kind -eq 'plugin-dll') {
        $version = [string]$entry.version
        if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Plugin '$componentId' requires a semantic version." }
    }

    $expectedHash = ([string]$entry.sha256).Trim().ToUpperInvariant()
    if ($expectedHash -notmatch '^[0-9A-F]{64}$') { throw "Entry '$destination' requires a valid SHA-256." }
    $sourceBase = if ($kind -in 'plugin-readme', 'plugin-config-toml') {
        $repositoryRoot
    }
    else {
        $resolvedSourceRoot
    }
    $absoluteSource = Resolve-ReleaseSource -Root $sourceBase -RelativePath $source
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $absoluteSource).Hash
    if ($actualHash -ne $expectedHash) {
        throw "SHA-256 mismatch for '$source': expected $expectedHash, found $actualHash."
    }
    $validated.Add([pscustomobject]@{
        Source = $absoluteSource; Destination = $destination; SHA256 = $expectedHash
        Kind = $kind; ComponentId = $componentId; Version = $version
    })
}

foreach ($kind in $kindCounts.Keys) {
    $actual = @($validated | Where-Object Kind -eq $kind).Count
    if ($actual -ne $kindCounts[$kind]) { throw "Expected $($kindCounts[$kind]) '$kind' entries; found $actual." }
}
$pluginDlls = @($validated | Where-Object Kind -eq 'plugin-dll')
$pluginIds = @($pluginDlls | ForEach-Object ComponentId)
if (@($pluginIds | Sort-Object -Unique).Count -ne $pluginDlls.Count) {
    throw 'Every plugin archive requires one unique plugin componentId.'
}
foreach ($entry in @($validated | Where-Object Kind -ne 'memory-patch-json')) {
    if ($pluginIds -notcontains $entry.ComponentId) {
        throw "Entry '$($entry.Destination)' references unknown plugin '$($entry.ComponentId)'."
    }
}
$pluginReadmes = @($validated | Where-Object Kind -eq 'plugin-readme')
if ($pluginReadmes.Count -ne 0) {
    throw 'README files must not be release assets.'
}
$pluginConfigs = @($validated | Where-Object Kind -in 'plugin-config-toml', 'loose-config-json')
if ($pluginConfigs.Count -ne $pluginDlls.Count) {
    throw "Every plugin archive requires exactly one configuration; found $($pluginConfigs.Count) for $($pluginDlls.Count) plugins."
}
foreach ($pluginId in $pluginIds) {
    $ownedConfigs = @($pluginConfigs | Where-Object ComponentId -eq $pluginId)
    if ($ownedConfigs.Count -ne 1) {
        throw "Plugin '$pluginId' requires exactly one configuration entry; found $($ownedConfigs.Count)."
    }
}

$expectedAssets = $document.distribution.expectedGithubAssets
if ([int]$expectedAssets.individualPluginArchives -ne $pluginDlls.Count -or
    [int]$expectedAssets.individualPatchFiles -ne [int]$expectedCounts.memoryPatchJson -or
    [int]$expectedAssets.optionalBundles -ne 2 -or
    [int]$expectedAssets.total -ne 38) {
    throw 'The modular asset counts do not match the approved 17/19/2 contract.'
}

if (Test-Path -LiteralPath $absoluteOutputDirectory) {
    throw "Output directory already exists; refusing to overwrite '$absoluteOutputDirectory'."
}
New-Item -ItemType Directory -Path $absoluteOutputDirectory | Out-Null
$generated = [Collections.Generic.List[object]]::new()
try {
    foreach ($plugin in @($pluginDlls | Sort-Object ComponentId)) {
        $slug = $plugin.ComponentId.Substring('ruffneckk-'.Length)
        $assetName = "RuffnecKk-$slug-v$($plugin.Version).zip"
        $assetPath = Join-Path $absoluteOutputDirectory $assetName
        $assetHash = New-VerifiedZip -Path $assetPath -Items @($validated | Where-Object ComponentId -eq $plugin.ComponentId)
        $generated.Add([pscustomobject]@{ Name = $assetName; Path = $assetPath; SHA256 = $assetHash; Kind = 'individual-plugin' })
    }

    foreach ($patch in @($validated | Where-Object Kind -eq 'memory-patch-json' | Sort-Object Destination)) {
        $assetName = [IO.Path]::GetFileName($patch.Destination)
        $assetPath = Join-Path $absoluteOutputDirectory $assetName
        if (Test-Path -LiteralPath $assetPath) { throw "Duplicate generated asset '$assetName'." }
        Copy-Item -LiteralPath $patch.Source -Destination $assetPath
        $assetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $assetPath).Hash
        if ($assetHash -ne $patch.SHA256) { throw "Copied patch hash mismatch for '$assetName'." }
        $generated.Add([pscustomobject]@{ Name = $assetName; Path = $assetPath; SHA256 = $assetHash; Kind = 'individual-patch' })
    }

    $suiteVersion = [string]$document.suite.version
    $pluginBundleName = "RuffnecKk-All-Plugins-v$suiteVersion.zip"
    $pluginBundlePath = Join-Path $absoluteOutputDirectory $pluginBundleName
    $pluginBundleHash = New-VerifiedZip -Path $pluginBundlePath -Items @(
        $validated | Where-Object { $_.Kind -ne 'memory-patch-json' -and $_.Kind -ne 'plugin-readme' }
    )
    $generated.Add([pscustomobject]@{ Name = $pluginBundleName; Path = $pluginBundlePath; SHA256 = $pluginBundleHash; Kind = 'plugin-bundle' })

    $patchBundleName = "RuffnecKk-All-Patches-v$suiteVersion.zip"
    $patchBundlePath = Join-Path $absoluteOutputDirectory $patchBundleName
    $patchBundleHash = New-VerifiedZip -Path $patchBundlePath -Items @($validated | Where-Object Kind -eq 'memory-patch-json')
    $generated.Add([pscustomobject]@{ Name = $patchBundleName; Path = $patchBundlePath; SHA256 = $patchBundleHash; Kind = 'patch-bundle' })

    if ($generated.Count -ne [int]$expectedAssets.total -or
        @($generated.Name | Sort-Object -Unique).Count -ne $generated.Count) {
        throw "Expected $($expectedAssets.total) unique generated GitHub assets; found $($generated.Count)."
    }
    if (@(Get-ChildItem -LiteralPath $absoluteOutputDirectory -File).Count -ne $generated.Count) {
        throw 'The output directory contains files outside the generated modular catalog.'
    }
    if (@(Get-ChildItem -LiteralPath $absoluteOutputDirectory -File | Where-Object Name -Match '(?i)^readme(?:\..+)?$').Count -ne 0) {
        throw 'README files must be reviewed beside the generated catalog, not generated as release assets.'
    }

    [pscustomobject]@{
        OutputDirectory = $absoluteOutputDirectory
        PluginArchives = @($generated | Where-Object Kind -eq 'individual-plugin').Count
        PatchFiles = @($generated | Where-Object Kind -eq 'individual-patch').Count
        OptionalBundles = @($generated | Where-Object Kind -Match 'bundle$').Count
        Assets = $generated.Count
        Result = 'VALID'
    }
}
catch {
    if (Test-Path -LiteralPath $absoluteOutputDirectory -PathType Container) {
        foreach ($file in @(Get-ChildItem -LiteralPath $absoluteOutputDirectory -File)) {
            Remove-Item -LiteralPath $file.FullName -Force
        }
        if (@(Get-ChildItem -LiteralPath $absoluteOutputDirectory -Force).Count -eq 0) {
            Remove-Item -LiteralPath $absoluteOutputDirectory -Force
        }
    }
    throw
}
