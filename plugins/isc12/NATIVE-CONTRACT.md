# ISC12 native contract

ISC12 never selects compatibility from a D2R build name or version. Those
values are diagnostic only. Before every native mutation, the DLL must validate the
complete governed fingerprint, native layout witnesses and exclusive ownership
of every surface.

Duplicate global/mod-local loads are excluded by a Windows mutex whose name is
suffixed with the current process ID. It permits separate host and joiner D2R
processes in the same Windows session while still allowing only one ISC12 owner
inside each process.

## D2RCore provider composition

D2RLoader 1.1/1.2 may redirect the governed compiler and save callsites through
D2RCore. ISC12 accepts either the canonical direct native target or an exactly
attested provider. Provider admission validates the export/body, live
PDATA/unwind tuple, a bounded loop-free chain of unconditional `E9`/`FF25`
relays, the live forward slot and the exact native destination. Exact canonical
pattern masks remain unchanged, and ISC12 never rewrites a provider CALL.

The save-stat providers `WriteItemSaveStatId` and `WritePlayerSaveStatId`
preserve `RCX`, `EDX` and `R8D` and call `D2R+0xA1B710` exactly once. Their
private TLS census records only IDs below `0x200`; IDs `512..4094` still reach
the native writer unchanged with ISC12's 12-bit width. The missing census rows
are a D2RLoader metadata/handshake hardening gap, not local ID truncation and
not a reason to mutate D2RCore's private bitmap.

The dynamic player-save caller is admitted only as one of two indivisible
contracts: canonical `R13D=0x8000` with a direct call to `D2R+0x52F090`, or
D2RLoader `R13D=0xFFFF` with a relay to
`D2RCore!WritePlayerSaveWithEnvironmentCapture`. The 1.2 provider is exact at
`[0x634650,0x636068)`, has unwind RVA `0x50EFD0`, body SHA-256
`A4A0E2A5E70AEFB613016739E914225CEE2A20BB06F13197CEAC182E11648667`
and forwards through `D2RCore+0x5372C0`. The 1.1 provider is exact at
`[0x563D80,0x565103)`, has unwind RVA `0x452480`, body SHA-256
`66C61BC1678375C9E373FD2141F409244B450D1411906B7FF8C27C1241E69F6A`
and forwards through `D2RCore+0x480DE8`. Both live slots must resolve exactly
to `D2R+0x52F090`; ISC12 preserves the larger D2RLoader buffer and provider.

Version 0.2.0 fingerprints the complete loader/count/DescFunc seam, signed
priority comparator, native qsort, native vector layout, resizer, register
restores and epilogue contract. Its startup transaction installs a
`PatchJmpRel32` over the exact eight-byte seam at `0x31F0AB`, activates the
guarded replacement, and only then writes the aligned count immediate at
`0x31ED38` from `0x1FF` to `0xFFF` while the initial-load publication window
remains active.

The eight-byte seam is not naturally aligned, and the PluginSDK contract does
not prove that `PatchJmpRel32` suspends competing threads or publishes the write
atomically. ISC12 therefore confines all writes to the synchronous initial
`D2RLoaderLoadPlugin` callback and terminates the process on every post-write
ambiguity. Runtime qualification must still test this startup assumption.

The same borrowed, validate-only `NativePublicationLeaseView` gates G0 before
resource reservation, before the first native write and after every attempted
write. Its production instance is created only by a same-thread initial-load
window and becomes invalid before the callback returns. It owns or releases no
loader resource and is not a general runtime-quiescence token.

Before calling `PatchJmpRel32`, ISC12 irrevocably reserves the RX relay and RW
state for the process lifetime. A false return is an uncertain mutation result,
not a pre-mutation failure: ISC12 reads and logs the eight observed seam bytes,
keeps both pages resident and immediately terminates fail-closed with a cold
restart requirement. It never unloads a possible jump target.

