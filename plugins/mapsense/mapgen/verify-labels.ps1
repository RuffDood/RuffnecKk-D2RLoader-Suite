param(
    [Parameter(Mandatory = $false)]
    [string]$Executable,

    [Parameter(Mandatory = $false)]
    [string]$ActiveDataRoot,

    [Parameter(Mandatory = $false)]
    [string]$NodeExecutable = 'node'
)

$ErrorActionPreference = 'Stop'
$scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($Executable)) {
    $Executable = Join-Path $scriptDirectory 'zig-out/bin/RuffnecKkMapSenseMapgen.exe'
}

$expectedWaypoints = @(9, 9, 9, 3, 9)
$expectedPortals = @(1, 0, 0, 0, 3)
$currentLevels = @(1, 40, 75, 103, 109)
$seeds = @(6, 1337, 1395822899, 2147483647)

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Map generator not found: $Executable"
}

function Invoke-MapGenerator {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Seed,
        [Parameter(Mandatory = $true)]
        [int]$Act,
        [Parameter(Mandatory = $true)]
        [int]$CurrentLevel,
        [Parameter(Mandatory = $false)]
        [string]$ExcelRoot,
        [Parameter(Mandatory = $false)]
        [string]$TilesRoot
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = "labels $Seed 2 $Act $CurrentLevel"
    if (-not [string]::IsNullOrWhiteSpace($ExcelRoot)) {
        $startInfo.Arguments += " --excel-root `"$ExcelRoot`""
    }
    if (-not [string]::IsNullOrWhiteSpace($TilesRoot)) {
        $startInfo.Arguments += " --tiles-root `"$TilesRoot`""
    }
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start map generator for seed=$Seed act=$Act"
    }

    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.Result
    $stderr = $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()

    $lines = @(($stdout + $stderr) -split '\r?\n' | Where-Object { $_ -ne '' })
    return [pscustomobject]@{
        ExitCode = $exitCode
        Lines = $lines
    }
}

function Invoke-GeometryBinary {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Seed,
        [Parameter(Mandatory = $true)]
        [int]$Act,
        [Parameter(Mandatory = $true)]
        [string]$Directory,
        [Parameter(Mandatory = $true)]
        [string]$FileName,
        [Parameter(Mandatory = $false)]
        [string]$ExcelRoot,
        [Parameter(Mandatory = $false)]
        [string]$TilesRoot,
        [Parameter(Mandatory = $false)]
        [int]$MaximumMilliseconds = 30000
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = "geometry-binary $Seed 2 $Act `"$FileName`""
    if (-not [string]::IsNullOrWhiteSpace($ExcelRoot)) {
        $startInfo.Arguments += " --excel-root `"$ExcelRoot`""
    }
    if (-not [string]::IsNullOrWhiteSpace($TilesRoot)) {
        $startInfo.Arguments += " --tiles-root `"$TilesRoot`""
    }
    $startInfo.WorkingDirectory = $Directory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start geometry generator for seed=$Seed act=$Act"
    }
    try { $process.PriorityClass = 'BelowNormal' } catch {}
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.WaitForExit($MaximumMilliseconds)) {
        try { $process.Kill($true) } catch {}
        $process.WaitForExit()
        $stopwatch.Stop()
        $process.Dispose()
        throw "Geometry generation exceeded ${MaximumMilliseconds}ms for seed=$Seed act=$Act"
    }
    $stopwatch.Stop()
    $diagnostics = $stdoutTask.Result + $stderrTask.Result
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) {
        throw "Geometry generation failed for seed=$Seed act=$Act diagnostics=$diagnostics"
    }
    return [int]$stopwatch.ElapsedMilliseconds
}

function Invoke-PrimaryAtlasBinary {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Seed,
        [Parameter(Mandatory = $true)]
        [int]$Act,
        [Parameter(Mandatory = $true)]
        [int]$CurrentLevel,
        [Parameter(Mandatory = $true)]
        [string]$Directory,
        [Parameter(Mandatory = $true)]
        [string]$FileName,
        [Parameter(Mandatory = $false)]
        [string]$ExcelRoot,
        [Parameter(Mandatory = $false)]
        [string]$TilesRoot,
        [Parameter(Mandatory = $false)]
        [int]$MaximumMilliseconds = 30000
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments =
        "atlas-binary $Seed 2 $Act $CurrentLevel `"$FileName`""
    if (-not [string]::IsNullOrWhiteSpace($ExcelRoot)) {
        $startInfo.Arguments += " --excel-root `"$ExcelRoot`""
    }
    if (-not [string]::IsNullOrWhiteSpace($TilesRoot)) {
        $startInfo.Arguments += " --tiles-root `"$TilesRoot`""
    }
    $startInfo.WorkingDirectory = $Directory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Could not start primary atlas generator for seed=$Seed act=$Act"
    }
    try { $process.PriorityClass = 'BelowNormal' } catch {}
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    if (-not $process.WaitForExit($MaximumMilliseconds)) {
        try { $process.Kill($true) } catch {}
        $process.WaitForExit()
        $stopwatch.Stop()
        $process.Dispose()
        throw "Primary atlas generation exceeded ${MaximumMilliseconds}ms for seed=$Seed act=$Act"
    }
    $stopwatch.Stop()
    $lines = @(($stdoutTask.Result + $stderrTask.Result) -split '\r?\n' |
        Where-Object { $_ -ne '' })
    $exitCode = $process.ExitCode
    $process.Dispose()
    if ($exitCode -ne 0) {
        throw "Primary atlas generation failed for seed=$Seed act=$Act"
    }
    return [pscustomobject]@{
        ElapsedMilliseconds = [int]$stopwatch.ElapsedMilliseconds
        Lines = $lines
    }
}

