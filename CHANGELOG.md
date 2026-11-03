# Changelog

## Floating Damage 1.5.4 - 2026-11-02

- Fix startup refusal when Reimagined Maps redirects the stat reader.
- Validate the witnessed Maps 0.12.1, 0.12.4, and 0.12.5 reader and ownership state while retaining Core and independent setter checks.
- Preserve Maps' reader and stat modifiers; configuration and MapSense overlay sharing remain unchanged.

Release x64 and offline compatibility checks passed for the preceding private
candidate. Final artifact checks are recorded with the individual Hub package.
Startup, visible gameplay, persistence, and multiplayer remain unqualified.

## [1.4.2] - 2026-09-20

This is strictly a MapSense update. All other Suite components remain unchanged
from 1.4.1.

### MapSense 2.0.2

- Reveal Map now remembers your preference while GPS navigation remains available independently.
- Settings save automatically when changed through the in-game menu.
- Custom navigation destinations added manually to the TOML are preserved when settings are saved.
- The settings menu and all its sections start closed when entering a game.
- Added red navigation lines for the Moldy Tome in Stony Field, Radament in Sewers Level 3, Frozenstein in Frozen River, and Nihlathak in Halls of Vaught. Frozenstein replaces the Anya destination.
- Added clearer custom navigation examples using either an area ID or an English name. These instructions remain in the TOML after settings are saved.

After manually editing custom destinations, save the TOML and fully restart D2R
to load the updated list.

## [1.4.1] - 2026-09-19

- Improved compatibility with CelestialRay's Suite across the known plugin overlaps.
- Updated MapSense 2.0.1 with Izual navigation, clearer map labels, improved renderer startup and Engine Stability compatibility.
- Fixed Equipped Item to Cube 1.0.4 under D2RLoader 1.3.
- Fixed the Remote Stash 2.3.2 Inventory button under D2RLoader 1.3.
- Updated Armageddon-Hurricane CtC Fix, Bulk Currency Deposit, Burn Damage Fix, Cast Triggers, Ethereal Item Rules, Floating Damage, Larzuk Sockets, MassID, PlayerX Scaling Tweaks and Prevent Merc Death in Town for safer coexistence.
- Retired Charm Aura Trigger Fix in favor of the external CelestialRayOne Aura Reactivation plugin.
- Preserved existing TOML and JSON configuration files during updates.

## [1.4.0] - 2026-09-17

Suite 1.4 updates the following plugins for D2RLoader 1.3:

- Bulk Skill Point Allocation 1.3.5
- Burn Damage Fix 1.1.0
- Cast Triggers 1.1.0
- Charm Aura Trigger Fix 1.7.0
- Floating Damage 1.5.0
- Larzuk Sockets 1.1.0
- MassID 2.1.0
- Prevent Merc Death in Town 1.1.0
- Vendor Stock Refresh 2.1.0

### PlayerX Scaling Tweaks 1.0.0

Added expanded `/players` controls, including configurable command limits,
minimum difficulty, separate monster scaling caps, and optional NoDrop party
simulation. An optional Battle.net-style simulation mode can use connected
players for scaling.

### MapSense 2.0.0

MapSense now includes GPS route lines for Walk/Run mode and Teleport mode. GPS
lines automatically draw the fastest route to a destination and reconnect as
your character moves. This is especially helpful on closed maps with corridors,
doors, walls and other obstacles.

### Repair Costs Cap 1.4.5

Repair Costs Cap can now use separate limits for individual item repairs and
Repair All. Existing configurations remain compatible: the previous limit
continues to apply to both types of repairs until separate limits are configured.

### Four Character Item Codes

New patch: Four Character Item Codes allows modders to use four-character item
codes, expanding the previous three-character limit.

### No Gold Loss on Death

No more losing or dropping gold upon death.

### Removed

- ISC12 and its D2R Save Converter are no longer included because D2RLoader 1.3
  now handles their expanded item-stat and save features.
- Extended Act Level IDs is no longer included because it is now a native
  D2RLoader feature.
- Ground Item Label Limit 64 and Ground Item Label Limit 128 are no longer
  needed because D2RLoader now provides this feature.
- Normal Area Scaling is no longer distributed with the Suite. Yinyin's working
  alternative is recommended instead.
- Player Difficulty Overrides is no longer included because PlayerX Scaling
  Tweaks provides more player-count and monster-scaling controls.

### Configuration files

Do not overwrite existing configuration files. MapSense upgrades its existing
configuration automatically, and Repair Costs Cap continues to support its
previous single-limit setting. When installing PlayerX Scaling Tweaks for the
first time, copy its included configuration file; the two new optional patches
do not use configuration files. The other D2RLoader 1.3 updates require no
configuration changes.

## [1.3.3] - 2026-09-05

This release updates **MapSense to 1.0.2** and **Resistance Floor to 1.0.2**. All other plugins and patches retain their versions from Suite 1.3.2.

### MapSense 1.0.2

- Fixes map overlays disappearing when New Stats, New Skills or Quest Log notifications appear.
- Keeps overlays visible when an unrecognized UI state does not represent a blocking panel.
- Fixes vanilla map labels and objects without requiring `-txt`.
- Makes menu localization independent of the mod’s TXT files.
- Adapts label sizes and spacing to the current resolution, including changes made while playing.
- Improves Chinese, Japanese and Korean font selection and glyph coverage.
- Fixes a shutdown cleanup issue and reduces repeated startup warnings.
- Improves map generation portability and reveal accuracy for custom levels.

### Resistance Floor 1.0.2

- Removes the D2R build-number restriction. Loading now depends on validation of the native code and layouts used by the plugin.
- Preserves existing resistance-floor behavior and configuration.

### Installation

Replace the updated plugin files. For MapSense, keep its included map generator executable beside the DLL. Preserve your existing configuration files.

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