The replacement uses a fixed 4,095-entry scratch array, the native qsort and
signed comparator, and the native vector whose ABI is `{data*, size,
capacity|flag}`. It never interprets that vector as three pointers. Failures
before native-vector mutation may return to vanilla only when the proven table
count is at most 511; extended counts and every post-resize failure terminate
fail-closed.

The entry relay is copied to an RX process-lifetime page and references a
separate RW state page. Both success and vanilla exits return through this
persistent relay, which decrements the callback count only after all DLL code
has finished. After the tail is confirmed installed, the RW state
conservatively treats the count cap as potentially modified even if the cap API
reports failure. An inactive relay accepts only a proven vanilla row count;
otherwise it issues a non-resumable Windows fast-fail. Rundown has a five-second
bound and also fast-fails rather than permitting the DLL to unmap with an active
callback. This closes the DLL-unload race. Hot rollback remains unsupported.

The full-item decoder and serializer entries are intentionally absent from the
exclusive foundation fingerprint. ISC12 fingerprints and will later mutate
only the proven internal ID-width sites; it neither needs nor claims either
native entry.

G0-BBE is a separate static proof of the native `stat()` expression path, from
the ItemStatCost linker through bytecode and evaluation. The official eezstreet
`items.playerConditionCalc` feature remains an external coexistence surface; it
does not own, implement or close the ISC12 ordinal-width proof.

The governed native census finds seven compiler call sites, three resolver
front ends converging on one shared core, and seven evaluator call sites. The
ItemStatCost branch at `0x3B5D80` stores the linker result as a full dword. The
generic compiler emits callback token 5 plus a 16-bit operand for every ISC12
ID `512..4094`; the decoder restores that operand into EDX, and the attested
stat callback at `0x3B33F0` preserves the dword through its row-count guard and
three stat-accessor tails. Ten proof-only ledger sites fingerprint this path,
and the ledger validator forbids any G0-BBE target other than `unchanged`.

The native membership proof is closed by a governed read-only capture of D2R
`3.3.93847`. Both exact `.text` anchors match. Protected RDATA at `0x1CFA68C`
contains the NUL-terminated keyword `stat`; its captured 16-byte SHA-256 is
`83626D991B1A0AD789DDB50D993623989AB1C1257879530005CEE0CA411F0E4F`.
The protected table at `0x1D09C50` is nine 16-byte records, not nine QWORDs:
`{callback VA:qword, arity:dword, padding:dword}`. Record 5 at `0x1D09CA0`
contains callback RVA `0x3B33F0`, arity 2 and zero padding. Its complete
ASLR-normalized SHA-256 is
`E8B9B76F9D7320BDFA8129F8D22B0CB19B450AC4D655F53A8D2E56E47CF224ED`.
The capture requested query/read rights only and attempted no mutation. D2MOO
remains semantic corroboration and no BBE byte is ever patched. This closure
does not grant publication authority or constitute a save/network runtime test.

The production-callable startup transaction contains these current domains:

1. G0 loader count plus bounded DescFunc replacement tail;
2. G10 persistence reader and writer;
3. G9 full-item transport: queue `0x9C`, queue `0x9D`, producer entry `0x9C`,
   then producer entry `0x9D`;
4. G5 packet `0x3E`, G6 packet `0xA8`, G7 inner packet `0xAA` stat lists and
   estimator, then G8 packet `0xAC`;
5. G1 generic item reader, writer and sentinels;
6. G2–G4 player/save reader-writer pairs and preview readers.

The specialized network groups are in the compiled canonical plan. Quantity
and outer State-ID fields remain intentional 9-bit exclusions. The exhaustive
fixed-width census proves packet families `0x1D..0x1F`, `0x9E..0xA2` and `0x20`
already use uint16 stat-ID fields on both sides; no byte-to-word layout change is
needed. Packet `0xAC` is bounded to 16 copied entries and a worst-case 175-byte
packet, leaving 69 bytes below its unchanged 0xF4 guard.
ISC12 claims no decoder, serializer, dispatcher or native queue-entry mutation:
`0x4817F0` remains unchanged as a governed witness and the replay target.

