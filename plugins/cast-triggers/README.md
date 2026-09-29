# Cast Triggers 1.1.12

By **RuffnecKk**, with integrated **Cast on Cast aiming and visual corrections
by CelestialRayOne (Celestial)**, used with his permission.

Cast Triggers adds Path of Exile-style skill procs to Diablo II: Resurrected
items.

```text
X% Chance to cast level X [Skill] when casting a skill
X% Chance to cast [Skill] at the source skill level when casting a skill
X% Chance to cast level X [Skill] while channeling
X% Chance to cast level X [Skill] when casting [Source Skill]
X% Chance to cast level X [Skill] when you Kill an Enemy
X% Chance to cast level X [Skill] when you Block
```

> [!IMPORTANT]
> Cast Triggers is a framework for mod authors. Installing the DLL does not add
> properties to items by itself. The consuming mod must define its own
> ItemStatCost rows, Properties rows, tooltip strings, and item or affix data.

The item always chooses the triggered skill, chance and level. A mod may also
limit a property to one source skill or to a manually defined list of source
skills.

## Quick setup for mod authors

1. Install the DLL and TOML in one D2RLoader scope.
2. Reserve stable, unused `ItemStatCost.txt` and `Properties.txt` numeric IDs.
3. Clone `item_skillonattack` once for every trigger stat you want.
4. Clone `att-skill` once for every matching property.
5. Add the tooltip keys to `item-modifiers.json`.
6. Put the synthetic ItemStatCost IDs in the TOML when the trigger family
   requires them.
7. Add the property to an item, unique, set, runeword, or affix.
8. Verify the tooltip first, then test the proc in game at `100%` chance.

The sections below walk through the complete setup.

## Install the plugin

Copy the DLL and TOML into one D2RLoader scope. Use either the global scope or
the mod-local scope, never both at once.

```text
<D2R>/d2rloader/plugins/d2rl-ruffneckk-cast-triggers.dll
<D2R>/d2rloader/config/ruffneckk-cast-triggers.toml
```

```text
<D2R>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-cast-triggers.dll
<D2R>/mods/<mod>/d2rloader/config/ruffneckk-cast-triggers.toml
```

## Add the properties to a mod

Choose unused numeric IDs in the consuming mod and keep them stable after
publishing items. `ItemStatCost.txt` and `Properties.txt` use independent ID
spaces: an ItemStatCost `*ID` does not need to equal the matching Properties
`*Id`.

Start with only the trigger families the mod actually needs. Generic
cast-on-cast requires two rows; channeling, each source-conditioned family, and
each combat outcome require their own rows.

Copy `item_skillonattack` in `ItemStatCost.txt` and `att-skill` in
`Properties.txt` for every wanted row:

| Purpose | Example `Stat` | Example property `code` | TOML mapping |
|---|---|---|---|
| Cast fixed level | `item_skilloncast` | `cast-skill` | none |
| Cast at source level | `item_skilloncastsamelevel` | `cast-skill-same-level` | none |
| Channel fixed level | `item_skillwhilechanneling` | `cast-skill-while-channeling` | `while_channeling.fixed_stat_id` |
| Channel at source level | `item_skillwhilechannelingsamelevel` | `cast-skill-while-channeling-same-level` | `while_channeling.same_level_stat_id` |
| Critical Strike | `item_skilloncritical` | `cast-skill-on-crit` | `critical_strike_stat_id` |
| Crushing Blow | `item_skilloncrushingblow` | `cast-skill-on-cb` | `crushing_blow_stat_id` |
| Open Wounds | `item_skillonopenwounds` | `cast-skill-on-ow` | `open_wounds_stat_id` |
| Attack Attempt | `item_skillonattackattempt` | `cast-skill-on-attack` | `attack_attempt_stat_id` |
| Block | `item_skillonblock` | `cast-skill-on-block` | `on_block.stat_id` |

For every `ItemStatCost.txt` row, copy the complete native row instead of
building one from empty cells. Then change its `Stat` and `*ID`, set
`itemevent1=doactive`, set `itemeventfunc1=20`, clear the second event pair,
keep `descfunc=15`, and put the tooltip key in both `descstrpos` and
`descstrneg`. Keeping the copied save, encode, callback, and send-bit fields is
required.

For every `Properties.txt` row, copy the complete native `att-skill` row. Keep
`*Enabled=1`, `func1=11`, `uiRangeType=7`, and set `stat1` to the matching
ItemStatCost `Stat` value. The example property codes may be changed, but must
remain stable after items using them are published.

