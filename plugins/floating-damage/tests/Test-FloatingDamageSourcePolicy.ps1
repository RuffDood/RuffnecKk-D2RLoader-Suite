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
$floatingHeader = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'floating_damage.hpp')
$configParser = Get-Content -Raw -LiteralPath (
    Join-Path $sourceDirectory 'config_parser.cpp')
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
Assert-Policy (-not $disabledLoad.Value.Contains('RegisterInputAction')) `
    'the master disabled path must not register an input action'
Assert-Policy ($plugin -match 'parsed\.diagnosticsEnabled\s*\?\s*LogOverlayDiagnostic') `
    'renderer diagnostics must be controlled by the TOML option'

Assert-Policy ($plugin -match 'InputServiceV1') `
    'the toggle must use SDK v3 Input service v1'
Assert-Policy ($plugin -match '\.logicalId\s*=\s*"toggle-floating-damage"') `
    'the native input action must keep its stable logical id'
Assert-Policy ($plugin -match '\.displayName\s*=\s*"Toggle Floating Damage"') `
    'the native input action must use its approved player-facing name'
Assert-Policy ($plugin -match '\.category\s*=\s*"RuffnecKk Suite"') `
    'the native input action must appear in the Suite category'
Assert-Policy ($plugin -match 'D2RL::Input::Key::Z[\s\S]*?D2RL::Input::Modifier::Shift') `
    'the native input action must default to Shift+Z'
Assert-Policy ($plugin -match 'D2RL::Input::Key::None[\s\S]*?D2RL::Input::Modifier::None') `
    'the native input action must default its secondary binding to None'
Assert-Policy ($plugin -match 'ActionEventKind::Pressed[\s\S]*?FloatingDamage::RequestToggle\(\)') `
    'only the Pressed event may request a session-state toggle'
$inputCallback = [regex]::Match(
    $plugin,
    'auto\s+__cdecl\s+OnToggleInputAction\([\s\S]*?(?=\r?\nvoid\s+UnregisterInputAction)')
Assert-Policy $inputCallback.Success `
    'the native Input callback could not be audited'
Assert-Policy (-not $inputCallback.Value.Contains('SetEnabled')) `
    'the Input callback must not mutate render-thread state'
Assert-Policy (-not $inputCallback.Value.Contains('GetConfig')) `
    'the Input callback must not access the shared configuration'
Assert-Policy ($floating -match 'std::atomic_bool\s+g_toggleRequested') `
    'the Input callback request must cross threads through an atomic flag'
Assert-Policy ($floating -match 'void\s+RequestToggle\(\)\s+noexcept[\s\S]*?g_toggleRequested\.store\(true,\s*std::memory_order_release\)') `
    'the Input callback request must only arm the atomic flag'
Assert-Policy ($floating -match 'void\s+Update\(float\s+dt\)[\s\S]*?g_toggleRequested\.exchange\(false,\s*std::memory_order_acq_rel\)[\s\S]*?SetEnabled\(!IsEnabled\(\)\)') `
    'the render-thread update must consume and apply the toggle request'
Assert-Policy ($plugin -match 'OnToggleInputAction[\s\S]*?ActionResult::Ignored') `
    'the native action must preserve input pass-through to D2R'
Assert-Policy ($plugin -match 'unregisterAction') `
    'the native input action must be explicitly unregistered'
Assert-Policy (-not $plugin.Contains('ActionResult::Handled')) `
    'the Floating Damage toggle must never consume the input action'
Assert-Policy (-not $config.Contains('[hotkey]')) `
    'the shipped TOML must not own a hotkey table'
Assert-Policy (-not $config.Contains('toggle_hotkey')) `
    'the shipped TOML must not expose obsolete hotkey keys'
Assert-Policy ($configParser -match 'table\s*==\s*"hotkey"[\s\S]*?key\s*==\s*"toggle_hotkey_enabled"[\s\S]*?key\s*==\s*"toggle_hotkey"') `
    'the parser must retain the narrow 1.3.x hotkey migration shim'
$inputSources = $renderer + $floating + $floatingHeader + $configParser + $plugin
foreach ($obsoleteInputToken in @(
    'GetAsyncKeyState',
    'PollToggleHotkey',
    'HotkeyBinding',
    'toggleHotkey',
    'VK_MBUTTON',
    'VK_XBUTTON1',
    'VK_XBUTTON2'
)) {
    Assert-Policy (-not $inputSources.Contains($obsoleteInputToken)) `
        "obsolete private input token remains: $obsoleteInputToken"
}

Assert-Policy ($plugin -match 'DiagnosticsServiceV1') `
    'the shared STATLIST_SetUnitStat entry must use Diagnostics v1'
Assert-Policy ($plugin -match 'ModificationState::Unchanged') `
    'the vanilla STATLIST_SetUnitStat entry must remain accepted'
Assert-Policy ($plugin -match 'ModificationState::Tracked') `
    'a loader-tracked shared entry must be recognized'
Assert-Policy ($plugin -match 'ModificationKind::InlineHook') `
    'only a loader-owned inline hook may compose at STATLIST_SetUnitStat'
Assert-Policy ($plugin -match 'status\.ownerCount\s*!=\s*1') `
    'exactly one tracked owner must be required at STATLIST_SetUnitStat'
Assert-Policy (
    [regex]::Matches(
        $plugin,
        'MatchesSignature\(SetUnitStatRva').Count -eq 1) `
    'the vanilla setter check must exist only as the Diagnostics-unavailable fallback'
Assert-Policy ($plugin -match 'SetUnitStat\s*=\s*reinterpret_cast<SetUnitStatFn>\(Base\s*\+\s*SetUnitStatRva\)') `
    'Floating Damage must call the live STATLIST_SetUnitStat entry so later hooks also compose'

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
Assert-Policy (-not $floating.Contains('SHIFT+Z')) `
    'the DPS overlay must not show a stale static binding hint'
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
