param(
    [Parameter(Mandatory = $true)]
    [string]$PluginDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-Policy {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        throw "Floating Damage source policy failed: $Message"
    }
}

$sourceDirectory = Join-Path $PluginDirectory 'src'
$renderer = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'd3d12_renderer.cpp')
$rendererHeader = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'd3d12_renderer.hpp')
$floating = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'floating_damage.cpp')
$plugin = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'plugin.cpp')
$config = Get-Content -Raw -LiteralPath (
    Join-Path $PluginDirectory 'config\ruffneckk-floating-damage.toml')

Assert-Policy ($rendererHeader -match 'kFloatingDamageFontCount\s*=\s*13') `
    'the public font range must include Kodia index 12'
Assert-Policy ($renderer -match 'constexpr\s+int\s+KodiaFontIndex\s*=\s*12') `
    'Kodia must keep stable font index 12'
Assert-Policy ($renderer -match 'std::vector<unsigned char>\s+ModFontBytes') `
    'the active-mod font bytes must use persistent ModFontBytes storage'
Assert-Policy ($renderer -match 'AddFontFromMemoryTTF\([\s\S]*?ModFontBytes\.data\(\)') `
    'Kodia must be rebuilt from persistent ModFontBytes'

$resetRenderer = [regex]::Match(
    $renderer,
    'void\s+ResetRendererState\(\)\s+noexcept\s*\{[\s\S]*?(?=\r?\nvoid\s+ResetRenderer\(\))')
Assert-Policy $resetRenderer.Success 'ResetRenderer could not be audited'
Assert-Policy (-not $resetRenderer.Value.Contains('ModFontBytes.clear()')) `
    'ResetRenderer must retain Kodia bytes across 4K/2K swap-chain rebuilds'
Assert-Policy ($renderer -match 'FailRendererInitialization\([\s\S]*?ResetRendererState\(\)') `
    'partial ImGui initialization failures must release renderer state'

Assert-Policy ($config -match '(?m)^font_index\s*=\s*0\s*$') `
    'the shipped configuration must retain the Community Pack default font index'
Assert-Policy ($config -match '(?m)^enabled\s*=\s*true\s*$') `
    'the shipped configuration must enable the plugin by default'
Assert-Policy ($config -match '(?ms)^\[diagnostics\].*?^enabled\s*=\s*false\s*$') `
    'the shipped configuration must disable detailed diagnostics by default'
Assert-Policy ($config -match 'Kodia') `
    'the shipped configuration must identify index 12 as Kodia'

$disabledLoad = [regex]::Match(
    $plugin,
    'if\s*\(!FloatingDamage::GetConfig\(\)\.enabled\)\s*\{[\s\S]*?return true;')
Assert-Policy $disabledLoad.Success `
    'the master disabled path must return before renderer and combat hooks'
Assert-Policy (-not $disabledLoad.Value.Contains('RegisterLifecycleListeners')) `
    'the master disabled path must not register gameplay listeners'
Assert-Policy ($plugin -match 'parsed\.diagnosticsEnabled\s*\?\s*LogOverlayDiagnostic') `
    'renderer diagnostics must be controlled by the TOML option'

Assert-Policy ($plugin -match 'LifecycleServiceV1') `
    'Lifecycle service v1 must own the gameplay boundary'
foreach ($eventName in @('GameJoined', 'GameLeft', 'LocalPlayerReady')) {
    Assert-Policy ($plugin -match "GameplayEventKind::$eventName") `
        "the $eventName lifecycle event must be handled"
}
Assert-Policy ($plugin -match 'unregisterGameplayEventListener') `
    'lifecycle listeners must be explicitly unregistered'
Assert-Policy ($floating -match 'std::atomic_bool\s+g_gameplayActive') `
    'gameplay activity must remain distinct from the enabled configuration'
Assert-Policy ($floating -match 'void\s+Render\([\s\S]*?if\s*\(!IsGameplayActive\(\)') `
    'rendering must be gated outside gameplay'
Assert-Policy ($floating -match 'void\s+QueueGameDamage\([\s\S]*?if\s*\(!IsGameplayActive\(\)') `
    'game damage queuing must be gated outside gameplay'
Assert-Policy ($floating -match 'void\s+Update\([\s\S]*?if\s*\(!IsGameplayActive\(\)') `
    'DPS and display updates must be gated outside gameplay'
Assert-Policy ($floating -match 'hotkeyFontSize\s*=\s*fontSize\s*\*\s*\(2\.0f\s*/\s*3\.0f\)') `
    'the DPS hotkey hint must use exactly two thirds of the DPS font size'
Assert-Policy ($floating -match 'blockSize\s*\{[\s\S]*?std::max\(textSize\.x,\s*hotkeySize\.x\)[\s\S]*?textSize\.y\s*\+\s*lineGap\s*\+\s*hotkeySize\.y') `
    'the DPS edge clamp must measure the complete two-line block'
Assert-Policy ($floating -match 'g_config\.toggleHotkeyEnabled[\s\S]*?g_config\.toggleHotkeyText\.c_str\(\)') `
    'the rendered DPS hint must come from the active configured hotkey'
foreach ($staleLabel in @(
    'D2RHUD Options',
    'Bundled fonts are embedded',
    'Defaults to Exocet',
    'loading embedded fonts'
)) {
    Assert-Policy (-not (($floating + $renderer).Contains($staleLabel))) `
        "stale player-facing label remains: $staleLabel"
}

Write-Host 'Floating Damage source policy: PASS'
