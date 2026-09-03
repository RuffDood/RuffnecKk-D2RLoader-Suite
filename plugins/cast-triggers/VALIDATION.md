# Cast Triggers validation

The completed evidence below belongs to candidate 0.1.0. Version 1.0.0 must be
rebuilt and requalified as the exact final artifact before release.

Status as of **2 September 2026**: **0.1.0 release-candidate evolution passed
its focused BKVince gameplay gate and is ready for the next public release**.
The channeling, combat-trigger and persistent authoritative input-routing
implementation is built and documented. Vincent authorized deployment to the
full BKVince/QtyTester stack. The fresh full-stack cold start passed; gameplay
for targeting, channeling, Attack Attempt, Crushing Blow and Open Wounds passed.
The Critical marker-loss cause is proven, fixed and passed in focused gameplay.
The Deadly Strike negative gate exposed stale Critical provenance on reused
`D2Damage` storage; the lifecycle reset is implemented and passed its focused
gameplay retest. Combat-family filtering and proc-chain exclusion also passed
their focused gameplay gate. The split runtime counter export is now fully
visible and passed with zero Critical-marker overflow. The new candidate keeps
those mechanisms unchanged while separating channeling stats and adding
source-conditioned stat families.

## Current candidate

| Gate | Status | Evidence |
|---|---|---|
| Version and author | passed | `0.1.0`, author `RuffnecKk` |
| Scope | passed statically | Global or mod-local; no `ModScopedOnly` |
| Version allowlist | passed | `92777`, `93847` and Steam `93787` are admissible; build name, channel and version are diagnostic only, and no allowlist exists |
| TOML parser | passed | Strict tables and types; non-empty named source rules; non-empty source-ID lists; at least one stat per rule; every nonzero channel/source/combat stat ID globally distinct |
| Public TOML collision safety | passed | Channel and combat IDs default to zero; source-rule examples remain commented; a consuming mod must opt in with its own IDs |
| Tooltip localization | passed in fixture and focused gameplay | The fixture idempotently merges ten canonical/example entries into the loaded `item-modifiers.json`, rejects key/ID/content collisions and verifies one complete entry per trigger; Vincent confirmed the prepared gate works in BKVince |
| Channel-family separation | passed in policy and gameplay | Generic stats proc only on ordinary casts; channel dispatch exposes only its dedicated fixed/source-level pair |
| Source-conditioned families | passed in policy and gameplay | Frost Nova activated its configured family while unlisted source skills remained inactive |
| Channel cadence | passed in gameplay | The dedicated channel family retained the immediate dispatch plus the 50-frame cadence |
| Cast-on-cast attack exclusion | passed statically; historical gameplay pass | Weapon animations are not eligible source casts |
| Custom Cast on Attack Attempt | passed in gameplay | Accepted attack-family inputs dispatch before hit resolution; direct targets, misses and Shift-ground attempts work without a skill-ID allowlist |
| Critical provenance | passed in gameplay, including Deadly exclusion | Weapon-mastery/passive Critical RNG is predicted without mutating the seed and confirmed by the native result. A new logical hit retires any marker on reused `D2Damage` storage before prediction, while copies made during that hit still preserve provenance. A 100% Deadly Strike ring produced no Critical Fire Ball after a positive Critical proc in the same session |
| Diagnostic performance | synchronous cause proven and removed; buffered export runtime-confirmed | The earlier synchronous trace produced 122 log lines in 11 seconds around 9 Critical Fire Ball procs and caused visible frame drops. The current candidate replaces hot-path writes with a bounded 64-entry in-memory trace; the runtime command reported `retained=64/64`, `total=702` and `combat-hook logging=deferred`. The public TOML still defaults diagnostics to false |
| Crushing Blow observation | passed in gameplay | EventFunc16 original return is authoritative |
| Open Wounds observation | passed in gameplay | EventFunc15 original return is authoritative |
| Combat stat filtering | passed by policy tests and gameplay | With the Mana Potion ring and no Crushing Blow source, Critical launched Fire Ball while the unmatched Crushing Blow Nova remained inactive |
| Proc-chain guard | passed statically and in gameplay | With the Mana Potion and Antidote rings equipped together, the Critical Fire Ball did not feed the cast-on-cast family or launch a second Fire Ball |
| Target routing | passed in gameplay | War Cry and Taunt preserve direct-unit and Shift-click ground targets across direction changes; Inferno channel ticks retain current input |
| Console diagnostics | passed at runtime | The three status lines remain fully visible. The final capture reported Critical `created=1`, `propagated=3`, `consumed=1`, `removed=1`, `event flags without marker=0` and `critical marker overflows=0` |
| Debug build/test | passed | CTest 1/1 |
| Release build/test | passed | Two independent Release CTest runs passed 1/1 |
| Reproducible DLL | passed | Independent Release builds are byte-identical at 247,296 bytes, SHA-256 `EF982340...275DBB` |
| Full BKVince 3.3 integration | passed | The reproducible DLL passed its focused gate in the complete BKVince/QtyTester runtime. Test-only recipes and starter items are absent; the consumer-owned stats, properties, tooltip strings, DLL and production TOML remain installed |
| Public ZIP | next release | The previously prepared ZIP predates this evolution. Build the replacement archive only when the next public release is assembled |

