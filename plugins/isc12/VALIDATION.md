# ISC12 validation gates

The completed evidence below belongs to candidate 0.2.1. Version 1.0.0 must be
rebuilt and requalified as the exact final artifact before release.

## Foundation and governance

- [x] Governed producer/consumer/sentinel ledger inventories 211 sites across
  15 atomic, proof-only or exclusion groups.
- [x] The ledger validator rejects every `ready` site without a concrete,
  unique expected byte pattern.
- [ ] Quantity and State-ID exclusions are fingerprinted.
- [x] The isolated mod-local cold start admitted the complete active Suite and
  all five eezstreet DLLs without a competing owner on any ISC12 surface.
- [x] API v3, manifest resource, three exports and hybrid flags are scaffolded.
- [x] No build-name/version allowlist exists.
- [x] Admit D2RLoader 1.1/1.2 composition only through exact D2RCore
  providers: bounded relay, export/body, live PDATA/unwind, forward slot and
  exact native destination; retain every provider CALL.
- [x] The config-free 0.2.1 candidate is active by DLL presence and retains no
  parser, path, dependency or configuration artifact.
- [x] The 0.2.1 target is not eligible for a final release archive; the small
  config-free archive is explicitly a public-test candidate.
- [x] Duplicate-scope mutex is PID-qualified: one owner per D2R process without
  blocking a second local host/joiner process.

## G0 — loader and DescFunc

- [x] Pure commit tests prove process-lifetime reservation → tail → conservative
  `capMayBeExtended` guard → count cap. Global readiness/operational is
  published only after G0, G10 and codec all commit; a false tail result
  performs no later write and enters the uncertain-commit path, while cap
  failure enters the guarded cold-restart state.
- [x] A false tail patch result keeps RX/RW resources process-lifetime, logs the
  observed eight-byte seam and immediately terminates fail-closed.
- [x] A failed cap API call is treated as potentially mutating; inactive relay
  fallback accepts only a proven row count at most 511.
- [x] Pure builder accepts 511, 512, 1023, 2047 and 4095 rows.
- [x] Pure builder rejects 4096 rows without changing its destination.
- [x] Dense and sparse fixtures preserve the native ascending signed-16-bit
  `DescPriority` order, including stat ID 4094 and priorities `0x7FFF`,
  `0x8000` and `0xFFFF`.
- [x] MASM restores the original RAX and R9 on vanilla fallback and all six
  remaining nonvolatile registers on success.
- [x] Callback rundown decrements only inside persistent RX success/vanilla
  exits, after the thread has left all DLL code.
- [x] Unsafe inactive-relay state and rundown timeout use Windows fast-fail,
  never a resumable `UD2` or an unsafe unload.
- [x] Require a live borrowed `NativePublicationLeaseView` before G0 resource
  reservation or mutation; it owns no authority and has no release operation.
  The production view is same-thread and bounded to the initial
  `D2RLoaderLoadPlugin` callback. Loss before the first write and after an
  attempted write are tested as distinct outcomes.
- [ ] Prove quiescent or transactional publication of the non-aligned
  eight-byte `PatchJmpRel32` seam.
- [x] Native G0 relay executed twice during the qualified TXT load and reached
  `DataTablesLoaded` plus complete frontend startup without stack/order fault.

### G0-BBE — separate native expression path

- [x] Census seven compiler callsites, three resolver frontends converging on
  one shared resolver core, seven D2R evaluator callsites using one protected
  callback table/count, and the eighth generic evaluator call using no table.
- [x] Prove the ItemStatCost branch loads `DataTables+0x1270`, resolves the
  name and stores the returned ordinal as a full dword without a `0x1FF` mask.
- [x] Prove IDs `512..4094` compile as callback token 5 plus a 16-bit operand,
  decode unchanged and positive into EDX, and remain full-width through the
  attested stat callback's row guard and three stat-accessor tails.
- [x] Govern ten exact proof-only sites under `G0-BBE-stat-formula`; the ledger
  validator rejects any target other than `unchanged` for this group.
- [x] Attest from governed read-only D2R `3.3.93847` memory that protected
  literal `0x1CFA68C` is the slot-5 `stat` keyword and record 5 of the nine
  16-byte callback-info records at `0x1D09C50` links that slot to callback
  `0x3B33F0` with arity 2. Both code anchors matched; no write right or
  mutation was used. Preserve the raw/session and ASLR-normalized hashes in the
  governed findings.
