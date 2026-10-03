# D2R research snapshot

This directory publishes maintained research from the RuffnecKk Diablo laboratory.
The 28 September 2026 snapshot includes the source working copy, including its
uncommitted research. It is documentation and reference material, not a release
qualification or a portable copy of the laboratory tools.

- [Native research](d2r/README.md): known addresses, findings and the DataTables atlas.
- [Argument contracts](d2r/argument-contracts.md) and [generated catalogue](d2r/argument-contracts.generated.md).
- [Skill-cost reconstruction](d2r/modules/README.md).
- [Imported RVA reference and provenance](d2r/references/rva-93847-intake.md).
- [SDK contribution research](d2r/sdk-contribution/README.md).
- [Player-sequence data](d2r/player-sequences/d2r-3.3.93847-player-sequences.manifest.json).
- [Loader audit](d2rloader-1.3-public-audit.md) and [historical baseline registry](d2rloader-baselines.json).
- [Source paths and SHA-256 hashes](source-snapshot.json).

Original notes, attribution, historical build identifiers and uncertainty labels
are preserved. The argument catalogue was regenerated from the copied registry
because the laboratory's generated view was stale; its source and published
hashes are recorded separately in the snapshot manifest.

Laboratory commands and paths in [LABORATORY.md](LABORATORY.md),
[WORKBENCH.md](d2r/WORKBENCH.md), and copied documents retain their original
context. References to `reverse-engineering/d2r-3.2.92777/` correspond to
`research/d2r/` here. Other laboratory paths, private caches, executable images,
runtime captures and supporting tools are not supplied by this documentation
snapshot. Local path strings identify provenance, not portable installation paths.

Static findings and historical runtime observations prove only their documented
scope. Unverified references remain unverified; neither addresses nor baseline
labels establish compatibility with another game or Loader build.