## Native fingerprint

All 27 exact witnesses are validated before the first hook is installed. A
mismatch refuses loading cleanly. D2R 3.2.92777, Battle.net 3.3.93847 and Steam
3.3.93787 are all admissible without a number or channel gate. The governed
common corpus proves the same RVA, bytes and ABI for 92777 and 93847; only
93847 receives the current runtime matrix unless a surface or environment
differs. Steam 93787 remains provisional until its identified executable and
fresh logs prove every witness or require a separate qualification.

| Surface | RVA | Use |
|---|---:|---|
| Central server skill handler | `0x43ACB0` | Hook manual cast completion |
| Skill-handler context witness | `0x43ACEC` | Prove `Game+0x106` access |
| Server-frame witness | `0x42E615` | Prove `Game+0x170` frame |
| Unit-stat event wrapper | `0x44D570` | Hook damage events; dispatch synthetic `doactive` |
| Player position-input executor | `0x4FDB40` | Persist exact ground/Shift input before player-mode finalization |
| Player unit-input executor | `0x4F8DE0` | Persist exact target type/GUID before player-mode finalization |
| Active-skill layout witness | `0x33DBA0` | Prove `D2Skill+0x00 -> SkillsTxt` and the compiled skill ID association |
| Server unit resolver | `0x48FE80` | Resolve persisted type/GUID to a fresh native unit at handler consumption |
| Target resolver | `0x48FE20` | Observe native unit targets |
| Unit type helper | `0x34B9D0` | Restrict source actors to players |
| Dynamic path helper | `0x34AE80` | Filter ground-target observations |
| First-point X/Y | `0x341CC0`, `0x341CD0` | Observe native ground target |
| SkillsTxt lookup | `0x097790` | Classify cast/repeat animation |
| SkillsTxt stride witness | `0x09780B` | Prove compiled stride `0x2EC` |
| Item-skill casters | `0x5896E0`, `0x589820` | Same-level substitution, target routing and chain guard |
| Damage builder | `0x44C030` | Capture strict Critical provenance |
| Damage copy/move/destructor | `0x4494B0`, `0x449760`, `0x4496E0` | Propagate, transfer and retire Critical markers |
| Open Wounds callback | `0x584170` | Observe successful native Open Wounds |
| Crushing Blow callback | `0x583150` | Observe successful native Crushing Blow |
| EventFunc20 | `0x583B30` | Filter synthetic stat families only |
| Active weapon resolver | `0x4242B0` | Mirror mastery-Critical prerequisites |
| Mastery Critical helper | `0x33D4F0` | Read native Critical chance |
| Unit stat getter | `0x2F5020` | Read passive Critical stat 337 |
| Unit seed accessor | `0x34A1E0` | Predict the native roll without advancing it |

The five pinned eezstreet plugins do not own EventFunc15, EventFunc16,
EventFunc20, the damage builder/copy/move/destructor or the Critical helper surfaces.
Their item-skill patches remain inside the caster bodies, after Cast Triggers'
entry hooks. The prior candidate passed a fresh full-stack cold start. The new
candidate changes no native surface but still requires one full-stack start and
its focused gameplay gate before release. The public plugin has no BKVince,
BKVCombat or Melee Splash dependency.

## BKVince laboratory

Gameplay qualification is performed in the full active BKVince stack with
QtyTester, not in an isolated mod. The deterministic fixture provides ten
recipes:

1. fixed cast-on-cast Fire Ball;
2. same-level Nova from a Stamina Potion;
3. separated generic Nova and channel-only Fire Ball with Inferno/Chain
   Lightning oskills;
4. Frost Nova-conditioned fixed Fire Ball plus source-level Nova;
5. custom Cast on Attack Attempt;
6. positive Critical Strike;
7. Deadly Strike exclusion;
8. Crushing Blow;
9. Open Wounds;
10. combat-family filtering plus proc-chain exclusion.

No custom recipe consumes a Town Portal Scroll, and no separate 25% gameplay
case exists. All gameplay gates use 100% for deterministic observation. The
generated ItemStatCost, Properties, CubeMain and CharStats tables passed
byte-exact parser round-trip, CRLF and row-width checks before deployment. The
fixture was regenerated idempotently from the existing BKVince tables: a second
generation was byte-identical across all eight staged files. The six-file
runtime allowlist was backed up and deployed directly into the existing
BKVince/QtyTester profile. The installed DLL matched the reproducible artifact
at SHA-256 `EF982340...275DBB`.

