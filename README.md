# RuffnecKk D2RLoader Suite

This is the source repository for RuffnecKk's D2RLoader plugins, JSON memory
patches, and Diablo II: Resurrected research.

**Browse plugins on the [D2RLoader Hub](https://d2rloader.net/hub).**
Set the **Type** filter to **Plugin**, then search for **RuffnecKk** or the
plugin name. Plugins are released individually. Check each listing for its
supported game and D2RLoader versions, dependencies, and installation details.

## Quick installation

Follow the instructions on the plugin's Hub listing. For manual installation,
choose one location and keep the package's folder layout:

- Global: `<D2R>/d2rloader/`
- One mod only: `<D2R>/mods/<mod>/d2rloader/`

Place DLLs and their companion files in `plugins/`, bundled settings in
`config/`, and memory patch JSON files in `patches/`, following the package's
instructions. Keep one copy of each plugin and start the game with D2RLoader.

## Plugin source

The [Suite build list](plugins/CMakeLists.txt) defines the plugin targets.
Plugin directories hold the source, build files, and any settings, assets,
or tests that belong to that component. The table highlights several of those plugins.

| Plugin | What it does | Main options |
|---|---|---|
| Cube Quick Move | Ctrl-Click moves items to cube starting from the bottom right. | No extra options. |
| Automatic Materials Deposit | Deposits supported materials into their assigned Advanced Stash slots. | Hotkey, optional Inventory button, item filters, and support for materials in a carried Cube. |
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
| Burn Damage Fix | Restores Burn damage, applies fire resistance and mastery, and adds a burning overlay. | No extra options. |
| Floating Damage | Shows damage numbers and an optional DPS counter. | Colors, size, animation, layout, font, combining, and Controls binding. |
| Prevent Merc Death in Town | Stops lingering damage from killing mercenaries in town (Open wounds, poison). | No extra options. |
| Cast Triggers | Adds chance-to-cast events for modders, including skill casts, Open Wounds, Crushing Blow, and attack attempts. | Trigger families, conditions, and skills. |
| Armageddon-Hurricane CtC Fix | Lets Armageddon and Hurricane start correctly from chance-to-cast effects. | No extra options. |
| Resistance Floor | Configures minimum resistances for characters, minions, and monsters. | Limits for each category. |
| MapSense | Reveals maps, marks destinations, draws GPS routes, and includes a map seed selector. | Walk/Run and Teleport routes, markers, themes, and custom destinations. |
| PlayerX Scaling Tweaks | Extends `/players` controls. | Command limit, minimum setting, monster XP and life scaling caps, NoDrop party simulation, and optional Battle.net-style scaling. |
| Doll Explosion | Delays undead Doll death explosions and makes their physical damage configurable. | Monster IDs, timing, radius, and damage formula. |

### Default hotkeys

- Remote Stash: `Shift+R`
- Floating Damage: `Shift+Z`
- Automatic Materials Deposit: `Shift+D`

You can change these bindings in D2RLoader Controls.

## Memory patches

The [patch directory](patches) contains optional JSON memory patches. Check
each patch's game-build compatibility before installing it. Installing or
removing a patch takes effect after a restart.

| Patch | What changes for the player |
|---|---|
| -% to Enemy Resistance vs Immunes | -% to enemy res can affect immune monsters. |
| Four Character Item Codes | Allows modders to use four-character item codes, expanding the previous three-character limit. |
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
| No Gold Loss on Death | Prevents gold loss and drops on death. |
| No Run Penalties | Keeps full defense and block chance while running. |
| Preserve Terror Zone Music | Keeps an area's normal music while it is terrorized. |
| Quantity Display Fix | Restores quantity display on affected stackable items. |
| Shadow Master AI Fix | Fixes Shadow Master (and Warrior) AI that kept targeting same targets as it owners |
| Thorns/Burn Kill Credit | Restores experience and kill credit for reflected or burning kills. |

For complete Burn damage behavior, install Burn Damage Fix together with the
independent Thorns/Burn Kill Credit patch. The DLL owns damage behavior and visual replay; Floating Damage owns periodic numbers, and the JSON patch owns
experience and kill attribution.

## Build from source

Use Windows x64 with Visual Studio 2022 and the Desktop development with C++
workload, CMake 3.28 or newer, Git, and PowerShell. The pinned D2RLoader SDK
sources are included. CMake fetches additional dependencies for individual
plugins when needed.

From the repository root, run these commands:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

CTest runs offline checks. In-game compatibility is tracked per plugin release.

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

Start with the [D2R research snapshot](research/README.md) for native findings,
RVA and argument-contract references, the DataTables atlas, and source
provenance. The [native corpus identity](research/NATIVE-CORPUS.md) describes
the captured D2RLoader-hosted game image used for the research.