- [ ] During a future qualified cold start, compare the same protected 16/144
  bytes before TXT compilation and after `Gameplay data tables loaded` to
  strengthen lifecycle immutability; any divergence keeps runtime publication
  closed.
- [ ] Compile/evaluate disposable ID-512 and ID-4094 fixtures after the runtime
  gates open, without a real save.
- [x] Keep G0-BBE distinct from the official eezstreet
  `items.playerConditionCalc` feature; PluginPack coexistence cannot close the
  ISC12 native ordinal-width proof.

## G1 — generic item codec

- [x] Prove the critical bounded-serializer defect in the earlier plan: the
  owner has a 511-DWORD compound-suppression table and its lookup at
  `0x37F17C` would read beyond that table for every ID at or above 511.
- [x] Replace the superseded narrow writer-ID site in the source planner with
  the exact in-place 42-byte body `[0x37F17C,0x37F1A6)`, without double
  ownership. Preserve the table compare/suppression for IDs `0..510`; bypass
  the lookup for larger IDs;
  emit `min(ID,0xFFF)` at width 12; preserve either the direct native CALL or
  the exactly attested `WriteItemSaveStatId` provider at `0x37F1A1`, and
  continue rejecting a real stat ID `0xFFF` in the schema.
- [x] Read-only live proof confirms the D2RLoader 1.2 G1 provider forwards
  every ID unchanged to `D2R+0xA1B710`; its `<0x200` private census limit is a
  metadata gap, not an ISC12 writer truncation. The qualified cold start now
  publishes G1 as part of the canonical set.
- [x] The reader width/sentinel/seed mutations have exact signatures; the
  subsequent-reader interior CALL resolves exactly to governed
  `BITSTREAM_ReadBitsThunk 0xA1B6C0`, and retargeting is rejected before any
  write.
- [x] The first reader preserves the source-level `previousStatId = -1`
  invariant.
- [x] The canonical G1–G4 full-set preflight, bounded replacement semantics and
  corrupt-fingerprint tests pass with the 42-byte writer body integrated.
- [x] Require the active initial-load publication view before G1 can commit
  with the canonical set.
- [x] Route G1 only through the full G0/G10/codec startup coordinator; no
  independent or legacy G1 publication path exists.

## G2–G4 — player, save and preview codecs

- [x] Historical bounded-G1 intermediate checkpoint after replacing the old
  narrow writer site: 20 mutable sites, 84 differing byte slots and 71
  witnesses. Bounded G1 contributes 44 slots plus 14 new owner/caller/table/
  snapshot/compound witnesses; G2–G4 retain 40 slots/51 witnesses and the
  intermediate total included six then-read-only G9 evidence surfaces.
- [x] Integrate G9-A as the first canonical group, bringing the whole prepared
  transaction to 24 mutable sites, 102 differing-byte mutations and 77 witnesses.
  Its internal order is queue `0x9C`, queue `0x9D`, entry `0x9C`, entry `0x9D`;
  G2, G4, G1 and G3 follow.
- [x] Pair every reader, writer and terminator with unique signatures; the G4
  owner and version dispatcher prove its four ID reads are exhaustive while
  its two schema-driven value reads remain unchanged.
- [x] Preserve legacy-version rejection through G10 rather than implicit
  migration.
- [x] Pure 12-bit ID fixtures cover boundary IDs, the `0xFFF` terminator and
  missing-terminator rejection without changing the caller's output.
- [x] Prepare 40 governed byte slots across 16 exact sites as three fail-closed
  atomic groups: all mutation signatures plus 51 unchanged
  owner/return/capacity/layout/cleanup/overrun witnesses
  preflight before the first write, publication requires the canonical live
  quiescence lease and any false write or flush result requires a cold restart.
- [x] Keep all 40 G2–G4 slots inside the single canonical transaction.
- [x] Confirm all 84 recomputed canonical G1–G4 slots remain owned by that
  transaction after source integration.
- [x] Confirm all 102 slots in the G9 plus G1–G4 plan are reached only through
  the one production startup caller; later full-stack cold starts attest the
  runtime publication in both supported scopes.
- [x] A full-stack D2RLoader 1.2 cold start attests the exact
  `WritePlayerSaveStatId` G3 provider and its live forward slot to
  `D2R+0xA1B710` before the source transaction can publish.
- [x] Attest the dynamic caller as one indivisible pair: canonical
  `0x8000 + direct D2R+0x52F090`, or D2RLoader
  `0xFFFF + WritePlayerSaveWithEnvironmentCapture`. Validate the provider's
  complete 1.1/1.2 body hash, PDATA/unwind, bounded relay and native forward
  slot; preserve both the capacity and CALL.
