# Skill resource costs — D2R 3.3.93847

Status: partial static reconstruction. No runtime testing was performed for
this module. The target is D2R 3.3.93847, using the governed common native
corpus whose historical directory retains the 92777 provenance. Function names
below are reconstruction names, not official exported symbols.

## Can another state duplicate Blood Mana independently?

The examined payment paths explicitly test state `0x72` (114), then call the
Blood Mana handler. The local vanilla 3.3 `states.txt` identifies `blood_mana`
as 114; `itemstatcost.txt` identifies hitpoints as 6, mana as 8 and charged
skills as 204. These rows were read through `scripts/build-data/tsv.js`.
Copying a state row does not copy these native conditions.

The ordinary path calls the handler at `0x436A2A`; periodic payment tail-jumps
to it at `0x436AB6`. Both target `0x584E80` after checking the constant state.
Crucially, the handler itself retrieves and can remove the original `0x72`
statlist. Extending only the callers' state predicates would therefore not
provide an independent custom state. A future implementation must account for
affordability, payment and the state owned by the payment handler.

This corrects the initial design suggestion made before native verification.
No hook or complete plugin contract is supplied by this module.

## Affordability is a separate decision

`D2Common_SKILLMANA_CheckStat`, logical entry `0x340900`, receives a unit in
RCX and skill in RDX. It first reads both mana and hitpoints. A charged-owner
value at `skill+0x4C` other than -1 selects a charge-count check at `skill+0x50`
instead of the ordinary resource formula.

For ordinary skills, the observed cost expression uses signed words at skill
record offsets `0x22A` and `0x22C`, shifted by the byte at `0x228`, with a
bounded level index. State `0x72` selects `hitpoints >= cost`; otherwise it
selects `mana >= cost`, except for record word `+0x4E == 116` combined with
the predicate at `0x336090`. The individual structure observations are not a
complete skill ABI. This body does not apply the minimum-cost clamp used by
the payment calculator.

The direct caller at `0x33F489` belongs to the use-state decision. Later in
that caller, `0x33F512..0x33F554` checks a positive start-cost word at record
`+0x224` against **mana**, shifted by eight. If insufficient, state `0x0C`
(`inferno` in the local vanilla table) is the exception tested at `0x33F547`.
That branch has no Blood Mana substitution. A future life-cost plugin must
therefore inspect the separate start-cost gate as well; changing this one
affordability helper does not necessarily remove all mana requirements.

## Ordinary consumption

`D2GAME_SKILLMANA_Consume`, logical entry `0x436830`, receives a game context
in RCX, unit in RDX, skill ID in R8D and level in R9D; the observed result is
an integer in EAX. Pointer types remain opaque in this reconstruction.

The ordinary branch calls `0x33AA00` with the context byte at `game+0x106`,
skill ID and level. A nonpositive cost returns zero from this routine. That
return alone does not establish that free skills cannot execute elsewhere.

For a player it checks state `0x72`, returning the Blood Mana handler's result
when present. Otherwise, it compares mana (stat 8, layer 0) against the cost
and subtracts it if the global byte at `0x2AA6A69` is zero. The purpose of that
global byte is not identified here; it must not be assigned a gameplay mode
name based only on this branch.

Documentary pseudocode of this branch, not compilable replacement code:

```cpp
// The enclosing routine already returned success for nonplayers.
cost = CalculateSkillCost(gameContextByte, skillId, level);
if (cost <= 0)
    return 0;
if (GetUnitType(unit) != Player)
    return 1;
if (CheckState(unit, 0x72))
    return BloodManaPayment(unit, cost);
if (GetStat(unit, Mana, 0) < cost)
    return 0;
if (unidentifiedPaymentBypassByte == 0)
    AddStat(unit, Mana, -cost, 0);
return 1;
```

The enclosing routine has an earlier nonplayer gate, before used-skill and
charge resolution. The repeated type check shown here is local to this branch.

### Payment cost calculator

`D2Common_SKILLMANA_GetManaCost` at `0x33AA00` receives a context, skill ID and
level. It returns zero for an absent record or when both signed words at
`+0x22A` and `+0x22C` are zero. Otherwise it computes
`(word_22A + max(level-1,0) * word_22C) << byte_228` and clamps the result
upward to `word_226 << 8`. Arithmetic follows the observed signed 32-bit
instructions; this expression is explanatory, not portable C++ overflow logic.

Unlike the affordability helper, this path applies the floor. A floor above
the un-clamped cost can therefore produce different check and payment values;
the in-game outcome of such data was not tested. The promoted atlas does not
yet name these individual offsets, so semantic associations with TXT cost
fields are not a promoted compiled layout.

An earlier charged-item branch uses stat `0xCC`, validates, decrements and
bounds the charge value, then writes and synchronizes it. It bypasses the
Blood Mana branch above. This describes that branch, not every possible
item-triggered effect. Two direct callers of the consumption entry are
indexed at `0x43B135` and `0x43B5B4`.

The registry already identified this function from the pinned official
PluginPack. A future plugin must independently audit active hook ownership;
this module does not establish that the entry is free to hook.

## Periodic payment

`D2GAME_SKILLMANA_AuraConsume`, logical entry `0x436A80`, receives a unit in
RCX and an already calculated cost in EDX. For players it checks the same
state and reaches the same Blood Mana handler. Otherwise it checks mana and
subtracts it under the same global byte. Nonplayers return success here.

Four direct call sites are indexed: `0x563650`, `0x563CE1`, `0x564224`, and
`0x564578`. These calls do not establish that every continuous skill uses this
routine. The names and behavior of every caller have not been reconstructed.