function Test-GeometryBinary {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [uint32]$Seed,
        [Parameter(Mandatory = $true)]
        [int]$Act
    )

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 32 -or
        [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4) -ne 'MSA1' -or
        [BitConverter]::ToUInt16($bytes, 4) -ne 2 -or
        [BitConverter]::ToUInt16($bytes, 6) -ne 1 -or
        [BitConverter]::ToUInt32($bytes, 8) -ne $Seed -or
        $bytes[12] -ne 2 -or $bytes[13] -ne $Act -or
        $bytes[14] -ne 0 -or $bytes[15] -ne 0) {
        throw "Invalid MSA1 v2 header for seed=$Seed act=$Act"
    }
    $levelCount = [BitConverter]::ToUInt32($bytes, 16)
    $expectedCellCount = [BitConverter]::ToUInt32($bytes, 20)
    if ($levelCount -eq 0 -or $expectedCellCount -eq 0) {
        throw "Empty geometry artifact for seed=$Seed act=$Act"
    }

    $offset = 32
    [uint64]$parsedCells = 0
    [uint64]$floorTreeCells = 0
    [uint64]$wallTreeCells = 0
    [uint64]$raisedCells = 0
    [uint64]$wallWithoutRaise = 0
    for ($levelIndex = 0; $levelIndex -lt $levelCount; ++$levelIndex) {
        if ($offset + 12 -gt $bytes.Length) {
            throw "Truncated MSA1 level record for seed=$Seed act=$Act"
        }
        $levelId = [BitConverter]::ToInt32($bytes, $offset)
        $layer = $bytes[$offset + 4]
        $cellsInLevel = [BitConverter]::ToUInt32($bytes, $offset + 8)
        if ($levelId -le 0 -or $layer -gt 3 -or $cellsInLevel -eq 0 -or
            $bytes[$offset + 5] -ne 0 -or $bytes[$offset + 6] -ne 0 -or
            $bytes[$offset + 7] -ne 0) {
            throw "Invalid MSA1 level record for seed=$Seed act=$Act"
        }
        $offset += 12
        for ($cellIndex = 0; $cellIndex -lt $cellsInLevel; ++$cellIndex) {
            if ($offset + 16 -gt $bytes.Length) {
                throw "Truncated MSA1 cell record for seed=$Seed act=$Act"
            }
            $frame = [BitConverter]::ToInt32($bytes, $offset)
            $tileX = [BitConverter]::ToInt32($bytes, $offset + 4)
            $tileY = [BitConverter]::ToInt32($bytes, $offset + 8)
            $wallTree = $bytes[$offset + 12]
            $raised = $bytes[$offset + 13]
            if ($frame -lt 0 -or $tileX -lt 0 -or $tileY -lt 0 -or
                $wallTree -gt 1 -or $raised -gt 1 -or
                $bytes[$offset + 14] -ne 0 -or $bytes[$offset + 15] -ne 0) {
                throw "Invalid MSA1 cell record for seed=$Seed act=$Act"
            }
            if ($wallTree -eq 1) {
                ++$wallTreeCells
                if ($raised -eq 0) { ++$wallWithoutRaise }
            } else {
                ++$floorTreeCells
            }
            if ($raised -eq 1) { ++$raisedCells }
            ++$parsedCells
            $offset += 16
        }
    }
    if ($offset -ne $bytes.Length -or $parsedCells -ne $expectedCellCount -or
        $floorTreeCells -eq 0 -or $wallTreeCells -eq 0 -or
        $wallWithoutRaise -eq 0) {
        throw "MSA1 floor/wall provenance contract failed for seed=$Seed act=$Act"
    }
    return [pscustomobject]@{
        Cells = $parsedCells
        FloorTree = $floorTreeCells
        WallTree = $wallTreeCells
        Raised = $raisedCells
    }
}