- [x] Reject the native schema fail-closed when `CsvBits > 32` or
  `CsvParamBits > 16`, matching the native 32-bit value and 16-bit parameter
  widths. Under the unchanged 512-entry cap, a complete worst-case section is
  3,844 bytes including marker and fits G2's fixed `0x4000` buffer.
- [x] Pure G2/G3 preflight validates marker `0x6667`, schema/ID bounds, every
  param/value width, at most 512 entries, truncation and the `0xFFF` sentinel;
  failure leaves output unchanged.
- [x] Retarget the exhaustive G2/G3 CALLs `0x531A6D`, `0x52EC4A` and
  `0x530A34` in the prepared set, leaving native owners `0x530A00`/`0x533760`
  untouched. Process-lifetime RX entries, FRAME wrappers and no-throw helpers
  preserve the six-argument ABI, accept exact v105 only, return native error
  `0x12` on preflight rejection and fast-fail on native fault or successful
  cursor divergence. The schema shared lock spans preflight, native decode and
  postcheck.
- [x] Prepare governed propagation of the G3 bit-writer overrun flag: a copied
  RX leaf preserves the native used-end result, returns the DWORD at
  `[bitstream+0x20]` in EDX, and is ordered to commit the exact CALL at
  `0x5353C2` before `mov eax, edx` at `0x5353D2` as the final canonical G1–G4
  site in a future authorized transaction, flushing both ranges.
  Signed rel32 limits, opaque loader provenance, no-op displacement bytes,
  corrupt fingerprints, partial writes and both final flush failures are unit
  tested. No code cave or unwind-owned byte is used.
- [x] Replace the forgeable quiescence boolean with a borrowed, validate-only
  `NativePublicationLeaseView`; validate it before every
  fingerprint/write/flush and unit-test absent or revoked authority plus
  partial-mutation handling. Production constructs it only inside the
  same-thread initial-load window.
- [x] Add the one-shot full-set coordinator and its production startup caller:
  preflight G0, G10 and codec before reservation/write; reserve process lifetime
  once; commit G0, G10 and codec; stop at `CommittedPendingReadiness` with all
  readiness flags false; publish readiness exactly once before the initial
  callback returns; make `Poisoned` terminate the process without rollback.
  Unit-test absent/revoked views, every preflight rejection, reentry,
  mutate-then-uncertain outcomes, monotone terminal states and both startup
  readiness paths.
- [x] Split G0, G10 and codec into immutable preflight/commit adapters and bind
  the real loader callbacks and relay targets to the tested coordinator. Prove
  all three preflights before reservation/write, G0 tail→guard→cap, G10
  reader→writer, codec G9/G2/G4/G1/G3, one process-lifetime reservation,
  native-commit-before-readiness, actual patch/write/flush failure poison, both
  startup outcomes, exactly one production `PublicationCoordinator::Publish`
  caller and no production G0-only fallback.
- [ ] Optional upstream hardening: obtain a documented loader-owned transaction
  if D2RLoader wants reusable cross-publisher serialization and explicit
  owner/thread/epoch semantics. This is no longer a prerequisite for the
  isolated ISC12 runtime spike.
- [x] Retarget only preview CALL `0x61CF90` in the prepared set, call shared
  native copy owner `0xA1E110` exactly once, then validate whole v105 D2S,
  context `<4`, wrapper-required marker `0x6667`, every schema-bound ID/payload,
  cap 512 and `0xFFF` within copied `N<=0x4000`; rejection returns zero through
  the governed buffer-free/false exit `0x61D87D`. Legacy branch A stays 9-bit.
- [x] Unit-test G4 valid input plus bad magic/version/declared size/checksum,
  context, marker, ID, missing sentinel, native-underflow length 342 and
  capacity overflow, with failure output unchanged. The frontend preflight
  also accepts the exact native 403-byte (`0x193`) header-only form emitted for
  a newly created character, while rejecting every ambiguous intermediate
  length before the full player-stat section.

## G5–G9 — network

- [x] Govern and atomically publish the `0x3E`, `0xA8` and inner `0xAA`
  producer/consumer width and sentinel pairs. The `0xAA` estimator changes both
  +9 contributions to +12 before its unchanged 0xF4 guard; outer State IDs stay
  9-bit.
