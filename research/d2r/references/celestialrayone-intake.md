# CelestialRayOne native research intake

The workspace now retains native research extracted from
[CelestialRayOne/d2rloader_plugins](https://github.com/CelestialRayOne/d2rloader_plugins)
at commit `924072545985e0d1bef58c3362c1c1572531aa2e`, inspected on 2026-11-18.
Vincent authorized this research intake with `Go` after reviewing the proposed
destinations. This is documentation and static comparison work; no plugin was
built, installed, started or modified.

Start with the [topic and address index](celestialrayone-native-inventory.md).
The [structured catalogue](celestialrayone-native-inventory.json) retains the
actual research excerpts, declarations and individual comparison results.
The source is registered as `celestialrayone` in
[references.json](../../references.json). The clean pinned checkout remains
under ignored `analysis-cache/references/CelestialRayOne-d2rloader_plugins`;
upstream plugin implementations and release assets are not vendored into Diablo.

## What belongs where

| Information | Authoritative destination | Intake treatment |
|---|---|---|
| Upstream revision, license, credits, source scope | `reverse-engineering/references.json` | Registered as native research reference only |
| Upstream function addresses and aliases | This inventory | Deduplicated by numeric value in the Markdown index; every original declaration retained in JSON |
| Source function-pointer declarations | This inventory | Claimed contracts, separate from native argument contracts |
| Interior hook windows, expected bytes and rejoin sites | This inventory | Entries, continuations and byte arrays remain distinct records |
| Structure offsets, field widths, strides and helper access expressions | This inventory | Source context retained; game versus plugin-local ownership is unresolved unless the source establishes it |
| D2RCore providers and exports | This inventory | Keep module-specific provenance; never interpret Core RVAs as base-game RVAs |
| SDK service definitions and SDK documentation | This inventory, marked `vendored-sdk-contract-reference` | Interface reference only; not proof of a native game implementation or the workspace's current SDK baseline |
| Independently established game-image identities and callable contracts | Existing `known-rvas.json` and its `argumentContract` objects | No automatic promotion in this intake |
| Independently admitted compiled table layouts | Existing `datatables-atlas/catalog.json` | No automatic promotion in this intake |
| Static atlas extraction candidates | Existing `datatables-atlas/candidates.json` | Its generated extraction ownership remains intact; external claims are linked from the atlas README instead of mixed into it |

This separation makes the material reusable before every individual claim has
been independently checked. The catalogue is a research source, not an SDK or a
hook-installation contract. An absent registry entry is a lead, not a newly
verified game function.

## Coverage and measured results

The scan covers 142 authored source, documentation and configuration files,
including source from 110 plugin directories, plus 36 current vendored SDK
headers/documents. It includes authored C++/headers/Markdown/TOML, release
Markdown/TOML/README documents and current SDK headers and main documentation.
The redundant SDK backup, binary assets, gameplay TSV rows and build
infrastructure are excluded. Their exclusion does not imply they contain no
useful future context.

The catalogue contains 12,483 records:

- 2,229 named address declarations, containing 1,438 distinct numeric values.
- 1,565 function-pointer contract declarations, including SDK interfaces.
- 1,453 layout/domain constants and 562 layout-access expressions.
- 2,266 research comment blocks and 110 documentation paragraphs.
- 1,620 byte arrays, 505 explicit byte bindings and 98 recognized hook-site rows.
- 119 structure declarations, 313 other native-binding expressions and 1,643
  fallback large-hexadecimal-mention records outside the recognized spans.

Of the 1,438 distinct named address values, 232 also occur in the current
workspace registry and 1,206 do not. This is numeric comparison only. The set
includes functions, data, interior sites, Core addresses and other source
constants; it is not a count of 1,206 new functions. Registry aliases are retained
beside the upstream names without declaring semantic agreement or disagreement.
The catalogue records the exact SHA-256 of the registry snapshot used for this
comparison, including existing concurrent worktree changes.

603 explicit bindings/site rows provide directly resolvable expected bytes:

| Result | Records | Interpretation |
|---|---:|---|
| Equal bytes at numeric RVA in captured game image | 547 | Byte equality only; function role, ABI and runtime ownership remain separate |
| Different bytes at numeric RVA | 37 | Preserve both sequences; investigate module/provider/capture state before attributing an error |
| Outside mapped captured PE ranges | 3 | No captured-image byte comparison is available at that numeric location |
| Explicitly declared Core witnesses | 16 | Kept as module-specific upstream claims and not compared to the game image |

These are witness records, not necessarily unique addresses or independent
observations. Several plugins reuse the same native surface. Only the canonical
game-image artifact was used for the byte comparison; D2RCore was not verified.
`npm.cmd run re:d2r33 -- status` verified the existing canonical image, analysis
image and index before the intake. The extractor also verifies the canonical
image hash before reading any byte witnesses.

## Useful research entry points

All addresses and behavioral descriptions in this section are **upstream
claims**, not newly admitted workspace contracts. The pinned source links are
the evidence for what the upstream author documents.

| Question | Start with these upstream source topics | Useful native material |
|---|---|---|
| How are items created, rolled, modified and transferred? | `blacksmith`, `jeweler`, `archaeology`, `quality-affixes`, `clone-item`, `transfer-to-stash`, `large-item-transfer` | Spawn/quality/cube callsites, stat-list helpers, item ownership, inventory lookups and client/server request paths |
| Where is combat damage calculated or capped? | `damage-overflow-fix`, `enhanced-damage-fix`, `attack-rating-overhaul`, `critical-strike-damage`, `crushing-blow-modifier`, `bleed-damage`, `soft-hits` | Arithmetic windows, resistance pipeline, percent/stat calculations, saturation behavior and Core stat-engine boundaries |
| How do skills, states and auras interact? | `aura-reactivation`, `left-click-auras`, `forever-states`, `curse-cap`, `restrict-state`, `cooldown-reset`, `dynamic-etype` | Skill lookup/selection, state/stat-list handling, cooldown thunks, callbacks and dispatch behavior |
| How do pets persist and acquire their stats? | `summon-enhancements`, `bind-demon-scaling`, `petmax-fix`, `summon-resist-cap`, `trap-owner-credit` | Serialization, restore order, pet buckets, summon arguments, skill-record fields and ownership |
| How are animation and combat modes selected? | `weapon-block-enhancements`, `frenzy-fpa`, `charge-fix`, `weapon-swap-queue`, `whirlwind-follow-cursor` | Composite weapon class, animation lookup, player-mode transitions, frame data and input/targeting callsites |
| How are monsters initialized and modified? | `monster-affixes`, `monster-display`, `terror-zone-density`, `herald-bosses`, `treasure-goblin`, `baal-ai-fix` | Rank flags, aura lists, spawn/rebuild behavior, AI and encounter/Core boundaries |
| Which services expose data or hide native complexity? | Current SDK `data_tables.h`, `custom_tables.h`, `item.h`, `inventory.h`, `core_exports.h`, `reimplementation_exports.h` | Service contracts and provider indirection, kept distinct from game-function declarations |

For example, [Summon Enhancements](https://github.com/CelestialRayOne/d2rloader_plugins/blob/924072545985e0d1bef58c3362c1c1572531aa2e/plugins/summon-enhancements/plugin.cpp#L12-L112)
documents the save writer `0x533AE0`, reader `0x531510`, first-pet lookup
`0x4FF1A0`, and the restore timing relative to world entry. Its source also
describes pet buckets with a `0x20` stride and a node chain, rather than merely
listing addresses. The catalogue preserves those explanations alongside the
source pointer declarations and byte witnesses. The writer's explicit
18-byte witness matches the captured image; that does not independently prove
the entire serialization contract.

The same source's [Skills field declarations](https://github.com/CelestialRayOne/d2rloader_plugins/blob/924072545985e0d1bef58c3362c1c1572531aa2e/plugins/summon-enhancements/plugin.cpp#L918-L925)
name `srvdofunc` at `+0x4E`, summon row at `+0x110`, pet type at `+0x112`, summon
mode at `+0x113`, petmax calc at `+0x114`, and a calc field at `+0x194`.
These remain imported layout leads. Compare them with the admitted Skills atlas
and its consumers before adding or changing a field contract.

[Weapon Block Enhancements](https://github.com/CelestialRayOne/d2rloader_plugins/blob/924072545985e0d1bef58c3362c1c1572531aa2e/plugins/weapon-block-enhancements/plugin.cpp#L33-L87)
connects the composite weapon-class helper `0x3B6E00` to the server block roll
and client display, and explains the missing-animation fallback. This is useful
behavioral context for a future block or animation investigation.

[Enhanced Damage Fix](https://github.com/CelestialRayOne/d2rloader_plugins/blob/924072545985e0d1bef58c3362c1c1572531aa2e/plugins/enhanced-damage-fix/plugin.cpp#L56-L105)
explicitly attributes the wide-stat engine to D2RCore, including evaluator and
base-stat export details. Its Core witnesses are deliberately excluded from
game-image byte matching. Several other differences concern stat-reader/skill
thunks, item-generation windows or patched request paths; the JSON preserves
the exact expected and observed bytes without asserting a common cause.

## Interpretation and provenance limits

Native identity follows [NATIVE-CORPUS.md](../../NATIVE-CORPUS.md): the workspace
reference is the captured D2RLoader-hosted game image, corpus
`d2rloader-game-image-20260717`, canonical SHA-256
`CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`.
Upstream labels such as `3.3.93847` and `D2RLoader 1.3.1` are retained source
metadata. They do not establish independently captured retail image identity
or current provider compatibility.

The extraction is pattern-based. It preserves complete selected comment blocks,
documentation paragraphs, recognized declarations and evidence expressions. It
does not resolve C++ scopes, evaluate arbitrary constant expressions, infer
field types, reconstruct all helper implementations or prove control flow.
Duplicate names with different values are not resolved by choosing the last
declaration. The fallback numeric index contains masks and immediates as well
as possible addresses. Unresolved values keep their source context.

Core module attribution is carried forward only from explicitly typed
`CoreWitness` tables; other module fields remain unresolved. Source commentary
can supply additional module claims for a focused follow-up. A matching prefix
does not prove the entire hook window, safe relocation, callable entry, ABI,
callee behavior, provider lifetime or present hook ownership. No new governed
native identity, callable contract or atlas layout is promoted by this intake.

Upstream's [third-party notices](https://github.com/CelestialRayOne/d2rloader_plugins/blob/924072545985e0d1bef58c3362c1c1572531aa2e/THIRD_PARTY_NOTICES.md)
credit D2MOO and the RuffnecKk Suite as references. Matching Suite-derived
information returning through this repository is not independent corroboration.
The notices declare a PluginSDK v4 pin; the catalogue hashes the actual current
vendored files rather than treating the notice as proof that every file is
unchanged from that SDK revision. No workspace SDK baseline is changed.

The upstream MIT notice is retained in [celestialrayone-LICENSE.txt](celestialrayone-LICENSE.txt),
and the SDK's separate MIT notice in [celestialrayone-sdk-LICENSE.txt](celestialrayone-sdk-LICENSE.txt).
They cover extracted source excerpts with their respective original attribution.

## Search and reproduce

Run from the workspace root:

```powershell
python scripts/reverse-engineering/celestial-native-intake.py --query FirstPetOfType
python scripts/reverse-engineering/celestial-native-intake.py --query 0x533AE0
python scripts/reverse-engineering/celestial-native-intake.py --query summon-enhancements --kind function-pointer-contract
python scripts/reverse-engineering/celestial-native-intake.py --query DataTables --kind research-comment --limit 10
python scripts/reverse-engineering/references.py status celestialrayone
python scripts/reverse-engineering/references.py search celestialrayone SkillPetMaxField --limit 10
python scripts/reverse-engineering/celestial-native-intake.py --check
python scripts/reverse-engineering/test_celestial_native_intake.py
```

Queries use the versioned JSON and work without the source checkout. They return
at most 30 records by default; `--limit 0` returns all matches. The existing
reference helper can `bootstrap celestialrayone` on another workstation after
reviewing the registered source. Regeneration additionally requires the
hash-matching canonical image and the matching registry snapshot. Regeneration
rewrites the inventory and retained licenses; `--check` writes nothing and
detects drift. Advancing the source revision requires reviewing the pin in both
the reference registry and extractor, then regenerating and reviewing the intake.

## Validation receipt

The generated inventory freshness check, seven focused extraction tests and
pinned-reference identity/cleanliness/license check passed on 2026-11-18.
The tracked documentation diff passed `git diff --check`.

The separate existing DataTables atlas validator did not pass: three evidence
entries (`item-stat-cost-compile`, `item-stat-cost-count`,
`item-stat-cost-fields`) reference the missing
`reverse-engineering/d2r-3.2.92777/isc12/native-sites.json`. This intake does not
change `catalog.json`, supply replacement evidence or claim that the atlas
passes validation. The imported catalogue remains usable independently.
