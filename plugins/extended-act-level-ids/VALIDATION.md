# Extended Act Level IDs validation

The historical evidence below belongs to candidate 0.1.1. Version 1.0.0 must
be rebuilt and requalified under its final public identity before release.

## Proven native contract

- Runtime under test: official D2R `3.3.93847`.
- Governed common corpus: D2R `3.2.92777` / `3.3.93847` byte-exact surfaces.
- Central resolver: `D2R.exe+0x326710`.
- ABI: `uint8 (uint8 dataContext, int32 levelId)`.
- Direct-call census: 113.
- Resolver fingerprint: 48 bytes, one match in the governed image.
- Runtime probe: `Levels` row size `0x18C`, `Act` byte `+0x0D`, Classic and
  LoD 137 rows, BKVince RotW 147 rows.
- BKVince gameplay context observed: `dataContext=3`, matching `Bank::Rotw=3`.
- Evidence log:
  `analysis-cache/extended-act-level-ids-probe/evidence/20260901-1918/ruffneckk-extended-act-level-ids-probe.log`.

## Automated gates

- [x] Policy tests pass (`1/1`) in both clean builds.
- [x] Two independent clean Release builds pass.
- [x] Final DLL hashes match byte-for-byte:
  `16805ACA4207015729516687D1A869226569A64BCCEC0BED318E453CD08E7775`.
- [x] The final DLL is 48,640 bytes and contains no JSON parser, embedded
  configuration, configuration path, or `enabled` branch.
- [x] Required D2RLoader exports and API v4 manifest pass.
- [x] PE metadata, dependencies, author, description, and version pass.
- [x] No build-name or version allowlist is present.
- [x] Mod-local cold start without a configuration file passes on 2 September
  2026 with the complete active stack: 38 plugins, 17 memory patches, and
  startup `24/24`.
- [x] Global cold start without a configuration file passes on 2 September
  2026 with the same complete stack and startup `24/24`.
- [ ] Duplicate global plus mod-local arbitration was proven on 0.1.0 but was
  not rerun for 0.1.1 after runtime control was paused.

The 0.1.1 scope logs are archived locally under
`analysis-cache/extended-act-level-ids-product/evidence/20260902-0851-v0.1.1-configless-matrix/`.
The mod-local run reached complete startup before the already known D2RLoader
TACT assertion occurred afterward; no Extended Act Level IDs refusal or failure
was logged.

## Functional resolver fixture

A temporary CRLF `levels.txt` was generated for 0.1.0 through the governed TSV API by
copying row 146 to `Id=147`, setting `Act=0`, and preserving all 188 columns.
The source and generated table both passed byte-exact round trips.

- [x] D2R compiled the fixture and the RotW cache increased from 147 to 148
  rows while Classic and LoD remained at 137.
- [x] `extended-act-level-ids resolve 147` called the hooked central resolver
  at runtime and returned `Act index 0 (Act 1), data context 3,
  source=Levels.txt`.
- [x] The proof was persisted in the plugin log at
  `2026-09-01 19:47:22.495` and archived under
  `analysis-cache/extended-act-level-ids-product/evidence/20260901-1947-functional-fixture/`.
- [x] The temporary row was removed. Runtime and governed `levels.txt` both
  returned to SHA-256
  `A46B5438164ADB1FB9540890103594EA48A79AFA2478CB6865D2E6DB5795EB04`.

The resolver and cache implementation are unchanged in 0.1.1, but the exact
fixture was not rerun after configuration removal. That current-version runtime
case remains `not run` until Vincent authorizes another launch sequence.

## Playable-area release matrix still required

The central resolver defect is proven fixed, but the temporary row was not a
fully authored playable area with a valid transition. A public release gate
therefore remains open until a real new area after ID 146 is exercised for:

- generation and travel in both directions;
- town/start behavior, portals, automap, waypoint and quest interactions;
- save/reload;
- mouse and controller;
- solo, host, and joiner authority with no desynchronization;
- coexistence with all active RuffnecKk and eezstreet plugins.

No item in this playable-area matrix is claimed as passed.
