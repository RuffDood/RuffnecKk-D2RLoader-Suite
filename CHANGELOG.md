# Changelog

## [1.2.0] - 2026-08-20

### Added

- Added Bulk Currency Deposit 1.0.0. It transfers supported stackable
  currency items from the player inventory to their assigned native or
  mod-specific stash slots. Its `Bulk Currency Deposit` action defaults
  to `Shift+D` in D2RLoader Controls, and its optional Inventory button and
  position are configured in TOML.

### Changed

- Release downloads now contain only game-ready DLL and configuration files;
  README files remain in the repository.
- Floating Damage 1.4.0 now registers `Toggle Floating Damage` in the native
  D2RLoader Controls menu with `Shift+Z` as its default binding. Its obsolete
  TOML hotkey, private Windows polling, mouse bindings, and static DPS binding
  hint were removed. Existing `[hotkey]` tables remain accepted as ignored
  migration data; bindings are now owned exclusively by D2RLoader Controls.

### Removed

- Retired Transmute Hotkey because D2RLoader now provides the native
  `cube_transmute` action in its Controls menu. Upgraders must manually remove
  `plugins/d2rl-ruffneckk-transmute-hotkey.dll` and
  `config/ruffneckk-transmute-hotkey.toml`, because extracting a new archive
  cannot delete files left by an older installation.

### Fixed

- Floating Damage 1.4.0 now composes through one D2RLoader-managed inline
  hook at `STATLIST_SetUnitStat`, including Poison Energy Shield 0.2.0,
  while still rejecting untracked, byte-patched, or multiply owned entries.
  The native-write and external-compatibility catalogs now pin this fourth
  composable call-through contract and reject owner or consumer drift.

## [1.1.0] - 2026-08-19

### Compatibility

- Added explicit D2R 3.3 support to all 17 plugin while retaining 3.2 support.
- If my plugins still loaded correctly on your setup after the update, you
 can ignore the updated files (except maybe for the specific
 Charm Aura trigger fix, look below)
- Updated every plugin to validate the actual running D2R build instead of
  relying on the mod's `DataVersionBuild` value.
- Verified the complete Suite on D2R 3.3, including all plugins and memory
  patches.

### Changed

- Every plugin download now includes its configuration file.
- TOML configurations no longer require an initial D2RLoader launch to be
  created. Automatic generation remains available as a fallback.
- Potion Auto Pickup now includes a practical example configuration: Health
  uses belt columns 1-2, Mana uses column 3, and Rejuvenation uses column 4.
- Floating Damage now exposes a versioned shared overlay API for compatible
  plugins.

### Fixed

- Charm Aura Trigger Fix now preserves the game's charm eligibility
  rules instead of reactivating charms that should remain inactive
  (for inventories with Charm Zones)

### Documentation

- Updated the requirements for D2R 3.2 and 3.3
- Corrected the Floating Damage credits and third-party notices.
- Credited Fr4nsson and D2R Damage Numbers for the original feature design.
- Credited locbones / D2RHUD-2.4 as the direct D3D12/ImGui implementation
  source.
- Added the original D2R Damage Numbers MIT license.

## [1.0.0] - 2026-08-18

- Initial public release of the RuffnecKk D2RLoader Suite.
