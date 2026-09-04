[CmdletBinding(DefaultParameterSetName = 'Path')]
param(
    [Parameter(Mandatory = $true, Position = 0, ParameterSetName = 'Path')]
    [ValidateNotNullOrEmpty()]
    [string[]]$Path,

    [Parameter(Mandatory = $true, ParameterSetName = 'SelfTest')]
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$forbiddenPackagedBuildIdentifiers = @('92777', '93847', '93787')

function Get-PrintableSegments {
    param([Parameter(Mandatory)][byte[]]$Bytes)

    $segments = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($encoding in @([Text.Encoding]::ASCII, [Text.Encoding]::Unicode)) {
        $text = $encoding.GetString($Bytes)
        foreach ($match in [regex]::Matches($text, '[\x20-\x7E]{6,}')) {
            [void]$segments.Add($match.Value)
        }
    }
    return @($segments)
}

function Test-PluginDllBytes {
    param(
        [Parameter(Mandatory)][byte[]]$Bytes,
        [Parameter(Mandatory)][string]$Label
    )

    $errors = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($segment in @(Get-PrintableSegments -Bytes $Bytes)) {
        foreach ($identifier in $forbiddenPackagedBuildIdentifiers) {
            if ($segment -match "(?<!\d)$([regex]::Escape($identifier))(?!\d)") {
                [void]$errors.Add(
                    "$Label embeds forbidden D2R build identifier '$identifier'.")
            }
        }

        # Diagnostic-only build names and ISC12's explicit denial of an
        # allowlist are permitted. Remove those phrases before searching for
        # actual rejection language.
        $restrictionCandidate = [regex]::Replace(
            $segment,
            '(?i)diagnostic\s+only|without\s+(?:a\s+)?version\s+allowlist',
            '')
        $mentionsD2RBuild = $restrictionCandidate -match
            '(?i)\bD2R\b.{0,160}\bbuild(?:s|-name)?\b'
        $usesRestrictionLanguage = $restrictionCandidate -match
            '(?i)\b(?:only|unsupported|reject(?:ed|ing)?|refus(?:e|ed|ing|al)?|allow(?:ed|list)?|support(?:ed|ing)?)\b'
        if ($mentionsD2RBuild -and $usesRestrictionLanguage) {
            [void]$errors.Add(
                "$Label embeds D2R build-version restriction text: '$segment'.")
        }
    }

    return @($errors)
}

function Invoke-PackagedPluginBuildPolicy {
    param([Parameter(Mandatory)][string[]]$ArtifactPaths)

    $errors = [Collections.Generic.List[string]]::new()
    $inspectedDlls = 0
    foreach ($artifactPath in $ArtifactPaths) {
        $resolvedPath = (Resolve-Path -LiteralPath $artifactPath).Path
        $extension = [IO.Path]::GetExtension($resolvedPath)
        if ($extension -ieq '.dll') {
            $dllName = [IO.Path]::GetFileName($resolvedPath)
            if ($dllName -notmatch '^d2rl-ruffneckk-[a-z0-9-]+\.dll$') {
                throw "DLL is not a RuffnecKk plugin artifact: '$resolvedPath'."
            }
            $inspectedDlls++
            foreach ($errorMessage in @(Test-PluginDllBytes `
                -Bytes ([IO.File]::ReadAllBytes($resolvedPath)) `
                -Label $dllName)) {
                $errors.Add($errorMessage)
            }
            continue
        }

        if ($extension -ine '.zip') {
            throw "Expected a RuffnecKk plugin DLL or ZIP artifact: '$resolvedPath'."
        }

        $archive = [IO.Compression.ZipFile]::OpenRead($resolvedPath)
        try {
            $pluginEntries = @($archive.Entries | Where-Object {
                $_.Name -match '^d2rl-ruffneckk-[a-z0-9-]+\.dll$'
            })
            if ($pluginEntries.Count -eq 0) {
                $errors.Add("$resolvedPath contains no RuffnecKk plugin DLL.")
                continue
            }
            foreach ($entry in $pluginEntries) {
                $entryStream = $entry.Open()
                $memoryStream = [IO.MemoryStream]::new()
                try {
                    $entryStream.CopyTo($memoryStream)
                    $dllBytes = $memoryStream.ToArray()
                }
                finally {
                    $memoryStream.Dispose()
                    $entryStream.Dispose()
                }
                $inspectedDlls++
                $label = "$([IO.Path]::GetFileName($resolvedPath))::$($entry.FullName)"
                foreach ($errorMessage in @(Test-PluginDllBytes `
                    -Bytes $dllBytes `
                    -Label $label)) {
                    $errors.Add($errorMessage)
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }

    if ($errors.Count -ne 0) {
        throw "Packaged plugin build-compatibility policy failed:`n - $($errors -join "`n - ")"
    }

    return [pscustomobject]@{
        Artifacts = $ArtifactPaths.Count
        PluginDlls = $inspectedDlls
        Result = 'VALID'
    }
}

function New-PluginFixtureZip {
    param(
        [Parameter(Mandatory)][string]$ZipPath,
        [Parameter(Mandatory)][byte[]]$Payload
    )

    $fileStream = [IO.File]::Open(
        $ZipPath,
        [IO.FileMode]::CreateNew,
        [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None)
    $archive = [IO.Compression.ZipArchive]::new(
        $fileStream,
        [IO.Compression.ZipArchiveMode]::Create,
        $false)
    try {
        $entry = $archive.CreateEntry(
            'plugins/d2rl-ruffneckk-policy-fixture.dll',
            [IO.Compression.CompressionLevel]::NoCompression)
        $entryStream = $entry.Open()
        try {
            $entryStream.Write($Payload, 0, $Payload.Length)
        }
        finally {
            $entryStream.Dispose()
        }
    }
    finally {
        $archive.Dispose()
        $fileStream.Dispose()
    }
}

if ($SelfTest) {
    $releaseGeneratorPath = Join-Path $PSScriptRoot 'New-Release.ps1'
    $releaseGeneratorText = Get-Content -LiteralPath $releaseGeneratorPath -Raw
    $sourceGateCall = $releaseGeneratorText.IndexOf(
        '& $suiteSourcePolicyValidator',
        [StringComparison]::Ordinal)
    $outputCreation = $releaseGeneratorText.IndexOf(
        'New-Item -ItemType Directory -Path $absoluteOutputDirectory',
        [StringComparison]::Ordinal)
    $pluginBundleCreation = $releaseGeneratorText.IndexOf(
        '$pluginBundleHash = New-VerifiedZip',
        [StringComparison]::Ordinal)
    $packagedGateCall = $releaseGeneratorText.IndexOf(
        '& $packagedPluginPolicyValidator',
        [StringComparison]::Ordinal)
    if ($sourceGateCall -lt 0 -or $outputCreation -lt 0 -or
        $sourceGateCall -ge $outputCreation) {
        throw 'New-Release.ps1 must run Test-Suite.ps1 before creating release output.'
    }
    if ($pluginBundleCreation -lt 0 -or $packagedGateCall -lt 0 -or
        $packagedGateCall -le $pluginBundleCreation) {
        throw 'New-Release.ps1 must inspect packaged plugin ZIPs after creating the plugin bundle.'
    }

    $temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $fixtureRoot = Join-Path $temporaryBase `
        "ruffneckk-packaged-build-policy-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    try {
        $allowedZip = Join-Path $fixtureRoot 'allowed.zip'
        New-PluginFixtureZip `
            -ZipPath $allowedZip `
            -Payload ([Text.Encoding]::ASCII.GetBytes(
                'MapSense: D2R build-name %s is diagnostic only; validating the complete fail-closed fingerprint. ISC12 validates the native foundation without a version allowlist.'))
        $allowed = Invoke-PackagedPluginBuildPolicy -ArtifactPaths @($allowedZip)
        if ($allowed.PluginDlls -ne 1 -or $allowed.Result -ne 'VALID') {
            throw 'Allowed diagnostic-only fixture did not pass the packaged plugin policy.'
        }

        $publishedRegressionZip = Join-Path $fixtureRoot 'published-regression.zip'
        New-PluginFixtureZip `
            -ZipPath $publishedRegressionZip `
            -Payload ([Text.Encoding]::ASCII.GetBytes(
                'ResistanceFloor: only governed D2R builds 92777 and 93847 are supported.'))
        $publishedRegressionRejected = $false
        try {
            Invoke-PackagedPluginBuildPolicy `
                -ArtifactPaths @($publishedRegressionZip) | Out-Null
        }
        catch {
            if ($_.Exception.Message -match '92777' -and
                $_.Exception.Message -match 'build-version restriction') {
                $publishedRegressionRejected = $true
            }
            else {
                throw
            }
        }
        if (-not $publishedRegressionRejected) {
            throw 'Published Resistance Floor regression fixture was not rejected.'
        }

        $futureRegressionZip = Join-Path $fixtureRoot 'future-regression.zip'
        New-PluginFixtureZip `
            -ZipPath $futureRegressionZip `
            -Payload ([Text.Encoding]::ASCII.GetBytes(
                'Load refused: unsupported D2R build 99999.'))
        $futureRegressionRejected = $false
        try {
            Invoke-PackagedPluginBuildPolicy `
                -ArtifactPaths @($futureRegressionZip) | Out-Null
        }
        catch {
            if ($_.Exception.Message -match 'build-version restriction') {
                $futureRegressionRejected = $true
            }
            else {
                throw
            }
        }
        if (-not $futureRegressionRejected) {
            throw 'Future D2R build restriction fixture was not rejected.'
        }

        [pscustomobject]@{
            ReleaseGateWiring = 'VALID'
            AllowedDiagnostic = 'VALID'
            PublishedRegression = 'REJECTED'
            FutureRegression = 'REJECTED'
            Result = 'VALID'
        }
    }
    finally {
        $resolvedFixtureRoot = [IO.Path]::GetFullPath($fixtureRoot)
        if ($resolvedFixtureRoot.StartsWith(
                $temporaryBase,
                [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolvedFixtureRoot -Recurse -Force
        }
    }
    return
}

Invoke-PackagedPluginBuildPolicy -ArtifactPaths $Path
