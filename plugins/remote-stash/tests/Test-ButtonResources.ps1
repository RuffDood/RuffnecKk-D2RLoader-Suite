[CmdletBinding()]
param([Parameter(Mandatory)][string]$PluginPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$pluginRoot = Split-Path -Parent $PSScriptRoot
$resolvedPlugin = (Resolve-Path -LiteralPath $PluginPath).Path

# Read PE resources as data; do not execute DllMain or load the plugin in D2R.
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RemoteStashResourceReader {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")]
    public static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool FreeLibrary(IntPtr module);
}
'@

$module = [RemoteStashResourceReader]::LoadLibraryExW($resolvedPlugin, [IntPtr]::Zero, 0x22)
if ($module -eq [IntPtr]::Zero) { throw 'Could not open the compiled DLL resources.' }
try {
    $assets = @(
        @{ Id = 1101; Path = 'assets/mod-data/data/hd/global/ui/panel/inventory/remotestashbutton.sprite' },
        @{ Id = 1102; Path = 'assets/mod-data/data/hd/global/ui/panel/inventory/remotestashbutton.lowend.sprite' },
        @{ Id = 1103; Path = 'assets/strings.json' }
    )
    foreach ($asset in $assets) {
        $resource = [RemoteStashResourceReader]::FindResourceW($module, [IntPtr]$asset.Id, [IntPtr]10)
        if ($resource -eq [IntPtr]::Zero) { throw "Missing resource $($asset.Id)" }
        $size = [RemoteStashResourceReader]::SizeofResource($module, $resource)
        $loaded = [RemoteStashResourceReader]::LoadResource($module, $resource)
        $pointer = [RemoteStashResourceReader]::LockResource($loaded)
        if ($size -eq 0 -or $pointer -eq [IntPtr]::Zero) { throw 'Empty or unreadable resource.' }
        $bytes = New-Object byte[] $size
        [Runtime.InteropServices.Marshal]::Copy($pointer, $bytes, 0, $bytes.Length)
        $source = [IO.File]::ReadAllBytes((Join-Path $pluginRoot $asset.Path))
        if ([Convert]::ToBase64String($bytes) -cne [Convert]::ToBase64String($source)) {
            throw "Compiled resource differs from its source: $($asset.Path)"
        }
        if ($asset.Id -eq 1103) {
            $utf8 = New-Object Text.UTF8Encoding($false, $true)
            $strings = @($utf8.GetString($bytes) | ConvertFrom-Json)
            if ($strings.Count -ne 1 -or $strings[0].Key -cne 'OpenStash') {
                throw 'Expected one plugin-local OpenStash definition without game-text overrides.'
            }
            $locales = @('enUS','zhTW','deDE','esES','frFR','itIT','koKR','plPL','esMX','jaJP','ptBR','ruRU','zhCN')
            $actual = @($strings[0].PSObject.Properties.Name | Where-Object { $_ -cne 'Key' })
            if (@(Compare-Object $locales $actual -CaseSensitive).Count -ne 0) {
                throw 'The tooltip must provide exactly all 13 D2R locales.'
            }
            foreach ($locale in $locales) {
                $text = $strings[0].$locale
                if ($text -isnot [string] -or [string]::IsNullOrWhiteSpace($text) -or $text.Contains('@')) {
                    throw "Invalid translation for $locale"
                }
            }
            if ($strings[0].enUS -cne 'Open Stash') { throw 'English tooltip must be Open Stash.' }
            Write-Output 'PASS: compiled tooltip contains all 13 non-empty locale translations.'
        }
        Write-Output "PASS: compiled resource $($asset.Id) matches $($asset.Path) ($size bytes)."
    }
} finally {
    [void][RemoteStashResourceReader]::FreeLibrary($module)
}