Diablo II already provides Chance to Cast on Kill through the native **`kill`
event**, `item_skillonkill` stat and `kill-skill` property. This plugin does not
recreate that trigger. It optionally extends the existing callback to kills
caused by player spells, which vanilla normally excludes from item-proc
eligibility.

Set `on_kill.stat_id` to native `item_skillonkill` (`196`) to extend existing
`kill-skill` properties, or clone the complete `item_skillonkill` and
`kill-skill` rows into unused IDs. Keep `itemevent1=kill`,
`itemeventfunc1=20`, native encoding and save fields on that clone. A clone lets
the mod extend one property without extending its other native on-kill items.
Only one on-kill stat can be configured at a time; it is not a `doactive` stat.

### Source-conditioned families

Each condition needs its own fixed/source-level pair. For example:

| Purpose | Example `Stat` | Example property `code` |
|---|---|---|
| Fixed, only from Frost Nova | `item_skillwhenfrostnova` | `cast-skill-when-frost-nova` |
| Source level, only from Frost Nova | `item_skillwhenfrostnovasamelevel` | `cast-skill-when-frost-nova-same-level` |

These names do not force the triggered skill to be Nova. The item's `par#`
may select Fire Ball, Nova, or any other existing skill. The TOML rule only
selects which manually cast source skills may activate that property family.

## Add tooltip strings

The plugin does not edit another mod's localization files. Add each key to a
string table D2R loads, normally:

```text
data/local/lng/strings/item-modifiers.json
```

Use unused numeric string IDs and add every language shipped by the mod. These
English values are ready for `enUS`:

| Key | `enUS` |
|---|---|
| `CastOnCast` | `%d%% Chance to cast level %d %s when casting a skill` |
| `CastOnCastSameLevel` | `%d%% Chance to cast %.*s at the source skill level when casting a skill` |
| `CastWhileChanneling` | `%d%% Chance to cast level %d %s while channeling` |
| `CastWhileChannelingSameLevel` | `%d%% Chance to cast %.*s at the source skill level while channeling` |
| `CastOnCritical` | `%d%% Chance to cast level %d %s on Critical Strike` |
| `CastOnCrushingBlow` | `%d%% Chance to cast level %d %s on Crushing Blow` |
| `CastOnOpenWounds` | `%d%% Chance to cast level %d %s on Open Wounds` |
| `CastOnAttackAttempt` | `%d%% Chance to cast level %d %s on Attack Attempt` |
| `CastOnBlock` | `%d%% Chance to cast level %d %s when you Block` |
| `CastOnKillConfigured` | `%d%% Chance to cast level %d %s when you Kill an Enemy` |
| `CastWhenFrostNova` | `%d%% Chance to cast level %d %s when casting Frost Nova` |
| `CastWhenFrostNovaSameLevel` | `%d%% Chance to cast %.*s at the source skill level when casting Frost Nova` |

The last two keys are examples. Create wording that matches each source family
defined by the mod. A key is reusable on every item and affix using that family.

Merge each entry inside the existing JSON array. Do not replace the complete
string table. For example:

```json
{
  "id": 51000,
  "Key": "CastOnCast",
  "enUS": "%d%% Chance to cast level %d %s when casting a skill"
},
{
  "id": 51001,
  "Key": "CastOnCastSameLevel",
  "enUS": "%d%% Chance to cast %.*s at the source skill level when casting a skill"
}
```

The JSON `id` values above are examples, not values reserved by Cast Triggers.
Choose IDs that are unused in the consuming mod. The `Key` must exactly match
the value placed in `descstrpos` and `descstrneg`.

Fixed-level tooltips use `%d` for the encoded target level. Source-level
tooltips must use `%.*s`: the reserved value `63` becomes the precision
argument for the skill name and is intentionally not printed as a level.

## Connect the TOML to the data rows

The TOML IDs are ItemStatCost `*ID` values, not Properties `*Id` values. Every
nonzero trigger ID must be unique.

