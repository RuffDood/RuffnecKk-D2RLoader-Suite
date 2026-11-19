# Native workbench path migration

On 2026-11-19, the governed captured-image workbench moved from
`reverse-engineering/d2r-3.2.92777/` to
`reverse-engineering/d2rloader-game-image/`. Vincent authorized this migration
after reviewing its scope. The folder now names the D2RLoader-hosted image rather
than the installed game/data label observed at capture.

## Current entry points

| Need | Entry point |
|---|---|
| Workbench, contracts, topics, atlas and imported references | [d2rloader-game-image](d2r/WORKBENCH.md) |
| Artifact provenance and native identity | [NATIVE-CORPUS.md](NATIVE-CORPUS.md) |
| Verify artifacts and index | `npm run re:native -- status` |
| Search identified native surfaces | `npm run re:native -- known <term>` |
| Inspect a function or its callers | `npm run re:native -- function <RVA>` / `xrefs <RVA>` |
| Check local Python and dependencies | `npm run re:native:doctor` |
| Query the existing Ghidra project | `npm run re:native:ghidra -- status` / `function <RVA>` |

The implementation entry points are `scripts/reverse-engineering/native.mjs`
and `native.py`, with neutral Ghidra launchers. Original `d2r32` Python/Node and
Ghidra script names remain compatibility wrappers. Existing `re:d2r32` and
`re:d2r33` commands reach the same captured workbench. The old `--workbench
d2r-3.2.92777` selector resolves to `d2rloader-game-image`; this is a path alias,
not a native compatibility decision. An explicit retired/different workbench
selector must still fail rather than silently choosing this image.

## Following historical evidence

For a workspace-relative or absolute path in an older receipt, replace the
directory component `reverse-engineering/d2r-3.2.92777` with
`reverse-engineering/d2rloader-game-image`. The remainder identifies the same
artifact or document. There is no old-directory junction or duplicate workbench.

Original imported source documents, pinned external excerpts, dated test receipts,
and captured filenames retain their original identifiers. The local PE filenames,
SQLite filename and Ghidra project/program names are historical artifact names;
their names do not establish retail version identity. The source capture date,
Loader/Core context and byte hashes in the manifest identify the evidence.

Active workspace script consumers, current manifest paths, cadastre entries and
navigation links use the new location. Derived contract, DataTables candidate,
sequence and research views are regenerated from their owning sources. The
existing atlas layout headers and output manifest receive a path/digest-only
rebase. During delivery, the missing atlas evidence was recovered from its
hash-verified published snapshot, allowing the guarded checks to pass. Historical command
transcripts and observation records are preserved rather than rewritten to
claim that a new command/path was used at the time.

## Local artifacts and rollback

The whole local cache moved on the same volume. The captured image bytes,
function/xref data and accumulated Ghidra analysis are reused; this migration
does not redump the game, rebuild its native index or run global disassembly.
Cached knowledge source paths and textual Python activation paths are rehomed.

Pre-edit source bytes, concurrent dirty changes, artifact hashes and the original
index are retained locally under `analysis-cache/workbench-migration-20261119/`.
That local receipt supports a targeted rollback: restore only migration edits
and move the directory back after verifying both resolved paths and any later
work. Do not reset the shared checkout or discard unrelated changes.

The manifest retains historical version/build fields for existing metadata
consumers, explicitly marked as compatibility metadata. Corpus identity and
artifact fingerprints govern native access. No plugin, mod data, installation,
gameplay, save or multiplayer qualification is changed by this migration.

## Verification receipt

On 2026-11-19, `re:native -- status`, doctor, topic/known lookup, a bounded
function query and its xrefs passed from the new directory. Both legacy command
aliases and the old explicit workbench selector reached that same directory.
Ghidra opened the moved project and reported its 696 persisted functions.

All 47 targeted tests passed: 28 native CLI/argument-contract/DataTables-candidate/
player-sequence Node tests and 19 topic/research-extraction Python tests. The
contract and both imported research catalogues passed freshness checks; the
DataTables candidate extractor reproduced its output. The exact pinned D2MOO
reference was restored into the ignored reference cache from the existing local
clone, enabling player-sequence regeneration without changing its pin.

Independent comparison verified all three captured image artifacts and all
1,224,090 native index rows. The only index value change is the intended textual
source-path substitution. The imported original, original runtime layout, TSV
records and retained licenses/notices remain byte-identical. Sixty-five files
selected for historical/reference preservation are unchanged. Concurrent registry
and package changes were preserved, and the Git staging index was untouched.

Delivery follow-up:

- The missing ItemStatCost evidence was recovered byte-for-byte from the
  published snapshot; [its restoration record](d2r/isc12/README.md)
  gives source commits and hashes. The atlas validator now passes with seven
  proven triplets; all four generated outputs pass byte-exact checks. Header
  layout bodies are unchanged; only catalogue digest comments and manifest
  path/hash bookkeeping are rebased.
- The historical player-sequence compatibility audit lacks its old cached
  Suite/PluginPack manifest inputs outside this workbench. The first missing
  input is `analysis-cache/ruffneckk-d2rloader-suite/manifests/native-writes-3.2.92777.json`.
  Its prerequisites and acceptance rules are unchanged; that audit has not passed.

These limits do not prevent use of the relocated native queries or the imported
research catalogues. No runtime action was performed. The subsequent explicit
request to commit and push both repositories authorizes targeted laboratory
delivery and public research refresh, excluding unrelated working changes.
