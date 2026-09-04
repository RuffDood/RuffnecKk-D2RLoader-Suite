# Changelog

## [Unreleased]

## [1.3.2] - 2026-09-04

This hotfix updates four plugins for D2RLoader 1.2.1. All other Suite 1.3.1
files remain unchanged.

### Fixed

- MapSense 1.0.1 now loads alongside Bind And Summon 1.4.4 and works with
  D2RLoader 1.2.1.
- Floating Damage 1.4.3 now works correctly when MapSense is installed. They
  can work together or separately.
- ISC12 1.0.2 now loads with D2RLoader 1.2.1.
- Vendor Stock Refresh 2.0.2 now works with D2RLoader 1.2.1.

### Important ISC12 notice

**Important : most users should not install ISC12.**

Install ISC12 only when a mod specifically requires it. Your existing
characters and shared stashes will not load while ISC12 is installed because
it uses a different save format.

ISC12 is intended only for mods that need more item-stat entries than the base
game supports (ItemStatCost IDs above 511). The mod author will tell you
whether ISC12 is required and whether your existing save files must be
converted.

**If you are unsure, do not install it.**

## [1.3.1] - 2026-09-03

This complete release includes 24 plugins, 17 patches, and two bundles.
It contains 7 new plugins and 1 new patch.

### Hotfix

- ISC12 1.0.1 and Vendor Stock Refresh 2.0.1 are now compatible with D2RLoader 1.2.1.
- Multiplayer setup checks now cover all 12 configurable Suite plugins.

### New plugins

- Cast Triggers 1.0.0.
- Armageddon-Hurricane CtC Fix 1.0.0.
- Resistance Floor 1.0.1.
- MapSense 1.0.0.
- Extended Act Level IDs 1.0.0.
- ISC12 1.0.1 with D2R Save Converter 1.0.0.
- Burn Damage Fix 1.0.1.

### New patch

- Shadow Master AI Fix.

### Updated

- Steam users on latest patch will now be able to load plugins.
- Remote Stash 2.3.1 fixes behavior inconsistencies and lets each active mod use its MPQ to automatically apply its own placement and sprites to the global Remote Stash button.
- Bulk Currency Deposit 1.1.2 fixes a tooltip conflict with D2RLoader's native Stat Ranges feature and updates its button sprite to better match the regular Inventory panel.
- Suite downloads now use paths supported by D2RMM Custom for D2RLoader.
- D2R Save Converter is included inside the ISC12 download.

### Removed

- Ground Item Label Limit 64 and 128 are no longer included because D2RLoader provides this feature natively.
- Normal Area Scaling is no longer included because Yinyin has a working patch and mine apparently didn't work.

## [1.3.0] - 2026-09-03

Suite 1.3.0 includes 24 plugins, 17 patches, and two bundles.

### New plugins

- Cast Triggers 1.0.0.
- Armageddon-Hurricane CtC Fix 1.0.0.
- Resistance Floor 1.0.0.
- MapSense 1.0.0.
- Extended Act Level IDs 1.0.0.
- ISC12 1.0.0 with D2R Save Converter 1.0.0.
- Burn Damage Fix 1.0.0.

### New patch

- Shadow Master AI Fix.

### Changed

- Suite plugins no longer reject Steam users running D2RLoader on the latest Steam patch solely because Steam uses a different D2R build number.
- Remote Stash 2.3.0 fixes behavior inconsistencies and lets each active mod use its MPQ to automatically apply its own placement and sprites to the global Remote Stash button.
- Vendor Stock Refresh 2.0.0 now works with D2RLoader 1.2.
- Bulk Currency Deposit 1.1.1 fixes a tooltip conflict with D2RLoader's native Stat Ranges feature and updates its button sprite to better match the regular Inventory panel.
- Suite downloads now use paths supported by D2RMM Custom for D2RLoader.
- D2R Save Converter now ships inside the ISC12 download.

### Fixed

- Fixed Hit Chance limits in game and on the Character Screen.

### Removed

- Removed both Ground Item Label Limit patches because D2RLoader 1.2 includes this feature.
- Normal Area Scaling is no longer distributed because Yinyin has a working
  patch and mine apparently didn't work.

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
