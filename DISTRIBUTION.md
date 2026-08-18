# Distribution model

The RuffnecKk D2RLoader Suite is published from one repository as a modular
catalog. The repository keeps plugin sources under `plugins/` and memory patch
sources under `patches/`.

## GitHub release assets

Every Suite release publishes:

- one ZIP for each of the 17 plugins;
- each of the 19 memory patch JSON files as an individual download;
- an optional `All Plugins` ZIP;
- an optional `All Patches` ZIP;
- `SHA256SUMS.txt` for all generated download assets.

Individual downloads are the primary distribution. The two bundles are only a
convenience for players who intentionally want a larger selection.

GitHub release assets are displayed as one flat list. Stable filename prefixes
and the release notes separate plugins from patches; repository folders do not
become folders on the Releases page.

## Version contract

Each plugin keeps its own semantic version. A Suite version identifies one
locked catalog of component versions and SHA-256 hashes that was qualified
together. Players may install a subset without installing the full catalog.

Replacing one plugin does not silently replace another plugin, a memory patch,
or an existing player configuration. An individual update is supported only
when the release compatibility information explicitly confirms it.

## Archive contents

Each plugin ZIP uses canonical install-relative paths and contains only that
plugin DLL plus its required loose configuration or optional assets. Embedded
TOML files remain embedded in their DLL and are created by D2RLoader only when
missing.

Memory patches remain plain JSON downloads so a player can place only the
selected files in `d2rloader/patches/`.

Generated ZIP files never include README files. Before a public release,
`README.md` and any component-specific installation guide are reviewed beside
the generated assets and then added or linked by Vincent during publication.

## Release gate

The release manifest is the allowlist. Packaging must fail when a component is
missing, has an unexpected version or SHA-256, uses a forbidden legacy name, or
would produce an unexpected GitHub asset. The complete Suite lock remains the
reference used for reproducible builds, runtime qualification, rollback, and
compatibility reporting.

PowerShell 7 is the canonical packaging runtime. Two clean publication runs
must produce byte-identical assets before their hashes are accepted. Windows
PowerShell 5.1 remains a supported policy check, but its ZIP implementation is
not used to establish the authoritative release-asset hashes.
