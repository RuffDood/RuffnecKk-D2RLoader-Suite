# Remote Stash 2.0.0

Remote Stash opens the native personal and shared stash outside town and adds
its own configurable keyboard-and-mouse Inventory button.

Version 2.0.0 is the canonical Remote Stash baseline for future releases of
the RuffnecKk D2RLoader Suite.

## Installation

1. Put `d2rl-ruffneckk-remote-stash.dll` in exactly one D2RLoader plugin
   folder: global or mod-local, never both.
2. Start D2R once. The plugin creates `ruffneckk-remote-stash.toml` when no
   configuration already exists.
3. Restart D2R after changing the configuration.

No Inventory JSON merge and no sprite copy into a mod MPQ are required. The
default RuffnecKk chest artwork is embedded in the DLL.

## Button configuration

The default automatic placement adapts to the Inventory layout loaded by the
active mod. Users may instead select a corner anchor, offsets, width, height,
and frame indexes in the `[button]` section of the TOML configuration.

A custom D2R `SpA1` version-31 `.sprite` file may be supplied with
`sprite_file`. `lowend_sprite_file` is optional. Relative paths are resolved
beside `ruffneckk-remote-stash.toml`. Invalid or incomplete sprites fall back
to the embedded chest without disabling the button.

## Upgrade from 1.5.0 or older

Older releases could require a manually merged Inventory button. Version
2.0.0 automatically hides both known legacy widgets:

- `remote_stash`;
- `ruffneckk_remote_stash_button`.

This prevents the obsolete button from opening the native Drop Gold modal or
appearing beside the new plugin-owned button.

For a clean permanent migration, remove those complete `ButtonWidget` objects
from every customized desktop Inventory layout. Do not remove or rename the
real vanilla `gold_button`. Old external `remotestashbutton.sprite` files may
also be removed when no other feature uses them.

## Compatibility

This build targets D2R 3.2.92777 and the governed D2RLoader PluginSDK v3
baseline. It is mod-independent and may be installed globally or mod-locally.
The active mod must retain the standard `PlayerInventory` panel contract.

## Credits

Remote Stash and the embedded chest artwork are by RuffnecKk.

D2MOO provided semantic reference material for historical Diablo II engine
behavior; all D2R 3.2 addresses and runtime contracts were verified
separately.
