# ISC12 1.0.0

> Config-free public release. Installing the DLL activates ISC12 after
> its complete native fingerprint passes.

ISC12 is a clean-sheet D2RLoader format for overhaul mods that need more than
511 `ItemStatCost` rows. It reserves serialized IDs `0..4094` for stats and
`0xFFF` as the list terminator.

Version 1.0.0 promotes the proven 0.2.1 candidate and its narrowly attested
coexistence contract for `ExtendedItemStats.dll` 0.3.14. It uses a same-thread
authority bounded to the
synchronous initial
`D2RLoaderLoadPlugin` callback, preflights every G0, G10 and codec surface before
the first write, reserves relay/state lifetime, then commits G0, G10 and codec
in one startup window. This follows D2RLoader's official startup patching model;
it does not claim that the current SDK documents a global quiescence service.

The fixed 512-entry `DescFunc` sorting tail is replaced with bounded 4,095-entry
storage before the row cap moves from `0x1FF` to `0xFFF`. G10 persistence and
the G9/G2/G4/G1/G3 codec plan then commit before readiness is published. A false
patch result may follow a real write, so every post-mutation ambiguity poisons
readiness and terminates the process; hot rollback is forbidden. The shipped
DLL is active by presence and exposes no configuration file. Do not create,
load, save or transmit ISC12 data without backups.

The current source also includes the G10-B persistence boundary. ISC12 keeps the
standard external D2S/D2I containers: target reads validate the complete native
container without replacing its buffer, and target writes validate the complete
buffer before delegating the final write to D2RCore. Retaining the native
`WriteD2sFileWithEnvironment` / `CloseD2sFileWithEnvironment` pair lets
D2RLoader compose its `.d2rl` environment sidecar and backups normally. The
earlier 96-byte outer-envelope prototype is retired from the runtime DLL after
its first live write produced a structurally valid wrapped D2S that D2RLoader's
frontend nevertheless rejected as `invalid-character`. Its envelope and atomic
file primitives remain unit-tested only as possible building blocks for the
future external migration tool.

> **Save warning:** ISC12 changes the serialized stat-ID width. Do not load an
> existing vanilla or non-ISC12 save directly. Create a new character under
> ISC12 or migrate the save with a compatible external tool when one becomes
> available. Back up every character and stash before installing, removing,
> updating or migrating ISC12. Forcing an incompatible save, or removing ISC12
> from a save that depends on it, is unsupported.

G0 no longer treats the first compiled ItemStatCost table as globally
authoritative. D2R compiles both the 368-row Classic/base table and the 400-row
ISC12Lab expansion table in one load. ISC12 stages each exact compiled snapshot
with `SchemaReady=false`; D2RLoader's existing `DataTablesLoaded` event and
`DataTableServiceV1` RotW TableView then select the matching records pointer and
row count. Finalization decodes the active bytes again, so in-place loader
post-processing or pointer reuse cannot silently publish the earlier capture.
Only that authoritative snapshot is published. A later non-zero revision token
distinct from the previous load must match its semantic hash or the process
stops fail-closed. Listener shutdown is an atomic `Stopping` state; a callback
racing explicit unregister returns benignly while unregister waits for every
already-running callback.

G1–G4 remain one canonical four-group subplan, ordered G2, G4,
G1, G3 so overflow-status publication remains final. A critical static audit
invalidated G1's earlier narrow writer patch: the native serializer owner has a
511-DWORD compound-suppression table, and an ID at or above 511 would index
past that table into adjacent frame storage before G9 could stage the packet.
The source planner now rewrites the exact 42-byte body
`[0x37F17C,0x37F1A6)` in place. IDs `0..510` preserve the original table
comparison and compound suppression; IDs `511..4094` bypass the unsafe lookup;
the writer emits `min(ID, 0xFFF)` at 12 bits and retains the live CALL at
`0x37F1A1`. That CALL may target the canonical native bit writer directly or
the exactly attested D2RCore `WriteItemSaveStatId` provider; ISC12 never
rewrites the provider. The schema continues to reserve `0xFFF` as the
terminator.
The subsequent-reader window likewise must resolve its interior CALL exactly to
the governed native `BITSTREAM_ReadBitsThunk`; arbitrary retargeting fails
before any write. Fourteen new exact witnesses cover the owner frame, its sole
caller, the 511-DWORD clear, adjacent snapshot layout and all eight low-ID
compound writes.