$temporaryRoot = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::GetTempPath())
$validationDirectoryName =
    "mapsense-geometry-validation-" + [guid]::NewGuid().ToString('N')
$validationRoot = [System.IO.Path]::GetFullPath(
    (Join-Path -Path $temporaryRoot -ChildPath $validationDirectoryName))
if (-not $validationRoot.StartsWith(
        $temporaryRoot,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw 'Resolved geometry validation directory escaped the temporary root'
}
New-Item -ItemType Directory -Path $validationRoot | Out-Null

try {
foreach ($seed in $seeds) {
    for ($act = 0; $act -lt 5; ++$act) {
        $firstResult = Invoke-MapGenerator -Seed $seed -Act $act -CurrentLevel $currentLevels[$act]
        if ($firstResult.ExitCode -ne 0) {
            throw "Label generation failed for seed=$seed act=$act"
        }
        $first = @($firstResult.Lines)

        $secondResult = Invoke-MapGenerator -Seed $seed -Act $act -CurrentLevel $currentLevels[$act]
        if ($secondResult.ExitCode -ne 0) {
            throw "Repeated label generation failed for seed=$seed act=$act"
        }
        $second = @($secondResult.Lines)

        $firstStable = @($first | Where-Object { $_ -match '^MS1 [HLREWP] ' })
        $secondStable = @($second | Where-Object { $_ -match '^MS1 [HLREWP] ' })
        if (@(Compare-Object $firstStable $secondStable).Count -ne 0) {
            throw "Non-deterministic MS1 records for seed=$seed act=$act"
        }

        $headers = @($first | Where-Object { $_ -match '^MS1 H ' })
        if ($headers.Count -ne 1) {
            throw "MS1 header count mismatch for seed=$seed act=$act"
        }
        $headerFields = "$($headers[0])" -split ' '
        if ($headerFields.Count -ne 7 -or
            [int]$headerFields[2] -ne 3 -or
            [int]$headerFields[3] -ne $seed -or
            [int]$headerFields[4] -ne 2 -or
            [int]$headerFields[5] -ne $act -or
            [int]$headerFields[6] -ne $currentLevels[$act]) {
            throw "Invalid MS1 v3 header for seed=$seed act=$act"
        }

        $waypoints = @($first | Where-Object { $_ -match '^MS1 W ' })
        if ($waypoints.Count -ne $expectedWaypoints[$act]) {
            throw "Waypoint count mismatch for seed=$seed act=$act"
        }

        $portalPairs = @{}
        $portals = @($first | Where-Object { $_ -match '^MS1 P ' })
        if ($portals.Count -ne $expectedPortals[$act]) {
            throw "Red portal count mismatch for seed=$seed act=$act"
        }
        foreach ($line in $portals) {
            $fields = "$line" -split ' '
            if ($fields.Count -ne 7 -or
                [int]$fields[4] -lt 0 -or
                [int]$fields[5] -lt 0 -or
                [int]$fields[6] -le 0) {
                throw "Invalid red portal record for seed=$seed act=$act"
            }
            $key = "$($fields[2])>$($fields[3])"
            if ($portalPairs.ContainsKey($key)) {
                throw "Duplicate red portal for seed=$seed act=$act pair=$key"
            }
            $portalPairs[$key] = @([int]$fields[4], [int]$fields[5])
        }
        if ($act -eq 4) {
            $expectedPortalPairs = @('111>125', '112>126', '117>127')
            if (@(Compare-Object @($portalPairs.Keys | Sort-Object) $expectedPortalPairs).Count -ne 0) {
                throw "Act V permanent portal destinations regressed for seed=$seed"
            }
        }
        if ($act -eq 0 -and
            @($portalPairs.Keys).Count -eq 1 -and
            -not $portalPairs.ContainsKey('4>38')) {
            throw "Act I Tristram portal destination regressed for seed=$seed"
        }

        $seams = @{}
        $seamCount = 0
        foreach ($line in @($first | Where-Object { $_ -match '^MS1 E .* 1$' })) {
            $fields = "$line" -split ' '
            $key = "$($fields[2])>$($fields[3])"
            if ($seams.ContainsKey($key)) {
                throw "Duplicate physical seam for seed=$seed act=$act pair=$key"
            }
            $seams[$key] = @([int]$fields[4], [int]$fields[5])
            ++$seamCount
        }

        foreach ($key in @($seams.Keys)) {
            $parts = $key -split '>'
            $reverse = "$($parts[1])>$($parts[0])"
            if (-not $seams.ContainsKey($reverse)) {
                throw "Missing reciprocal seam for seed=$seed act=$act pair=$key"
            }
            $source = $seams[$key]
            $target = $seams[$reverse]
            $distance = [Math]::Abs($source[0] - $target[0]) +
                [Math]::Abs($source[1] - $target[1])
            if ($distance -ne 1) {
                throw "Non-adjacent reciprocal seam for seed=$seed act=$act pair=$key"
            }
        }

        if ($seed -eq 1395822899 -and $act -eq 2) {
            $spiderToFlayer = $seams['76>78']
            $flayerToSpider = $seams['78>76']
            $knownSeamMatches = $null -ne $spiderToFlayer -and
                $spiderToFlayer[0] -eq 5000 -and
                $spiderToFlayer[1] -eq 4268 -and
                $null -ne $flayerToSpider -and
                $flayerToSpider[0] -eq 4999 -and
                $flayerToSpider[1] -eq 4268
            if (-not $knownSeamMatches) {
                throw 'Known Spider Forest/Flayer Jungle physical seam regressed'
            }
        }

        if ($seed -eq 1337 -and $act -eq 0) {
            $tamoeToMonastery = $seams['7>26']
            $monasteryToTamoe = $seams['26>7']
            $knownFacadeMatches = $null -ne $tamoeToMonastery -and
                $tamoeToMonastery[0] -eq 15140 -and
                $tamoeToMonastery[1] -eq 5077 -and
                $null -ne $monasteryToTamoe -and
                $monasteryToTamoe[0] -eq 15140 -and
                $monasteryToTamoe[1] -eq 5078
            if (-not $knownFacadeMatches) {
                throw 'Known Tamoe Highland/Monastery Gate facade regressed'
            }
            $tristramPortal = $portalPairs['4>38']
            if ($null -eq $tristramPortal -or
                $tristramPortal[0] -ne 5179 -or
                $tristramPortal[1] -ne 5299) {
                throw 'Known Stony Field/Tristram portal anchor regressed'
            }
        }

        $summaries = @($first | Where-Object { $_ -match '^MS1 Z ' })
        if ($summaries.Count -ne 1) {
            throw "MS1 summary count mismatch for seed=$seed act=$act"
        }
        $summary = $summaries[0]
        $summaryFields = "$summary" -split ' '
        if ($summaryFields.Count -ne 8 -or
            [int]$summaryFields[4] -ne $waypoints.Count -or
            [int]$summaryFields[5] -ne $portals.Count) {
            throw "Invalid MS1 v3 summary for seed=$seed act=$act"
        }
        $firstGeometryName = "seed-$seed-act-$act-a.msa"
        $secondGeometryName = "seed-$seed-act-$act-b.msa"
        $firstGeometryElapsed = Invoke-GeometryBinary -Seed $seed -Act $act -Directory $validationRoot -FileName $firstGeometryName
        $secondGeometryElapsed = Invoke-GeometryBinary -Seed $seed -Act $act -Directory $validationRoot -FileName $secondGeometryName
        $firstGeometryPath = Join-Path $validationRoot $firstGeometryName
        $secondGeometryPath = Join-Path $validationRoot $secondGeometryName
        $geometry = Test-GeometryBinary -Path $firstGeometryPath -Seed ([uint32]$seed) -Act $act
        $firstHash = (Get-FileHash -LiteralPath $firstGeometryPath -Algorithm SHA256).Hash
        $secondHash = (Get-FileHash -LiteralPath $secondGeometryPath -Algorithm SHA256).Hash
        if ($firstHash -ne $secondHash) {
            throw "Non-deterministic MSA1 geometry for seed=$seed act=$act"
        }
        if ($seed -eq 1337 -and $act -eq 2) {
            $combinedGeometryName =
                "seed-$seed-act-$act-primary-atlas.msa"
            $combined = Invoke-PrimaryAtlasBinary `
                -Seed $seed `
                -Act $act `
                -CurrentLevel $currentLevels[$act] `
                -Directory $validationRoot `
                -FileName $combinedGeometryName
            $combinedStable = @($combined.Lines | Where-Object {
                $_ -match '^MS1 [HLREWP] '
            })
            if (@(Compare-Object $firstStable $combinedStable).Count -ne 0) {
                throw "Combined primary-atlas labels differ for seed=$seed act=$act"
            }
            $combinedGeometryPath = Join-Path $validationRoot $combinedGeometryName
            $combinedGeometry = Test-GeometryBinary `
                -Path $combinedGeometryPath `
                -Seed ([uint32]$seed) `
                -Act $act
            $combinedHash = (Get-FileHash `
                -LiteralPath $combinedGeometryPath `
                -Algorithm SHA256).Hash
            if ($combinedHash -ne $firstHash -or
                $combinedGeometry.Cells -ne $geometry.Cells) {
                throw "Combined primary-atlas geometry differs for seed=$seed act=$act"
            }
            Write-Output "PASS primary-atlas seed=$seed act=$act labels=$($combinedStable.Count) geometry=$($combinedGeometry.Cells) elapsed-ms=$($combined.ElapsedMilliseconds)"
        }
        Write-Output "PASS seed=$seed act=$act seams=$seamCount portals=$($portals.Count) geometry=$($geometry.Cells) floor/wall/raised=$($geometry.FloorTree)/$($geometry.WallTree)/$($geometry.Raised) geometry-ms=$firstGeometryElapsed/$secondGeometryElapsed $summary"
    }
}

if (-not [string]::IsNullOrWhiteSpace($ActiveDataRoot)) {
    $activeDataRoot = [System.IO.Path]::GetFullPath($ActiveDataRoot)
    $activeExcel = Join-Path $activeDataRoot 'excel'
    $activeTiles = Join-Path $activeDataRoot 'tiles'
    if (-not (Test-Path -LiteralPath $activeExcel -PathType Container) -or
        -not (Test-Path -LiteralPath $activeTiles -PathType Container)) {
        throw "Active data root must contain excel and tiles: $activeDataRoot"
    }

    $activeResult = Invoke-MapGenerator `
        -Seed 1337 `
        -Act 4 `
        -CurrentLevel 109 `
        -ExcelRoot $activeExcel `
        -TilesRoot $activeTiles
    if ($activeResult.ExitCode -ne 0) {
        throw 'Active-mod label generation failed'
    }
    $activeEdge = @($activeResult.Lines | Where-Object {
        $_ -match '^MS1 E 109 138 [0-9]+ [0-9]+ 0$'
    })
    $activeWaypoints = @($activeResult.Lines | Where-Object {
        $_ -match '^MS1 W '
    })
    if ($activeEdge.Count -ne 1 -or $activeWaypoints.Count -ne 9) {
        throw 'Active-mod exact custom entrance/waypoint contract failed'
    }
    Write-Output "PASS active-mod exact-entry=$($activeEdge[0]) waypoints=$($activeWaypoints.Count)"

    # The runtime passes these roots to geometry generation too. This matrix
    # prevents a fast vanilla-only gate from hiding an active-mod watchdog
    # regression such as the former universal five-second timeout.
    foreach ($seed in $seeds) {
        for ($act = 0; $act -lt 5; ++$act) {
            $activeGeometryName = "active-seed-$seed-act-$act.msa"
            $activeGeometryElapsed = Invoke-GeometryBinary `
                -Seed $seed `
                -Act $act `
                -Directory $validationRoot `
                -FileName $activeGeometryName `
                -ExcelRoot $activeExcel `
                -TilesRoot $activeTiles `
                -MaximumMilliseconds 30000
            $activeGeometryPath = Join-Path $validationRoot $activeGeometryName
            $activeGeometry = Test-GeometryBinary `
                -Path $activeGeometryPath `
                -Seed ([uint32]$seed) `
                -Act $act
            Write-Output "PASS active-geometry seed=$seed act=$act cells=$($activeGeometry.Cells) geometry-ms=$activeGeometryElapsed"
        }
    }

    $syntheticExcel = Join-Path $validationRoot 'synthetic-excel'
    Copy-Item -LiteralPath $activeExcel -Destination $syntheticExcel -Recurse
    $workspaceRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $scriptDirectory '..\..\..'))
    $fixtureScript = @'
const fs = require('fs');
const path = require('path');
const [workspaceRoot, excelRoot, targetText] = process.argv.slice(1);
const target = Number(targetText);
const api = require(path.join(workspaceRoot, 'scripts/build-data/tsv'));

function mutate(fileName, callback) {
    const filePath = path.join(excelRoot, fileName);
    const before = fs.readFileSync(filePath, api.ENCODING);
    const table = api.parseTable(filePath);
    if (api.serializeTable(table) !== before || table.eol !== '\r\n') {
        throw new Error(`non-exact source round-trip: ${fileName}`);
    }
    callback(table);
    api.writeTable(filePath, table);
    const after = fs.readFileSync(filePath, api.ENCODING);
    const reparsed = api.parseTable(filePath);
    if (api.serializeTable(reparsed) !== after || reparsed.eol !== '\r\n') {
        throw new Error(`non-exact fixture round-trip: ${fileName}`);
    }
}

mutate('levels.txt', table => {
    const id = table.headers.indexOf('Id');
    const name = table.headers.indexOf('Name');
    const levelName = table.headers.indexOf('LevelName');
    const displayName = table.headers.indexOf('*StringName');
    if (id < 0 || name < 0 || levelName < 0) throw new Error('invalid Levels headers');
    if (table.rows.some(row => Number(row[id]) === target)) {
        throw new Error('synthetic target already exists');
    }
    let renamed = 0;
    for (const row of table.rows) {
        for (const columnName of ['Depend', 'Vis0', 'Vis1', 'Vis2', 'Vis3', 'Vis4', 'Vis5', 'Vis6', 'Vis7']) {
            const column = table.headers.indexOf(columnName);
            if (column >= 0 && Number(row[column]) === 138) row[column] = String(target);
        }
        if (Number(row[id]) !== 138) continue;
        row[id] = String(target);
        row[name] = 'Synthetic Arbitrary Level';
        row[levelName] = 'SyntheticArbitraryLevel';
        if (displayName >= 0) row[displayName] = 'SyntheticArbitraryLevel';
        ++renamed;
    }
    if (renamed !== 1) throw new Error(`expected one source level, got ${renamed}`);
});

for (const [fileName, columnName] of [
    ['lvlmaze.txt', 'Level'],
    ['lvlprest.txt', 'LevelId'],
]) {
    mutate(fileName, table => {
        const column = table.headers.indexOf(columnName);
        if (column < 0) throw new Error(`missing ${columnName}`);
        for (const row of table.rows) {
            if (Number(row[column]) === 138) row[column] = String(target);
        }
    });
}
'@
    & $NodeExecutable -e $fixtureScript $workspaceRoot $syntheticExcel 733
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not create the synthetic mod-data fixture'
    }

    $syntheticResult = Invoke-MapGenerator `
        -Seed 1337 `
        -Act 4 `
        -CurrentLevel 109 `
        -ExcelRoot $syntheticExcel `
        -TilesRoot $activeTiles
    if ($syntheticResult.ExitCode -ne 0) {
        throw 'Synthetic custom-id label generation failed'
    }
    $syntheticEdge = @($syntheticResult.Lines | Where-Object {
        $_ -match '^MS1 E 109 733 [0-9]+ [0-9]+ 0$'
    })
    $syntheticLevel = @($syntheticResult.Lines | Where-Object {
        $_ -match '^MS1 L 733 '
    })
    if ($syntheticEdge.Count -ne 1 -or $syntheticLevel.Count -ne 1) {
        throw 'Synthetic arbitrary custom level was not generated exactly'
    }
    Write-Output "PASS synthetic-custom exact-entry=$($syntheticEdge[0]) level-id=733"
}
} finally {
    if (Test-Path -LiteralPath $validationRoot -PathType Container) {
        Remove-Item -LiteralPath $validationRoot -Recurse -Force
    }
}
