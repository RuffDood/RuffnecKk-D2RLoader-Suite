# Remote Stash — Inventory Button and Migration Guide

Remote Stash 2.0.0 creates its own keyboard-and-mouse Inventory button. The
default RuffnecKk chest artwork is embedded in the DLL, and its placement is
calculated from the Inventory layout that is actually loaded by the game.

This 2.0.0 line is the canonical Remote Stash baseline for future releases of
the RuffnecKk D2RLoader Suite.

No Inventory JSON merge and no sprite copy into a mod MPQ are required.

## Install

1. Put `d2rl-ruffneckk-remote-stash.dll` in exactly one D2RLoader plugin
   folder: global or mod-local, never both.
2. Start D2R once. D2RLoader creates `ruffneckk-remote-stash.toml` when that
   configuration file does not already exist.
3. Keep `inventory_button_enabled = true` to use the physical button.

The default button uses the four-state RuffnecKk chest, measures `176 × 112`,
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

Set `sprite_file` to a D2R `SpA1` version-31 `.sprite` file. A relative path is
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