D2RLoader 1.2 also composes the governed compiler, G3 stat writer and dynamic
player-save caller through D2RCore. ISC12 accepts either each canonical direct
native target or the corresponding exactly attested provider. Admission binds
the export/body, live PDATA and unwind contract, a bounded unconditional relay
chain, the live forward slot and the exact native destination. The dynamic
caller is accepted only as an indivisible pair: vanilla `0x8000` capacity plus
direct serializer, or D2RLoader `0xFFFF` capacity plus
`WritePlayerSaveWithEnvironmentCapture`. ISC12 preserves the pair and all
provider CALLs. The save-stat providers still forward IDs above 511 unchanged;
only D2RLoader's private 512-bit compatibility census omits those IDs, which is
a separate metadata/network-hardening gap.

The complete prepared source plan now contains 24 mutable sites, 102
differing-byte mutations and 77 witnesses. G1–G4 retain 20 sites and 84 slots;
G9 adds four transport sites and brings its governed surface to twelve
witnesses. Two Release `/W4 /WX` builds, CTest `5/5`, unique signatures and the
expanded `211/15` native ledger pass. The first byte-identical
standard-container candidate was 435,712 bytes with SHA-256
`E7167627F3577C4F2A7222BBD81D3D2343874A4BBFF94DE008AA86F6ACC4F568`;
the later G9-invariant candidate supersedes it.
Its full-stack mod-local cold start reaches `D2R startup complete` with
`SchemaReady=true`. A disposable Amazon then completed native creation,
frontend preview, Save & Exit to a 1,297-byte D2S plus 6,261-byte `.d2rl`, and
gameplay reload with standard-container `error=0/0`. This proved the first
end-to-end runtime vertical. The later final qualification closes IDs 512/4094,
G9 stress, the controlled D2I and global scope; multiplayer remains open. The previous cold-started
outer-envelope candidate was 445,952 bytes with SHA-256
`EFCA4EBAECDC7E0EF7BE70D2BE741FD7D73DED0ACA85873507CCA2D2B625F3DB`.

Twelve exact native witnesses govern G9: the 0x9C/0x9D producers serialize one
node with recursion disabled, a root 0x9C has exactly 244 native payload bytes,
and every 0x9D node has exactly 239. Socketed descendants are emitted as
separate 0x9D packets. The added exact and unique epilogue witnesses are
`[0x479E23,0x479E41)` and `[0x47A019,0x47A037)`; they cover the cookie check,
stack deallocation, nonvolatile pops and `RET`. Vincent selected autonomous native-only G9-A on
30 August 2026. The pure snapshot planner now validates every node before its
first callback: exact payload caps, record/sentinel counts, packed child lists,
occupied-child count versus socket capacity, indices, shared children, cycles
and unreachable nodes. Accepted visits use the native depth-first preorder;
every rejection invokes zero callbacks. Partially filled socketed items remain
valid because occupied children may be fewer than the socket capacity. Native
D2R evidence bounds the occupied children of any one item to seven, but it does
not establish a global bound on the total nodes or depth of a recursively
socketed tree.

The same static audit bounds each node to at most seven emitted stat lists
(base, five set slots and runeword) and each list snapshot to 511 records. Six
low-ID compound primaries suppress eight partner records after emitting one ID
token with multiple values. The resulting 3,577-ID-token plus seven-sentinel
ceiling is structural only, not a realizable item or worst-case byte guarantee.
The prepared G9 path therefore validates the bytes copied from the one real
native serialization rather than authorizing a flush from record estimates.

Scratch serialization at producer entry remains rejected because both producers
temporarily alter item state before their real serialization. G9-A is the first
canonical patch group. Its startup commit order is
queue 0x9C, queue 0x9D, entry 0x9C, entry 0x9D, so both native queue calls are
redirected before either producer entry can begin staging. The
previous long witnesses are split around the mutable CALLs at `0x479E10` and
`0x47A001`.