- [x] Close `0xAC` cardinality and headroom: at most 16 copied records, 1,289
  worst-case bits = 162 bytes, 175 bytes including its 13-byte header and 69
  bytes remaining below the unchanged 244-byte guard.
- [x] Census the suspected fixed-byte packet paths. `0x1D..0x1F`,
  `0x9E..0xA2` and `0x20` use WORD stat-ID fields in producer and consumer; the
  mercenary/private dispatcher also loads the queued stat ID as WORD. Nine exact
  unique proof-only windows require no packet-layout mutation.
- [x] Build the canonical G5-G8 transaction with 19 additional mutable sites,
  27 mutations and eight capacity witnesses. All 27 mutation/capacity windows
  match exactly once in the governed native corpus.
- [x] Govern twelve exact G9 witnesses proving that 0x9C/0x9D serialize one node
  with recursion disabled, the root cap is 244 bytes for 0x9C, every 0x9D cap
  is 239 bytes, socketed descendants are separate 0x9D packets, and the
  unchanged real queue is `0x4817F0` with its span dispatch.
- [x] Add exact unique producer-epilogue witnesses `[0x479E23,0x479E41)` and
  `[0x47A019,0x47A037)`, covering cookie check, stack deallocation,
  nonvolatile pops and `RET`.
- [x] Purely account for every node's 12-bit record/sentinel tokens, root versus
  descendant packet kind and exact native boundaries: 244 bytes for a root
  0x9C and 239 bytes for every 0x9D, including a root sent as 0x9D.
- [x] Record Vincent's 30 August 2026 product decision: autonomous native-only
  G9-A, with the exact native caps as the sole product contract.
- [x] Implement and test an allocation-free pure snapshot planner that rejects
  invalid counts, packed child lists, socket capacity, indices, shared children,
  cycles, unreachable nodes and oversized late descendants before any callback.
- [x] Preserve native depth-first preorder and accept partially filled socketed
  items by separating occupied-child count from total socket capacity.
- [x] Prove the native per-item occupied-child upper bound `occupied <= 7`:
  the `0x37D60B` loop counts immediate children, clamps at seven and writes
  three bits at `0x37D66F..0x37D678`, independently from socket capacity.
  This local branching bound does not prove a global node or depth bound for a
  recursively socketed tree.
- [x] Impose an independent ISC12 staging contract of 64 packets, `0x4000`
  total captured bytes, depth 16 and seven immediate children per node. These
  fixed safety caps do not claim a native/global tree maximum.
- [x] Split the long 0x9C witness into
  `0x479D85+139` and `0x479E15+14`, and the long 0x9D witness into
  `0x479F76+139` and `0x47A006+19`, excluding the five-byte CALLs at
  `0x479E10` and `0x47A001` from every unchanged witness.
- [x] Fix the engineering sequence as bounded G1 serializer first, then G9
  staging. This sequencing is not permission to publish either group.
- [x] Prepare the four-site native staging transaction in the fail-closed order
  queue `0x9C`, queue `0x9D`, entry `0x9C`, entry `0x9D`. Copy actual native
  packet bytes into fixed thread-local storage; validate the complete batch
  before any real call to `0x4817F0`; discard every rejected batch with zero
  sends. Do not use a second scratch serialization at producer entry.
- [x] Use registered 10-byte producer trampolines, register live unwind metadata
  through `RtlAddFunctionTable` before publication, fail closed on registration
  failure, and abort/discard the transaction from SEH `finally` on abnormal
  producer exit.
- [x] Require source-side live `RtlLookupFunctionEntry` checks at each producer
  body and `RET`: matching Begin/End/UnwindData, End inside the image and a
  function range covering the governed epilogue.
- [ ] Attest those lookups against the official live image. Rehydrated static
  `.pdata` is protected and non-authoritative; the runtime may fail closed.
- [x] Prove the structural native cardinalities: at most seven emitted stat
  lists and sentinels per node, at most 511 snapshot records per list, and six
  low-ID compound primaries suppressing eight partner records. The resulting
  ceiling of 3,577 ID tokens plus seven sentinels is not a realizable item or a
  worst-case byte bound.
- [x] Validate actual captured packet bytes, packet headers/lengths, preorder,
  parent links, cycles and all four fixed caps across the complete batch before
  source logic permits replay.
- [ ] Exercise live-tree stability, TLS reentry, overflow, backpressure and
  delayed queue semantics in D2R before any runtime claim.
- [ ] Prove the G9-A preflight with the complete Suite and the five official
  eezstreet plugins without assigning transport ownership to any of them.
