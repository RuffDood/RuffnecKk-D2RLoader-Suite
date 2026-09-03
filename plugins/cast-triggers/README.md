# Cast Triggers 1.0.0

Cast Triggers adds Path of Exile-style skill procs to Diablo II: Resurrected
items.

```text
X% Chance to cast level X [Skill] when casting a skill
X% Chance to cast [Skill] at the source skill level when casting a skill
X% Chance to cast level X [Skill] while channeling
X% Chance to cast level X [Skill] when casting [Source Skill]
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



## Credits

- Author: `RuffnecKk`.
- D2MOO is the semantic reference for item properties and server skill behavior.
- D2RLoader and its PluginSDK provide the plugin runtime.
