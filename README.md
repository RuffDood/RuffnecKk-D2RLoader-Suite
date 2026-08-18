# RuffnecKk D2RLoader Suite

> Release candidate. Final gameplay and TCP/IP qualification is still in progress.

The Suite contains 17 independent D2RLoader plugins and 19 optional memory
patches. Install only the
components you want.

Requires D2R build 3.2.92777 and D2RLoader 1.1.0-beta.

## Downloads

GitHub releases provide every plugin and memory patch as an individual
download. Pick only the components you want. Optional `All Plugins` and
`All Patches` bundles are also provided for convenience.

Each Suite release identifies one complete combination tested together, even
when you install only a subset. Download installable files from **Releases**;
the automatic GitHub source archives are not installation packages.

## Installation

- Use either the global `<D2R>/d2rloader/` root or one mod-local
  `<D2R>/mods/<mod>/d2rloader/` root; never install the same plugin in both.
- Put plugin DLLs in `d2rloader/plugins/` and patch JSON files in
  `d2rloader/patches/` under that root.
- D2RLoader creates an embedded TOML in `d2rloader/config/` only when it is
  missing and never overwrites an existing player file.
- Larzuk Sockets and Bulk Skill Point Allocation use loose JSON files in
  `d2rloader/config/`.

Do not run these replacements with the old `plugin-items.dll`,
`plugin-levels.dll`, `plugin-misc.dll`, `plugin-quests.dll`, or
`plugin-skills.dll`

## Plugins and configuration

Every plugin has a master `enabled` switch and ships with it set to `true`.
The table lists only its additional configuration choices.

| Plugin | What it does | Main configuration choices |
|---|---|---|
| Item Durability | Controls durability loss and adds optional bow durability. | Durability loss resistance, ethereal maximum durability, and bow durability. |
| Charm Aura Trigger Fix | Restores inventory charm auras after transitions, corpse recovery, and respawns. | No other choices. |
| Enhanced Damage Min/Max Fix | Fixes off-weapon Enhanced Damage with flat minimum or maximum damage. | No other choices. |
| Ethereal Item Rules | Controls which generated items may roll ethereal and droprate. | Enable each rule group, ethereal chance, excluded item-type codes, and Set or indestructible exceptions. |
| Repair Costs Cap | Limits NPC repair prices and can add permanent durability wear. | Gold cost cap and wear chance. |
| Vendor Stock Refresh | Adds a button that refreshes a vendor's stock like the gamble refresh | No other choices. |
| Potion Auto Pickup | Auto picks ground potions to belt columns or inventory. | Exact misc.txt potion codes, belt columns, player-action scan rate, and inventory fallback codes. |
| Mass Identify | Identifies items by Shift-right-clicking a Tome of Identify. | Inventory is always included; choose free identification and whether to include Cube, personal stash, and shared stash. |
| Cube Quick Move | Packs quick-moved multi-row items into the Cube from the bottom-right. | No other choices. |
| Equipped Item to Cube | Moves a Ctrl-clicked equipped item directly into the Horadric Cube. | No other choices. |
| Transmute Hotkey | Activates the Cube Transmute button from a hotkey. | Configurable keyboard hotkey that remains visible to D2R after the action is queued. |
| Prevent Merc Death in Town | Stops targeted lingering-damage ticks from killing mercenaries in town. | No other choices. |
| Remote Stash | Opens the native personal and shared stash from anywhere. | Hotkey mode plus a plugin-owned Inventory button with automatic or anchored placement, custom dimensions, sprite files, and state frames. |
| Floating Damage | Shows damage numbers and DPS | Numbers, DPS, colors, sizes, animation, combining, layout, font, and session hotkey. The assigned hotkey appears under the DPS counter at two-thirds size. Index 12 loads Kodia from the active mod |
| Larzuk Sockets Tweaks | Controls Larzuk's socket reward by difficulty and item quality. | Minimum and maximum sockets for Magic, Rare, Set, Unique, and Crafted items; `null` uses vanilla. Native base, type, item-level, and inventory limits still apply. |
| Bulk Skill Point Allocation | Uses Ctrl for a batch and Shift for all usable skill points. | Ctrl batch size, Shift confirmation, diagnostics, and displayed confirmation text. |
| Progressive Affixes*| Force a number of affixes on magic, rare and crafted items, automatic or progressive | Item-level thresholds and weighted affix-count rows; every percentage row must total 100. |

*Progressive Affixes plugin replaces my related memory patches. You can now use this plugin and remove these patches.

