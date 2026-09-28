# ISC12 G10 clean-sheet format audit

Date: 2026-08-30
Status: **G10-B P3a PROVEN — native transaction seams remain disconnected**

## Runtime disposition — 2026-08-31

This document preserves the evidence and design history that produced the
96-byte envelope prototype. Its runtime recommendation is superseded. The first
live persistence execution wrote an exact 499-byte disposable file — a valid
96-byte ISC12 envelope followed by a valid 403-byte D2S — but D2RLoader 1.2
rejected the character in its frontend as `invalid-character`.

Vincent selected standard external D2S/D2I containers for the plugin. ISC12 now
validates the native container unchanged and delegates physical writes to the
D2RCore writer/closer pair so D2RLoader can generate `.d2rl`, backups and its
environment record normally. Clean-sheet compatibility is a documented support
boundary: create a new ISC12 character or use a future external migration tool,
and back up saves. The envelope and atomic writer remain disconnected,
unit-tested building blocks for that future tool, not runtime DLL behavior.

## Decision

ISC12 needs a real outer file envelope, not a repurposed D2S version, padding
field, D2I reserved byte range or sidecar. The envelope must be validated in its
entirety before the first 12-bit stat or item payload is decoded. The write path
must produce the same contract atomically.

The D2S replacement-buffer lifetime, the complete D2I ingress, both physical
store objects and their shared client-side reader/writer are now identified.
The modern D2S serializer, its final size/checksum block, both native D2I sector
readers/builders and the server-to-client blob transport are also identified
exactly. Vanilla persistence is proven non-atomic. The disconnected G10-B core
now owns the atomic file primitive, outer envelope, semantic schema descriptor,
reader/writer preparation policy and governed native `Stat`-linker snapshot.
It publishes one fail-closed canonical hash before `DescFunc` mutation. Exact
compiled-name acquisition is closed; G10 remains blocked only on connecting the
frozen policy and transaction to the governed native reader/writer seams.

## Governed D2S evidence

- `0x41DF40` is the outer file loader. Its long entry signature is unique. It
  receives the owning context in RCX, the file/view object in RDX and an output
  pointer in R9, then obtains the raw buffer through `0x484FE0(file, 0)` and its
  size through `0x485070(file, 0)`. Its sole indexed caller is `0x483B92`.
- `0x41DFEC` requires the normal `0xAA55AA55` D2S magic. `0x41DFF3` sends
  versions at most 91 to the legacy decoder and every larger value to the modern
  path. A private version value is therefore not a fail-closed namespace.
- `0x52E910` is the modern payload decoder and has one direct caller,
  `0x41E03B`. The exact long entry signature is unique.
- `0x52E9C6` selects the pre-104 or 104+ header parser. `0x52EA3E` later checks
  the `Woo!` header witness.
- The exact call setup at `0x52EC28` reaches the modern player-stat decoder
  `0x533760` at `0x52EC4A`. An outer rejection at `0x41DF40` can consequently
  occur before the first proven ISC-sensitive stat decode.
- On decode failure, `0x41E04D..0x41E065` destroys a non-null output through
  `0x48FAA0`, nulls the caller-owned slot and clears the current-unit attachment
  through `0x486190(file, 0)`. `0x486190` does not free a record.
- On success, `0x484B00` destroys the native record collection at `0x41E073`
  before `0x5137B0` attaches the decoded player at `0x41E084`. No pointer into
  the source bytes survives the decoder return. A DLL-owned unwrap buffer can
  therefore live for the `0x52E910` call and be freed immediately afterward on
  every return. It must never be inserted into the native `file+0x280` vector,
  whose records are destroyed with the native allocator.

## Governed D2S write evidence

- `0x52F090` is the modern D2S serializer. Its exact long entry signature is
  unique and it has two direct callers, at `0x413631` and `0x41E207`.
- `0x534340`, called only at `0x52F29D`, constructs the inner D2S header. It
  writes the standard magic at `0x534408` and version 105 at `0x53440F`.
- `0x52F839..0x52F891` is the final size/checksum block. It calculates
  `cursor - start`, narrows the length through `0x084970`, writes the length at
  `header+8`, calculates the rolling checksum through `0xA1B830`, then writes
  it at `header+0x0C`. The block has a unique exact signature.
- `0x41E0D0` orchestrates the player D2S blob and the following D2I sectors.
  `0x4838A0` only appends each completed blob to a collection; neither function
  performs a physical file write.

