# Cast Triggers intermod data contract

Cast Triggers intentionally does not identify items in TOML. A consuming mod
adds ordinary native D2R properties to any item table that already supports
`prop#`, `par#`, `min#` and `max#` columns.

Numeric IDs below are placeholders. Choose unused IDs in your own tables and
never renumber them after release.

## 1. Add the ItemStatCost rows

Copy `item_skillonattack` once per row, then change the following fields. Keep
the native encoding, save and callback fields copied from the source row.

| Purpose | `Stat` | `*ID` | String key |
|---|---|---:|---|
| Fixed cast-on-cast | `item_skilloncast` | `<FREE_STAT_ID_1>` | `CastOnCast` |
| Source-level cast-on-cast | `item_skilloncastsamelevel` | `<FREE_STAT_ID_2>` | `CastOnCastSameLevel` |
| Critical Strike | `item_skilloncritical` | `<FREE_STAT_ID_3>` | `CastOnCritical` |
| Crushing Blow | `item_skilloncrushingblow` | `<FREE_STAT_ID_4>` | `CastOnCrushingBlow` |
| Open Wounds | `item_skillonopenwounds` | `<FREE_STAT_ID_5>` | `CastOnOpenWounds` |
| Attack Attempt | `item_skillonattackattempt` | `<FREE_STAT_ID_6>` | `CastOnAttackAttempt` |
| Fixed while channeling | `item_skillwhilechanneling` | `<FREE_STAT_ID_7>` | `CastWhileChanneling` |
| Source level while channeling | `item_skillwhilechannelingsamelevel` | `<FREE_STAT_ID_8>` | `CastWhileChannelingSameLevel` |

A source-conditioned family adds another distinct pair. This Frost Nova example
is only a naming convention chosen by the consuming mod:

| Purpose | `Stat` | `*ID` | String key |
|---|---|---:|---|
| Fixed when Frost Nova is cast | `item_skillwhenfrostnova` | `<FREE_STAT_ID_9>` | `CastWhenFrostNova` |
| Source level when Frost Nova is cast | `item_skillwhenfrostnovasamelevel` | `<FREE_STAT_ID_10>` | `CastWhenFrostNovaSameLevel` |

For every row, set `itemevent1=doactive`, `itemeventfunc1=20`, clear the second
event pair, retain `descfunc=15`, and put the listed key in both description
string fields.

The copied fields should remain equivalent to the native row:

```text
Signed=1
Send Bits=7
Send Param Bits=16
fCallback=1
Encode=2
Add=190
Multiply=256
1.09-Save Bits=21
1.09-Save Add=0
Save Bits=7
Save Add=0
Save Param Bits=16
damagerelated=1
descpriority=158
advdisplay=2
*eol=0
```

## 2. Add the Properties rows

Copy `att-skill` once per row. Retain `*Enabled=1`, `func1=11`,
`uiRangeType=7` and `*eol=0`:

| Purpose | `code` | `*Id` | `stat1` |
|---|---|---:|---|
| Fixed cast-on-cast | `cast-skill` | `<FREE_PROPERTY_ID_1>` | `item_skilloncast` |
| Source-level cast-on-cast | `cast-skill-same-level` | `<FREE_PROPERTY_ID_2>` | `item_skilloncastsamelevel` |
| Critical Strike | `cast-skill-on-crit` | `<FREE_PROPERTY_ID_3>` | `item_skilloncritical` |
| Crushing Blow | `cast-skill-on-cb` | `<FREE_PROPERTY_ID_4>` | `item_skilloncrushingblow` |
| Open Wounds | `cast-skill-on-ow` | `<FREE_PROPERTY_ID_5>` | `item_skillonopenwounds` |
| Attack Attempt | `cast-skill-on-attack` | `<FREE_PROPERTY_ID_6>` | `item_skillonattackattempt` |
| Fixed while channeling | `cast-skill-while-channeling` | `<FREE_PROPERTY_ID_7>` | `item_skillwhilechanneling` |
| Source level while channeling | `cast-skill-while-channeling-same-level` | `<FREE_PROPERTY_ID_8>` | `item_skillwhilechannelingsamelevel` |
| Fixed when Frost Nova is cast | `cast-skill-when-frost-nova` | `<FREE_PROPERTY_ID_9>` | `item_skillwhenfrostnova` |
| Source level when Frost Nova is cast | `cast-skill-when-frost-nova-same-level` | `<FREE_PROPERTY_ID_10>` | `item_skillwhenfrostnovasamelevel` |

