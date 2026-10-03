# RuffnecKk D2RLoader Suite

This repository holds buildable plugin source code. New plugin downloads
are distributed individually through the [D2RLoader Hub](https://d2rloader.net/).
Historical Suite downloads remain available. The Suite 1.4.2 installation notes
below describe that historical release; current source changes are not a new
Suite release or a claim that every component has completed runtime testing.
Research, validation notes, plugin guides, caches, and generated previews stay outside the tracked source tree.



Suite 1.4.2 contains 22 independent plugins and 18 optional memory patches.
You can install one component, a few favorites, or the complete bundles.

This is strictly a MapSense update. All other components remain unchanged from
Suite 1.4.1. MapSense 2.0.2 improves saved settings, quest navigation and custom
destination instructions; see [the changelog](CHANGELOG.md).

## Requirements

- Diablo II: Resurrected **3.3.93847**, **3.2.92777**, or Steam **3.3.93787**.

- **D2RLoader 1.3** for the published Suite 1.4 release.


Download D2RLoader from [D2RLoader.net](https://d2rloader.net/).


## What should I download?

Open the GitHub **Releases** page and choose one of these options:

- **Individual plugin ZIPs** : recommended when you only want specific
  features.
- **Individual patch JSON files** : same as above.
- **All Plugins** : downloads every Suite plugin.
- **All Patches** : downloads every Suite patch.

## Quick installation

Choose one installation location:

- Global: `<D2R>/d2rloader/`
- One mod only: `<D2R>/mods/<mod>/d2rloader/`

Then:

1. Put plugin DLLs in `d2rloader/plugins/`.
2. Put patch JSON files in `d2rloader/patches/`.
3. Start the game with D2RLoader.


Never install the same plugin globally and inside a mod at the same time.

When a plugin includes a configuration, copy both the `plugins/` and `config/`
folders into the same D2RLoader installation. Plugins without settings are
active by the presence of their DLL. Existing configuration files are never
overwritten when a plugin starts.

### D2RMM Custom for D2RLoader

Suite plugin ZIPs and patch downloads use the paths expected by D2RMM Custom
1.9.6 for D2RLoader. Put ZIP archives or loose patch JSON files in D2RMM
Custom's `d2rloader/` folder, then restart D2RMM Custom or choose
**Plugins > Refresh**. Required companion files such as the MapSense map
generator are imported automatically.

### Upgrading to Suite 1.3.x

D2RLoader 1.2 now provides the ground-item label limit feature natively. Remove
all older copies of these files before upgrading:

```text
d2rloader/patches/ruffneckk-ground-item-label-limit-64.json
d2rloader/patches/ruffneckk-ground-item-label-limit-128.json
```

Normal Area Scaling is also no longer distributed by the Suite because Yinyin
has a working patch and mine apparently didn't work.


## Plugins

Install only the features you want. A plugin is active when its DLL is present;
plugins with real settings may also provide an in-game or file-based master
control.

| Plugin | What it does | Main options |
|---|---|---|
| Cube Quick Move | Ctrl-Click moves items to cube starting from the bottom right. | No extra options. |
| Automatic Materials Deposit | Deposits supported materials into their assigned stash slots in one action. | Controls hotkey, optional Inventory button, position, and item filters. |
| Equipped Item to Cube | Moves a Ctrl-clicked equipped item directly into the Cube. | No extra options. |
| Mass Identify | Identifies items by Shift-right-clicking a Tome of Identify. | Free identification and optional Cube or stash coverage. |
| Potion Auto Pickup | Sends ground potions to matching belt columns or inventory. | Potion priorities, belt columns, and inventory overflow. |
| Remote Stash | Opens personal and shared stash pages from anywhere. | Hotkey, Inventory button, placement, size, custom sprites, and active-MPQ skin overrides. |
| Vendor Stock Refresh | Adds a button that refreshes vendors stock screens. | No extra options. |
| Bulk Skill Point Allocation | Uses Ctrl+Click for a batch and Shift+Click for all usable skill points. | Batch size and confirmation text. |
| Ethereal Item Rules | Controls which items can become ethereal and how often. | Chance, excluded item types, Set items, and Indestructible items. |
| Item Durability | Adjusts durability loss and can give bows durability. | Loss resistance, ethereal durability, and bow durability. |
| Larzuk Sockets | Allows tweaking Larzuk's socket reward by difficulty and item quality. | Minimum and maximum sockets rewarded (by quality). |
| Progressive Affixes | Controls how many affixes Magic, Rare, and Crafted items receive. | Automatic or progressive item-level rules. |
| Repair Costs Cap | Limits repair prices and can add permanent durability wear. | Gold cap and wear chance. |
| Enhanced Damage Min/Max Fix | Fixes off-weapon Enhanced Damage with flat damage bonuses. | No extra options. |
| Burn Damage Fix | For modders. Restores and fixes Burn damage, adds a new fire overlay from Burning state, now goes through resistances + fire mastery is applied.| No extra options. |
| Floating Damage | Shows damage numbers and an optional DPS counter. | Colors, size, animation, layout, font, combining, and Controls binding. |
| Prevent Merc Death in Town | Stops lingering damage from killing mercenaries in town (Open wounds, poison). | No extra options. |
| Cast Triggers | For modders. Unlocks new CtC ideas : X% CtC X Skill when X skill is cast, CtC from OW, CB, Attack attempts and more | Trigger families, conditions, skills |
| Armageddon-Hurricane CtC Fix | Lets Armageddon and Hurricane start correctly from chance-to-cast effects. |  |
| Resistance Floor | Lowers resistances floors for characters, minions or monsters all below the vanilla resistance floor (used to be -100, now unlocked at -1000) | Limits for each category |
| MapSense | Reveals maps, marks important targets, and draws GPS route lines around corridors, doors, walls, and other obstacles. | Walk/Run and Teleport routes, in-game menu, colors, markers, themes, and custom destinations. |
| PlayerX Scaling Tweaks **NEW** | Expands `/players` controls and replaces the older Player Difficulty Overrides patch. | #of plyaers limit, force a minimum difficulty setting, monster xp/hp scaling caps, optional NoDrop party simulation, and optional Battle.net-style scaling. |

### Default hotkeys

- Remote Stash: `Shift+R`
- Floating Damage: `Shift+Z`
- Automatic Materials Deposit: `Shift+D`

These bindings are configurable in D2RLoader Controls. D2RLoader's current
Input service supports keyboard bindings, but not mouse buttons.

## Memory patches

A memory patch is a small optional rule change. Installing its JSON file
enables the complete behavior; removing the file disables it after a restart.

| Patch | What changes for the player |
|---|---|
| -% to Enemy Resistance vs Immunes | -% to enemy res can affect immune monsters. |
| Four Character Item Codes **NEW** | Allows modders to use four-character item codes, expanding the previous three-character limit. |
| Gamble Screen Limit | Raises Gamble screen from 14 items to 32. |
| Gold Capacities | Greatly raises carried-gold and stash-gold limits. |
| Ranged Hireling AI | Improves following, activity, and retreat behavior for ranged mercenaries. |
| Hit Chance 0% to 100% | Replaces the normal 5%-95% gameplay and Character Screen hit-chance limits with 0%-100%. |
| Infinite Quantities | Stops ammunition, throwing weapons, and tomes from consuming quantity. |
| Infinite Quest Rewards | Allows the Anya, Charsi, and Larzuk rewards to be reused |
| ITD vs Champions and Uniques | Extends Ignore Target Defense to champions and unique monsters. |
| Level 100+ Characters | Allows characters above level 99 to join games. |
| Linear Magic Find | Uses a linear Magic Find formula without diminishing returns. |
| Maximum Staffmods | Gives eligible items three random +1 to +3 staffmods. |
| No Gold Loss on Death **NEW** | No more losing or dropping gold upon death. |
| No Run Penalties | Keeps full defense and block chance while running. |
| Preserve Terror Zone Music | Keeps an area's normal music while it is terrorized. |
| Quantity Display Fix | Restores quantity display on affected stackable items. |
| Shadow Master AI Fix | Fixes Shadow Master (and Warrior) AI that kept targeting same targets as it owners |
| Thorns/Burn Kill Credit | Restores experience and kill credit for reflected or burning kills. |

For complete Burn damage behavior, install Burn Damage Fix together with the
independent Thorns/Burn Kill Credit patch. The DLL owns damage behavior and visual replay; Floating Damage owns periodic numbers, and the JSON patch owns
experience and kill attribution.

## Source code

The source code for the Suite plugins is available under `plugins/`.
Each plugin keeps its own build files, source, configuration, required assets, and tests when
applicable.

## Changing or removing features

- Disable a plugin with its master `enabled = false` setting, or remove its
  DLL and configuration after closing the game.
- Disable a memory patch by removing its JSON file and restarting the game.
- Keep only one copy of each plugin or patch.
- Back up characters and shared stashes before changing a heavily customized
  setup.

## Credits

- **RuffnecKk** — Suite integration, D2R 3.2 ports, configuration, and testing (Assisted with AI)
- **eezstreet** — [D2R data documentation](https://eezstreet.github.io/d2rdoc/).
- **D2RLoader contributors** — D2RLoader and PluginSDK v3/v4.
- **Fr4nsson** — original [D2R Damage Numbers](https://github.com/Fr4nsson/D2RDamageNumbers) project and feature design.
- **locbones / D2RHUD-2.4** — direct implementation source used for the Suite's D3D12/ImGui port
- **D2MOO contributors** — semantic reference for applicable historical Diablo II behavior.

See `THIRD_PARTY_NOTICES.md` for complete component-level credits and licenses.
This project is not affiliated with or endorsed by Blizzard Entertainment.

## Research for modders

Browse the [D2R research notes](research/d2r/README.md) for known RVAs, native findings and the DataTables atlas, including confidence levels, provenance and partial C++/Ghidra layouts.