The standard D2S header still carries its normal magic, version, declared size
and checksum. That checksum protects file integrity; it does not identify an
ISC12 namespace or bind the payload to an `ItemStatCost` schema.

## Governed shared-stash evidence

The governed fixture `data-BKVince/ModernSharedStashSoftCoreV2.d2i` is 680 bytes
and contains seven concatenated sectors. Each observed sector starts with a
64-byte header:

- `+0x00`: `0xAA55AA55`;
- `+0x04`: format `2`;
- `+0x08`: item format `105`;
- `+0x0C`: gold;
- `+0x10`: 16-bit sector size;
- `+0x12`: 16-bit stat `0xB8`;
- `+0x14`: sector type;
- `+0x15..+0x3F`: zero in this fixture.

The observed sector sizes are `68, 68, 68, 68, 68, 162, 178`. The local Hero
Editor codec first walks every 64-byte header and declared size, then hydrates
items in a second phase. That is useful format evidence, but it does not prove
that D2R itself has an equivalent whole-file preflight seam.

`0x52FDB0` and `0x52FAE0` are the two governed native D2I sector readers. Both
receive `(context, file/view, player)`, enumerate records starting at index 1
through `0x485070`/`0x484FE0`, require 64-byte headers and the normal sector
magic, then select their sector types. `0x52FDB0` owns ordinary type-0/type-1
sectors; `0x52FAE0` owns the type-2 Chronicle sector. Their long entry
signatures are unique.

Neither reader performs a whole-file preflight. `0x52FAE0`, for example,
decodes the first matching type-2 payload at `0x52FBF8` before it advances to a
later record. `0x52FDB0` reaches its item mutation through `0x532D60` at
`0x53014E`, then attaches a proxy through `0x538690` at `0x530218`, before all
later records are inspected. A hook at their
entry is therefore useful only after proving that it sees every record of the
physical D2I and dominates both readers.

`0x530760` builds the ordinary 64-byte D2I sectors and `0x530900` builds the
type-2 Chronicle sector. `0x41E0D0` calls them at `0x41E323` and `0x41E370`, then appends
their outputs separately through `0x4838A0`. They are sector writers, not a
whole-file writer.

The earlier `0x61CFA7` candidate is excluded: it belongs to the frontend D2S
reader around `0x61CF10`, not to the physical D2I ingress path.

The physical path is nevertheless whole-file before those server readers.
`0x9FC550` builds one canonical object path, opens it in read mode, obtains the
complete length, resizes the object-owned buffer through `0xA1E1F0`, reads the
whole store into `object+0x08`, closes it and marks the object state `3`.
`0x9F9113..0x9F9119` applies it to every registered object; `0x9F92FE` is the
single-object owner.

For shared stash upload, `0xEDE00` resolves that same selected store object,
copies its full buffer and length at `0xEE0DC..0xEE0FE`, and only then walks the
concatenated sectors at `0xEE158..0xEE1D7`. The first native sector decode can
therefore be dominated by a physical whole-file envelope check.

The zero bytes at `+0x15..+0x3F` are only an observation. Nothing proves that
vanilla rejects, preserves or semantically ignores them. They are not a safe
marker.

## Transport boundary and remaining blocker

`0x408AB0` consumes the completed blob collection and emits each record as
`{ uint16 opcode = 0x00B3, uint8 chunkLength, uint8 firstChunk, payload[] }`.
Payload chunks are at most 255 bytes and leave through the indirect call at
`0x408BB6`; `{ 0x00B3, 0, 0 }` leaves through `0x408C34` as the collection
terminator. This proves that server-side serialization ends before physical
persistence.

The client dispatcher at `0x131A70` routes opcode `0x00B3` to `0x131CE4`.
That handler appends each payload through `0x1307D0`; a zero-length record with
`firstChunk == 0` calls `0x133610` at `0x131D04`. `0x133610` validates the
complete D2S header and, at `0x133B7D`, passes the whole buffer and its length
to `0xA1E220`. `0xA1E220` locks the destination object, resizes and copies the
complete byte vector, marks the object state `4`, then unlocks it. The receive
finalizer releases and resets the accumulator on its cleanup paths.

The first B3 record is accumulated at `0x22ABC00/+0x08`; every later record is
concatenated at `0x22AFC20/+0x08`. Before publishing that second buffer,
`0x1338A7..0x133A73` walks every 64-byte D2I header, magic, version and 16-bit
size until the remaining byte count is exactly zero. `0x133B67` then resolves
one store object, `0x133B7D` installs the complete concatenation, and
`0x133BC5` advances it through `0x9F82F0`. There are no per-sector disk files.

