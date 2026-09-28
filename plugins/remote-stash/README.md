# Remote Stash — Inventory Button and Migration Guide

Remote Stash 2.3.5 creates its own keyboard-and-mouse Inventory button. The
bronze chest, recessed frame and side ornaments are embedded in the DLL, and placement is
calculated from the Inventory layout that is actually loaded by the game.

This 2.x line is the canonical Remote Stash baseline for future releases of
the RuffnecKk D2RLoader Suite.

No Inventory JSON merge and no sprite copy into a mod MPQ are required.

The self-contained button has transparent surroundings so the active Inventory's
own texture shows around the carved recess. Its four states are normal bronze, disabled iron, darker pressed,
and softly highlighted bronze on hover. The tooltip reads **Open Stash** in English
and follows the game's selected language automatically. Translations for all
13 D2R locales are embedded; no language setting or companion MPQ is needed.

The plugin does not replace the Inventory background, grid, equipment positions
or storage capacity. Smaller inventories keep their own panel. Placement uses
the active panel's geometry; unusual skins may still need offset adjustments.

## Upgrade from the withdrawn full-panel assets

The older Inventory Layout Assets r1/r2 downloads replaced complete panel files;
they are not required for 2.3.5. If you installed them, restore your own backed-up
inventory layouts and background textures. Remove their appearance override at
`data/global/ui/layouts/ruffneckk-remote-stash/button.toml`, or adapt it if it also
contains your own custom settings. Do not install another mod's panel to undo it.

Then replace the plugin DLL, preserve your hotkey configuration, and use
`placement = "auto"`, zero offsets, `width = 128`, `height = 80`, and empty sprite
paths in `[button]` to select the portable embedded design. Restart the game.
The old full-panel files cannot be automatically restored by the DLL because
their previous contents belong to the player's mod.

## Upgrade the button from 2.3.3 or earlier

The new default click area is `128 × 80` Inventory-layout units; the chest sits
inside that area with transparent padding. Existing configuration files are
preserved, so an explicit `176 × 112` setting keeps the larger size. To adopt
the compact appearance, change only these entries in your existing `[button]`
section, leaving your hotkey and other settings intact:

```toml
width = 128
height = 80
```

Empty `sprite_file` and `lowend_sprite_file` entries select the embedded artwork.
If your active mod supplies `button.toml`, its appearance settings take priority;
adjust that file instead. Intentional custom artwork and sizes remain supported.

Mod authors can translate or rename this tooltip with the string key
`ruffneckk-remote-stash:OpenStash` in their own localization JSON. This does not
replace the game's `OpenCurrentStashLegend` or affect other controls.

## Let each active mod skin a global installation

A mod can override only the Inventory-button appearance while keeping one
global Remote Stash DLL and one global hotkey configuration. Put this optional
file in the active mod's unpacked MPQ directory:

```text
<ModName>.mpq/data/global/ui/layouts/ruffneckk-remote-stash/button.toml
```

The file contains one `[button]` section using the same placement, dimensions,
sprite paths, and frame keys as the D2RLoader TOML. It does not accept plugin,
hotkey, close-behavior, or diagnostics settings.

```toml
[button]
placement = "custom"
anchor = "bottomLeft"
offset_x = 24
offset_y = -18
width = 176
height = 112
sprite_file = "data/hd/global/ui/panel/inventory/my-skin-remote-stash.sprite"
lowend_sprite_file = "data/hd/global/ui/panel/inventory/my-skin-remote-stash.lowend.sprite"
normal_frame = 0
pressed_frame = 2
disabled_frame = 1
hovered_frame = 3
```

Relative sprite paths in this file start at the `<ModName>.mpq` root. Absolute
paths and `..` traversal are rejected so the skin remains portable. A valid
active-MPQ file replaces the complete `[button]` section from the D2RLoader
TOML; omitted keys use Remote Stash's built-in button defaults. When the MPQ
file is absent, the D2RLoader TOML is used unchanged. A present but malformed
MPQ file refuses plugin loading instead of silently mixing two configurations.
If its referenced sprite is missing or invalid, the existing safe behavior
uses the embedded RuffnecKk chest, not the TOML sprite.

## Item-routing behavior

Remote Stash keeps D2R's native Inventory companion open whenever the stash
needs it for item routing. The hotkey setting
`close_remote_stash_and_inventory_together` controls the optional close action:
`true` closes both panels when the hotkey closes Remote Stash, while `false`
leaves Inventory open. The physical Inventory button always closes only Remote
Stash because Inventory is already open when that button is available.