- [x] Source policy admits only native packet lengths through `0xFC`, so an
  accepted staged packet cannot wrap its one-byte length.

## G10 — clean-sheet format gate

- [x] D2S outer-load, magic/version dispatch, modern decoder and first
  player-stat decode boundary have exact unique signatures.
- [x] Historical audit proved that version-only and padding/reserved-byte
  markers cannot enforce a hard fail-closed namespace. Vincent later selected
  a support-boundary contract instead of hard physical rejection.
- [x] Prove D2S outer-load replacement-buffer lifetime, native record cleanup,
  output nulling and failure destruction.
- [x] Identify the physical D2S/D2I state-1 writer and prove vanilla commit is
  direct, non-atomic and blind to failed or partial writes.
- [x] Prove D2I physical whole-file ingress and upload splitting before either
  native sector reader can mutate an item.
- [x] Implement and unit-test the disconnected sibling-temp/full-write/flush/
  atomic-replace primitive: existing
  and absent destinations, looping partial writes, flush, write and replace
  failures preserve the original and clean ordinary siblings.
- [x] Retire that primitive from the runtime DLL while retaining it as a future
  external migration-tool building block.
- [x] Prove the `0x00B3` fragment format, client handler, collection terminator,
  whole-D2S validation and copy into object state `4`.
- [x] Trace object state `4` and the secondary concatenated store to the shared
  physical writer.
- [x] Freeze the writer job's status/callback/lock/tag/failure contract: every
  pre-commit ISC12 failure reuses native status `6` and the existing
  open-failure branch; no automatic retry is claimed.
- [x] Freeze exact object classification: path-free lower-case `.d2s`, but only
  the four canonical full shared-stash names for `.d2i`; every other manager
  object passes through vanilla.
- [x] Freeze reader rejection cleanup: resize buffer to zero, reset aux/state,
  then reuse native close/unlock/status-6 before either direct caller can
  publish the object.
- [x] Govern exact unique five-byte reader `0x9FC654` and writer `0x9F95A2`
  mutation seams without claiming either hook is installed.
- [x] Connect both seams to process-lifetime RX relays and separate RW rundown
  state. The current reader validates the unchanged native container; the
  current writer validates then delegates to the native continuation before
  `CREATE_ALWAYS`.
- [x] Include both persistence seams only in the canonical startup transaction;
  source/build validation and first runtime publication are green.
- [x] Preserve the tail-entered ABI contract without claiming general unwind:
  MASM metadata covers each stub's local allocation only; all callbacks are
  `noexcept`, expected faults are contained and unexpected failures fast-fail.
- [x] Historical prototype froze the 96-byte envelope v1, exact length, store
  kind, codec/sentinel, schema and payload SHA-256 fields plus deterministic
  D2S/D2I inner preflight.
- [x] Freeze canonical schema descriptor v1 with strict UTF-8 `Stat`, physical
  ordinal, normalized semantic flags, current v105 save fields, resolved
  references and effective `stuff`; `*ID`, legacy and display-only fields are
  excluded.
- [x] Prove that strict D2S/D2I version 105 selects only current save fields;
  the individual item tag does not reactivate the legacy ItemStatCost layout.
- [x] Golden fixtures prove rename, reorder and current semantic mutation while
  all three unreachable legacy fields leave the descriptor unchanged;
  wrong schema, truncation, trailing bytes, mixed D2I sectors and payload hash
  corruption; builders leave output unchanged on rejection.
- [x] Current whole-store policy passes every non-target object through,
  rejects failed/short target reads, validates complete standard D2S/D2I
  targets, and rejects the retired outer envelope at the runtime boundary.
- [x] Recover the exact runtime `Stat` sequence from `DataTables+0x1270` through
  governed count/name functions and copy every linker-owned name immediately.
  Stage the Classic/base and expansion captures with `SchemaReady=false`, then
  publish only the exact candidate named by the RotW `DataTableServiceV1`
  TableView at the synchronous `DataTablesLoaded` boundary. Re-decode the
  authoritative bytes after loader post-processing; pointer/count equality
  alone is not sufficient.
- [x] Treat a non-zero changed lifecycle revision as an opaque token rather
  than assuming numeric monotonicity, and make listener unregister race-safe
  with an atomic `Stopping` state plus a stable callback token.
- [x] Hold a non-blocking shared schema lease across targeted native buffer
  snapshot and validation; if reload owns the exclusive lock, reject without
  waiting or delegating a write.