The earlier narrow G1 writer patch is invalid. The serializer owner allocates a
511-DWORD compound-suppression table and the vanilla lookup beginning at
`0x37F17C` indexes it with the current stat ID; IDs at or above 511 would read
adjacent frame storage before any G9 transport guard could run. The approved
replacement must own exactly `[0x37F17C,0x37F1A6)` as one 42-byte in-place body
and supersedes the old `0x37F186` writer site. Its exact 50-byte fingerprint is
`[0x37F174,0x37F1A6)`: the unchanged nonzero guard at `0x37F174` dominates the
body and the live final CALL remains inside the fingerprint. IDs `0..510`
retain the table comparison and
compound suppression, while IDs at or above 511 bypass that lookup. The body
then emits `min(ID,0xFFF)` at width 12 and preserves either the direct native
CALL or the exactly attested `WriteItemSaveStatId` provider at `0x37F1A1`; the
schema rejects a real ID `0xFFF` because that value remains the terminator. No
other owner or relay is permitted for this body. The source planner contains
this replacement. At that intermediate bounded-G1 gate,
Release `/W4 /WX`, CTest `4/4` and all exact signatures passed statically. No
native write is authorized.

The remaining G1 reader/terminator changes include preservation of the
first-reader `previousStatId = -1` invariant. G1 still claims neither decoder
nor serializer entry and is part of the canonical startup transaction. Its subsequent-reader interior
CALL must retain the exact governed target `BITSTREAM_ReadBitsThunk 0xA1B6C0`;
an arbitrary retarget is not compatible.

## G10 persistence boundary

The schema snapshot is a two-phase lifecycle contract. Every G0 compiler call
copies the exact record array and linker-owned `Stat` names into one of three
bounded candidates while holding `SchemaReady=false`; it does not publish the
first compile opportunistically. The required PluginSDK
`LifecycleServiceV1::registerDataTablesLoadedListener` callback then runs on
the game thread after table loading and asks `DataTableServiceV1` for
`Bank::Rotw / TableId::ItemStatCost`. Publication requires an exact records
pointer, row count and `0x144` row size match plus a non-zero revision distinct
from the previously observed load token; PluginSDK does not promise numeric
monotonicity. Finalization decodes the authoritative bytes again instead of
trusting that post-processing left the staged allocation unchanged. This
distinguished the 368-row Classic/base compile from the 400-row
ISC12Lab RotW compile at revision 1. Later revisions may reuse only the same
semantic hash; mismatch is terminal. These existing lifecycle/data services
identify the authoritative table and are not a native-patch quiescence service.
The listener handle and lifecycle state are atomic. `Stopping` is a benign
callback outcome during explicit unregister, whose SDK contract waits for an
already-running callback before unload may clear the service pointers.

G10-B P3b supplies exact reader `0x9FC654` and writer `0x9F95A2` mid-hooks to
the canonical startup transaction. Both target objects are classified by their exact terminal
name. A target reader must report the current native success value and an
announced length equal to the actual DWORD byte count before ISC12 snapshots
the buffer. It then validates the complete standard D2S/D2I container without
replacing native storage.
Every target rejection clears the native buffer and state before reusing the
governed close/unlock/status-6 continuation; uncertain cleanup fast-fails.

The target writer converts the already-canonical native UTF-8 path exactly and
validates the complete standard D2S/D2I buffer. Success returns to the native
continuation before `CREATE_ALWAYS`; D2RCore therefore retains ownership of the
physical write and of the paired environment close. This preserves D2RLoader's
normal `.d2rl` sidecar, backup and environment-capture composition. Native
persistence remains non-atomic; Vincent accepts that product tradeoff in favor
of loader integration and mandatory user backups.

