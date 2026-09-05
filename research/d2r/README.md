# D2R research notes

Public research by RuffnecKk, shared to help D2R modders/devs reuse documented discoveries.
This is a documentation snapshot maintained from the Diablo laboratory.

## Start here

- [Known RVAs](known-rvas.json): native addresses, identified roles, confidence levels, provenance and limits. The snapshot contains 864 entries: 849 marked `high` and 15 marked `medium`.
- [Findings](findings.md): detailed research conclusions, rejected hypotheses, ABI pitfalls and follow-up questions. The original French notes are preserved.
- [DataTables atlas](datatables-atlas/README.md): seven documented compiled-table layouts, evidence records and partial C++/Ghidra headers. Read the laboratory-reference notes below before using its commands.

Within the atlas, [catalog.json](datatables-atlas/catalog.json) records admitted claims, [candidates.json](datatables-atlas/candidates.json) keeps static candidates separate, and [atlas.schema.json](datatables-atlas/atlas.schema.json) describes the data format. The generated [C++ header](datatables-atlas/generated/d2r33_datatables_atlas.hpp) and [Ghidra C header](datatables-atlas/generated/d2r33_datatables_ghidra.h) are partial reference views, not a complete SDK or ABI.

## Coverage and interpretation

The historical source directory and some document titles use `3.2.92777`. They identify the origin of the native corpus. The governed atlas documents byte-exact coverage of `92777` and Battle.net `3.3.93847`; consult individual claims and their evidence for the scope of that coverage. Steam `3.3.93787` is an admissible candidate, not qualified by this snapshot. No claim of compatibility with a future build is implied.

A `high` confidence entry proves the specific statement in its notes. It does not automatically prove a complete function ABI, structure, safe hook location or multiplayer behavior. `candidate`, `medium` and unknown fields retain their stated limits. Before using native addresses, validate the actual bytes, layouts, ABI and ranges your code depends on; a build number alone is not a compatibility check.

## Credits

Research and documentation: **RuffnecKk**. Original third-party citations and credits remain in the notes. **D2MOO** provided semantic references for Diablo II 1.10f; its 32-bit addresses, structures and ABI must not be transplanted into D2R. References to D2RLoader, PluginSDK and other contributors retain their individual provenance. See the Suite [license](../../LICENSE) and [third-party notices](../../THIRD_PARTY_NOTICES.md).
