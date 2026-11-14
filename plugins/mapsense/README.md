# RuffnecKk MapSense

An independently versioned RuffnecKk D2RLoader plugin with global or mod-local installation. Preserve existing compatible configuration when updating.

## Public Suite build

This directory is the source for the public MapSense DLL and release configuration.
The private Hoardling customization lives as a patch in the Diablo workspace
under `plugin-dev/mapsense/reimagined-local/` and has its own build output.
The Suite packager checks the public plugin identity before accepting its DLL.

## Reimagined 3.1.1 R27

MapSense recognizes the exact Maps stat reader and Pyre automap callback shipped
in the [public Reimagined 3.1.1 R27 archive](https://github.com/D2R-Reimagined/d2r-reimagined-mod/releases/tag/3.1.1).
The existing hook ownership and trampoline checks still apply. To run the two
offline compatibility tests, set `RUFFNECKK_MAPSENSE_REIMAGINED_R27_PLUGINS`
to the extracted archive's `mods/Reimagined/d2rloader/plugins` directory.

## Offline map seeds

MapSense includes MadMike's Map Seed Selector. Disable the standalone
`d2rl-map-seed-selector.dll` when using the integrated picker. Keep the existing
`map-seed-selector.toml` beside `ruffneckk-mapsense.toml` in the active config
scope to retain saved seeds, the selected seed and UI placement. MapSense
creates a seedless config if the file is missing.

Choose a saved seed or add a decimal or hexadecimal seed on the Offline
character selection screen. **Off** uses the game's character map behavior.
Keep D2RLoader's **Generate new map layouts** disabled for static seeds.
The picker is gated to the Offline route. MapSense builds its atlas from the
active game's seed.

The picker is based on [Map Seed Selector v0.1.54](https://github.com/MadMike616/d2rl-map-seed-selector/tree/68b0802d0f7e6e1cd782d2874dfce25272fa9b98)
and retains [MadMike's MIT license](src/seed_selector/LICENSE).

## Reimagined 3.1.1 bundle r15

Version 2.2.6 also admits the exact bundle-r15 Maps 0.12.5 reader through a complete additional profile. Earlier supported profiles, independent Core/ownership checks and existing features remain intact. Offline qualification is distinct from startup, gameplay, saves and multiplayer.

## Eastern Sun Resurrected compatibility (2.2.7)

Supports the exact CelestialRayOne Loot Filter 1.5.3 automap callback while
preserving its return value, plus the directly installed Max Life One 1.0.2
stat-reader hook. Complete code and ownership checks remain mandatory. The
existing Pyre and Reimagined Maps profiles remain available.

The map-generation companion now leaves preset levels without a matching
LvlPrest definition empty instead of crashing generation of the whole act.
This fixes the ESR Act V reveal crash caused by its undefined level 137.
Update the helper executable together with the DLL and preserve existing settings.

ESR tester confirmation covers navigation, monster markers and the reveal fix.
Offline fixtures cover both loot filters separately and damaged-hook rejection;
loading both filters together, persistence and multiplayer are not qualified.

## Reimagined Bundle 16 compatibility

Version 2.2.8 admits the exact Reimagined 3.1.1 Bundle 16 Maps provider while retaining all earlier supported profiles and fail-closed checks. Also supports Pyre Loot Filter revision 5 without replacing the native hook; retains the ESR and Act V fixes from 2.2.7. Private merged-installation startup evidence does not qualify these newly versioned Hub artifacts for gameplay, persistence or multiplayer.