The physical reader runs during frontend enumeration and may precede
`DataTablesLoaded`; it therefore performs only complete, schema-independent
standard-container validation and does not require `SchemaReady`. Semantic
player-stat validation remains in G2–G4 after authoritative schema publication.
For targeted writes, a non-blocking shared `PublishedSchemaReadLease` spans the
native buffer snapshot and validation result. A concurrent reload that already
owns the exclusive schema lock therefore causes immediate rejection instead of
a wait. The successful disposable save/reload proves this path for one native
generation, but does not yet establish a general provenance invariant for every
pre-existing `state-1` buffer.

The 96-byte outer envelope and sibling-temp atomic writer remain disconnected,
unit-tested migration-tool building blocks. They are intentionally absent from
the runtime DLL: the first live envelope write was structurally correct, but
D2RLoader 1.2 rejected the wrapped file in its frontend as `invalid-character`
before the game could load it.

The persistence relay is process-lifetime RX with separate RW state and bounded
rundown. Its reader/writer seams are part of the startup transaction. The
qualified mod-local cold start published both seams with the canonical set and
reached complete startup. The retired envelope writer executed once. The
standard-container path then accepted a native 403-byte header-only D2S,
delegated its 1,297-byte full save and D2RLoader's 6,261-byte `.d2rl` sidecar,
and reloaded the character into gameplay with `error=0/0` diagnostics.
The PE unwind records
describe only each MASM
stub's local `0x20`/`0x30` allocation. Because entry is a tail-jump from the
middle of a native frame and copied relays are not registered/chained, ISC12
does not claim generic recoverable unwind across these boundaries. Callbacks are
`noexcept`, contain the expected access/in-page/guard failures and fast-fail on
every unexpected exception. Any future recoverable cross-boundary unwind would
require a separately governed registered/chained unwind design.

## Canonical G9 plus G1–G4 startup transaction

The canonical codec transaction contains five groups, 24 mutable
sites, 102 differing-byte mutations and 77 witnesses. G1–G4 retain their 20-site,
84-slot subplan: G2–G4 contribute 16 windows, 40 slots and 51 witnesses, while
bounded G1 contributes four mutable sites, 44 slots and 14 exact
owner/caller/table/snapshot/compound witnesses. G9 contributes four mutable
transport sites, 18 slots and twelve exact witnesses. Replacement and full-set
tests pass in Release. Within the initial-load startup commit, G9 is first and is
internally ordered queue `0x9C`, queue `0x9D`, entry `0x9C`, entry `0x9D`;
the remaining order is G2, G4, G1, G3 so G3 overflow-status publication is
strictly final. G1
decoder/serializer entry fingerprints remain static identity and ownership
evidence rather than runtime witnesses, because the transaction mutates neither
entry.

G2 auxiliary player stats, G3 regular modern player stats and G4 frontend
preview are represented as three atomic groups. Sixteen exact
unique mutation-site patterns plus 51 unchanged owner, native-return, buffer,
cardinality, field-layout, cleanup and overrun-path witnesses prove five G2
sites, eight G3 sites and three v105 G4 sites. G4 branch A remains the unchanged
legacy 9-bit witness; only branch B is eligible for v105. The two other preview
bit-reader calls are driven by compiled-record value widths and remain
untouched.

The G2–G4 subplan governs 40 byte slots: four rel32 CALL groups for G2, both G3 callers
and G4 copy preflight; ID width immediates `9→12`; sentinel immediates
`0x1FF→0xFFF`; four dynamic rel32 bytes for the G3 finalize relay; and two final
status bytes. Every target is opaque outside the loader authority and is bound
to an entry in the copied process-lifetime RX template. Dynamic bytes already
equal to their expected value are confirmed as no-op slots rather than passed
to the writer, while each complete instruction range is still flushed. A target
that resolves back to the original native CALL target is rejected. The plan
validates every mutation and witness signature before the first write and
refuses publication without a live borrowed `NativePublicationLeaseView`. The
view is revalidated before every fingerprint and write and before and after
every instruction-cache flush. It has no ownership or release operation and
exists only inside the same-thread initial `D2RLoaderLoadPlugin` window.
Because the patch API cannot prove that a false result left a byte unchanged,
any attempted failure is a partial/uncertain commit requiring a process stop;
there is no speculative hot rollback.