- [x] Reject a divergent schema reload fail-closed; an identical reload reuses
  the immutable published snapshot without a sidecar or generated manifest.
- [x] The first disposable write with the envelope candidate produced an exact
  499-byte file: 96-byte ISC12 envelope plus a valid 403-byte D2S. D2RLoader
  1.2 nevertheless rejected it at the frontend as `invalid-character`; this is
  the runtime evidence that retired the outer envelope.
- [x] Vincent selected standard native D2S/D2I containers, D2RLoader `.d2rl`
  environment warnings, mandatory backups, new characters or future external
  migration. ISC12 does not promise a hard block against deliberate misuse.
- [x] Pure policy and native-adapter fixtures accept valid standard target
  stores, reject malformed or retired-envelope targets, preserve native read
  storage, and delegate valid writes through D2RCore's environment pair.
- [x] Execute a standard-container disposable create/save/reload and confirm
  the `.d2rl` sidecar plus accepted read/delegated-write diagnostics. The
  `Iiscxiirawtest` Amazon was created as a native 403-byte D2S, appeared in the
  frontend, entered the Rogue Encampment, saved to a 1,297-byte D2S plus a
  6,261-byte `.d2rl`, then re-entered gameplay. The standard-container reader
  and writer reported `error=0/0`; the full D2S carries marker bytes `67 66` at
  `0x341`.
- [x] Disposable D2S fixtures serialize IDs 512 and 4094 with values 12 and
  94, preserve both through two cold save/reload cycles, and remain accepted by
  the standard-container reader. A controlled shared-stash D2I grows from
  68,216 to 68,307 bytes after moving a socketed Gothic Plate into it; SHA-256
  `7375F2F7CB2DAC3178853397D5A7FCE6782F5B26220A0980995261C76E96507F`
  remains unchanged through a full process restart, the armor reappears and
  its real three-node socket tree is captured again.

## Runtime and gameplay

- [x] Foundation 0.1.0: two byte-identical Release builds with `/W4 /WX`,
  CTest `1/1`, PE x64, version 0.1.0 and three exports.
- [x] Historical initial loader-only 0.2.0 artifact, superseded and not current:
  two byte-identical 179,200-byte Release builds,
  SHA-256 `C2B461CF8373CD3FD49D125A1DA9B195E6D917A62EE24CFEBFABD1FA0D1A4D93`,
  `/W4 /WX`, CTest `1/1`, PE x64 and three exports.
- [x] Last verified pre-discovery baseline: G10-B P3b plus the former canonical
  G1–G4 planner passed Release `/W4 /WX` and CTest `3/3`; the governed ledger
  was `VALID` at 178 sites / 14 groups, with 20 mutable codec windows, 49
  mutation slots, 57 witnesses and zero publication.
- [x] Intermediate bounded-G1 source/static gate: Release `/W4 /WX`, CTest `3/3`,
  governed ledger `VALID` at 193 sites / 14 groups and every added signature
  unique, with 20 mutable sites, 84 differing slots, 71 witnesses and zero
  native publication.
- [x] Historical outer-envelope G9-A plus startup-publication source/static
  gate: two full Release `/W4 /WX` builds and CTest `5/5` passed; the
  byte-identical 445,952-byte governed DLL SHA-256 was
  `EFCA4EBAECDC7E0EF7BE70D2BE741FD7D73DED0ACA85873507CCA2D2B625F3DB`; the
  ledger is `VALID` at 211 sites / 15 groups and the canonical codec transaction
  has 24 mutable sites, 102 mutations and 77 witnesses.
- [x] Current standard-container source/static gate: two full Release `/W4 /WX`
  builds and CTest `5/5` pass; both 435,712-byte DLLs have SHA-256
  `E7167627F3577C4F2A7222BBD81D3D2343874A4BBFF94DE008AA86F6ACC4F568`;
  the ledger remains `VALID` at 211 sites / 15 groups.
- [x] Final G9-invariant candidate: two reproducible Release builds, the main
  build and the deployed DLL are byte-identical at 446,464 bytes, SHA-256
  `1311F1C4BE44B0918F34C32007C3A19D35D240D8B72DCAD8C1853EEE53EC11B5`;
  CTest remains `5/5`. The invariant is exactly
  `childTemporaryFlags = parentTemporaryFlags | 0x08`.