```toml
enabled = true

[on_cast]
include_skill_ids = []
exclude_skill_ids = []

[while_channeling]
enabled = true
interval_frames = 50
fixed_stat_id = <CHANNEL_FIXED_STAT_ID>
same_level_stat_id = <CHANNEL_SOURCE_LEVEL_STAT_ID>
include_skill_ids = []
exclude_skill_ids = []

[[source_skill_triggers]]
name = "frost_nova"
source_skill_ids = [44]
fixed_stat_id = <FROST_NOVA_FIXED_STAT_ID>
same_level_stat_id = <FROST_NOVA_SOURCE_LEVEL_STAT_ID>

[combat_triggers]
attack_attempt_stat_id = <ATTACK_ATTEMPT_STAT_ID>
critical_strike_stat_id = <CRITICAL_STAT_ID>
crushing_blow_stat_id = <CRUSHING_BLOW_STAT_ID>
open_wounds_stat_id = <OPEN_WOUNDS_STAT_ID>

[on_kill]
stat_id = <NATIVE_OR_CLONED_KILL_STAT_ID>

[on_block]
stat_id = <BLOCK_STAT_ID>

[diagnostics]
enabled = false
```

`source_skill_ids` accepts one or many `Skills.txt` IDs. For example, a mod may
put all of its Cold spells in one list. It is a manual list, not an automatic
elemental classification. Add another `[[source_skill_triggers]]` block and
another stat/property pair for a different condition.

Generic `cast-skill` and `cast-skill-same-level` rows need no numeric TOML
mapping. Their native `doactive` event is enough. Channeling, source-specific,
and combat families do require their ItemStatCost IDs in the matching TOML
section. Leave an unused family at `0`; never reuse the same nonzero ID in two
families.

Channeling rolls once immediately, then at `interval_frames`. D2R runs at 25
server frames per second, so 50 frames equals two seconds. Ordinary
`cast-skill` properties do not activate from channeling skills.

### Kill and block behavior

`on_kill.stat_id=0` leaves vanilla On Kill unchanged. A nonzero ID extends that
stat when the server reports a player kill whose damage record lacks the native
item-proc eligibility bit. The original callback still reads the item, rolls
the player's native chance and selects the skill, level and target. Spell
damage and weapon damage are unchanged. Kills already eligible natively, other
stats/events, and non-player owners are forwarded unchanged. Minion and
mercenary ownership is not reassigned to the player.

`on_block.stat_id=0` disables block dispatch. A nonzero ID reserves a separate
`doactive` stat for confirmed player blocks, including Assassin Weapon Block.
The blocking player owns the proc and the incoming attacker is its native
target. Shield and Weapon Block are intentionally combined: some missile
paths encode both as the same final block result. Misses, Dodge, Avoid, Evade,
absorb and zero damage alone do not qualify. Block animation cooldown and an
uninterruptible animation do not define whether a block occurred. Each
accepted native block outcome gets one dispatch, with no per-frame cooldown;
each matching item entry retains its normal chance roll.

Both IDs must be distinct from every configured channel/source/combat stat.
Use fixed-level item properties for these families; neither derives a source
skill level. A triggered missile that kills another enemy can activate On Kill again.
For example, a kill can trigger Fire Ball, and a kill caused by that Fire Ball
can trigger another Fire Ball. This is intentional On Kill behavior. It does
not mean that every triggered spell counts as a new manual cast.
Trigger decisions and chance rolls run on the server. Multiplayer, PvP,
damage-over-time kills and minion/mercenary attribution are not newly qualified
by the single-player tests for this release.

To disable either addition, set its ID to `0`. Keep ItemStatCost rows and IDs
that saved items use; removing them is not a safe rollback. A DLL rollback must
also restore its compatible TOML, since older versions reject the new sections.

## Put a proc on an item or affix

The native fields retain their usual meaning:

| Item field | Affix field | Value |
|---|---|---|
| `prop#` | `mod#code` | Cast Triggers property code |
| `par#` | `mod#param` | Triggered skill ID |
| `min#` | `mod#min` | Chance from 1 to 100 |
| `max#` | `mod#max` | Fixed level 1-62, or `63` for source-level mode |

Examples:

```text
prop1=cast-skill
par1=<TRIGGERED_SKILL_ID>
min1=<CHANCE>
max1=<FIXED_LEVEL>
```

```text
prop1=cast-skill-while-channeling-same-level
par1=<TRIGGERED_SKILL_ID>
min1=<CHANCE>
max1=63
```

```text
mod1code=cast-skill-when-frost-nova
mod1param=<TRIGGERED_SKILL_ID>
mod1min=<CHANCE>
mod1max=<FIXED_LEVEL>
```

`63` is only the source-level marker; it does not cast a level 63 skill.
Attack Attempt includes misses and Shift-ground attacks. It remains separate
from Diablo's native `att-skill` and `hit-skill`. Critical Strike does not
include Deadly Strike.

### How the four value columns work