The SDK audit is conclusive for API v4 commit
`6eb8f8b6192868214706bd6d528c5294f2f551b7`: public service IDs stop at 17,
with `Http = 16` and `ItemInteraction = 17`. Lifecycle and ThreadService define
callback timing, and the appended existing-item transaction governs item state;
none documents a global executable-publication barrier. The former provisional
`NativePublication = 16` value is retired. The ISC12 DLL itself remains compiled
against governed PluginSDK v3 commit
`4933e2c42cb2592958cd0df3b6dc5003102252d1`.

This absence no longer blocks the experimental path. ISC12 uses the official
startup patching model directly inside the initial `D2RLoaderLoadPlugin` call.
The optional upstream `NativePublication V1` proposal remains useful future
hardening if D2RLoader later wants to document cross-publisher serialization,
owner/thread/epoch binding and a reusable poisoned-transaction contract.

The one-shot coordinator and the real local G0/G10/codec adapters have exactly
one production caller. Each adapter performs a read-only preflight and owns the
resulting immutable plan; all three preflights complete before the coordinator
reserves every relay/state allocation once for process lifetime or reaches a
native write. Commit order is G0, G10, codec; the codec plan retains internal
order G9, G2, G4, G1, G3. The G0 guard only publishes `capMayBeExtended`; G10
patches reader then writer. Successful native commit stops at
`CommittedPendingReadiness`, with every private readiness flag still false.
While the same initial-load window remains valid, the caller publishes private
readiness exactly once without another native write, then verifies all G0/G10,
codec and transport postconditions before marking the plugin operational.

All stage callbacks receive the same borrowed view. Absent/revoked authority,
every preflight rejection, missing callbacks, reentry, mutate-then-uncertain
results, write/flush failures, terminal-state monotonicity and both startup
readiness outcomes are executable unit tests. A byte already equal to its
replacement does not set `mutationAttempted`; once any actual write callback is
attempted, every ambiguous result is terminal `Poisoned`, clears readiness and
terminates the process without rollback.

The adapter set is bound during loader preparation and exposes coordinator
callbacks only while preparation is complete and no mutation has occurred. The
legacy G0-only installer is excluded from production builds, so the full-set
coordinator is the sole production publication path.

## G9-A native-only bounded staging transaction

The 0x9C and 0x9D producers call the shared serializer with recursion disabled.
A root is therefore one 0x9C or 0x9D packet; the native walker later sends each
socketed descendant as a separate 0x9D. Twelve exact witnesses govern both
recursion-zero call setups split around the mutable queue CALLs, capacities,
header arithmetic, serializer zero-on-overflow, consumer buffer/header
assumptions, the descendant walker, the native queue entry at `0x4817F0` and
its span dispatch. The added exact unique ranges `[0x479E23,0x479E41)` and
`[0x47A019,0x47A037)` cover each producer's cookie check, stack deallocation,
nonvolatile pops and `RET`. ISC12 owns neither a decoder nor serializer entry,
dispatcher or native queue entry.

The native-safe payload ceilings are 244 bytes for a root 0x9C and 239 bytes for
every 0x9D node. The pure snapshot planner computes each node from all emitted
ID and sentinel tokens, classifies the root with its packet kind, then every
descendant as 0x9D. A root sent as 0x9D therefore also has a 239-byte cap. It
validates the complete packed tree before the first callback, including
independent occupied-child counts, socket capacity, indices, shared children,
cycles and unreachable nodes. Accepted callbacks follow the native depth-first
preorder. Rejection invokes zero callbacks. These are accounting and planner
facts, not a runtime capability claim or an ISC12 worst-case item guarantee.
The serializer window beginning at `0x37D60B` counts every immediate inventory
child into EBX, clamps every count at or above seven to exactly seven, then
writes that occupied-child count in three bits at `0x37D66F..0x37D678`. This is
independent from `STAT_ITEM_NUMSOCKETS` (`0xC2`) capacity and proves the local
encoding constraint `occupied <= 7`. ISC12 separately requires
`occupied <= capacity` without assuming equality. The walker at
`0x481B50` and recursive 0x9D call at `0x47A014` expose no global node/depth
counter. The staging transaction must therefore impose and prove its own
bounded-storage contract; no global native tree maximum is claimed.