`0x9F82F0` consumes object state `4` and advances the normal writable path to
state `1` through `0xA1E200`; it is not itself a filesystem write. The shared
physical owner is `0x9F94A0`.

## Physical object writer and atomicity verdict

`0x9F94A0(ResultRef*, SaveManager*)` iterates the registered object array at
`manager+0x360/+0x368`. For every state-1 object it joins the base directory at
`manager+0x38` with the canonical filename at `object+0x20`, locks the object,
opens that final path, writes the buffer at `object+0x08` with its 64-bit length
at `object+0x10` narrowed to a DWORD, closes it, records a timestamp and marks
state `3`.

The mode-4 branch of `0x122B920` maps exactly to `GENERIC_WRITE` and
`CREATE_ALWAYS`, with normal attributes and no write-through flag. The write
wrapper at `0x122BFF0` has the Windows `WriteFile` ABI, but `0x9F94A0` ignores
both its status and `bytesWritten`. The close wrapper at `0x11C7E30` only closes
the handle. Import names are not hydrated in the governed image, so the API
names are semantic ABI identifications; the constants, arguments and control
flow are byte-exact.

Vanilla persistence is therefore proven non-atomic: it truncates the canonical
final file before one buffered write, without temporary file, flush,
write-through, replace/rename, backup, rollback or short-write validation. The
state-2 delete branch is likewise direct. ISC12 must own a sibling-temp, full
write, flush, atomic replace and cleanup transaction; merely wrapping the
buffer before calling vanilla would preserve this corruption window.

### Writer job result and failure ABI

`0x9F9D40` is the concrete scheduled callback and returns `0x9F94A0`'s Boolean
unchanged. Its result object uses `operationKind=2` at `+0x00`, status at
`+0x04`, the failing object at `+0x08` and `operationCode=4` at `+0x30`.
The scheduler `0x9E9770` invokes the callback once: true publishes task-node
state `1`, false publishes state `2`; no scheduler retry exists.

The proven writer statuses are `0` for success/continue, `6` for an
object-specific physical open failure and `14` for writer preflight failure.
No unproven native error code will be invented. The manager sets its busy flag
at `+0x391` when scheduling; only the all-success tail clears it. The vanilla
open-failure path closes and unlocks, leaves the object in state `1`, publishes
status `6` plus the failing object, calls `0x9F80A0` then `0x9F80F0`, returns
false and leaves only latent retry eligibility for a future external action.
It is not an automatic retry.

Every ISC12 transaction failure before replacement commits will reuse that
same native status-6 branch. Only a complete commit may enter the vanilla
timestamp/state-3 success continuation. This deliberately fixes vanilla's
silent failed/short-write success defect while preserving the proven game ABI,
lock lifetime, callbacks, manager flag and object state.

The G10-B file primitive now writes a unique same-directory sibling with
`CREATE_NEW`, loops until every byte is written, calls `FlushFileBuffers`,
closes the handle, and replaces the final path without ever opening it through
`CREATE_ALWAYS`. Existing targets use `ReplaceFileW` with a unique backup;
first creation uses `MoveFileExW(..., MOVEFILE_WRITE_THROUGH)`. The documented
`ERROR_UNABLE_TO_MOVE_REPLACEMENT_2` partial-transition case attempts to restore
the backup without overwriting another file and preserves both siblings if
rollback itself fails. This primitive is compiled and unit-tested but is not
connected to either native seam yet.

### Exact object classification and rejection contract

The physical pair is generic: at least 23 governed object-lookup call sites
exist, so an arbitrary `.d2i` wildcard is not accepted. The native object is
`0x68` bytes. Its filename is a native string subobject at `object+0x20`:
`char*` at `+0x20`, explicit byte length at `+0x28`, capacity/SSO at `+0x30`
and inline bytes at `+0x38`. Comparing bytes at `object+0x20` directly would be
an ABI error.

The frozen fail-closed selector is:

- exact lower-case `.d2s` suffix on a nonempty, path-free basename;
- exact full-name allowlist for `SharedStashSoftCoreV2.d2i`,
  `SharedStashHardCoreV2.d2i`, `ModernSharedStashSoftCoreV2.d2i` and
  `ModernSharedStashHardCoreV2.d2i`;
- embedded NUL, slash, backslash, wrong case and every other `.d2i` pass as
  non-target objects and retain vanilla behavior.

