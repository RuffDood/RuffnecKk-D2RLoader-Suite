# D2R reconstruction modules

These modules organize native behaviors by topic for research and future
plugins. The target runtime is D2R 3.3.93847; the historical 92777 directory
keeps the shared governed native corpus. Its name does not guarantee
compatibility with any other executable.

## Consult

```powershell
npm run re:d2r33 -- status
npm run re:d2r33 -- topic blood_mana
npm run re:d2r33 -- topic "coût en vie"
npm run re:d2r33 -- topic "skill cost"
```

`topic` searches titles, aliases, summaries, and function names. `known` also
shows matching modules before its usual native results. Addresses are resolved
from `known-rvas.json` for each query; discovering a module never requires
rebuilding the index.

## Documentation contract

Each topic has a JSON manifest and an adjacent Markdown document:

- `schemaVersion: 1`, `id`, `title`, `aliases`: identity and search terms;
- `status`: `partial-static` or `static-verified`, never runtime qualification;
- `summary`, `document`: a compact answer and the document's relative path;
- `symbols`: exact native-registry names, with no second authority for addresses;
- `limitations`: uncertainties that must accompany every result.

The document connects observed rules, explanatory pseudocode, functions, and
the commands that reproduce the evidence. It distinguishes native facts,
hypotheses, and historical references. Pseudocode is documentation, not a
usable ABI or a recompilable implementation.

`static-verified` means that the explicitly described scope is statically
demonstrated. It does not mean that every skill is covered, that the result was
tested in game, or that a future hook is compatible. An incomplete module stays
`partial-static`.

`known-rvas.json` remains authoritative for identifications. The atlas remains
authoritative for its accepted layouts. Raw outputs and decompilations remain in
the local cache; the versioned document keeps enough references and commands to
reproduce its conclusions without relying on a conversation summary.

## Extend

Start with a concrete question, consult the modules and existing
identifications, then follow the workspace reverse-engineering procedure. Use
D2MOO only as a semantic lead; every D2R claim must return to native evidence.
Promote stable identifications to the registry before referring to them from a
module. Do not globally decompile the engine merely to inflate coverage.

The first pilot is [skill payment](skill-costs.md). Its usefulness criterion is
recovering the native answer to `blood_mana` from the workbench. Any benefit for
a second plugin remains to be measured through actual reuse.