When the Horadric Cube is visible, the plugin dismisses the Cube companion,
restores the standalone Inventory panel, opens Remote Stash, and completes the
native stash transition synchronously. This makes drag, held-item deposit,
Ctrl-click, and withdrawal routing available on the first open.

Configurations from Remote Stash 2.0.x remain compatible. Legacy
`hotkey_mode = "remoteOnly"` is read as `false`, and
`hotkey_mode = "remoteAndInventory"` is read as `true`. Do not declare the
legacy key and `close_remote_stash_and_inventory_together` together; an
ambiguous configuration is rejected instead of choosing one silently.

## Install

1. Put `d2rl-ruffneckk-remote-stash.dll` in exactly one D2RLoader plugin
   folder: global or mod-local, never both.
2. Start D2R once. D2RLoader creates `ruffneckk-remote-stash.toml` when that
   configuration file does not already exist.
3. Keep `inventory_button_enabled = true` to use the physical button.

The default button uses the four-state RuffnecKk chest, measures `128 × 80`,
and opens Remote Stash through its private message. It never reuses the native
Drop Gold action.

## Place the button

The default `placement = "auto"` looks at the active Inventory panel, grid, and
gold footer. It first tries the open space below the grid and then safe panel
corners. This works without hard-coding one mod's coordinates.

Offsets fine-tune the automatic result:

```toml
[button]
placement = "auto"
anchor = "bottomLeft"
offset_x = 0
offset_y = 0
```

For an exact user-owned position, switch to `custom`. The offsets are measured
from the selected corner of the active Inventory panel:

```toml
[button]
placement = "custom"
anchor = "bottomRight"
offset_x = -24
offset_y = -18
width = 176
height = 112
```

Valid anchors are `topLeft`, `topRight`, `bottomLeft`, and `bottomRight`.
Custom placement intentionally permits overlap or partially off-panel
positions; the user owns that rectangle.

## Supply a custom sprite

Set `sprite_file` to a D2R `SpA1` version-31 `.sprite` file. In the D2RLoader
TOML, a relative path is
resolved beside `ruffneckk-remote-stash.toml`; an absolute path is also valid.
Use forward slashes, escaped backslashes, or a TOML literal string.

```toml
[button]
width = 220
height = 96
sprite_file = "sprites/my-remote-stash.sprite"
lowend_sprite_file = "sprites/my-remote-stash.lowend.sprite"
normal_frame = 0
pressed_frame = 1
disabled_frame = 2
hovered_frame = 3
```

The frame indexes are zero-based and independently configurable. The plugin
validates the sprite header, complete pixel payload, frame count, dimensions,
and every configured frame before registering the asset. When
`lowend_sprite_file` is empty, the main custom sprite is reused in low-end mode.

If a custom file is missing or invalid, or one of its configured frames does
not exist, Remote Stash logs one warning and safely uses the embedded RuffnecKk
chest with its default `176 × 112` dimensions and `0 / 2 / 1 / 3` normal,
pressed, disabled, and hovered frames. The Inventory button remains usable.

Restart D2R after changing placement, dimensions, sprite paths, or frames.

## Upgrade from a version that required manual layout edits

Versions through 1.5.0 could require a manually merged Inventory button. The
2.0.0 plugin hides both known legacy widgets automatically so they cannot open
the Drop Gold modal or create a duplicate button:

- `remote_stash` — the older button that reused `PlayerInventoryPanelMessage:DropGold`;
- `ruffneckk_remote_stash_button` — the later manual button with the private
  Remote Stash message.

Clean the old installation when convenient:

1. Open every customized desktop Inventory layout used by the mod, normally
   `playerinventoryoriginallayouthd.json` and
   `playerinventoryexpansionlayouthd.json`.
2. Remove the complete `ButtonWidget` object named `remote_stash` or
   `ruffneckk_remote_stash_button`.
3. Do **not** remove or rename the real vanilla `gold_button`.
4. If nothing else uses them, remove the old external files
   `data/hd/global/ui/panel/inventory/remotestashbutton.sprite` and
   `remotestashbutton.lowend.sprite` from the mod.
5. Restart D2R. The only Remote Stash button should now be the plugin-owned
   button configured through TOML.

Leaving an old snippet in place temporarily is safe because 2.0.0 disables and
hides it at runtime. Removing it is still recommended so the mod's layouts no
longer carry dead integration data.

## Credits



D2MOO provided semantic reference material for historical Diablo II engine
behavior; all D2R 3.2 addresses and runtime contracts were verified separately.