One allocation-free thread-local transaction copies the real packet bytes into
fixed storage capped at 64 packets and `0x4000` total bytes, with depth 16 and
seven immediate children per node. Only after the root returns does it validate
the complete captured batch, packet headers and lengths, preorder, parents,
cycles and all four caps. Rejection discards the batch before any real queue;
acceptance replays it through the unchanged native queue at `0x4817F0`.

The producer entries invoke registered 10-byte trampolines that preserve the
overwritten prologues and continue in the original functions. Their live unwind
metadata is registered with `RtlAddFunctionTable` before any publication can be
attempted; failure is fail-closed. SEH `finally` paths abort and discard the
transaction on abnormal producer exit. The source loader also requires
`RtlLookupFunctionEntry` to return matching live unwind metadata at both the
producer body and its `RET`: identical Begin/End/UnwindData, an End within the
image and a range covering the governed epilogue. The statically rehydrated
`.pdata` is protected and non-authoritative. The isolated cold start attested
the live PDATA/XDATA contract and published all four G9 sites. The final
qualification executes real 0x9C/0x9D roots, descendants and bounded failure
cases.

The specialized network groups are now part of the same fail-closed publication
transaction. G5 widens packet `0x3E`; G6 widens `0xA8`; G7 widens only the inner
ItemStatCost fields and estimator of `0xAA`, leaving outer State IDs unchanged;
and G8 widens `0xAC`. Every producer, consumer, sentinel, estimator and admission
window has an exact unique signature. Packet `0xAC` admits at most 16 copied stat
entries: its governed 12-bit worst case is 1,289 bits = 162 bytes, or 175 bytes
with the 13-byte header, leaving 69 bytes below the native 244-byte guard.

The remaining fixed-width packet census requires no expansion. Families
`0x1D..0x1F`, `0x9E..0xA2` and packet `0x20` already store and read stat IDs as
native WORD fields; the mercenary/private dispatcher also zero-extends its
queued stat field from WORD before dispatch. These verification-only sites are
exact, unique and unchanged. Complete multiplayer coverage still requires a
real matching host/joiner and mismatch-rejection matrix.

For provenance only, the earlier Extended Item Transport prototype belongs to
RuffnecKk ExtendedItemStats and was exercised in an experimental RuffDood fork
build. It was never part of the pinned upstream eezstreet PluginPack and is not
an ISC12 product dependency or gate.

The separate G0-BBE gate fingerprints ten proof-only native sites. Seven
compiler call sites converge through one shared ItemStatCost resolver: it keeps
the ordinal in a dword, the generic compiler encodes every valid ISC12 ID
`512..4094` as token 5 plus a 16-bit operand, the evaluator restores it, and the
stat callback forwards the full dword to native stat accessors. A governed
read-only capture of D2R `3.3.93847` closes the two memberships hidden by the
protected static RDATA: `0x1CFA68C` contains `stat\0`, and record 5 of the nine
16-byte callback-info records at `0x1D09C50` points to `0x3B33F0` with arity 2.
Both code anchors matched exactly, no mutation right was requested and no write
was attempted. The ASLR-normalized table SHA-256 is
`E8B9B76F9D7320BDFA8129F8D22B0CB19B450AC4D655F53A8D2E56E47CF224ED`.
D2MOO remains semantic corroboration only. The official eezstreet
`items.playerConditionCalc` feature is only a coexistence surface and does not
own or close this proof.

The clean-sheet schema rejects `CsvBits > 32` and `CsvParamBits > 16`. The G2
and G3 exhaustive callers are prepared to enter process-lifetime RX relays,
then no-throw FRAME wrappers that accept only v105, validate marker, IDs,
widths, truncation, the 512-entry cap and terminator, and invoke the untouched
native readers under the immutable schema snapshot. Rejection returns the
native malformed status `0x12`; a successful native cursor that differs from
the preflight used-end fast-fails. G4 leaves its legacy branch A in 9-bit form,
changes only the v105 branch B, and prepares the exact copy CALL so the original
SaveObject copy runs once before whole-D2S validation. Invalid magic, version,
size, checksum, data context, marker, ID, payload bound or sentinel returns zero
through the native buffer-free/false exit.

