# PD2 Resurrected native research intake

Native research from [synpoox/pd2-resurrected](https://github.com/synpoox/pd2-resurrected)
is retained at commit `8303651998382477fdac695d7dbb2cf11ce311df`, inspected on
2026-11-19. Vincent requested the same research intake as CelestialRayOne after
committing and pushing that earlier lot. This intake changes research documents
and tools; it does not port a plugin, alter mod data, run the game or change an
existing donor/build pin used by another component.

Use the [topic, location and contract index](pd2-resurrected-native-inventory.md)
or [structured catalogue](pd2-resurrected-native-inventory.json). The source is
registered as `pd2resurrected` in [references.json](../../references.json).
Its clean pinned checkout remains under ignored
`analysis-cache/references/PD2-Resurrected`; plugin implementations, game files
and assets are not vendored into Diablo.

## What this adds to workspace knowledge

This source contributes both reverse-engineered behavior and implementation
evidence. Research documents explain why a native site matters, compare legacy
PD2 with D2R, identify dead code and unused function slots, and retain uncertain
or untested cases. The port's source adds C++ pointer casts, typed field access,
hook declarations, mutation requests and native-symbol consumers. These are
searchable together while retaining their different roles and provenance.

| Material | Destination and treatment |
|---|---|
| Source revision, license, attribution and scope | Existing reference registry, new `pd2resurrected` entry |
| Research explanations and native comparison tables | PD2 inventory, preserving complete selected paragraphs, table headers/cells and source lines |
| Engine helpers and function-pointer casts | PD2 inventory's `engine-call-contract` and `function-pointer-contract` records; source contracts only |
| Structure fields and compiled table access | PD2 inventory's typed accesses and namespace declarations; no inferred complete layout |
| Hook entries and expected arrays | Explicit hook-binding records; bytes compared only when the owned declarations resolve uniquely |
| Native references whose definitions live in the toolkit | Symbolic references and unresolved Engine-symbol list; no substitute values |
| Comparison with our existing research | Numeric overlaps with `known-rvas.json` and the Celestial catalogue, without claiming semantic agreement |
| Independently supported native contracts and compiled layouts | Existing `known-rvas.json` and datatables atlas remain their authorities; no automatic promotion |
| Upstream tests and gameplay reports | Labelled as source scenarios or upstream observations; not executed or independently reproduced here |

## Source coverage

The catalogue scans 269 tracked files:

- 133 authored plugin source/header files and 11 plugin test files.
- 118 documentation files, including all `docs/` Markdown and the root
  overview, notice, changelog and glossary.
- Four game Python test scenarios and three focused native/hook/catalyst
  static test files.

It retains 4,604 records, including 980 Engine call casts covering 256 distinct
symbols, 791 typed field accesses, 85 function-pointer declarations, 403 research
table rows, 750 documentation paragraphs, 297 research comment blocks, 27
inline-hook bindings and 10 mutation-patch declarations.

The research tables mention 566 distinct hexadecimal values of at least
`0x10000`. These include legacy PD2 virtual addresses, D2R RVAs, data locations
and other values. The location index is a way to find their documented context,
not a new native address registry. The original table columns are retained so a
PD2 address does not silently become a D2R address.

Authored game data/TSV rows, assets, unrelated tools and upstream agent
instructions are excluded. Upstream game tests are read as evidence definitions,
not run. This is pattern-based extraction, not complete semantic decompilation
or automatic analysis of every helper body.

## Missing pinned toolkit dependency

The source's [engine header](https://github.com/synpoox/pd2-resurrected/blob/8303651998382477fdac695d7dbb2cf11ce311df/plugin/common/pd2_engine.h#L1-L11)
explicitly says shared addresses, structures and engine declarations live in
`d2rAI-toolkit/shared/plugin-common`, including `d2r.h` and `rvas_93847.h`.
Its own header retains additional values that the toolkit does not hold.
[CMake](https://github.com/synpoox/pd2-resurrected/blob/8303651998382477fdac695d7dbb2cf11ce311df/plugin/CMakeLists.txt#L16-L40)
consumes that external `plugin_common` library and its SDK.

The exact declared toolkit pin is
`14a5433dfe82d98a1966c247f9f123e62cc3230c`, from
[`toolkit.version`](https://github.com/synpoox/pd2-resurrected/blob/8303651998382477fdac695d7dbb2cf11ce311df/toolkit.version).
At intake:

- `synpoox/d2rAI-toolkit` returned HTTP 404 through GitHub and repository-not-found
  through Git.
- The public `Nugwa628/d2rAI-toolkit` fork did not resolve that exact commit.
- The existing local toolkit checkout at `projects/reimagined/pd2/local/d2rAI-toolkit`
  was at `98b5eb5b08c03eda9a979ddddcc2d3717a65bd51`; it could not resolve the
  required object. It was not modified or used as replacement evidence.

Consequently, 255 of the 256 distinct Engine call symbols lack an available
PD2-owned literal address declaration. That is an availability boundary, not a
finding that their names or contracts are wrong. Some values may also be stated
in research prose or tables, which remain separate source claims rather than
silently becoming a replacement toolkit header. The catalogue retains their
source pointer types and consumers and lists the unresolved symbols explicitly.

The workspace's older advisory toolkit snapshot under `tools/d2rai-toolkit`
has a separate owner, revision and scope. It is not refreshed or substituted.
Newer research in `synpoox/d2r-tools` is also a separate source; the PD2 pin
does not establish equivalence with it.

## Useful native research routes

All descriptions and addresses below are attributed upstream findings. The
source authors' static and gameplay evidence remains distinct from workspace
verification.

| Question | Upstream research and source | Why it is useful |
|---|---|---|
| Skill/missile dispatch tables, bounds and free slots | `docs/research/skills/skill-function-tables.md`; `plugin/core-rules/skill_functions.cpp`, `missile_functions.cpp` | Distinguishes server/client start/do/hit/damage tables, dispatch consumers, overflow/router choices and collisions with live prg functions |
| Critical/deadly strike, Crushing Blow, Open Wounds and leech | `docs/research/items/ir3-combat-stats.md`; `plugin/core-rules/item_rules.cpp` | Compares live PD2 readers with D2R hooks; identifies missile and self-damage differences and dead PD2 code |
| Curses and state/stat-list behavior | `docs/research/items/ir4-curses.md`; `docs/research/skills/states-immed.md`; `item_rules.cpp` | Separates curse resistance on the target, curse count/replacement, extra aura stat handling and state lifecycle |
| Cube rules, stacking and socket persistence | `docs/research/items/ir6-cube-rules.md`, `stack-split.md`, `split-throw.md`; `cube_rules.cpp`, `cube_stacks.cpp`, `socket_rules.cpp` | Source contracts for recipe matching, stack quantity, request paths, compact-save flags and socket mod application |
| Skill-specific native behavior | `docs/research/skills/` work items W6-W36 and `docs/features/core-rules.md` | Connects handler slots, native helpers, source field accesses and deliberate port deviations by skill |
| Boss AI and encounter state | `docs/research/maps/pd2-uber-bosses.md`, `uber-arenas.md`; `plugin/core-events/*_ai.cpp`, `lucion.cpp`, `rathma.cpp` | Separates legacy donor behavior from the D2R port's stages, timers, native helper use and custom state |
| Maps, rooms, portal routing and map properties | `docs/research/maps/`, `docs/cartographer/native-check.md`; `map_open.cpp`, `map_events.cpp`, `plugin/common/cartographer_*_v1.h` | Room/level and service relationships, map lifecycle, tested versus skipped native map checks and mod-owned interface definitions |
| HD and controller behavior | `docs/research/hd/`, `docs/research/controller/`; `plugin/core-ui/` | Distinguishes engine behavior from UI work, service-based stack split, thread boundaries and documented multiplayer shortcuts |
| What the port changed or omitted | `docs/deviations.md`, `docs/pd2/engine-dll-triage.md`, `docs/process/d2rloader-capabilities.md` | Prevents data-only, service-based, native-hook, legacy x86 and UI findings being treated as interchangeable evidence |

For example, the [dispatch table research](https://github.com/synpoox/pd2-resurrected/blob/8303651998382477fdac695d7dbb2cf11ce311df/docs/research/skills/skill-function-tables.md)
reports these values from its author's D2RLoader runtime dump, explicitly
describing that investigation as static rather than tested in game:

| Claimed table | Claimed RVA | Reported size/bound |
|---|---|---|
| Skill server start | `0x238E710` | 91 |
| Skill server do | `0x238EA00` | 191, with different bounds on different consumers |
| Skill client start | `0x235F590` | 70 |
| Skill client do | `0x235F7D0` | 130 |
| Missile server do | `0x2390E80` | 54, no dispatcher bound reported |
| Missile server hit | `0x2391030` | 71 |
| Missile server damage | `0x2391270` | 31 |
| Missile client do | `0x2358430` | 92, no dispatcher bound reported |
| Missile client hit | `0x2358710` | 81 |
| Property functions | `0x2386AB0` | 37 |

The same document records unresolved stop/event tables and warns that several
apparently unused do slots are reached by prg handlers. Those limitations are
retained with the rows, rather than converting the slot list into a hook policy.

## Owned declarations and captured-image comparison

Four literal native address values remain in the PD2-owned engine header:

| Source name | Value | Source context |
|---|---|---|
| `ItemSpellUseTable` | `0x1D3EA90` | Item-use handler pointer table; a data address |
| `AlmanacUseSlotOriginal` | `0x580F10` | Claimed original handler stub for its selected slot |
| `SocketFillerMods` | `0x475140` | Claimed socket filler mod application helper |
| `UnitItemData` | `0x34A500` | Claimed item-data accessor |

Two of those numeric values occur in the current workspace registry, and one
occurs among the Celestial inventory's named address constants. The structured
records retain the matching names and the hashes of both comparison inputs;
numeric overlap alone does not establish agreement or independent corroboration.

One inline-hook binding can be resolved using only these owned declarations:
[`socket_rules.cpp`](https://github.com/synpoox/pd2-resurrected/blob/8303651998382477fdac695d7dbb2cf11ce311df/plugin/core-rules/socket_rules.cpp)
binds `SocketFillerMods` to `SocketFillerModsEntry`. Its expected sequence
`48 89 5C 24 20 56 57 41 56 48 83 EC 30` matches the captured image at
`0x475140`. This is 13-byte equality, not independent proof of the helper's
complete semantics, relocation safety, ABI or active ownership. The other
source hook bindings lack the uniquely available address/expected-byte pair
needed for the same comparison; no pairing was guessed from similar names.

`npm.cmd run re:d2r33 -- status` passed before native comparison. The extractor
also verifies canonical SHA-256
`CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`
before reading bytes. Native identity follows
[NATIVE-CORPUS.md](../../NATIVE-CORPUS.md): the captured D2RLoader-hosted game
image, corpus `d2rloader-game-image-20260717`. Upstream `93847` labels remain
source metadata; the source does not establish retail cross-build equivalence.

No existing native registry entry or atlas layout is promoted or rewritten.
The separate existing atlas validation gap recorded in the
[Celestial intake](celestialrayone-intake.md#validation-receipt) remains outside
this imported reference catalogue.

## License and evidence preservation

The [retained MIT license](pd2-resurrected-LICENSE.txt) covers the original
authored work as described by the [retained notice](pd2-resurrected-NOTICE.txt).
Its exclusions for Blizzard/PD2 game content, companion plugins and the
separately licensed Loader/SDK are preserved. The retained notice is plain text;
its original relative links refer to the upstream repository.

The catalogue stores source hashes, commit-pinned URLs, line ranges, original
excerpts, table headings and source roles. Gameplay reports retain their dates,
debug setup, passed/skipped checks and deviations as upstream evidence. No
current runtime, save/persistence or multiplayer qualification is claimed by
this intake. Source wrappers and casts are not governed `argumentContract`
objects. Neither repeated toolkit ancestry nor a returned Suite-derived
identification becomes an independent observation.

## Search and reproduce

```powershell
python -B scripts/reverse-engineering/pd2-native-intake.py --query SocketFillerMods
python -B scripts/reverse-engineering/pd2-native-intake.py --query 0x238EA00
python -B scripts/reverse-engineering/pd2-native-intake.py --query NewStateList --kind engine-call-contract
python -B scripts/reverse-engineering/pd2-native-intake.py --query Tables --kind typed-field-access --limit 10
python -B scripts/reverse-engineering/references.py status pd2resurrected
python -B scripts/reverse-engineering/references.py search pd2resurrected SocketFillerMods --limit 10
python -B scripts/reverse-engineering/pd2-native-intake.py --check
python -B scripts/reverse-engineering/test_pd2_native_intake.py
```

Queries read the versioned JSON and need no source checkout. The default result
limit is 30; `--limit 0` returns all matches. The existing reference helper can
bootstrap the pinned source on another workstation. Regeneration needs its
clean checkout, the hash-matching captured image, the current registry and the
Celestial catalogue. `--check` writes nothing and detects generated drift.
The extractor reuses the committed Celestial extraction helpers without
changing that catalogue's implementation. A dependency or source update requires
explicit review of the reference and extractor pins before regeneration.

## Validation receipt

On 2026-11-19, the PD2 generated-file freshness check, all six PD2 extraction
tests, the seven existing Celestial extraction tests and Celestial's own
freshness check passed. The PD2 reference check verified the source remote,
exact revision, clean checkout and retained license. Independent checks verified
all 269 source hashes and all 4,604 excerpt ranges and upstream-claim statuses.
The local research links and representative address/symbol/kind-filtered queries
passed, as did whitespace checks for the task's authored files and tracked diff.

The dependency availability gap remains documented above; these checks do not
claim that the unavailable toolkit contents were extracted or verified. No game
test scenario was executed, and no native ABI, save or multiplayer qualification
was added.
