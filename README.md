# RuffnecKk D2RLoader Suite



This Suite contains 24 independent plugins and 17 optional memory patches. The
ISC12 download also includes the offline D2R Save Converter. You can install
one component, a few favorites, or the complete bundles.

## Requirements

- Diablo II: Resurrected **3.3.93847**, **3.2.92777**, or Steam **3.3.93787**.
  Steam was not play-tested for this release, but plugins no longer reject it
  because of its channel, build name, or version number.

- **D2RLoader 1.2.0-beta**


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

The ISC12 ZIP also contains the offline D2R Save Converter in its own folder.
D2RMM Custom installs ISC12, but the Converter must be extracted and launched
manually when you need it.

### Upgrading to Suite 1.3.0

D2RLoader 1.2 now provides the ground-item label limit feature natively. Remove
all older copies of these files before upgrading:

```text
d2rloader/patches/ruffneckk-ground-item-label-limit-64.json
d2rloader/patches/ruffneckk-ground-item-label-limit-128.json
```

Normal Area Scaling is also no longer distributed by the Suite because Yinyin
has a working patch and mine apparently didn't work.


## Important compatibility note

Do not use this Suite together with the old Community Pack files:

- `plugin-items.dll`
- `plugin-levels.dll`
- `plugin-misc.dll`
- `plugin-quests.dll`
- `plugin-skills.dll`

Those older combined DLLs are no longer supported.

Feel free to install back Eezstreet's plugin pack : https://github.com/eezstreet/D2RL-Plugins

## Plugins

Install only the features you want. A plugin is active when its DLL is present;
plugins with real settings may also provide an in-game or file-based master
control.

| Plugin | What it does | Main options |
|---|---|---|
| Cube Quick Move | Ctrl-Click moves items to cube starting from the bottom right. | No extra options. |
| Bulk Currency Deposit | Transfers supported stackable currency items from inventory to their assigned stash slots. | Controls hotkey, optional Inventory button, position, and item filters. |
| Equipped Item to Cube | Moves a Ctrl-clicked equipped item directly into the Cube. | No extra options. |
| Mass Identify | Identifies items by Shift-right-clicking a Tome of Identify. | Free identification and optional Cube or stash coverage. |
| Potion Auto Pickup | Sends ground potions to matching belt columns or inventory. | Potion priorities, belt columns, and inventory overflow. |
| Remote Stash | Opens personal and shared stash pages from anywhere. | Hotkey, Inventory button, placement, size, custom sprites, and active-MPQ skin overrides. |
| Vendor Stock Refresh | Adds a button that refreshes normal vendor stock. | No extra options. |
| Bulk Skill Point Allocation | Uses Ctrl+Click for a batch and Shift+Click for all usable skill points. | Batch size and confirmation text. |
| Charm Aura Trigger Fix | Restores inventory charm auras after respawns and zone transitions | No extra options. |
| Ethereal Item Rules | Controls which items can become ethereal and how often. | Chance, excluded item types, Set items, and Indestructible items. |
| Item Durability | Adjusts durability loss and can give bows durability. | Loss resistance, ethereal durability, and bow durability. |
| Larzuk Sockets | Controls Larzuk's socket reward by difficulty and item quality. | Minimum and maximum sockets by quality. |
| Progressive Affixes | Controls how many affixes Magic, Rare, and Crafted items receive. | Automatic or progressive item-level rules. |
| Repair Costs Cap | Limits repair prices and can add permanent durability wear. | Gold cap and wear chance. |
| Enhanced Damage Min/Max Fix | Fixes off-weapon Enhanced Damage with flat damage bonuses. | No extra options. |
| Burn Damage Fix **NEW** | Restores and fixes Burn damage, adds a new overlay from Burning state, now goes through resistances and fire mastery is applied.| No extra options. |
| Floating Damage | Shows damage numbers and an optional DPS counter. | Colors, size, animation, layout, font, combining, and Controls binding. |
| Prevent Merc Death in Town | Stops supported lingering damage from killing mercenaries in town (Open wounds, poison). | No extra options. |
| Cast Triggers **NEW** | Unlocks new CtC ideas : X% CtC X Skill when X skill is cast, CtC from OW, CB, Attack attempts and more | Trigger families, conditions, skills |
| Armageddon-Hurricane CtC Fix **NEW** | Lets Armageddon and Hurricane start correctly from chance-to-cast effects. | Supported skills |
| Resistance Floor **NEW** | Lets configured units fall below the vanilla resistance floor. | Player, companion, monster, and Character Screen limits. |
| MapSense **NEW** | Reveals maps, marks important targets, and draws navigation lines on D2R's native automap. | In-game menu, colors, markers, navigation, themes, and custom destinations. |
| Extended Act Level IDs **NEW** | Allows custom levels to belong to any act. | No extra options. |
| ISC12 **NEW** | Extends ItemStatCost IDs to 12 bits for larger mod stat catalogs (4095 max rows) and includes D2R Save Converter. | No extra options. |

### Default hotkeys

- Remote Stash: `Shift+R`
- Floating Damage: `Shift+Z`
- Bulk Currency Deposit: `Shift+D`

These bindings are configurable in D2RLoader Controls. D2RLoader's current
Input service supports keyboard bindings, but not mouse buttons.

## D2R Save Converter

D2R Save Converter 1.0.0 is included only in the individual ISC12 ZIP. Extract
its folder before running the executable. It converts standard D2R and ISC12
saves without launching the game. Back up characters and shared stashes, close
the game, and review the source and destination shown before converting.

## Memory patches

A memory patch is a small optional rule change. Installing its JSON file
enables the complete behavior; removing the file disables it after a restart.

| Patch | What changes for the player |
|---|---|
| -% to Enemy Resistance vs Immunes | -% to enemy res can affect immune monsters. |
| Gamble Screen Limit | Raises Gamble screen from 14 items to 32. |
| Gold Capacities | Greatly raises carried-gold and stash-gold limits. |
| Ranged Hireling AI | Improves following, activity, and retreat behavior for ranged mercenaries. |
| Hit Chance 0% to 100% | Replaces the normal 5%-95% gameplay and Character Screen hit-chance limits with 0%-100%. |
| Infinite Quantities | Stops ammunition, throwing weapons, and tomes from consuming quantity. |
| Infinite Quest Rewards | Allows the Anya, Charsi, and Larzuk rewards to be reused. |
| ITD vs Champions and Uniques | Extends Ignore Target Defense to champions and unique monsters. |
| Level 100+ Characters | Allows characters above level 99 to join games. |
| Linear Magic Find | Uses a linear Magic Find formula without diminishing returns. |
| Maximum Staffmods | Gives eligible items three random +1 to +3 staffmods. |
| No Run Penalties | Keeps full defense and block chance while running. |
| Player Difficulty Overrides | Allows `/players` values above 8, up to 65,535. |
| Preserve Terror Zone Music | Keeps an area's normal music while it is terrorized. |
| Quantity Display Fix | Restores quantity display on affected stackable items. |
| Shadow Master AI Fix **NEW** | Keeps Shadow Warrior and Shadow Master targeting independent from their owner. |
| Thorns/Burn Kill Credit | Restores experience and kill credit for reflected or burning kills. |

For complete Burn damage behavior, install Burn Damage Fix together with the
independent Thorns/Burn Kill Credit patch. The DLL owns damage behavior and visual replay; Floating Damage owns periodic numbers, and the JSON patch owns
experience and kill attribution.



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