The G3 finalize leaf preserves the native used-end result in RAX, returns the
sticky overrun flag in EDX, and is prepared without using D2R padding or
unwind-owned bytes. Its CALL is flushed before `mov eax, edx` is published as
the final site. A borrowed, validate-only `NativePublicationLeaseView` now gates
every full-set fingerprint, write and flush. Its production instance is
constructed only by the initial-load window, owns no loader resource and has no
release operation. The DLL
still builds against the governed PluginSDK v3 commit
`4933e2c42cb2592958cd0df3b6dc5003102252d1`; a separate audit of PluginSDK v4
`6eb8f8b6192868214706bd6d528c5294f2f551b7` finds services through 17
(`Http = 16`, `ItemInteraction = 17`) but no native-publication service.

The one-shot coordinator is connected to real local G0, G10 and codec adapters
and has exactly one production caller in `D2RLoaderLoadPlugin`. Their immutable
plans are all preflighted under the same borrowed view before reservation or
write. The coordinator then reserves process lifetime once, commits G0, G10
and codec, and keeps codec order G9/G5/G6/G7/G8/G2/G4/G1/G3. It then stops in
`CommittedPendingReadiness` with all private readiness flags false. While the
same initial-load window remains active, a separate no-write step publishes
readiness exactly once and a postcondition check requires every G0/G10/codec
surface to be active. Tests cover every preflight domain, native
patch/write/flush failure, revocation, reentry, mutate-then-uncertain outcomes
and both startup-readiness paths.
An already-equal byte is a confirmed no-op; an actual attempted write makes any
ambiguity terminal. The optional upstream `NativePublication V1` proposal is
retained as future hardening rather than a prerequisite for this experiment.
An isolated full-stack cold start executed the previous outer-envelope DLL on D2R
3.3.93847 with D2RLoader 1.2, the active RuffnecKk Suite and all five eezstreet
plugins. It accepted every D2RCore provider and live unwind contract, published
G0/G10/G9/G1-G4, compiled 190 TXT tables, selected the RotW ItemStatCost table
at revision 1 (`rows=400`, `G0-builds=2`, `SchemaReady=true`) and reached
`D2R startup complete`. D2RLoader reported 36 plugins loaded, two known
unrelated mod-local failures (Stash Search and Revive Overhaul) and 17 patches.
This closes mod-local publication and cold-start for that native surface. Its
first write later proved the outer envelope incompatible with the loader
frontend. The corrected standard-container candidate then closed native
create/save/reload and the first gameplay cycle.

The final internal candidate adds the exact native socket-walker invariant
`childTemporaryFlags = parentTemporaryFlags | 0x08` and a startup G9 runtime
self-test. Two reproducible Release builds and the deployed DLL are identical:
446,464 bytes, SHA-256
`1311F1C4BE44B0918F34C32007C3A19D35D240D8B72DCAD8C1853EEE53EC11B5`,
with CTest `5/5`. Disposable D2S fixtures serialize stat IDs 512 and 4094 with
values 12 and 94 and preserve both values through two cold save/reload cycles.
Real 0x9C and 0x9D roots and a socketed three-node Gothic Plate tree complete
with `captured=3`, `queued=3`, `staging-error=0`, and `flush-error=0`. The
runtime self-test proves an accepted 3/3 tree, node overflow with zero queue
callbacks, and flush reentry contained after one callback in terminal state.

Version 0.2.1 also recognizes the exact six-hook full-item transport installed
by `ExtendedItemStats.dll` 0.3.14. Admission requires every surface to be a
tracked inline hook with `extended-item-stats` as its sole owner, plus the exact
installed file version. When admitted, ISC12 retains its 12-bit stat codecs but
delegates G9 full-item packet transport to that provider. Native transport
remains the independent fallback when all six surfaces are untouched. Any
partial set, additional owner, different plugin ID, missing binary or other
version is rejected before publication. No generic compatibility with other
ItemStatCost serialization plugins is claimed.

Two independent Release `/W4 /WX` builds of 0.2.1 are byte-identical at
329,728 bytes, SHA-256
`C6B4E610F34E4AF42553E606EF835C9E7619916B5BC3ED1209490B9894B55395`;
both CTest runs pass `5/5`. That exact DLL completed the mod-local Yupgoolg
cold start with ExtendedItemStats 0.3.14, then loaded, saved and reloaded a
BKVince-to-Yupgoolg converted character in the Rogue Encampment with
standard-container `error=0/0`. The inverse load order is also qualified:
ISC12 loaded first, kept G9 unresolved, and the first live 0x9C/0x9D producer
sealed transport to the six-hook ExtendedItemStats 0.3.14 provider. A separate
global-scope cold start admitted the same provider during startup.

