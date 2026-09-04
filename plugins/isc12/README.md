# ISC12 1.0.2

**Important : most users should not install ISC12.**

Install ISC12 only when a mod specifically requires it. Your existing
characters and shared stashes will not load while ISC12 is installed because
it uses a different save format.

ISC12 is intended only for mods that need more item-stat entries than the base
game supports (ItemStatCost IDs above 511). The mod author will tell you
whether ISC12 is required and whether your existing save files must be
converted.

**If you are unsure, do not install it.**

## Overview

ISC12 extends `ItemStatCost.txt` to a maximum of 4,095 rows. It serializes
stat IDs `0..4094` with 12 bits and reserves `0xFFF` as the list terminator.

The plugin updates the native ItemStatCost compiler, player and item save
codecs, and the game packets that carry serialized item stats. External
character (`.d2s`) and shared-stash (`.d2i`) containers remain standard D2R
files; the stat-ID encoding inside them is different.

ISC12 does not add stats or modify a mod's data by itself. A mod must provide
the expanded `ItemStatCost.txt` and every related data change that uses the
additional IDs.

ISC12 is config-free. Installing the DLL activates it after its complete native
compatibility checks pass.

## Data contract for mod authors

- The table may contain at most 4,095 rows.
- Serializable stat IDs are `0..4094`.
- `0xFFF` is reserved as the 12-bit terminator and cannot be a real stat ID.
- `CsvBits` must not exceed 32.
- `CsvParamBits` must not exceed 16.
- Once saves exist, treat ItemStatCost row IDs and their serialization fields
  as persistent schema. Reordering or redefining them can reinterpret stored
  stats.
- Every client in a multiplayer game must use the same ISC12 version and
  compatible mod data.
- Do not combine ISC12 with another plugin that changes ItemStatCost save or
  full-item network serialization unless that exact combination has been
  validated by the mod author.

ISC12 keeps the existing D2R outer save containers and D2RLoader environment
sidecars. A `.d2rl` sidecar can warn about a different plugin or mod
environment, but it is not a cryptographic marker for the save schema. Do not
rely on it as the only protection against loading a save with the wrong setup.

## Save compatibility

Standard 9-bit saves and ISC12 12-bit saves are not interchangeable.

- Convert an existing standard character or shared stash before loading it
  while ISC12 is installed.
- Keep ISC12 installed when loading a 12-bit save.
- Convert a save back to the standard 9-bit format before removing ISC12.
- Back up all characters and shared stashes before installing, removing,
  updating or converting ISC12.

Forcing an incompatible save, or removing ISC12 from a save that depends on
12-bit stat IDs, is unsupported.

## D2R Save Converter

D2R Save Converter 1.0.0 is included in the ISC12 archive at:

```text
D2R Save Converter/D2RSaveConverter.exe
```

The Converter supports:

- character files (`.d2s`);
- shared-stash files (`.d2i`);
- standard 9-bit to ISC12 12-bit conversion;
- ISC12 12-bit to standard 9-bit conversion;
- compatible cross-mod migration without changing the bit width;
- combined format and mod migration.

The Converter writes each conversion to a new output folder and never
overwrites the source saves. Close Diablo II: Resurrected before running it.

For mod migration, select the source and target mod data requested by the
Converter. Loose TXT data, folder-based MPQs and supported binary MPQ archives
can be read. BIN-only data or an unknown custom DLL save format is rejected
instead of being guessed.

## Installation

Extract the release archive. It contains the ISC12 DLL and D2R Save Converter.

Install the DLL in exactly one D2RLoader scope.

Global installation:

```text
<D2R>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

Mod-local installation:

```text
<D2R>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

Do not install ISC12 in both scopes for the same game process. No configuration
file is required.

## Runtime safety

ISC12 does not decide compatibility from a D2R build name, distribution channel
or version number. Before its first native write, it validates the complete set
of code, layout and ABI surfaces that it uses. A mismatch refuses the plugin
without applying a partial patch.

If a native write produces an ambiguous result after mutation begins, ISC12
terminates the process rather than continue with a partially published codec.
Restart the game after correcting the installation or compatibility problem.

## Qualification scope

ISC12 1.0.2 passed a full-stack cold start on D2R 3.3.93847 with the public
D2RLoader 1.2.1 release. Its public 1.2.1 provider set, expanded ItemStatCost
schema and startup publication were accepted.

This compatibility-only hotfix did not rerun the broader gameplay, persistence
and TCP/IP matrices on the final 1.0.2 binary. Mod authors should validate
character creation, save/reload, shared stashes, high-ID item stats, host/joiner
play and mismatch handling with their exact mod data before release.

Steam D2R 3.3.93787 was not runtime-qualified for this release. Compatibility
still depends on native fingerprints rather than the Steam channel or build
number.

Detailed implementation evidence and historical qualification records remain in
`plugins/isc12/VALIDATION.md` in the source repository.

## Mod release checklist

Before telling players to install ISC12:

1. Confirm that the mod actually uses ItemStatCost IDs above 511.
2. Freeze the intended ItemStatCost row IDs and serialization fields.
3. Test a new character, save/reload and shared-stash round trip.
4. Exercise real items that use the lowest and highest extended stat IDs.
5. Test every supported multiplayer configuration with identical mod data.
6. Provide explicit conversion instructions for existing characters and shared
   stashes.
7. Tell players whether ISC12 is required and whether their saves must be
   converted.
8. Keep verified backups and a rollback path.

## Credits

- **RuffnecKk**: design, implementation, integration and testing.
- **D2MOO contributors**: semantic reference material for the historical
  ItemStatCost compiler and stat-list formats. Current x64 addresses,
  signatures and ABI contracts were proven independently.
- **D2RLoader and PluginSDK**: plugin runtime and services.