| Value | Meaning |
|---|---|
| Property code | Selects the trigger family. |
| `par#` / `mod#param` | Selects the skill that will be triggered. |
| `min#` / `mod#min` | Sets the native proc chance from 1 to 100. |
| `max#` / `mod#max` | Sets target level 1-62, or `63` for source-level mode. |

The source skill and triggered skill are separate. In a property named
`cast-skill-when-frost-nova`, Frost Nova is the condition configured in TOML;
`par#` may still select Fire Ball, Nova, or any other valid target skill.



## Integrated Cast on Cast features — CelestialRayOne

**Full credit for the three aiming and visual corrections below goes to
CelestialRayOne (Celestial), author of Cast on Cast.** They are adapted from his
code and integrated with his permission. His upstream attribution to **ESR**
for the original Diablo II 2.4 patch mechanics is preserved as well.

| Option | What it changes |
|---|---|
| `server_proc_aim` | Corrects native item-proc missile aiming toward the intended target on the server, where the actual skill is executed. |
| `client_proc_aim` | Corrects the matching client target handling so the displayed proc follows its intended target and origin. |
| `client_missile_rewind` | Places a newly created client missile one movement step farther back, making its first visible frame start closer to its origin. This does not add server damage. |

All three options default to `true`:

```toml
[proc_presentation]
server_proc_aim = true
client_proc_aim = true
client_missile_rewind = true
```

Add this section once to an existing configuration, or use the supplied TOML.
Set an individual option to `false` to disable that correction. Completely
close and restart the game after changing these settings; hot reload is not
supported.

These corrections require no new ItemStatCost rows, Properties rows, tooltip
strings or Cube recipes. The server and client aiming corrections apply to
native item procs passing through the affected skill paths, beyond just
Cast Triggers' on-cast family. The rewind adjustment also affects ordinary
missiles created through the shared standard client missile builder; it is
not limited to triggered missiles and does not cover every possible effect.

**Remove the separate Cast on Cast DLL before using this combined plugin.**
Cast Triggers supplies the cast-event handling and incorporates these three
corrections; running both is not supported. This does not require removing
Celestial's other plugins.

### Target and origin examples

- A manual cast with a ground target can trigger Fire Ball toward that target.
- Nova and Frost Nova expand from the triggering character, including the
  tested critical-strike, Crushing Blow, Open Wounds and block cases.
- A Teleport on-cast Fire Ball starts at the departure point and aims toward
  the selected destination. Cast Triggers additionally sends this proc's visual
  notification before relocation so its original position is preserved.
- Combat-triggered skills use their event target; they do not generally read
  the player's current cursor position.

The Teleport notification correction and integration with Cast Triggers are
RuffnecKk's work. The three merged aiming/presentation features retain
CelestialRayOne's authorship.

## Status and troubleshooting

Run the following in the D2RLoader console:

```text
cast-triggers
```

It reports plugin activation, trigger counters, and the three proc presentation
options. For retained event details, enable `[diagnostics] enabled = true` in
the TOML, restart the game, reproduce the behavior, then run the command again.
Disable detailed diagnostics for ordinary play.

The supplied configuration is a generic modder starter: optional numeric stat
mappings are `0`, diagnostics are off, and all three presentation options are
on. Replace the stat mappings with your mod's IDs to enable those families.
Do not copy BKVince's numeric IDs into another mod without checking its tables.

This DLL was tested in BKVince with D2RLoader 1.3.1-beta and Battle.net D2R
3.3.93847. Confirmed single-player checks include spell kills, intentional
On Kill missile chains, Open Wounds, Crushing Blow, critical strikes including
Whirlwind, actual shield blocks, absence of block procs during Battle Orders,
and Teleport's departure origin and cursor aim. These results do not establish
multiplayer, Steam, CrossOver or every consuming mod's compatibility.

## Credits

- **RuffnecKk** — Cast Triggers, its trigger families, integration and subsequent fixes.
- **CelestialRayOne (Celestial / Bogdan Bulai)** — original Cast on Cast code for server proc aiming, client proc target/origin handling, and missile first-frame rewind. Integrated with his permission; full credit for these features remains his.
- **ESR** — original Diablo II 2.4 patch mechanics, as credited by CelestialRayOne upstream.
- The adapted code is covered by the MIT license. Keep the accompanying `THIRD-PARTY-NOTICES.md`, including its copyright and permission text, with redistributed copies.
- D2MOO is the semantic reference for item properties and server skill behavior.
- D2RLoader and its PluginSDK provide the plugin runtime.