## Blood Mana payment and state ownership

`D2GAME_SKILLS_BloodMana`, logical entry `0x584E80`, receives the unit in RCX
and cost in EDX. It retrieves the statlist for state `0x72` and obtains an
associated identifier through `0x2F56A0`, later passed to the skill-record
lookup. It reads current hitpoints (stat 6, layer 0).

- If hitpoints are at least the cost, it subtracts the cost. It then obtains
  the associated skill record and compares remaining hitpoints against the
  dword at record offset `0x1C8`, shifted left by eight. Below that threshold
  it unlinks and frees the original `0x72` statlist when present. It returns 1.
- If hitpoints are below the cost, it unlinks and frees that original statlist
  when present, sets hitpoints to `0x100`, and returns 0.

The equality case takes the subtraction path. This code does not implement a
universal minimum-one-life clamp on successful payments. The semantic field
name at `SkillsRecord+0x1C8` and the resulting death/removal behavior must not
be inferred beyond the observed instructions without further evidence.

Documentary control flow, with helper details kept explicit:

```cpp
bloodList = GetStatListFromState(unit, 0x72);
sourceSkillId = ReadAssociatedIdentifier(bloodList);
hp = GetStat(unit, Hitpoints, 0);
if (hp < cost) {
    RemoveOriginalBloodManaListIfPresent(unit);
    SetStat(unit, Hitpoints, 0x100, 0);
    return 0;
}
AddStat(unit, Hitpoints, -cost, 0);
record = GetSkillRecord(GetContext(unit), sourceSkillId);
if (GetStat(unit, Hitpoints, 0) < (ReadDword(record, 0x1C8) << 8))
    RemoveOriginalBloodManaListIfPresent(unit);
return 1;
```

This is not a null-safety or full structure/ABI contract for calling the helper
with a custom state. In particular, a custom state does not automatically
provide the original statlist and its source-skill association.

## Evidence and reproduction

The symbol registry is authoritative for names, addresses and confidence.
These complete logical-body hashes bind the documented observations to code;
they are not installable hook signatures. End addresses are exclusive.

| Symbol | Logical range | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| D2Common_SKILLMANA_GetManaCost | `0x33AA00..0x33AA6A` | 106 | `317EA4C72D6B9D325977F6615E762819DEA0B30C48063C9DC7CA5B763ECDCC7F` |
| SKILLS_GetUseState_StartManaGateWitness (partial caller) | `0x33F512..0x33F554` | 66 | `108DB1999A9B214721E5970A667172965A0CCA89AD2561F09C732E061063CE82` |
| D2Common_SKILLMANA_CheckStat | `0x340900..0x340A78` | 376 | `6AFF612D41E94ED98BACC45CCCEE5F53787B2FCBE4831399E3F14B6BE037D87F` |
| D2GAME_SKILLMANA_Consume | `0x436830..0x436A7E` | 590 | `BB46A2F8878B6C9BCBC77243228F36CE08B4305E408D3B00D8B3CD00874B4F71` |
| D2GAME_SKILLMANA_AuraConsume | `0x436A80..0x436B05` | 133 | `3A0085AB6CFF2144F01ABCFB46DC5E08A39DF07E68446758ADE2876A95EC5133` |
| D2GAME_SKILLS_BloodMana | `0x584E80..0x584FA3` | 291 | `6F4AAA99FB080AFC99E25ED05EA4CDCBFAA88D4A184589D43C7081B041108821` |

```powershell
npm run re:d2r33 -- status
npm run re:d2r33 -- topic blood_mana
npm run re:d2r33 -- function 0x33AA00 --before 0 --after 35
npm run re:d2r33 -- function 0x340A15 --before 22 --after 36
npm run re:d2r33 -- function 0x33F51F --before 6 --after 20
npm run re:d2r33 -- function 0x436830 --after 150
npm run re:d2r33 -- function 0x436A16 --before 18 --after 38
npm run re:d2r33 -- function 0x436A80 --before 0 --after 80
npm run re:d2r33 -- xrefs 0x436830
npm run re:d2r33 -- xrefs 0x436A80
npm run re:d2r33 -- function 0x584E80 --before 0 --after 180
npm run re:d2r33 -- function 0x584F00 --before 0 --after 65
```

Unwind-table boundaries do not always match logical functions. For example,
`function 0x436A80` reports `0x436943..0x436B54`, which includes the ordinary
payment tail, periodic payment and a neighboring routine. Follow call targets,
prologues and returns. Likewise, the Blood Mana instruction at `0x584EF7`
crosses an indexed boundary: linear decoding of the full logical body gives
`mov ecx, dword ptr [rbx+0x1C8]`, then `shl ecx, 8` at `0x584EFD` and the
comparison at `0x584F00`. Truncated `.byte` output is not an unknown opcode.

## Historical reference

D2MOO guided localization:
`D2MOO@19019806df7f3e877fa105b05395d1e3597e2316:source/D2Game/src/SKILLS/Skills.cpp:1096`
and `:1161`, plus `source/D2Common/src/D2Skills.cpp:1631`. It describes Diablo II
1.10f only. The constants and calls above were independently inspected in D2R;
no 32-bit address or structure was transplanted.

## Remaining limits

- The full use-state decision and every special caller are not reconstructed.
- No complete client/server contract or exhaustive coverage of all skills.
- No runtime state coexistence, death, save or multiplayer testing.
- No active-plugin-stack hook ownership audit.
- Steam 93787 is not independently qualified for this module.
