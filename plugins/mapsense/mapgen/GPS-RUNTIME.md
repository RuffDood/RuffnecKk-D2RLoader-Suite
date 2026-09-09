# GPS collision comparison preparation — MapSense 1.0.2 r3

The external reader and the isolated `gps-export` target compare generated
collision data with D2R. **Two live Harrogath captures were accepted on
6 September 2026; raw collision differences remain.** No GPS renderer, movement
automation, installed plugin or configuration is changed by this work.
The [offline routing results](GPS-PROOF.md) remain a separate model proof.

## Source and scope

The governed reader is `workspace:scripts/reverse-engineering/mapsense-gps-runtime.py`;
its tests are `mapsense-gps-runtime-tests.py` beside it. The workspace repository
is resolved by `workspace-repositories.json`, independently of this product tree.
Both Python files use only the standard library. The Windows reader requests
`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ`; it never writes memory, injects
code, invokes native functions, suspends threads, sends input or starts/stops D2R.

Before following any data pointer, it checks module ranges and 21 exact native
witnesses against the running image. The same bytes pass the canonical-image
audit (SHA-256 `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`).
Fingerprint policy ID:
`E88F98A0BD6382D23A4AD7C74A320D4DB9A3B3E7E846009127C1981A25CCE9E5`.
Version, channel and retail hashes are provenance, never native admission gates.

The lookup follows local context → local player ID → client unit hash →
DynamicPath → active room → level → Drlg. It reads seed, difficulty, player
coordinates and mode, then copies only that level's **already active** room
collision grids and room lists. Inactive rooms are counted and never activated.
Room descriptors, active-room backlinks, grid rectangles, pointers and chains
must agree. Coordinates in the artifacts are world subtiles, five per game tile.
This chain records the local player's observed data; it does not independently
establish multiplayer server authority.

A normal live read records `collisionEvidence.method=live-room-activation-order`
and cannot admit static terrain. D2R builds a room CollMap from the neighbours
active at that moment. Admission requires the exact
`rebuilt-after-full-activation` method, zero inactive rooms and its explicit
rebuild witness; unknown, contradictory and legacy evidence fails closed.

Every observed memory range is read again and must match. This detects changing
reads but is **not an atomic snapshot guarantee**: an ABA change or a change
after verification remains possible. Limits are 4,096 rooms, 262,144 neighbour
entries, 16,777,216 collision cells, 80 MiB read across both passes and three
seconds. A menu, transition, unloaded room, modified native witness, invalid
layout or unstable read produces `REFUSED`; there is no permissive fallback.

## Export and comparison

`zig build gps-export -- <seed> <difficulty> <level-id> <new-output.msgc>` exports
one generated level. It uses the existing `LoadedInputs`, `gps.Environment` and
room model, accepts the helper's `--excel-root` and `--tiles-root` options and
resolves level IDs from effective tables. It is excluded from the default install.
Outputs are created exclusively, so an existing evidence file cannot be overwritten.

The version-one MSGC format is little-endian:

| Region | Fields |
| --- | --- |
| 48-byte header | `MSGC`, u32 version/seed/difficulty, i32 level/origin X/origin Y, u32 width/height/room count, u64 input fingerprint |
| Each room | i32 world X/Y/width/height, u32 neighbour count, then u32 room indices |
| Grid | Exactly width × height u16 raw collision cells, row-major |

The input fingerprint is the existing FNV digest of effective table overrides
and fallback markers. It is not a cryptographic manifest of embedded tables or
tile assets. Keep the generator pin/patch, command roots, input SHA-256 inventory
and output SHA-256 with each runtime experiment. Data on disk alone does not
prove what a previously opened D2R session loaded.

The Python comparator requires identical seed/difficulty/level and verified
capture metadata. It matches exact room rectangles and reports every raw bit
difference, per-bit counts, the `0x1C09` player mask and separate `0x804` gated
trace mask, plus one-sided blocked cells. It does not erase occupancy or door
bits. These are cell comparisons, not execution of a native footprint or trace.
Directed room-list membership is compared among observed, matched rooms only;
own-room membership is implicit. Missing generated rectangles and relationship
differences remain failures, with bounded examples. Inactive/unobserved rooms
cannot become passing coverage. Zero comparable cells is a refusal.