The source-level name is deliberately explicit. There is no abbreviated
`cast-skill-src`; `source` would mean the skill the player manually cast.

Enter the channeling, source-family and combat stat IDs in the TOML. Use `0`
to disable an unused fixed/source-level flavor. Every nonzero synthetic trigger
ID must be distinct:

```toml
[while_channeling]
fixed_stat_id = <FREE_STAT_ID_7>
same_level_stat_id = <FREE_STAT_ID_8>

[[source_skill_triggers]]
name = "frost_nova"
source_skill_ids = [44]
fixed_stat_id = <FREE_STAT_ID_9>
same_level_stat_id = <FREE_STAT_ID_10>

[combat_triggers]
attack_attempt_stat_id = <FREE_STAT_ID_6>
critical_strike_stat_id = <FREE_STAT_ID_3>
crushing_blow_stat_id = <FREE_STAT_ID_4>
open_wounds_stat_id = <FREE_STAT_ID_5>
```

## 3. Add localization

`descfunc=15` supplies three native formatter arguments: chance, encoded level
and skill name. The source-level string consumes the marker as the precision
of `%.*s`, so the skill name remains visible without printing level `63`.

Merge keys like these into a string table D2R actually loads, normally
`data/local/lng/strings/item-modifiers.json` in the consuming mod. An arbitrary
JSON filename is not mounted as a string table and produces `An Evil Force` in
the item tooltip. Use unused numeric IDs and translate every locale the mod
ships:

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
},
{
  "id": 51002,
  "Key": "CastOnCritical",
  "enUS": "%d%% Chance to cast level %d %s on Critical Strike"
},
{
  "id": 51003,
  "Key": "CastOnCrushingBlow",
  "enUS": "%d%% Chance to cast level %d %s on Crushing Blow"
},
{
  "id": 51004,
  "Key": "CastOnOpenWounds",
  "enUS": "%d%% Chance to cast level %d %s on Open Wounds"
},
{
  "id": 51005,
  "Key": "CastOnAttackAttempt",
  "enUS": "%d%% Chance to cast level %d %s on Attack Attempt"
},
{
  "id": 51006,
  "Key": "CastWhileChanneling",
  "enUS": "%d%% Chance to cast level %d %s while channeling"
},
{
  "id": 51007,
  "Key": "CastWhileChannelingSameLevel",
  "enUS": "%d%% Chance to cast %.*s at the source skill level while channeling"
},
{
  "id": 51008,
  "Key": "CastWhenFrostNova",
  "enUS": "%d%% Chance to cast level %d %s when casting Frost Nova"
},
{
  "id": 51009,
  "Key": "CastWhenFrostNovaSameLevel",
  "enUS": "%d%% Chance to cast %.*s at the source skill level when casting Frost Nova"
}
```

The example localization IDs are not reserved by the plugin. Replace them if
they collide with the consuming mod. The last two keys are examples whose
wording belongs to the consuming mod; create equivalent keys for every source
family. Each key is reusable for every item and affix using that family.

## 4. Put the property on an item

The native item columns keep their usual meaning:

- `par#`: target skill ID from `Skills.txt`;
- `min#`: chance from 1 to 100;
- `max#`: fixed target-skill level, or the reserved marker below.

Fixed level 12 Fire Ball at 25% chance (`Fire Ball` skill ID 47):

```text
prop1=cast-skill
par1=47
min1=25
max1=12
```

Fire Ball at the successful source skill's effective level:

```text
prop1=cast-skill-same-level
par1=47
min1=25
max1=63
```

Fixed level 12 Fire Ball only while channeling:

```text
prop1=cast-skill-while-channeling
par1=47
min1=100
max1=12
```

Fixed level 12 Fire Ball only when a configured Frost Nova source is cast:

```text
prop1=cast-skill-when-frost-nova
par1=47
min1=100
max1=12
```

The source-conditioned property name does not select Fire Ball. It selects the
stat family; `par1` still selects any target skill. A TOML rule may list one
source ID or a manual group such as all Cold-spell IDs.