Advanced Item Tooltips and Extended Item Stats were not included in this Suite. AIT is redundant with Dimentio's native Loader feature. EIS was buggy and not that useful.
### Default states

- Every plugin ships with its master `enabled` switch set to `true`.
- To disable one plugin, set its switch to `false` and restart the game, or
  uninstall its DLL.
- Default Hotkeys : `Shift+T` for Transmute, `Shift+R` for Remote
  Stash, and `Shift+Z` for Floating Damage.
Configurable.
- Transmute keeps its SDK v3 action in D2R's Controls menu, queues the native
  Cube button action, and passes both key press and release through to D2R.

## Memory patches

Installing a patch JSON enables its complete behavior; removing it disables the
behavior.

| Patch | Player effect | Configuration choice |
|---|---|---|
| Gamble Screen Limit | Expands the supported Gamble screen item count. | Fixed. |
| Ground Item Label Limit | Raises the number of simultaneous ground labels. | Choose the 64 or 128 file; never install both. |
| Linear Magic Find | Uses the linear Magic Find formula. | Fixed. |
| Quantity Display Fix | Corrects quantity display behavior. | Fixed. |
| Level 100+ Characters | Allows level 100+ characters to join games. | Fixed. |
| Extended Gold Pickup | Greatly increases automatic gold pickup range. | The paired range values may be changed together. |
| No Run Penalties | Keeps full defense and block chance while running. | Fixed. |
| Enemy Resistance vs Immunes | Allows resistance reduction to affect immune monsters. | Fixed. |
| Maximum Staffmods | Gives three staffmods with vanilla random +1 to +3 values. | Fixed. |
| Gold Capacities | Uses character level × 100,000 and a 25,000,000 stash cap. | Character multiplier and stash cap may be changed. |
| Hireling AI | Improves ranged hireling following, activity, and retreat behavior. | Follow, activity, retreat chance, and retreat distance values may be changed. |
| Hit Chance 0% to 100% | Replaces the vanilla 5%-95% actual and displayed limits. | Lower and upper bounds may be changed together. |
| ITD vs Champions and Uniques | Extends Ignore Target Defense to champions and uniques. | Fixed. |
| Infinite Quantities | Stops arrows, bolts, throwing weapons, and tomes from consuming quantity. | Fixed. |
| Infinite Quest Rewards | Allows Anya, Charsi, and Larzuk rewards to be reused. | Fixed. |
| Normal Area Scaling | Uses `levels.txt` area levels for Normal monsters. | Fixed. |
| Player Difficulty Overrides | Removes the `/players 8` ceiling and raises the allowed maximum to 65,535. | Choose the active value normally with `/players x`; no JSON edit is needed. |
| Thorns/Burn Kill Credit | Restores kill credit, experience, and death triggers for reflected or burning kills. | Fixed. |
| Preserve Terror Zone Music | Keeps each area's normal music while it is terrorized. | Fixed. |

### Editing patch values safely

- Never change `rva`, `expected`, `op`, or `size`.
- Values beginning with `0x` are hexadecimal.
- When a patch repeats a value, update every documented copy identically.
- Keep only one official or customized copy of the same patch installed.
- Ground Item Label Limit must use the validated 64 or 128 file rather than
  hand-edited values.
- Extended Gold Pickup has two one-byte values; keep them identical.
- Gold Capacities repeats the stash cap three times; keep all three identical
  and no higher than `0x7FFFFFFF`.
- Hireling AI repeats its activity value; keep both copies identical, keep
  chances from 0 to 100, and keep follow distance at or below catch-up distance.
- Hit Chance has three lower and three upper writes; keep each group identical
  and use `0 <= lower < upper <= 100`. Do not edit the separate `0xEB` display
  unlock.

`Player Difficulty Overrides` changes the maximum allowed value, not the active
difficulty. Use `/players 16`, `/players 64`, or another value in game. The old
Community Pack offers the same ceiling through `misc.playersCommandLimit`; do
not enable both implementations.

#
## Credits

- **RuffnecKk** : Suite integration, D2R 3.2 ports, configuration, and testing.
- **eezstreet** : [D2R data documentation](https://eezstreet.github.io/d2rdoc/).
- **D2RLoader PluginSDK**: plugin ABI and SDK v3 services.
- **D2RLAN / D2RHUD** : original Floating Damage renderer and behavioral reference
- **D2MOO contributors**: semantic reference for applicable historical Diablo
  II engine behavior; no D2MOO address or 32-bit ABI is used as D2R 3.2 proof.

See `THIRD_PARTY_NOTICES.md` for component-level credits. This project is not
affiliated with or endorsed by Blizzard Entertainment.