The native stat-list loop exposes at most seven list positions per node: base,
five set slots and one runeword list. Each list snapshots at most 511 eight-byte
records. Six low-ID compound primaries suppress eight partner records after one
ID token carries multiple values. A purely structural ceiling is therefore
3,577 ID tokens and seven sentinels per node, but skipped/null values and
compound suppression mean this is neither a realizable item claim nor a
worst-case byte bound. G9 therefore copies the actual bytes produced by the one
real native serialization; record-count estimates never authorize a queue
flush.

Vincent selected autonomous native-only G9-A on 30 August 2026. A second scratch
serialization at producer entry is forbidden because both producers temporarily
change serialized item state before calling the real serializer. The
source-prepared implementation instead stages the real output in one
allocation-free thread-local transaction with fixed caps of 64 packets,
`0x4000` total bytes, depth 16 and seven immediate children per node. These are
ISC12 safety limits, not an inferred native global tree maximum.

Entry wrappers at `0x479CD0` and `0x479EA0` preserve the native preparation and
recursive traversal. Relays at queue CALLs `0x479E10` and `0x47A001` copy each
stack packet into fixed storage without sending it. The split witnesses now
exclude those mutable five-byte CALLs: 0x9C owns
`[0x479D85,0x479E10)` (139 bytes) plus `[0x479E15,0x479E23)` (14 bytes), while
0x9D owns `[0x479F76,0x47A001)` (139 bytes) plus
`[0x47A006,0x47A019)` (19 bytes).

Only after the root returns does the wrapper validate the complete captured
batch, including packet headers and lengths, preorder, parent links, cycles and
all four fixed caps. Rejection discards the entire transaction before any real
queue call. Acceptance replays the batch through the unchanged native queue at
`0x4817F0`. The qualified startup transaction commits the four mutable sites
first in the fail-closed order queue `0x9C`, queue `0x9D`, entry
`0x9C`, entry `0x9D`, ensuring both queue calls are intercepted before either
producer entry can begin staging.

Each producer entry uses a registered 10-byte trampoline that preserves its
overwritten prologue and resumes the native function. Live unwind metadata is
registered through `RtlAddFunctionTable` before publication can be attempted;
registration failure is fail-closed. SEH `finally` paths abort and discard the
thread-local transaction after an abnormal producer exit. Before publication,
the source loader also calls `RtlLookupFunctionEntry` at both the producer body
and governed `RET`. Both lookups must report identical BeginAddress, EndAddress
and UnwindData, with EndAddress inside the image and the function range covering
the complete epilogue. The rehydrated static `.pdata` is protected and
non-authoritative; only a live lookup can attest this contract. The qualified
cold start accepted the live PDATA/XDATA and published G9 in the previous
445,952-byte outer-envelope candidate
`EFCA4EBAECDC7E0EF7BE70D2BE741FD7D73DED0ACA85873507CCA2D2B625F3DB`.
The standard-container pivot also passes two Release `/W4 /WX` builds, CTest
`5/5`, and the unchanged `VALID` ledger at 211 sites / 15 groups. Its
byte-identical 435,712-byte DLL SHA-256 is
`E7167627F3577C4F2A7222BBD81D3D2343874A4BBFF94DE008AA86F6ACC4F568`.
Its own full-stack mod-local cold start accepts the provider/unwind contracts,
publishes the canonical surface, selects the authoritative 400-row schema and
reaches `D2R startup complete`. Native create/save/reload and the first gameplay
cycle are closed. The final G9-invariant candidate is 446,464 bytes with
SHA-256
`1311F1C4BE44B0918F34C32007C3A19D35D240D8B72DCAD8C1853EEE53EC11B5`;
two reproducible Release builds, the deployed DLL and CTest `5/5` agree.