## Reproduce the first comparison

From the Diablo workspace, first run the canonical audit and synthetic tests:

```powershell
python -B scripts/reverse-engineering/mapsense-gps-runtime.py audit
python -B scripts/reverse-engineering/mapsense-gps-runtime-tests.py
```

Once a specifically authorized BKVince session has a character in a level, use
its observed process ID (the reader does not launch it):

```powershell
python -B scripts/reverse-engineering/mapsense-gps-runtime.py capture --pid 12345 --output analysis-cache/mapsense-gps-session/capture-01.json
```

Replace the example PID. Read seed/difficulty/level from the accepted capture,
then export those exact values from this `mapgen` directory with Zig 0.16.0:

```powershell
zig build gps-export -Doptimize=ReleaseFast -- 1337 2 2 C:/Workspaces/Diablo/analysis-cache/mapsense-gps-session/generated-01.msgc --excel-root "C:/Games/Diablo II Resurrected/mods/BKVince/data/global/excel" --excel-root "C:/Games/Diablo II Resurrected/mods/BKVince/BKVince.mpq/data/global/excel" --tiles-root "C:/Games/Diablo II Resurrected/mods/BKVince/data/global/tiles" --tiles-root "C:/Games/Diablo II Resurrected/mods/BKVince/BKVince.mpq/data/global/tiles"
```

The example session values are fixtures, not a discovered live session. Use the
actual configured mod roots and a new output path. Back in the workspace:

```powershell
python -B scripts/reverse-engineering/mapsense-gps-runtime.py compare --native analysis-cache/mapsense-gps-session/capture-01.json --generated analysis-cache/mapsense-gps-session/generated-01.msgc --output analysis-cache/mapsense-gps-session/comparison-01.json
```

Exit codes are 0 for `MATCH`, 2 for `DIFFERENCES`, 3 for `INCONCLUSIVE` and 1
for `REFUSED`. `byteComparisonStatus` preserves the observed byte result, but
static terrain is admitted only when every room was active and every collision
grid was rebuilt after full level activation. Plain live captures and legacy
captures without that witness are `INCONCLUSIVE`. `gpsAdmitted` and
`movementExecutionObserved` stay false. The recorded capture source determines
`d2rRuntimeCompared`; synthetic fixtures explicitly keep it false.

## Verification on 5 September 2026

- 29 synthetic capture/comparison tests pass, including alteration of each of
  the 21 witnesses, loops, backlinks, unstable/partial reads, bounds, inactive
  rooms, wrong sessions, corrupted artifacts and independent collision masks.
- Three real generator exports (embedded, workspace BKVince and installed
  BKVince data) use seed 1337, difficulty 2, level 2: each has 83 rooms and
  134,400 grid cells in 273,064 bytes. Synthetic extraction/comparison of each
  export matches 132,800 room cells and 6,889 directed room pairs. These are
  generated fixtures, not native samples.
- The embedded export repeats byte-exact. Workspace and installed BKVince
  exports match each other: SHA-256
  `1A66AE8F6063B6C7682E639D4422C47796960A4E9E95D5F48765C8004D598B4F`.
- The default ReleaseSafe helper rebuild matches the r3 ZIP byte-for-byte:
  11,786,240 bytes, SHA-256
  `74CC1DACA28E836C53E10FDB43EE7B37883E73F0894A06E14AA2A8287B138A43`.
- Installed MapSense DLL/helper match r3 and the Suite 1.3.3 allowlist. The three
  installed loader artifacts match the governed public 1.2.1 baseline, whose PE
  file version still reads `1.2.1-beta`. Retail D2R and `.build.info` match the
  governed official 3.3.93847 identity. Forty installed DLL files across both
  scopes were inventoried; that count is not a new loaded/compatibility result.

Local evidence: `workspace:analysis-cache/mapsense-gps-proof-20260905/`, including
`runtime-tests.log`, `capture-native-audit.json`, `runtime-preparation-verification.json`,
`synthetic-comparison-*.json` and `runtime-preflight.json`. The latter preserves
input hashes, installed inventory/configuration hashes and prior log offsets.
No D2R process was present during that 5 September preflight and no runtime file
was changed during preparation.

## First live observations — 6 September 2026