`max=63` is a reserved positive marker, not target level 63. Cast Triggers
replaces it only inside its own `doactive` dispatch. Keeping the marker
positive is required because native `descfunc=15` can omit the complete stat
line for the zero layer produced by the obsolete `max=64` test contract.
Fixed-level Cast Triggers properties therefore use levels 1 through 62.

For an always-on double-Nova item, use `min=100`, `par=48` (`Nova`) and either
a fixed `max` or `max=63` with `cast-skill-same-level`. Any intended damage
penalty still belongs in the consuming mod's item/skill balance data.

Combat properties use the same fixed-level encoding. For example:

```text
prop1=cast-skill-on-crit
par1=47
min1=100
max1=12
```

Critical means weapon-mastery or passive Critical Strike; Deadly Strike is
excluded. Crushing Blow and Open Wounds require their native effects to apply.
Combat source-level mode is outside version 0.1.0.

Cast on Attack Attempt uses `cast-skill-on-attack`. It rolls after D2R accepts
any player `A1`, `A2`, throw, kick or `S1` through `S4` attack input, including
a sequence that transitions into one of those families. Direct targets,
Shift-ground attacks and misses are eligible. No source skill ID is hardcoded.
Native `att-skill` and `hit-skill` remain separate and unchanged.

## Repeating and channelled source skills

Channeling uses the dedicated ItemStatCost/Properties pair configured above.
Ordinary `cast-skill` and `cast-skill-same-level` properties never react to a
repeating/channelled source skill. The plugin uses the native `repeat` flag to
select the dedicated family and configured cadence:

```toml
[while_channeling]
enabled = true
interval_frames = 50
fixed_stat_id = <FREE_STAT_ID_7>
same_level_stat_id = <FREE_STAT_ID_8>
include_skill_ids = []
exclude_skill_ids = []
```

D2R runs at 25 server frames per second, so the default 50 frames is exactly
two seconds. The first successful channel tick rolls the item's native chance
immediately. Later successful ticks roll no more often than this interval.
Stopping the source skill stops the rolls; there is no wall-clock timer.

The chance remains item-owned. For example, `min=25` means one 25% native roll
at each admissible interval, not one guaranteed proc every two seconds.

## Target routing

No skill IDs are configured or hardcoded for routing. The plugin captures the
authoritative server input before the player mode is finalized. A unit input
stores its native type and GUID; a ground input stores the exact X/Y position.
A bounded per-player record survives until the matching successful source
handler consumes it, which covers handlers such as War Cry that never query a
target helper. A channel retains and refreshes that native descriptor across
later ticks. The target-unit or complete first-point X/Y pair consumed by the
handler remains a fallback. Skills.txt target flags, client cursor state and
coordinate comparisons do not guess this decision.

EventFunc20's synthetic ordinary self target is replaced once only when the
input or channel fallback supplied a unit or position. A `self/none` descriptor preserves the
original call. Native `ItemTgtDo` flag-0 calls remain unchanged. There are no
position/self/unit retry chains and no path-direction writes. The triggered
skill's `ItemTarget`, `ItemEffect` and `ItemCltEffect` data continue to control
its native self, random, corpse and last-attacker semantics.

Test at least these four combinations for custom skills:

| Source context | Proc kind | Expected route |
|---|---|---|
| ground or Shift cast | directional/position skill | exact native coordinates carried by the server input |
| directly targeted unit | directional/unit skill | exact native unit identified by the server input |
| channel tick after the input call | directional skill | exact unit or position consumed by that tick's handler |
| self cast with no input or target access | self/subject skill | original self/target route |
| native `ItemTgtDo` flag-0 call | self/subject skill | original native call, untouched |
| triggered skill with `ItemTarget=1` | caster-centered skill | native handler overrides the descriptor with the caster |

## Stable data rules

- The consuming mod owns and freezes all numeric IDs.
- Do not reuse any shipped stat ID for a different event or encoding later.
- Keep every channeling, source-family and combat ItemStatCost ID synchronized
  with the TOML; all nonzero synthetic IDs must be distinct.
- Keep `doactive` limited to EventFunc20 for this contract.
- Keep the property name, stat name and localization key stable after items
  have shipped.
- Do not put item codes or item mappings in the plugin TOML.
- Test new custom source skills. Put non-repeating exclusions in
  `on_cast.exclude_skill_ids` and repeating/channelled exclusions in
  `while_channeling.exclude_skill_ids`.