- [x] Isolated full-stack mod-local cold start on D2R 3.3.93847 and D2RLoader
  1.2 admitted every D2RCore provider and live unwind contract; published
  G0/G10/G9/G1-G4; compiled 190 TXT tables; selected RotW ItemStatCost revision
  1 with `rows=400`, `G0-builds=2` and `SchemaReady=true`; and reached
  `D2R startup complete`. The stack reported 36 loaded plugins, all five
  eezstreet DLLs, 17 patches and only two known unrelated mod-local failures.
- [x] Cold-start the current exact standard-container DLL
  `E7167627…CC4F568` with the same full stack: 36 plugins loaded, all five
  eezstreet DLLs, 17 patches, the two known unrelated mod-local failures,
  authoritative 400-row schema published and `D2R startup complete`.
- [x] Execute create/save/reload on that exact DLL. The initial native
  header-only D2S was accepted before schema publication, Save & Exit produced
  the full D2S and D2RLoader environment sidecar, and the same character
  reloaded into the Rogue Encampment with ISC12 and the complete stack active.
- [x] Leave the reloaded session active for a bounded soak: repeated 1,297-byte
  D2S and 68,216-byte standard shared-stash D2I writes continue at the native
  cadence with `error=0/0`. This is write-path evidence, not a controlled D2I
  read/two-cycle fixture.
- [x] Complete-stack mod-local cold start.
- [x] Complete-stack global cold start with the same DLL and TOML: `scope=global`,
  standard D2S/D2I reads accepted, 36 plugins loaded, all five eezstreet DLLs,
  17 patches and only the same two known unrelated failures. A real three-node
  G9 tree passes before the pair is returned to mod-local scope.
- [x] Live 0x9C/0x9D root/descendant, overflow, reentry, bounded backpressure
  and coexistence cases. Real 0x9C and 0x9D transactions include a socketed
  three-node tree with `captured=3`, `queued=3`, `staging-error=0` and
  `flush-error=0`. The startup self-test reports `accepted-tree=3/3`,
  `overflow=zero-callback` and `flush-reentry=one-callback-then-fatal`. The
  native queue returns `void`; zero-call rejection is guaranteed before flush,
  while a failure after the first real queue call is terminal and not
  rollback-capable.
- [x] Initial disposable new-save gameplay and one save/reload cycle.
- [x] Second D2S round-trip, controlled shared-stash D2I fixture and serialized
  extended-stat fixtures 512/4094.
- [x] Reproducible G5-G8 publication candidate: two byte-identical 451,072-byte
  Release `/W4 /WX` builds, SHA-256
  `6089619DE3B01FD474669096A8AEC8A470559FAD993DCB939AC976709A7D2D52`,
  CTest `5/5`, ledger `VALID` at 228 sites / 15 required groups.
- [x] Full-stack mod-local and global cold starts of that exact candidate on D2R
  3.3.93847 / D2RLoader 1.2: each published 43 codec sites / 129 mutations,
  loaded 36 plugins with all five eezstreet DLLs, applied 17 patches, compiled
  190 TXT tables, selected the authoritative 400-row schema and reached
  `D2R startup complete`; only the two known unrelated Stash Search and Revive
  Overhaul failures remained. Final state is mod-local, hashes restored, no
  global duplicate and no running D2R process.
- [x] Public-description-only source rebuild: two byte-identical 451,072-byte
  Release `/W4 /WX` builds, SHA-256
  `AFB4B2D1F779A368C3139BB5AF9EDC59CFD4B83042C88AD2EE7991C9E62DFF00`,
  CTest `5/5`, exact PluginInfo/Windows description and full-stack mod-local and
  global cold starts with 36 plugins, all five eezstreet DLLs and 17 patches.
  Final state is the new DLL mod-local, no global duplicate and no running D2R
  process. Gameplay and TCP/IP were not rerun; their exact-binary evidence
  remains attached to `6089619D...D2D52`.
- [x] ISC12 0.2.1 policy accepts only the complete native G9 provider or the
  exact six tracked inline hooks solely owned by plugin ID
  `extended-item-stats` with installed file version 0.3.14. Unit tests reject a
  wrong version, partial hook set, multiple owners and a different owner ID.
- [x] Two independent Release `/W4 /WX` builds and CTest `5/5` runs are
  byte-identical at 329,728 bytes, version 0.2.1, SHA-256
  `C6B4E610F34E4AF42553E606EF835C9E7619916B5BC3ED1209490B9894B55395`.
- [x] Mod-local D2R 3.3.93847 / D2RLoader 1.2 cold start with
  `ExtendedItemStats.dll` 0.3.14 loaded first: all six hooks were attributed to
  its sole owner, ISC12 delegated G9 transport, published 43 codec sites / 129
  mutations and reached gameplay. The deployed DLL matched the reproducible
  build above.