`0x122ACA0` appends the lower-case extension without case folding. The four
D2I base slots are selected symmetrically at `0x1CC5F38/+50/+68/+88`; three
exact names are independently present in governed fixtures/references and the
Modern Hardcore spelling follows the byte-identical symmetric branch whose
BSS text is initialized only at runtime. This is a documented evidence limit,
not a wildcard expansion.

At reader mid-seam `0x9FC654`, rejection clears the owned byte vector through
`0xA1E1F0(object,0)`, resets aux and state to zero, then joins native failure at
`0x9FC66F`. Native code closes, unlocks and returns status `6`; both direct
callers test that status before any consumer. At writer mid-seam `0x9F95A2`,
the canonical path is already built and the object locked but `CREATE_ALWAYS`
has not run. A targeted handler completes timestamp/state/unlock only after an
atomic commit, or leaves state `1` and unlocks on failure, then joins
`0x9F963E` with native status `0` or `6`. Non-target objects replay the stolen
instruction and resume vanilla at `0x9F95A7`. Both long blocks and both
five-byte mutation seams have unique governed signatures.

## Rejected marker designs

- **D2S version only:** values above 91 already enter the modern decoder and do
  not bind the payload to ISC12.
- **D2S padding or D2I reserved bytes:** absence of ISC12 may still let vanilla
  enter its 9-bit decoders.
- **One marker per D2I sector without a whole-file preflight:** an earlier tab
  could be decoded or mutated before a later mismatch is discovered.
- **Sidecar file:** copying, renaming or losing one file can pair a payload with
  the wrong schema; the update is not intrinsically atomic.

## Frozen envelope v1

The outer header is exactly 96 bytes, explicitly little-endian and free of C
structure padding:

| Offset | Size | Field | Frozen value |
|---:|---:|---|---|
| `0x00` | 8 | magic | `49 53 43 31 32 0D 0A 1A` |
| `0x08` | 2 | envelope version | `1` |
| `0x0A` | 2 | header size | `96` |
| `0x0C` | 1 | store kind | `1` D2S, `2` whole-file D2I |
| `0x0D` | 1 | codec bits | `12` |
| `0x0E` | 2 | sentinel | `0x0FFF` |
| `0x10` | 4 | flags | `0` |
| `0x14` | 8 | payload length | exact inner byte length |
| `0x1C` | 4 | schema descriptor version | `1` |
| `0x20` | 32 | schema SHA-256 | canonical descriptor hash |
| `0x40` | 32 | payload SHA-256 | exact inner payload hash |

Validation requires exact total length with no trailing bytes, all frozen
dimensions, matching store kind and schema, payload hash, then inner preflight.
D2S requires magic `0xAA55AA55`, format `105`, exact declared size and the
native rolling checksum. D2I requires one or more exactly exhausting sectors,
each at least 64 bytes with the same magic, sector format `2`, item format
`105`, a valid 16-bit sector size and type at most `2`.

### Why legacy ItemStatCost save fields are unreachable

The complete item decoder does read an individual item-format value and stores
it at `ItemData+0x40` (`0x3788A0..0x37898C`). That value is not the selector for
the 1.09 `ItemStatCost` layout. The selector is the version argument inherited
from the containing stream. `0x374C0D` retains that sixth argument, and
`0x374F7F` forwards it to the complete decoder `0x378860`. At `0x379E04` the
decoder computes exactly `containerVersion < 93` and stores the Boolean at
`[rsp+0x40]`.

That Boolean selects `Save Bits` at compiled-record `+0x15/+0x16` and `Save
Add` at `+0x18/+0x1C`. The generic stat reader `0x37BED0` receives the same
Boolean and indexes `Save Param Bits` at `+0x20/+0x24`, `Save Bits` at
`+0x15/+0x16` and `Save Add` at `+0x18/+0x1C`. The compact item decoder has no
legacy selector and reads the current fields directly at `0x37868D` and
`0x3786A2`.

The version provenance is closed on both governed store kinds:

- D2S reads the outer header version at `0x41DFDA`, passes it to `0x52E910` at
  `0x41E03B`, and forwards that same retained value through the item-list calls
  at `0x52EE17`, `0x52EF12` and `0x531D4E`. The remaining modern sublist path
  `0x5331F0 -> 0x532E40 -> 0x43D900` forwards the same argument.