Vincent authorized one offline BKVince launch, up to five read-only captures and
leaving the game open. On official D2R 3.3.93847 / promoted Loader 1.2.1, startup
reached 24/24 with 38 plugins, one global duplicate skipped, the known Doll
Explosion refusal and 17 patches. All five eezstreet plugins were present; this
is startup evidence, not a complete functional coexistence matrix. Installed
Skill Trees Revamp 0.8.3 differed from the prior preflight and was preserved.
The installed MapSense DLL and helper still matched 1.0.2 r3.

The user reported **QtyTester, Harrogath, Insanity**. The read-only capture
independently observed seed **1396293576**, difficulty **2**, level **109**,
player ID 1 at world subtile **(5098, 5023)**. All 21 native witnesses passed in
the running image. At 11:03:39 and 11:07:32 UTC, both captures passed the two-read
stability checks in approximately 10 ms and copied all 25 active rooms (none
inactive), totaling 40,000 cells.

The generated level used those exact session values and the installed mod's
table/tile roots. Its SHA-256 is
`75AA63CEFF29C767F74F4545B89673E48E6F5B157C15FA7A17408DA84AB21010`.
Both comparisons have the same counts:

| Observation | Result |
| --- | --- |
| Exact room rectangles / directed room pairs | 25 / 625 matched |
| Raw cells differing | 58 of 40,000 |
| `0x1C09` differences | 13 cells blocked only in D2R |
| `0x804` differences | 0 |
| XOR bits | `0x80`: 5, `0x100`: 45, `0x400`: 8, `0x1000`: 5, `0x2000`: 5, `0x8000`: 5 |
| Overall comparison | **DIFFERENCES**, not MATCH |

Between the two native captures, the player position stayed unchanged while
20 cells changed (`0x100` in 20, `0x1000` in four). Two cross-shaped groups of
these bits moved; room membership stayed identical. This demonstrates changing
occupancy on this map. It does not identify every occupant or establish that
every raw difference is explained. In particular, do not erase the eight
`0x400` cells or infer ownership of `0x8000` solely from an enum label. The
player's initial cross carries `0x8080` and its centre `0x9080`; a moved-player
sample is still needed to distinguish the contributions at that location.

The observed wall/door/trace bits agree, but equality of these bits in Harrogath
does not qualify all terrain, native traversal or Teleport. Harrogath is a town,
where the existing MapSense navigation is intentionally empty. Live obstacles,
self-occupancy handling and revision updates must be accounted for before a
route can be trusted. Existing occupancy primitives are available in the offline
adapter; no live occupancy feed or renderer was added here.

Evidence: `workspace:analysis-cache/mapsense-gps-runtime-20260906T104056Z/`:
`preflight.json`, `launch.json`, fresh logs, `capture-01.json`, `capture-02.json`,
`generated-01.msgc`, `comparison-01.json`, `comparison-02.json`,
`observation-01.json` and `capture-delta-01-02.json`. For these comparisons,
`d2rRuntimeCompared=true`; `gpsAdmitted=false` and
`movementExecutionObserved=false` remain explicit.

## Remaining gates and rollback

The pilot ended after two of five capture attempts. At 07:42:22 EDT on 6 September,
while QtyTester remained stationary in Harrogath according to Vincent, D2Prism
src/D2Prism.cpp:2112 asserted Present failed (0x887A0005). MapSense logged device
reason 0x887A0006 (DXGI_ERROR_DEVICE_HUNG) and blocked further GPU submissions.
NVIDIA nvlddmkm event 153 was timestamped just before these messages. A WER
Kernel_141 directory was observed, but Windows denied access to its contents.
The CPU assertion report does not identify the originating GPU command or
assign the fault to the game, shared renderer, driver or hardware. The same
error pair already appears on 31 August and 5 September; recurrence of the
symptom alone does not prove the same cause.

Exit was selected at 07:53:16; child PID 25656 no longer exists. The complete
report, logs, event XML and launch dxdiag were recovered to
workspace:analysis-cache/mapsense-gps-runtime-20260906T104056Z/crash-20260906/.
Crash-report SHA-256:
3283B62A37A4621C2D1DAAD3079B036A5979F995D8187F561DA2FA320DD19413.
CRASH-ANALYSIS.md and the provenance JSON files document evidence and limits.
Both GPS captures/comparisons remain preserved. No new GPS renderer was installed
or executed; this diagnosis made no runtime, configuration or driver change.

