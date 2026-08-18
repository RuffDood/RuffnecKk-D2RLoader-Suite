# RuffnecKk D2RLoader Suite



This Suite contains 17 independent plugins and 18 optional memory-patch
features for D2RLoader. You can install one component, a few
favorites, or the complete bundles.
## Requirements

- Diablo II: Resurrected build **3.2.92777**
- **D2RLoader 1.1.0-beta**

Download D2RLoader from [D2RLoader.net](https://d2rloader.net/).

## What should I download?

Open the GitHub **Releases** page and choose one of these options:

- **Individual plugin ZIPs** : recommended when you only want specific
  features.
- **Individual patch JSON files** : same as above.
- **All Plugins** : downlaods every Suite plugin.
- **All Patches** : downloads every patch selected for that Suite release.
  It includes both Ground Item Label Limit presets; keep only one active.

Do not use GitHub's automatic **Source code** archives as installation files.

## Quick installation

Choose one installation location:

- Global: `<D2R>/d2rloader/`
- One mod only: `<D2R>/mods/<mod>/d2rloader/`

Then:

1. Put plugin DLLs in `d2rloader/plugins/`.
2. Put patch JSON files in `d2rloader/patches/`.
3. Start the game with D2RLoader.


Never install the same plugin globally and inside a mod at the same time.

Most plugin settings are stored in `d2rloader/config/`. Plugins create their
default TOML configuration the first time they run and do not overwrite your
existing settings. Larzuk Sockets and Bulk Skill Point Allocation use JSON
configuration files included in their downloads.

For Remote Stash, please read the README included in the downloaded ZIP. TDLR the setup should now be easier and it's recommended you delete stuff you installed from previous versions

## Important compatibility note

Do not use this Suite together with the old Community Pack files:

- `plugin-items.dll`
- `plugin-levels.dll`
- `plugin-misc.dll`
- `plugin-quests.dll`
- `plugin-skills.dll`

Those older combined DLLs are no longer supported.

Feel free to install back Eezstreet's plugin pack :

## Plugins

All plugins are enabled by default. Install only the features you want, or set
a plugin's `enabled` option to `false` in its configuration.

| Plugin | What it does | Main options |
|---|---|---|
| Cube Quick Move | Ctrl-Click moves items to cube starting from the bottom right. | No extra options. |
| Equipped Item to Cube | Moves a Ctrl-clicked equipped item directly into the Cube. | No extra options. |
| Mass Identify | Identifies items by Shift-right-clicking a Tome of Identify. | Free identification and optional Cube or stash coverage. |
| Potion Auto Pickup | Sends ground potions to matching belt columns or inventory. | Potion priorities, belt columns, and inventory overflow. |
| Remote Stash | Opens personal and shared stash pages from anywhere. | Hotkey, Inventory button, placement, size, and custom sprites. |
| Transmute Hotkey | Activates the Cube Transmute button with a keyboard shortcut. | Configurable keyboard hotkey. |
| Vendor Stock Refresh | Adds a button that refreshes normal vendor stock. | No extra options. |
| Bulk Skill Point Allocation | Uses Ctrl for a batch and Shift for all usable skill points. | Batch size and confirmation text. |
| Charm Aura Trigger Fix | Restores inventory charm auras after important game transitions. | No extra options. |
| Ethereal Item Rules | Controls which items can become ethereal and how often. | Chance, excluded item types, Set items, and Indestructible items. |
| Item Durability | Adjusts durability loss and can give bows durability. | Loss resistance, ethereal durability, and bow durability. |
| Larzuk Sockets | Controls Larzuk's socket reward by difficulty and item quality. | Minimum and maximum sockets by quality. |
| Progressive Affixes | Controls how many affixes Magic, Rare, and Crafted items receive. | Automatic or progressive item-level rules. |
| Repair Costs Cap | Limits repair prices and can add permanent durability wear. | Gold cap and wear chance. |
| Enhanced Damage Min/Max Fix | Fixes off-weapon Enhanced Damage with flat damage bonuses. | No extra options. |
| Floating Damage | Shows damage numbers and an optional DPS counter. | Colors, size, animation, layout, font, combining, and hotkey. |
| Prevent Merc Death in Town | Stops supported lingering damage from killing mercenaries in town (Open wounds, poison). | No extra options. |

### Default hotkeys

- Transmute Hotkey: `Shift+T`
- Remote Stash: `Shift+R`
- Floating Damage: `Shift+Z`

These hotkeys are configurable. D2RLoader's current Input service supports
keyboard bindings, but not mouse-buttons.
## Memory patches

A memory patch is a small optional rule change. Installing its JSON file
enables the complete behavior; removing the file disables it after a restart.

| Patch | What changes for the player |
|---|---|
| -% to Enemy Resistance vs Immunes | -% to enemy res can affect immune monsters. |
| Gamble Screen Limit | Raises Gamble screen from 14 items to 32. |
| Gold Capacities | Greatly raises carried-gold and stash-gold limits. |
| Ground Item Label Limit | Raises the simultaneous ground-label limit from 32 to either 64 or 128. Choose one preset. |
| Ranged Hireling AI | Improves following, activity, and retreat behavior for ranged mercenaries. |
| Hit Chance 0% to 100% | Replaces the normal 5%-95% hit-chance limits with 0%-100%. |
| Infinite Quantities | Stops ammunition, throwing weapons, and tomes from consuming quantity. |
| Infinite Quest Rewards | Allows the Anya, Charsi, and Larzuk rewards to be reused. |
| ITD vs Champions and Uniques | Extends Ignore Target Defense to champions and unique monsters. |
| Level 100+ Characters | Allows characters above level 99 to join games. |
| Linear Magic Find | Uses a linear Magic Find formula without diminishing returns. |
| Maximum Staffmods | Gives eligible items three random +1 to +3 staffmods. |
| No Run Penalties | Keeps full defense and block chance while running. |
| Normal Area Scaling | Uses each area's `levels.txt` level for Normal monsters. |
| Player Difficulty Overrides | Allows `/players` values above 8, up to 65,535. |
| Preserve Terror Zone Music | Keeps an area's normal music while it is terrorized. |
| Quantity Display Fix | Restores quantity display on affected stackable items. |
| Thorns/Burn Kill Credit | Restores experience and kill credit for reflected or burning kills. |

`Player Difficulty Overrides` only raises the allowed maximum. Choose the
active difficulty in game with commands such as `/players 16` or `/players 64`.
Do not enable it together with another feature that changes the same command
limit.

Progressive Affixes replaces the older Force Affixes patches for Magic, Rare,
Crafted, and Jewel items. Remove those older patches before enabling the
plugin.

Three earlier standalone DLLs are now simpler JSON patches:

- `GambleScreenLimit.dll` became `ruffneckk-gamble-screen-limit.json`.
- `GroundItemLabelLimit.dll` became
  `ruffneckk-ground-item-label-limit-64.json` and
  `ruffneckk-ground-item-label-limit-128.json`; **install only one**.
- `QtyDisplayIssue.dll` became `ruffneckk-quantity-display-fix.json

## Changing or removing features

- Disable a plugin with its master `enabled = false` setting, or remove its
  DLL and configuration after closing the game.
- Disable a memory patch by removing its JSON file and restarting the game.
- Keep only one copy of each plugin or patch.
- Back up characters and shared stashes before changing a heavily customized
  setup.

## Credits

- **RuffnecKk** — Suite integration, D2R 3.2 ports, configuration, and testing.
- **eezstreet** — [D2R data documentation](https://eezstreet.github.io/d2rdoc/).
- **D2RLoader contributors** — D2RLoader and PluginSDK v3.
- **D2RLAN / D2RHUD** — original Floating Damage renderer and behavioral reference.
- **D2MOO contributors** — semantic reference for applicable historical Diablo II behavior.

See `THIRD_PARTY_NOTICES.md` for complete component-level credits and licenses.
This project is not affiliated with or endorsed by Blizzard Entertainment.