- The ordinary type-0/type-1 D2I reader copies each 64-byte sector header to a
  local base at `[rbp-0x40]`; `0x530147` therefore loads the exact `+0x08`
  `itemVersion` field and `0x53014E` sends it through `0x532D60` to the same
  item-list decoder. The type-2 Chronicle path does not decode items.
- `0x43DA07` finally forwards the list decoder's version as the sixth argument
  of `0x374BF0`; neither the stored individual item-format value nor any field
  of the partially built item replaces it.

Consequently the frozen inner preflight above makes every legacy Save
Bits/Add/Param branch unreachable before native item mutation: D2S accepts
only outer format `105`, and whole-file D2I validation accepts only sectors
whose `+0x08 itemVersion` is `105` before the first sector is split or decoded.
G10 does not need to parse and reject a numerically old individual item-format
tag, because that tag does not select these fields. If a later envelope version
ever admits a containing D2S/D2I version below `93`, it must introduce a new
schema-descriptor version that includes the three legacy fields; v1 must not be
silently widened.

## Frozen canonical schema descriptor v1

The SHA-256 input starts with the terminal-NUL-inclusive domain tag
`ISC12.ItemStatCost.Descriptor.v1\0`, followed by descriptor version `u16LE=1`,
codec bits `u8=12`, sentinel `u16LE=0x0FFF`, row count `u32LE` and the effective
global `stuff` value as `u8`. `stuff` is the first compiled record's value in
`1..8`, otherwise the native fallback `6`.

Every row follows physical compiled ordinal order and contains:

1. ordinal/stat ID `u16LE`, exact strict-UTF-8 `Stat` length `u16LE` and bytes;
2. one canonical `u16LE` semantic flag word for `Send Other`, `Signed`,
   `UpdateAnimRate`, `Saved`, `CSvSigned`, `fCallback`, `fMin`, `direct` and
   `damagerelated` in bits `0..8`, with bits `9..15` zero;
3. Send/CSV widths and params; `Multiply`, `Add`, `ValShift`, `MinAccr`;
4. current `Save Bits`, `Save Add` and `Save Param Bits` values;
5. `Encode`, resolved `maxstat`, both item events and functions, `keepzero`,
   `op`, `op param`, and the four resolved op-stat references.

All integers are fixed-width little-endian; signed values retain their
two's-complement bits, booleans are normalized, references use resolved numeric
IDs, and invalid stat references canonicalize to `0xFFFF`. Raw TXT bytes,
`*ID`, `*eol`, localization, description/group display fields, `advdisplay`,
and every derived HP/Mana/Stamina or op cache are excluded. The compiled 1.09
siblings `n09SaveBits` (`+0x16`), `dw09SaveAdd` (`+0x1C`) and
`dw09SaveParamBits` (`+0x24`) are explicitly excluded by the v1/inner-format-105
contract proved above.

`*ID` cannot identify rows: the governed BKVince table has 394 physical rows,
but ordinal 212 (`passive_mastery_gethit_rate`) already says `*ID=213`, and
ordinal 213 also says `213`. Exact `Stat` plus physical ordinal is therefore the
identity. The synthetic golden descriptor hash is
`37EB7F2618667E3DD340674EDF15F325B18BD250236E50223AF3915F27711DE7`.

The binary builder rejects empty, embedded-NUL, malformed or overlong UTF-8;
row reorder, rename or current semantic changes alter the hash, while legacy
1.09 and display-only changes do not. The envelope and schema builders,
whole-store read/write preparation policy and atomic-file primitive compile
under `/W4 /WX` and pass their disconnected fixtures. They are not installed
into D2R.

## Next gate — G10-B P3b native transaction integration

The governed ledger now has 96 sites in 14 groups, including the exact native
`Stat` linker route and unique exact five-byte mutation seams at reader
`0x9FC654` and writer `0x9F95A2`. P3a closed the schema source: ISC12 copies
the linker names and compiled rows into an owned snapshot, then publishes one
fail-closed schema hash before `DescFunc` mutation. P3b must:

1. connect the frozen policy and atomic primitive to both seams while preserving
   native locks, timestamps, result status `0/6`, callback and retry behavior;
2. publish both hooks transactionally through persistent RX relay/RW state and
   prove rundown plus cold-restart behavior;
3. keep the hooks uninstalled until G1–G4 can produce genuine 12-bit inner
   payloads; wrapping a 9-bit payload as ISC12 is forbidden;
4. keep multiplayer mismatch rejection coupled to G9 because a physical
   envelope stripped before upload cannot authenticate a remote inner stream.
