# D2R research snapshot

This directory publishes maintained research from the RuffnecKk Diablo laboratory.
The snapshot manifest records the laboratory source commit, working-copy state,
and file hashes. Some entries retain their original snapshot and provenance.
The published files document findings and their evidence limits. Runtime
qualification and laboratory tools are tracked separately.

- [Native corpus identity](NATIVE-CORPUS.md).
- [Native research](d2r/README.md): known addresses, findings and the DataTables atlas.
- [Argument contracts](d2r/argument-contracts.md) and [generated catalogue](d2r/argument-contracts.generated.md).
- [Skill-cost reconstruction](d2r/modules/README.md).
- [Imported RVA reference and provenance](d2r/references/rva-93847-intake.md).
- [Player-sequence data](d2r/player-sequences/d2r-3.3.93847-player-sequences.manifest.json).
- [Loader audit](d2rloader-1.3-public-audit.md) and [historical baseline registry](d2rloader-baselines.json).
- [Source paths and SHA-256 hashes](source-snapshot.json).

Original notes, attribution, historical build identifiers and uncertainty labels
are preserved. The argument catalogue and imported-reference overlap inventory were regenerated
from the published registry. The snapshot manifest records the transformations.

Laboratory commands and paths in [LABORATORY.md](LABORATORY.md),
[WORKBENCH.md](d2r/WORKBENCH.md), and copied documents retain their original
context. References to `reverse-engineering/d2rloader-game-image/` correspond to
`research/d2r/` here. Other laboratory paths, private caches, executable images,
runtime captures and supporting tools are not supplied by this documentation
snapshot. Local path strings identify provenance, not portable installation paths.

Static findings and historical runtime observations prove only their documented
scope. Unverified references remain unverified; neither addresses nor baseline
labels establish compatibility with another game or Loader build.

## Current laboratory snapshot

This refresh comes from committed laboratory source [`66fbc2d1`](https://github.com/BRODIABLO/Diablo/commit/66fbc2d175191c52757914cbe07e0bfcaba9fa7a). Unrelated laboratory working changes are excluded.

- [Migration record](WORKBENCH-MIGRATION.md) and current `re:native` laboratory commands.
- [CelestialRayOne intake](d2r/references/celestialrayone-intake.md) and [PD2 Resurrected intake](d2r/references/pd2-resurrected-intake.md), with original licenses, attribution, source revisions and unresolved claims.

Only documented native research is published here. Private executable images, caches, Ghidra projects, game captures and plugin implementations are excluded. Laboratory commands require the laboratory checkout and its declared inputs.
