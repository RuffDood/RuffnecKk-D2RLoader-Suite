# Auto Pickup 3.0.1

Starts from atsay714's D2R-Pickit 0.10.1 at commit
`be89040f742666b6343c25811fc78b16f29ce468`, adding RuffnecKk potion belt-column
routing. Upstream item selection, Include/Exclude, category switches, walking,
post-cast item/gold pickup, retry scheduling and Auto Interact remain intact.

## Update

Adds compatibility with the second Reimagined Maps 0.12.5 DLL revision. Both revisions can report the same version and expose the same hook addresses. Complete code fingerprints select the correct ownership state while preserving previous supported Maps profiles and independent Core validation. Unknown, mixed and damaged evidence remains rejected.

## Installation and updates

1. Close Diablo II: Resurrected and back up the existing DLL and configuration.
2. Extract into one global or mod-local d2rloader directory. Keep one active copy of this plugin.
3. Replace the DLL. Preserve your existing configuration when updating; the packaged configuration supplies defaults for new installations.
4. Restart your normal profile and verify the affected features. To roll back, close the game and restore your previous DLL and settings backup.

Auto Pickup 3 uses the Pickit-based selection schema. Older RuffnecKk 2.x TOMLs must be backed up and translated. Do not also load upstream Pickit, legacy Potion Auto Pickup or standalone Teleport Auto Pickup; these overlap pickup ownership. Existing 3.0.0 settings remain valid.

Use Ctrl+P for item selection and Potion routing. Healing, mana and rejuvenation families support ordered belt columns and optional inventory overflow. Walking, post-cast item/gold pickup, retry scheduling and Auto Interact are retained.

## Planned archive contents

- plugins/d2rl-ruffneckk-auto-pickup.dll
- config/ruffneckk-auto-pickup.toml

## Qualification

This source implements the Auto Pickup 3.0.1 release candidate. Final artifact hashes and qualification belong to the adjacent Hub package manifest. Offline checks do not establish gameplay, persistence or multiplayer behavior.

## Credits

Author: RuffnecKk. Existing SDK, dependency, historical reference and third-party notices remain in the public source.

Based on Andrew Tsay (atsay714) D2R-Pickit 0.10.1, commit be89040f742666b6343c25811fc78b16f29ce468, MIT. Copyright notices are preserved in the source and embedded in the DLL.

Prepared for manual review. The README, release notes and manifest stay beside the ZIP and are not included in the agent-generated archive. No upload has been performed.

## Reimagined 3.1.1 bundle r15

Version 3.0.1 also admits the exact bundle-r15 Maps 0.12.5 reader through a complete additional profile. Earlier supported profiles, independent Core/ownership checks and existing features remain intact. Offline qualification is distinct from startup, gameplay, saves and multiplayer.
