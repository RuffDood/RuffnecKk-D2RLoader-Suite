# Argument contracts

The optional `argumentContract` object in `known-rvas.json` records calling
evidence separately from identification confidence. Existing entries remain
valid without it: absence means **undocumented**, not verified or unsafe.

Read the [generated catalogue](argument-contracts.generated.md) for structured
contracts, and the [reference intake](references/rva-93847-intake.md) for imported
candidate claims. The registry is the editable source; generated Markdown is a view.

## Authoring rules

For each new or revised native function identification, record the established
arguments and return contract in this format. Leave unknown types, names,
ownership, lifetimes and requirements explicitly unknown. Do not infer a
prototype from a function name, or upgrade a contract because the RVA has high
identification confidence. Preserve existing notes and source attribution.

Callsites, interior instructions, data and relay slots are not callable function
entries. Use `callable: false`, `status: "not-callable"`, `prototype: null`,
empty arguments and a null return when documenting them. Cite the callee or
surrounding function in evidence without copying its ABI onto the witness.

For callable entries, a null prototype is valid when register evidence is more
precise than a proposed C++ declaration. Position and width matter independently
of semantic names. Stack locations must state their reference point (for example,
callee-entry RSP before the prologue). Do not silently substitute XMM2 for XMM3.

Static proof, runtime exercise, and active Loader hook ownership are separate.
A statically verified contract does not establish safe hooking in a loaded game.
Imported labels such as "Exact" remain source claims until independently supported.

## Object format (version 1)

| Field | Required content |
|---|---|
| `schemaVersion` | `1`; additive to the existing registry format |
| `status` | `unknown`, `partial`, `statically-verified`, `conflicting`, or `not-callable` |
| `callable` | Whether this address is a function entry, not whether calling it is safe in every context |
| `prototype` | Supported declaration as a string, or null when unresolved |
| `arguments` | Ordered objects with `position`, `name`, `location`, `type`, `meaning` |
| `return` | `location`, `type`, `meaning`; null for non-callable addresses |
| `requirements`, `effects`, `unknowns` | Arrays describing established requirements, effects and unresolved limits |
| `scope` | `target` and `canonicalSha256`; exact applicable corpus identity |
| `evidence` | Nonempty array of `source` and `witness` pairs with precise sections, callers, body bounds or hashes |
| `runtimeQualification` | `status` and `notes`; name actual runtime/artifact and evidence if tested |

Use `partial` when argument semantics or types are unresolved, even if register
placement is established. Use `conflicting` when evidence disagrees and preserve
both claims. Runtime exercise is recorded separately and does not erase a conflict.
The initial three contracts normalize existing skill-cost evidence; no new
disassembly or runtime qualification is claimed.

## Generate, check and search

From the repository root:

```powershell
node scripts/reverse-engineering/argument-contracts.mjs
node scripts/reverse-engineering/argument-contracts.mjs --check
node scripts/reverse-engineering/argument-contracts.mjs --query 0x33AA00
node scripts/reverse-engineering/argument-contracts.mjs --query SKILLMANA
node --test scripts/reverse-engineering/argument-contracts.test.mjs
```

Generation validates contract structure and the imported source hash, then writes
the catalogue and address-overlap inventory. Check mode is read-only and fails on
drift. Query reads current JSON directly, including undocumented legacy entries;
it does not depend on a rebuilt native index. Existing `re:d2r33 known` behavior
is unchanged; use this query for structured contracts.

Backfill incrementally from primary local evidence. A full conversion of the
registry or automatic promotion of imported prototypes is not implied.