The governed non-empty BKVince shared stash converted from 680 to 695 bytes
with nine stackable material items on page 6. Yupgoolg accepted it, expanded
the outer stash layout to 101 pages and 7,299 bytes, then read the result after
a full process restart and again with ISC12 installed globally. All three
runtime captures are byte-identical at SHA-256
`ED0C4B77AD8C83A56C30080652FAF8C161B6754150A45668CFE316871238BB0C`
and target-schema parsing still finds the same nine items on page 6. The
original Yupgoolg stash was restored byte-for-byte after the test.

A controlled standard shared-stash D2I grew from 68,216 to 68,307 bytes after
the socketed armor was moved into it. Its SHA-256
`7375F2F7CB2DAC3178853397D5A7FCE6782F5B26220A0980995261C76E96507F`
remained identical across a full process restart; the armor reappeared and its
three-node tree was captured again. The same DLL/config pair also passed a
complete-stack global cold start, D2S/D2I reads, gameplay and real G9 tree,
then a final mod-local cold start. Both scopes kept 36 plugins loaded, the five
eezstreet plugins and 17 memory patches; the two known unrelated failures
remained unchanged. The queue ABI is `void`, so validation guarantees zero
calls before flush but cannot roll back a queue failure after a successful
flush has begun.

The gameplay and TCP/IP-qualified G5-G8 candidate is 451,072 bytes, SHA-256
`6089619DE3B01FD474669096A8AEC8A470559FAD993DCB939AC976709A7D2D52`.
CTest passes `5/5`; all 27 new mutation/capacity windows match exactly once in
the governed native corpus; and the ledger is `VALID` at 228 sites / 15 required
groups. Full-stack mod-local and global cold starts on D2R 3.3.93847 and
D2RLoader 1.2 published 43 codec sites and 129 mutations, loaded 36 plugins
including all five eezstreet DLLs, applied 17 patches, compiled 190 TXT tables,
selected the authoritative 400-row schema and reached `D2R startup complete`.
Only the two known unrelated Stash Search and Revive Overhaul failures remained.

The current public-metadata build changes only the PluginInfo and Windows
resource description in source to `Extends ItemStatCost.txt capacity to 4,095
rows. Requires ISC12-compatible save files.` Two byte-identical Release `/W4
/WX` builds remain 451,072 bytes and have SHA-256
`AFB4B2D1F779A368C3139BB5AF9EDC59CFD4B83042C88AD2EE7991C9E62DFF00`.
CTest passes `5/5`, and the exact DLL completes the full-stack mod-local and
global cold starts with 36 plugins, all five eezstreet DLLs and 17 patches. It
is restored mod-local with no global duplicate and no running game process.
Gameplay and TCP/IP were not rerun for this description-only source change;
those behavioral proofs remain attached to the preceding candidate.

## Planned release installation contract

After the config-free candidate is fully qualified, install the DLL in exactly
one D2RLoader scope.

Global:

```text
<D2R>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

Mod-local:

```text
<D2R>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

The clean-sheet contract is a product and support boundary, not an outer-file
marker: create new characters under ISC12 or migrate existing D2R 9-bit saves
with D2R Save Converter. Direct loading of existing vanilla/non-ISC12 saves
is unsupported. D2RLoader's native `.d2rl` environment record provides the visible
plugin/mod compatibility warning; it is not a cryptographic schema marker and
ISC12 does not promise to hard-block every misuse. Backups remain mandatory.

The config-free 1.0.0 plugin is distributed with D2R Save Converter 1.0.0 as a
separate companion tool. Rebuild and validate both final artifacts before
publishing them.

## Credits

- Author: `RuffnecKk`.
- D2MOO is credited for semantic knowledge of the historical ItemStatCost
  compiler and stat-list format. All addresses, signatures and x64 ABI are
  independently proven against the governed current D2R corpus.
- D2RLoader and PluginSDK provide the autonomous plugin runtime.
