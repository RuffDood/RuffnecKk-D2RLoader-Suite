# Argument contracts — generated catalogue

Generated from `known-rvas.json`. Do not edit this view by hand.

3 structured contracts / 1046 registry entries. Remaining 1043 entries have undocumented structured contracts; their existing notes are preserved.

See [format and authoring rules](argument-contracts.md). Imported claims remain in the separate [reference intake](references/rva-93847-intake.md).

## 0x33AA00 — D2Common_SKILLMANA_GetManaCost

Kind: function. Identification confidence: high.

Contract: **partial**. Callable entry: **yes**.

Prototype: **unresolved / not expressed**.

| Position | Location | Type / width | Name | Meaning |
|---|---|---|---|---|
| 1 | ECX | 32-bit value; semantic type unresolved | context | Lookup context; observed caller supplies game context byte |
| 2 | EDX | int32_t | skillId | Skill record identifier |
| 3 | R8D | int32_t | level | Skill level |

Return: EAX — int32_t — Calculated resource cost.

Scope: D2R 3.3.93847 using the governed common 92777/93847 corpus; canonical SHA-256 `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`.

Runtime: **not-tested** — Existing static reconstruction only; normalization into structured fields adds no runtime evidence.

Requirements:

- Only the documented argument/register observations are established; thread, lifetime and active-provider requirements need consumer-specific proof.

Effects:

- Looks up a skill record and calculates a floor-clamped cost.

Unknowns / limits:

- Exact semantic type and full valid domain of context remain unresolved.
- Individual compiled field names are not promoted in the DataTables atlas.

Evidence:

- reverse-engineering/d2r-3.2.92777/modules/skill-costs.md — Payment cost calculator — Body [0x33AA00,0x33AA6A); SHA256 317EA4C72D6B9D325977F6615E762819DEA0B30C48063C9DC7CA5B763ECDCC7F; callers 0x218937 and 0x4369FB.
- reverse-engineering/d2r-3.2.92777/known-rvas.json — D2Common_SKILLMANA_GetManaCost existing notes — Register mapping and return observation preserved from the pre-intake registry; no new native qualification.

Existing notes: Static logical body 0x33AA00..0x33AA6A exclusive, 106 bytes, SHA256 317EA4C72D6B9D325977F6615E762819DEA0B30C48063C9DC7CA5B763ECDCC7F. Context ECX, skillId EDX, level R8D; int32 cost EAX. Record lookup 0x97790. Missing record or both s16+0x22A and s16+0x22C zero returns zero; otherwise computes level-scaled signed cost shifted by u8+0x228 and applies floor s16+0x226 <<8. Direct calls 0x218937 and 0x4369FB. Unlike affordability body 0x340900, this calculator applies the floor. Individual compiled field names are not promoted in the atlas. Static only.

## 0x340900 — D2Common_SKILLMANA_CheckStat

Kind: function. Identification confidence: high.

Contract: **partial**. Callable entry: **yes**.

Prototype: **unresolved / not expressed**.

| Position | Location | Type / width | Name | Meaning |
|---|---|---|---|---|
| 1 | RCX | opaque pointer (64-bit) | unit | Unit whose resource availability is checked |
| 2 | RDX | opaque pointer (64-bit) | skill | Skill instance including charged-skill context |

Return: EAX — boolean result; exact C++ return type unresolved — Whether the examined affordability path succeeds.

Scope: D2R 3.3.93847 using the governed common 92777/93847 corpus; canonical SHA-256 `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`.

Runtime: **not-tested** — Existing static reconstruction only; normalization into structured fields adds no runtime evidence.

Requirements:

- Only the documented argument/register observations are established; thread, lifetime and active-provider requirements need consumer-specific proof.

Effects:

- Reads mana, hitpoints or charge count and evaluates affordability.

Unknowns / limits:

- Pointer layouts, ownership and lifetime are not fully established.
- The separate start-mana gate is outside this function; this contract does not cover all affordability.

Evidence:

- reverse-engineering/d2r-3.2.92777/modules/skill-costs.md — Affordability is a separate decision — Body [0x340900,0x340A78); SHA256 6AFF612D41E94ED98BACC45CCCEE5F53787B2FCBE4831399E3F14B6BE037D87F; caller 0x33F489.
- reverse-engineering/d2r-3.2.92777/known-rvas.json — D2Common_SKILLMANA_CheckStat existing notes — Register mapping and return observation preserved from the pre-intake registry; no new native qualification.

Existing notes: Static logical body 0x340900..0x340A78 exclusive, 376 bytes, SHA256 6AFF612D41E94ED98BACC45CCCEE5F53787B2FCBE4831399E3F14B6BE037D87F. Unit RCX, skill RDX, boolean EAX. Reads mana stat8 and HP stat6. Charged owner at skill+0x4C != -1 selects charge count skill+0x50 >0. Normal cost uses skill record s16+0x22A and s16+0x22C with u8+0x228 shift and bounded level index; no minimum clamp in this body. State114 check at 0x340A2C selects HP>=cost, otherwise compares mana, with the record+0x4E==116/0x336090 exception. Direct caller 0x33F489. That caller also has a separate start-mana gate; do not infer this function alone governs all affordability. No runtime or complete skill structure qualification.

## 0x436830 — D2GAME_SKILLMANA_Consume

Kind: function. Identification confidence: high.

Contract: **partial**. Callable entry: **yes**.

Prototype: **unresolved / not expressed**.

| Position | Location | Type / width | Name | Meaning |
|---|---|---|---|---|
| 1 | RCX | opaque pointer (64-bit) | game | Game context |
| 2 | RDX | opaque pointer (64-bit) | unit | Unit paying the resource cost |
| 3 | R8D | int32_t | skillId | Skill record identifier |
| 4 | R9D | int32_t | level | Skill level |

Return: EAX — int32_t — Branch-dependent consumption result; nonpositive normal cost returns zero.

Scope: D2R 3.3.93847 using the governed common 92777/93847 corpus; canonical SHA-256 `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`.

Runtime: **not-tested** — Existing static reconstruction only; normalization into structured fields adds no runtime evidence.

Requirements:

- Only the documented argument/register observations are established; thread, lifetime and active-provider requirements need consumer-specific proof.

Effects:

- Can update mana or charges, or delegate to Blood Mana payment, depending on the branch.

Unknowns / limits:

- Pointer ownership, lifetime and complete valid calling context are unresolved.
- Purpose of global byte 0x2AA6A69 remains unidentified.
- Active hook ownership is not established by this reconstruction.

Evidence:

- reverse-engineering/d2r-3.2.92777/modules/skill-costs.md — Ordinary consumption — Body [0x436830,0x436A7E); SHA256 BB46A2F8878B6C9BCBC77243228F36CE08B4305E408D3B00D8B3CD00874B4F71; callers 0x43B135 and 0x43B5B4.
- reverse-engineering/d2r-3.2.92777/known-rvas.json — D2GAME_SKILLMANA_Consume existing notes — Register mapping and return observation preserved from the pre-intake registry; no new native qualification.

Existing notes: Native mana and charged-item consumption path used by the official plugin pack. Static logical body 0x436830..0x436A7E exclusive, 590 bytes, SHA256 BB46A2F8878B6C9BCBC77243228F36CE08B4305E408D3B00D8B3CD00874B4F71. Observed arguments game RCX, unit RDX, skillId R8D, level R9D; int32 result. Normal branch calculates cost via 0x33AA00, returns 0 for nonpositive cost, checks player state 0x72 at 0x436A1C and calls 0x584E80 at 0x436A2A; otherwise checks stat 8 then subtracts cost unless unidentified global byte 0x2AA6A69 is set. Charged-item branch handles stat 0xCC independently. Direct calls at 0x43B135 and 0x43B5B4. Pdata fragments do not bound the complete logical function. No active-stack ownership or runtime qualification by this reconstruction.