The full BKVince laboratory completed a fresh 3.3.93847 cold start. Fingerprint
acceptance, hook installation, TXT compilation and startup passed. Vincent also
passed War Cry/Taunt direct and Shift-click routing plus Inferno immediate and
50-frame channel cadence. Diagnostics show Inferno position dispatches at frames
3695, 3745, 3795, 3845 and 3895. The focused Critical retest proved each native
outcome traversed marker creation, deep copy, move into the combat record, final
deep copy, event consumption and `critical-strike` dispatch. The Deadly Strike
negative gate then proved that D2R can reuse the original damage-builder address:
four created markers yielded 48 Critical dispatches from a recurring address.
The new lifecycle reset removes that stale marker before each new player damage
build. Vincent then confirmed in the same runtime that the 100% Deadly Strike
ring no longer launches Fire Ball. Combat-family filtering and proc-chain
exclusion subsequently passed. The final status-only candidate completed a full
cold start and displayed every counter without clipping; the capture ended with
`event flags without marker=0` and `critical marker overflows=0`. No gameplay
gate remained open for that DLL candidate. On 2 September, the evolved
candidate again accepted the complete native fingerprint and reported channel
stats `400/401`, one source-skill rule and combat stats
`399/396/397/398`. Vincent confirmed the prepared ordinary-cast, separated
channel and source-conditioned tests all function in BKVince. He accepted the
candidate for the next public release.

On 2 September, Vincent clarified the intended BKVince cleanup contract: only
the 10 deterministic CubeMain recipes and temporary Sorceress starter items
were laboratory data. The 10 ItemStatCost rows (`394` through `403`), 11
Properties rows (`313` through `323`), 10 mounted tooltip entries, mod-local
DLL and BKVince TOML are permanent consumer integration data.

The earlier full cleanup had restored a byte-exact pre-gate QtyTester snapshot,
but that snapshot already contained five laboratory rings carrying stats `394`
and `396/397`. With their ItemStatCost definitions removed, the offline
character-summary reader asserted at `D2Common/src/Items/Items.cpp:1990` and
retrying the assertion immediately retriggered it. An in-memory audit proved
that every other character and the shared stash decoded against the cleaned
tables; QtyTester alone retained the removed IDs.

The permanent rows and tooltip entries were restored exactly from the governed
post-gate evidence, while `cubemain.txt` remained unchanged and contains zero
Cast Triggers recipes. The package DLL was restored mod-locally and the BKVince
TOML activates channel stats `400/401`, source-conditioned stats `402/403` and
combat stats `399/396/397/398` with diagnostics disabled. Source/runtime hashes
match: ItemStatCost `8B097670...09BAE3`, Properties
`4BF96D66...336F00`, item modifiers `1BA60772...1C3FC`, DLL
`EF982340...75DBB` and TOML `C7013D75...627DA`. A fresh full-stack cold start
loaded 38 plugins and 17 patches, compiled 192 TXT tables, reached `24/24`,
decoded QtyTester and entered gameplay without the Items assertion. A separate
TACT resource assertion remains outside this item-data correction.

## Artifacts

| File | Bytes | SHA-256 |
|---|---:|---|
| current package DLL | 247296 | `EF9823406C1F14714572DD23F5DBF7AB13CEE2D45EA78DA255B5C21114275DBB` |
| current package TOML | 2155 | `7685BB873B2896ED33F2FBE779A77A3B00B898674C7D5F7338BD56C6F82EEF8D` |
| adjacent `README.md` | 6810 | `AA934D38F469BAB61A3FFB2C2F5AD859668A38B387CC12741F24B5BACA6044F5` |
| superseded `CastTriggers-0.1.0.zip` | 102560 | `C91D58DB4A1BB55D9FDCFFE23827ED0EFCE4C691107AA4B6901B631F4EE34140` |
| previous `CastTriggers-0.1.0-rc.zip` (superseded) | 96317 | `95B012496F74C8EE7C516AFC1E19B1A5C6A9F01A2360BFA99F8A7500D32AF00C` |

The superseded ZIP contains exactly:

```text
d2rl-ruffneckk-cast-triggers.dll
ruffneckk-cast-triggers.toml
```

README, intermod guide, lab guide and validation stay beside the archive for
human review.

## Next public release

1. Build a replacement `0.1.0` release archive from the validated package DLL
   and public TOML.
2. Inspect its exact two-file allowlist and verify both hashes against the
   validated package.
3. Keep the README beside the agent-generated ZIP for Vincent's final human
   review before he inserts it into the distribution archive.
4. Publish only as part of the next explicitly authorized public release.
5. Do not name Steam 3.3.93787 as qualified until its DLL hash, executable
   identity, complete Loader/plugin logs, fingerprint result, scope and startup
   are all recorded. Cross-build multiplayer remains a separate gate.

Relevant multiplayer host/client coverage remains `not run`; Vincent accepted
the completed single-player/full-stack matrix as sufficient for the next public
release rather than keeping multiplayer as a blocking gate.

## Rollback

Removing the DLL and TOML disables Cast Triggers dispatch, but a consuming mod
must retain every ItemStatCost row and compatible localization entry while any
save can contain those native stat IDs. Definitions may be removed only after
the affected items have been migrated or purged. For BKVince, rows `394` through
`403`, their Properties rows and tooltip entries are therefore permanent; the
test-only CubeMain recipes and starter items remain absent.