### Versioned external G9 provider in 0.2.1

ISC12 0.2.1 admits exactly one alternative to the complete native G9 surface:
the six tracked inline hooks owned exclusively by plugin ID
`extended-item-stats`, with the installed `ExtendedItemStats.dll` file version
equal to 0.3.14. The six entry fingerprints cover the two producers, decoder,
metadata builder, serializer and native queue entry. ISC12 keeps ownership of
the 12-bit ItemStatCost codecs and delegates only full-item packet transport.

The provider decision is fail-closed. A partial hook set, multiple owners,
another owner ID, missing versioned binary or any other version refuses
publication. If the native provider was observed before later plugin loading,
the first producer re-inspects and seals the process-lifetime provider before
starting a transaction; it never switches providers during an active packet.

The engineering dependency is explicit and satisfied: the bounded G1
serializer body precedes G9 staging. The production startup caller invokes the
prepared G0/G10/codec commit and performs the readiness step before the initial
callback returns; the qualified cold start attests this path. Live 0x9C/0x9D,
overflow, reentry, bounded backpressure and coexistence now pass. The socket
walker contract is exact: every child 0x9D producer receives
`parentTemporaryFlags | 0x08`, action `0x12`, the active item as parent and
gamble zero. Real 0x9C/0x9D transactions include a three-node socketed tree
with three captures and three queue calls. The startup self-test proves an
accepted 3/3 tree, overflow rejection with zero callback and flush reentry
contained after one callback in `Fatal`. G5-G8 and the fixed-width packet census
are now governed and published; matching host/joiner plus mismatch rejection is
the remaining network-completeness gate.

The G5-G8 publication candidate is reproducible at 451,072 bytes and SHA-256
`6089619DE3B01FD474669096A8AEC8A470559FAD993DCB939AC976709A7D2D52`.
Release `/W4 /WX`, CTest `5/5`, 27/27 exact unique native windows and the
`VALID` 228-site / 15-group ledger pass. On D2R 3.3.93847 / D2RLoader 1.2, the
full stack published 43 mutable codec sites and 129 mutations in both mod-local
and global scopes, loaded 36 plugins including all five eezstreet DLLs, applied
17 patches and reached `D2R startup complete` in each scope; only the two
pre-existing unrelated plugin failures remained. The pair was restored
mod-local without a global duplicate. This attests startup publication, not
two-client packet behavior.

The 0.2.1 provider contract was then exercised in both timing states. With the
mod-local ISC12 DLL ordered before `ExtendedItemStats.dll`, startup left the
provider unresolved; the first live 0x9C/0x9D producer observed the complete
six-hook ownership set and exact 0.3.14 file version, sealed once to
`ExtendedItemStatsV1`, and delegated through the preserved trampolines. A full
restart read the resulting 7,299-byte, nine-item D2I unchanged. With ISC12
installed globally, ExtendedItemStats was already present when the global
plugin initialized; startup admitted it directly and read the same D2S/D2I.
This closes provider timing and hybrid-scope qualification for the exact
0.2.1 binary; it does not broaden the admitted provider identity or version.

For historical provenance only, the six entry hooks previously audited belong
to the RuffnecKk ExtendedItemStats prototype and an experimental RuffDood fork
build. They are absent from the pinned upstream eezstreet baseline and are not
part of the ISC12 product, contract or active gate.