Unused capture attempts do not authorize a replacement launch. Prepare an
exploitable GPU failure trace and a separately authorized, bounded full-stack
reproduction before resuming moved-player and non-town observations. No GPU fix
or successful runtime navigation is claimed.

Terrain, room boundaries, doors and occupancy must then be compared to D2R;
actual walking/cast landings and real custom levels still require observations.
Multiplayer authority, effective cast reach/skill availability, stale session and
revision rejection, cancellation and rendering remain separate gates. An external
snapshot is insufficient to promote any of them.

Rollback removes only `gps_export.zig`, its build step, the external reader/tests
and these documentation additions. Preserve the earlier routing adaptation and
all unrelated changes. There is no installed binary, configuration or save to
restore for this lot. Existing D2MOO and libd2 credits remain in the plugin docs.

## Route transport preparation — 8 September 2026

The regular helper now owns an MSR1 v1 `route-binary` command. It accepts exact
world-subtile endpoints in one LevelId and emits either the qualified walking
route or the qualified Teleport route from the existing adapter. It does not
read D2R memory, issue movement input or render a path. The DLL contains a pure
MSR1 parser but does not invoke the command or publish a route yet.

The parser validates magic, version, mode, all reserved bytes, effective-data
fingerprint, seed, difficulty, level, exact endpoints, move count and total file
size. It rejects pad transitions, Teleport moves in a walking artifact, negative
coordinates and stale or mismatched request identity. The generated Harrogath
fixture from seed 1396293576 / difficulty 2 / level 109 produced a 16-point walk
and a 5-point Teleport route over the same exact endpoints. This is a helper
transport witness over generated data, not a gameplay or collision-admission
result.

ReleaseSafe GPS tests and the 300-fixture / 600-route proof still pass with zero
errors, zero corner candidates and zero player-footprint candidates. The normal
MapSense build passes all five CTest targets, including the new GPS artifact
contract. Runtime remains blocked on moved-player and non-town D2R comparisons;
therefore no route renderer or installed GPS candidate is enabled by this step.
Two independent clean helper builds are byte-identical at 11,891,712 bytes,
SHA-256 `20F4273B22807D660D280398E5AD3CF0ADC932821FBA5976FD07B7BA27F933C1`.
The portable CPU audit passes x86-64 PE32+ with no VEX/EVEX, YMM, ZMM or opmask
instructions.

## Outdoor movement and Teleport witness — 9 September 2026

The complete BKVince stack on D2R 3.3.93847 and D2RLoader
1.2.2-beta+candidate.2 produced the missing outdoor observations. A Stony Field
walk moved the player from `(5185,5032)` to `(5190,5030)` while the ten qualified
trace-mask differences remained unchanged. Room rectangles and all 1,225
directed room relations matched the generated level exactly.

A Blood Moor cast moved the right-skill Teleport user from `(4133,5216)` to
`(4142,5208)`. Fifteen native cells changed: five near the old position and five
near the new position, using only bits `0x80`, `0x100`, `0x1000` and `0x2000`.
The 26 `0x24` GPS-mask differences remained byte-for-byte stable before and
after the cast. Most are paired `0 <-> 37` cells shifted by one subtile and are
remote from both player positions. This establishes that they did not follow
that player movement; it does not establish generated/native placement drift.

The vendored libd2 byte-exact golden documents the missing control: a plain
activate-then-dump walk bakes room activation order into D2R's CollMaps (645
cells across 36 rooms for its seed-1 witness). Its authoritative capture frees
and reallocates every room grid after the full level walk. Blood Moor retained
four inactive rooms and did not rebuild its grids, so both captures are now
classified `status=INCONCLUSIVE`, with `byteComparisonStatus=DIFFERENCES` and
`terrainComparisonAdmitted=false`. Legacy captures are equally inconclusive.

Movement execution remains observed and GPS admission remains false. The next
gate is a native capture with every room active and every CollMap rebuilt after
full activation, followed by a qualified comparison before invoking MSR1 or
publishing a route. Evidence is under
`workspace:analysis-cache/mapsense-gps-runtime-20260909T030822Z/`.
