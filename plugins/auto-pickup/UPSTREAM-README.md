# D2R Pickit

Walking and post-cast pickup plus automatic container opening for Diablo II:
Resurrected, with live category switches, separate radii, and a searchable
Include/Exclude item picker. Auto Interact is built into the same Pickit DLL.
D2R Pickit is the successor to Auto Pickup Anything. Its stable plugin ID,
DLL filename and config filename remain unchanged for existing installations.
The standalone Teleport Auto Pickup plugin remains supported and available.

**Author and maintainer:** [atsay714](https://github.com/atsay714)
**Version:** 0.10.1 Â· **License:** MIT Â· **Runtime:** D2RLoader / Windows x64

## Installation

Requires D2RLoader and a compatible Windows x64 D2R runtime. Download the
matching ZIP from the project's releases or build it using [BUILDING.md](BUILDING.md).
The native fingerprints currently target D2R 3.3.0 build 93847. Other builds
must pass the plugin's automatic compatibility checks; ABI 3 and the required
loader services must be available. The development environment uses
D2RLoader 1.3.1-beta.

1. Close D2R and D2RLoader.
2. Extract **one** package into your D2R installation directory:
   - `native.zip`: global `d2rloader/plugins/` and `d2rloader/config/`.
   - `reimagined.zip`: the same files under `mods/Reimagined/d2rloader/`.
3. Start the game through D2RLoader. No first-run configuration edit is required.

These are native DLL packages, separate from the Reimagined launcher's JSON
User Plugins importer. The plugin supports global or mod scope. Keep only one
active copy. Restart after manual config edits; the item picker saves its lists live. Existing configs are retained
by the loader; copy new settings deliberately when upgrading.

DLL: `d2rl-auto-pickup-anything.dll`
Config: `d2rloader/config/auto-pickup-anything.toml` in the selected scope.

Installation ZIPs contain the DLL, default TOML and supporting text documents.
Documentation, licenses and internal checksums live under
`d2rloader/docs/auto-pickup-anything/` in the selected scope. Every supporting
filename starts with `auto-pickup-anything.`, so they also remain distinct if an
importer flattens directories. Build metadata (`plugin.json`) and the item-code
catalog are retained in the repository/source ZIP and are not installed as
runtime JSON patches. The source ZIP is for building, not installation.

For a manager that reports a `plugins/plugin.json differs between ...` error,
remove its conflicting imported package entries and import the new packages.
If the manager still uses cached contents, manual installation can bypass the
package importer: copy only this plugin's DLL into the active D2RLoader
`plugins/` folder. The DLL embeds the default config, which D2RLoader creates
when missing. Keep existing TOML settings when upgrading and use one active
copy of each DLL. Do not remove character saves or environment history files.

## Auto Interact settings

Press **Ctrl+P → Auto Interact** for the built-in object opener. It has its own
toggle, loot-container/scenery-corpse/rock/rack categories, locked chests,
interaction range, scan interval and retry cooldown. Loot containers includes
ordinary chests, hidden/jungle stashes, hollow logs, baskets, jars, ordinary
urns, crates, normal barrels, cocoons and bone/skull piles. Town/personal stashes,
Evil Urns, exploding barrels and quest/event objects remain manual.

**Apply & Save changes only the active tab's settings.** Both tabs save to
`auto-pickup-anything.toml`; object settings live in `[auto_interact]`. The item
pickup toggle and Auto Interact toggle are independent. Turning item pickup off
keeps automatic opening available. Live changes apply if the opener was active
at startup. Enabling after a disabled/validation startup requires restart.

`auto_interact.interact_range=0` keeps native reach; 1..20 uses that opening
distance without walking. Collision, idle/dead/cursor state, targetability,
keys, traps and native loot callbacks remain. Normal barrels keep native kick
reach. Locked chests default off. Move or cast once after entering a game to
acquire the actor. TCP/IP opening remains unsupported. Active source Class keys
require verified loose Objects TXT; packed mods without source are unqualified.
The `auto-interact` console command and Pickit log report opening counters and
average/max scan microseconds. Timings do not establish whole-game FPS impact.

### Upgrade from the separate Auto Interact test plugin

Close the game and keep both existing TOMLs. Back up and remove the retired
`d2rl-auto-interact.dll` from active plugin folders, then install the single
Pickit DLL. **Preserve your existing Pickit TOML when extracting the ZIP.** On
first launch, if `[auto_interact]` is absent, Pickit imports the sibling
`auto-interact.toml` once. It preserves your range, toggles, lock/key option,
validation/diagnostics and timing settings, along with item rules and comments.
The old TOML is retained as a backup and is no longer read after migration.
An existing `[auto_interact]` section always wins. A refused/malformed migration
leaves opening inactive and reports the problem without replacing legacy data.

Old Pickit configs with no object section or legacy file keep opening off until
you opt in. New installations ship useful opening defaults with range zero.
If an old Auto Interact DLL is still present, built-in opening stays inactive
and explains removing it and restarting. Item pickup continues to work.

## Configuration

```toml
# D2R Pickit 0.10.1 (stable config/ID: auto-pickup-anything)
# Ctrl+P opens the optional item picker. Apply & Save updates pickup settings live.
# Restart after manual file edits or changing validate_only. Master off retains your preferences.
enabled = true
# Optional troubleshooting mode; compatibility checks always run.
validate_only = false

[walking]
enabled = true
# Native distance units, not yards (1..20; default 4).
pickup_range = 4

[teleport]
# Also covers other cast actions. Pickit owns this with cooperative Teleport 0.3.5+.
# Older Teleport keeps control through teleport-auto-pickup.toml; no update required.
enabled = true
gold = true
items = true
pickup_range = 4
interval_ms = 100
window_ms = 2000
diagnostics_enabled = true

[vanilla]
gems = true
runes = true
keys = false
health_potions = false
mana_potions = false
rejuvenation_potions = false

[reimagined]
# Reimagined gems have a different item code
gems = true
gem_stacks = true
rune_stacks = true

[item_codes]
# Exact item codes, not numeric item IDs. Both lists are optional.
# Include adds to the groups above; exclude always wins. Gold stays native.
# Common extras: "jew" = jewels; "cm1" = small charms;
# "cm2" = large charms; "cm3" = grand charms; "rvl" = full rejuvenation potions.
# Codes select every quality, so charm/jewel codes do not filter for uniques.
# Example: include = ["jew", "cm1", "cm3", "rvl"]
include = []
# Example: exclude = ["r01"] leaves El runes even when runes = true.
exclude = []

[d2rl]
match = ["enabled", "validate_only", "vanilla.gems", "vanilla.runes", "vanilla.keys", "vanilla.health_potions", "vanilla.mana_potions", "vanilla.rejuvenation_potions", "reimagined.gems", "reimagined.gem_stacks", "reimagined.rune_stacks", "item_codes.include", "item_codes.exclude", "walking.enabled", "walking.pickup_range", "teleport.enabled", "teleport.gold", "teleport.items", "teleport.pickup_range", "teleport.interval_ms", "teleport.window_ms"]
```

| Setting | Includes |
| --- | --- |
| vanilla.gems | All 35 classic gem codes, chipped through perfect, including skulls |
| vanilla.runes | El through Zod |
| vanilla.keys | Ordinary keys; excludes Uber keys |
| vanilla.health_potions / mana_potions | All five standard sizes of the selected family |
| vanilla.rejuvenation_potions | Small and full rejuvenation potions |
| reimagined.gems | Reimagined's eight loose gem codes, including Chaos Onyx |
| reimagined.gem_stacks | Stacked forms of those eight Reimagined gems |
| reimagined.rune_stacks | Reimagined's El through Zod rune stacks |

The Reimagined switches default to true. Their item codes differ from vanilla
and select no unrelated vanilla items. Power orbs, crafting materials, Uber
keys, jewels, charms, equipment and other categories stay manual unless you
explicitly include their codes. The [catalog](catalog.json) lists the 130 codes
covered by the built-in category switches; custom lists can use other codes.
`[d2rl]` is loader compatibility metadata; retain it when editing item preferences.

### Custom item codes

Both lists ship empty, so the default pickup selection is unchanged:

```toml
[item_codes]
include = []
exclude = []
```

`include` adds items even when their category is disabled or absent from the
built-in catalog. `exclude` always wins, including over an explicit inclusion.
For example, pick up jewels and small charms, but leave El runes on the ground:

```toml
[item_codes]
include = ["jew", "cm1"]
exclude = ["r01"]
```

Use exact, quoted item codes of 1-4 lowercase letters or digits, not numeric
item IDs, display names or wildcards. Codes identify an item type, not its
quality: including `cm1` selects all small charms. Look up vanilla or mod codes
in the `code` column of `misc.txt`, `weapons.txt` or `armor.txt`; the catalog
above provides examples for the built-in groups. Codes from other mods work
when they follow the same format and the native compatibility checks pass.
Single or double quotes, multiline arrays, comments and trailing commas are
supported. Invalid lists prevent activation and produce a config error in the log.

Gold (`gld`) is controlled by the post-cast gold switch and normal game gold pickup; adding
it to an item list does not enable a second gold collector. Game pickup rules still apply
to every custom selection, including inventory capacity and collision.

When upgrading, existing configs without `[item_codes]` remain valid. To customize
one, add the section once before `[d2rl]`, retain the other settings, and add
`"item_codes.include"` and `"item_codes.exclude"` to `[d2rl].match` as shown in
the full default config above. Restart the game after editing.

### In-game item picker

Press **Ctrl+P**, rebind **Auto Pickup Settings** in D2R's Controls menu, or run
`auto-pickup-settings` in the loader console. Search by item name or exact code.
A resizable Windows overlay opens over the game with a scrollable **Item picker**
list. Every row has a checkbox: checked means pick up, unchecked means leave it.
Clicking a checkbox creates an explicit Include/Exclude override; the policy
column explains the current rule. Space toggles the selected row too.

After searching (for example, `orb`), **Pick up results** or **Leave results**
changes all matching items in one action, including results below the visible
area. **Use category defaults** removes overrides for those same matches.
The result count is shown on the buttons. Bulk actions use the rows displayed
when clicked, so a later search cannot redirect the action to different items.
**Overrides only** shows your explicit selections. The overlay takes keyboard
focus while open; Escape, Ctrl+P or Close returns focus to the game. This checkbox
frontend is preferred on all supported runtimes; the older paged native panel is
only a window-creation fallback.

The settings window uses the native Windows pointer and its own input queue.
It stays unowned by the game so game-side cursor hiding cannot affect settings,
and pointer movement does not wait for a software cursor repaint. Radius
controls repaint on open without requiring hover.

The **General** tab has a master switch, all nine category checkboxes,
walking/post-cast switches, post-cast gold/items switches and two radius dropdowns
(1-20, default 4; native distance units, not yards). The newer native panel exposes
these through **Options / Items** and paged Change buttons.

**Apply & Save** writes these settings and the include/exclude lists to the
existing active-scope TOML, then applies them to walking and built-in post-cast scans.
Closing without saving discards the draft. A failed config write leaves the
previous selection active. If another editor changed the lists after opening
the panel, close and reopen it before saving. Comments, diagnostic/timing settings
and loader metadata are preserved. Manual TOML edits and `validate_only` still
require restarting. Category, master and radius changes saved through the window
apply live. Turning the master off retains your selections and stops this plugin's
item and built-in post-cast gold requests. The game's own gold pickup and other
plugins keep their normal behavior.

The search catalog comes from the **loaded, compiled Items tables** exposed by
the SDK, combining the available Classic, LoD and RotW banks for the current
mod environment. It is not a bundled vanilla item-name database. Item codes
are verified against the SDK's semantic lookup; numeric row IDs are never
saved or reused across mods. Names are resolved through the game's `d2r:<code>`
localization keys, including mod overrides. A name-ID fallback is admitted only
when numeric and key-based localization agree across diverse table rows; this
also handles new codes that reuse an existing base name. D2 color markers are
removed from search labels. If neither lookup can resolve a name, the exact
code remains searchable and visible.
Different codes with the same display name remain separate rows. Definitions
can include unused or non-dropping base types; presence in the picker does not
make an item drop or bypass normal pickup rules.

Reimagined's gem codes therefore appear from its loaded tables alongside any
classic definitions it retains. There is no mod-name heuristic or automatic
translation of an old saved code into a different mod's item. Saved overrides
absent from the current tables are retained and shown as unavailable, so they
can be reviewed or removed. Separate loader config scopes remain separate.
Codes select every quality of a base type; this is not a socket/stat/rarity rule
editor or loot-filter integration. Gold remains with the gold collector.

The picker requires table, lifecycle and UI-thread services. The Windows
frontend supports the older Widget service in D2RLoader 1.3.1-beta; it does not
require a loader upgrade. The native panel is used only when its optional
panel/widget helpers are available. If neither frontend initializes, TOML
remains available. No loose UI files or extra install JSON are required.
Tests exercise real hidden Windows controls and loader fixtures. The user
accepted the merged 0.10.0 gameplay retest on Reimagined with D2RLoader
1.3.1-beta / D2R build 93847. Controller navigation, TCP/IP live-config matching
and other game/mod environments require separate qualification.

## Pickup behavior

Movement actions begin pickup checks that continue every 100 ms while walking
or running, using the player's current position and live walking radius
(default 4). Each scan requests at most one selected item. Long paths keep the
checks active without requiring more clicks; they stop after a short arrival
grace period. Player IDs are resolved on each authoritative game update, and
session changes, disabling walking/the master switch, or unload cancel old work.
Checks respect collision and use the normal pickup entry, preserving the game's
acceptance, stacking and placement rules. There is no inventory sweep.
Walking radius applies to selected items; the game's own walking gold collector
is unchanged. Post-cast gold has its separate radius.
This is a separate collector; exact vanilla auto-gold selection parity is
unverified.

After a pickup request, the same item is skipped for one second for that player
if it is still on the ground. This covers refused requests, native exceptions,
and reported successes that leave the item behind. Other items remain eligible
on subsequent scans, so an uncollectable nearest item does not hold up the rest.
Successfully collected items disappear from the scan without delaying other
pickups. The cooldown is automatic and requires no configuration changes.

Walking and both Teleport Auto Pickup scan APIs share this cooldown. Retry
state contains player/item IDs and item codes, not stored native pointers, and
resets on game join/leave, local-player readiness, and plugin unload. A full
inventory does not stop all item attempts: native stacking or Auto Deposit may
still accept an item. The plugin preserves those normal pickup paths.

### Built-in post-cast pickup and standalone compatibility

Pickit includes the landing-scan scheduler and gold collector from
[Teleport Auto Pickup](https://github.com/atsay714/d2rl-teleport-auto-pickup).
It scans after teleport and other cast actions without requiring a walk.
Gold respects the active game's carry cap, skips full wallets, and uses the
existing one-second no-progress retry guard. Item scans share the walking
selection and item retry guard. No gold cap is increased.

| Installed setup | Who owns post-cast pickup? |
| --- | --- |
| Pickit 0.10.0 alone | Pickit; General tab controls its radius, gold and items |
| Teleport 0.3.5 alone | Standalone, using its own TOML |
| Pickit 0.10.0 + Teleport 0.3.5 | Pickit; standalone loads without installing hooks or scans |
| Pickit 0.10.0 + Teleport 0.3.4 or older | Standalone, using its own TOML; updating Teleport is optional |
| Older Pickit/Anything + Teleport 0.3.5 | Standalone behavior remains active as before |

With the new pair, all settings and the master switch in Pickit control item and
post-cast gold pickup. Standalone's own config is kept intact and becomes active
again if Pickit is removed and the game restarted. No settings are automatically
copied between the two TOMLs.

Both binaries advertise a versioned ownership marker in a named DLL resource.
They inspect the current/global/active-mod plugin folders and loaded modules
before installing hooks. File probes map resources without executing DLL code,
so priority works regardless of load order. A new but disabled Pickit DLL also
reserves ownership. Remove it from active plugin folders and restart to use
standalone alone. No mid-session takeover or renamed-DLL detection is supported.
Keep one active copy of each plugin; an older Teleport copy found in these
folders retains the conservative legacy fallback until updated or removed.

An unmodified Teleport 0.3.4 cannot yield its hooks. Pickit keeps that workflow
working without requiring a Teleport update. Its post-cast controls are grayed
out with an explanation: edit `teleport-auto-pickup.toml` and restart to change
those settings, or optionally update Teleport to 0.3.5+ to manage them in Pickit.
The old standalone config remains untouched. Walking settings, category switches
and item rules stay editable in Pickit. In this mode, Pickit's master stops its
own item selections, while standalone still controls its own gold. The V1 item
bridge remains at distance 4 and V2 preserves the caller's radius, so older
versions remain usable.

The internal ID stays `auto-pickup-anything`, and the DLL/config retain their
original names. Existing lists and category settings remain valid; missing
walking/post-cast settings use the defaults above. This does not remove the
loader's normal saved-environment checks when changing a plugin setup.

Auto Deposit remains responsible for Reimagined bag/stash routing. Its 0.1.4
gem-bag path can miss delayed or burst pickups; the separate local pending-ID
queue fix addresses that issue. This repo does not contain or replace Auto
Deposit, and no bag transaction logic is embedded here.

Console command: `auto-pickup`. Log: the selected scope's
`d2rloader/logs/auto-pickup-anything.log`.
The status output includes `retry-skips` for items temporarily skipped,
`walking-follow-up-scans`, and `walking-queue-failures`. Sampled item-request
logs include the actual distance and configured radius to help diagnose refused
pickups. The existing `diagnostics_enabled` setting controls those sampled logs.

## Troubleshooting and removal

The plugin's status command and log report activation and compatibility checks.
`validate_only = true` checks compatibility without installing hooks; normal
play uses the shipped default `false`. An unknown or conflicting native
modification refuses activation rather than guessing addresses.

To disable, uncheck **Enable D2R Pickit** and Apply & Save, or set `enabled = false` in TOML and restart. To remove, close the game and
move its DLL out of the active plugin folder. It creates no custom items or
inventory pages. D2RLoader may show an environment-history warning after a
plugin change; keep plugin IDs stable and back up characters before changing
your setup. No save-history editing is part of installation or removal.

## Releases

The published Hub listing is backed up in [HUB_LISTING.md](HUB_LISTING.md).

See [RELEASING.md](RELEASING.md) for the D2RLoader Hub publishing workflow.
It builds and tests on Windows, then uploads the native package when a GitHub
release is published. Manual Actions runs build and package without publishing
when the optional release tag is left blank; see the release guide for retries.

## Development and credits

See [BUILDING.md](BUILDING.md) for builds, tests and packaging, and
[CHANGELOG.md](CHANGELOG.md) for changes. The maintainer and release author is
[atsay714](https://github.com/atsay714).

This project includes code adapted from RuffnecKk's MIT-licensed Auto Pickup
plugins and the MIT-licensed D2RLoader PluginSDK. Adapted native signatures and
hook patterns are credited in [UPSTREAM.md](UPSTREAM.md). Original notices are
preserved in [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).


Version 0.9.6 removes the software cursor and isolates settings input from the
game. Status and Auto Interact edits reuse
the item catalog instead of rebuilding thousands of rows. Built-in Auto Interact
uses positive range values as opening distance without walking; collision and
normal-barrel kick reach still apply. Zero remains native reach.