G2 owns a fixed `0x4000` buffer and an unchanged 512-entry cap. ISC12 rejects
the native schema before publication when `CsvBits > 32` or
`CsvParamBits > 16`, matching the native value/parameter representations. The
worst complete G2 section is therefore 3,844 bytes including its marker, well
inside the buffer. G3 instead receives only the space remaining after a
variable prefix. Its two total callers (`0x4000` stack and the dynamic
canonical `0x8000` or attested D2RLoader `0xFFFF` allocation) are exhaustively
fingerprinted. The prepared position-independent leaf
reproduces the native used-end result in RAX, loads the sticky overrun DWORD at
`[bitstream+0x20]` into EDX, and returns without touching RSP or a
nonvolatile register. The redirected CALL is written and flushed first; the
non-overlapping `mov eax, edx` status site is written and flushed strictly last,
so the existing sole caller's nonzero failure edge observes late overflow.
The abandoned bytes at `0x5353F0` are not a code cave: they belong to the next
function's PDATA/unwind range and are never mutated.

The startup publication reserves every copied relay for process lifetime before
the first codec byte and revalidates its same-thread initial-load view across
every G9 and G1–G4 fingerprint, write and cache flush. View loss or any
uncertain write/flush after the first attempted mutation requires an immediate
process stop; hot rollback remains forbidden. The qualified cold start reached
active readiness under this contract.

A no-allocation preflight parses each supplied G2/G3 section in native
LSB-first order: marker `0x6667`, 12-bit IDs, schema-driven param/value widths,
512-entry cap and terminator `0xFFF`. It leaves output unchanged on invalid ID,
unsafe schema, truncation or missing terminator. The exhaustive CALLs at
`0x531A6D`, `0x52EC4A` and `0x530A34` are prepared to target copied RX entries.
Those entries increment rundown, require both `operational` and `codecReady`,
and tail-jump to MASM FRAME wrappers in the DLL. Each wrapper forwards the exact
six-argument ABI to a `noexcept` helper, then exits through a copied RX leaf that
decrements rundown only after all DLL code has finished.

The helpers accept exact inner version 105 only. They copy at most 3,844 bytes
from `[*cursor,end)`, acquire the shared immutable schema snapshot, preflight,
invoke the untouched native owner `0x530A00` or `0x533760`, and require a zero
native status to leave `*cursor` at the predicted byte used-end. Expected
rejection returns the exact native malformed status `0x12`; a native fault or a
successful-cursor mismatch fast-fails. The legacy G2-to-G3 path therefore
rejects before native recursion and never reacquires the snapshot lock.

G4 retargets only preview CALL `0x61CF90`; shared copy owner `0xA1E110` remains
intact for its other callers. The no-throw wrapper calls that owner exactly
once, after which the SaveObject is unlocked and EAX is the completed copy
length. It then validates only `[temp,temp+N)`: D2S magic, exact v105, declared
size, checksum and context byte at `+0xF8` below four. An exact native
header-only length `0x193` succeeds with empty/default preview stats; every
intermediate length through `0x341` is rejected. A full save additionally
requires and validates marker `0x6667` at `+0x341`, each 12-bit ID below the
immutable schema row count, every CsvParamBits/CsvBits payload, the 512-entry
cap and `0xFFF`. Invalid data returns
zero through the existing `cmp eax,8 / jb` edge to the exact buffer-free/false
exit at `0x61D87D`. Native G4 never checks `0x6667` itself and may dereference a
null ItemStatCost lookup, so neither marker nor ID validation is optional.

All four reader/copy relays, the common RX rundown return and their RW handler
slots are published only through the canonical coordinator; `codecReady`,
`itemTransportReady` and persistence `operational` become true only in its
final no-write readiness step. The qualified cold starts reached that state in
both mod-local and global scopes. Standard D2S fixtures containing IDs 512 and
4094 preserve values 12 and 94 through two cold cycles. A controlled D2I
fixture writes a socketed three-node Gothic Plate tree, remains byte-identical
at 68,307 bytes across a full restart and is read back visibly and through G9.
The complete stack remains 36 loaded plugins, all five eezstreet plugins and 17
patches in both scopes. The native queue ABI returns `void`: rejection before
flush is all-or-none, but queue-side failure after the first successful call
cannot be rolled back and therefore forces terminal containment.