- [x] Positive source-to-target runtime witness: `HECubeMove.d2s` migrated from
  BKVince data to Yupgoolg data, loaded into the Rogue Encampment, saved
  repeatedly with `error=0/0`, exited and loaded into gameplay again. The final
  1,183-byte D2S is SHA-256
  `C634CA357C49B1609894CAAAB67AF2F243801136A11CBF3A57C7BD231099AEC5`.
  The compatible target-schema shared stash was also written repeatedly at
  7,016 bytes with `error=0/0`, final SHA-256
  `C5F8B88BB5BFB689A637509D4DA78D41C614B0600714AA994B10EBCE9D0C4070`.
- [x] Inverse load order plus live external G9 producer: ISC12 0.2.1 loaded
  before `ExtendedItemStats.dll` 0.3.14, initially kept G9 unresolved, then the
  first real 0x9C/0x9D producer re-inspected all six tracked hooks and logged
  `G9 transport sealed to the attested ExtendedItemStats 0.3.14 provider`.
  The same DLL then passed a cold reload and a separate global-scope cold
  start, where the provider was already present and admitted during startup.
- [x] Offline non-empty cross-mod D2I conversion: the governed 680-byte
  BKVince shared stash has six pages and nine items on page 6. It converts to a
  695-byte Yupgoolg-targeted ISC12 D2I, SHA-256
  `AACEB3C444903F8B1E777CC899E4D6EAF91449CE8DEC45B30E63E2DEA11CC85B`,
  without modifying the source SHA-256
  `86833D4BA22FD64DBEE0C4701B6CB5A4D0329A23EA79C08E9F5FFB572404E1B4`.
- [x] That exact 695-byte nine-item cross-mod D2I was accepted under Yupgoolg,
  rewritten by the target's 101-page stash layout to 7,299 bytes, and read
  again after a full process restart and in global scope. All three captured
  copies are byte-identical at SHA-256
  `ED0C4B77AD8C83A56C30080652FAF8C161B6754150A45668CFE316871238BB0C`;
  target-schema parsing still finds exactly nine stackable material items on
  page 6. The larger personal BKVince stash remains correctly refused because
  it contains seven target-missing item bases.
- [ ] Matching host/joiner passes; mismatches fail closed.

### External two-client handoff

One human may operate both sides, but the proof requires two independent D2R
clients active at the same time. A single process/account instance cannot close
host and joiner behavior. Each matching peer must use:

- the exact config-free 329,728-byte ISC12 0.2.1 DLL SHA-256
  `C6B4E610F34E4AF42553E606EF835C9E7619916B5BC3ED1209490B9894B55395`;
- matching mod data, high-ID ItemStatCost schema and test fixtures;
- the same D2RLoader scope and a matching generated environment fingerprint;
- a schema and saved/item fixture that actually reaches IDs above 510. The
  restored 400-row ISC12Lab/BKVince table cannot by itself exercise 12-bit
  network IDs.

The community matrix has three required sessions:

1. Matching ISC12 host and joiner: connect, materialize the high-ID fixtures on
   both peers, exercise shared item/stat transitions, change area, save, fully
   reconnect and reload with identical observed values.
2. ISC12 host with an absent/vanilla ISC12 joiner: reject fail-closed before
   shared state can diverge.
3. Absent/vanilla ISC12 host with an ISC12 joiner: reject fail-closed before
   shared state can diverge.

Collect fresh D2RLoader and ISC12 logs from both peers, DLL/data hashes,
scope, environment fingerprint, the visible before/after stat values and the
post-reconnect save evidence. A successful connection using only IDs below 511
does not close G5-G8 functional multiplayer behavior.

Mod-local and global publication, the schema lifecycle, native character
creation, standard D2S/D2I persistence, two cold D2S cycles, serialized IDs 512
and 4094, real 0x9C/0x9D socket-tree capture, overflow/reentry containment,
inverse-order ExtendedItemStats coexistence and a non-empty BKVince-to-Yupgoolg
D2I load/save/reload have run on disposable fixtures. The runtime profile and
original shared stash were restored byte-for-byte after evidence capture;
neither scope retains ISC12 and no Diablo process remains. G5-G8, their native
budgets and the fixed-width packet census are closed in source/static proof and
both hybrid startup scopes. Multiplayer host/joiner plus mismatch rejection
remain open before any complete network claim or public release.
