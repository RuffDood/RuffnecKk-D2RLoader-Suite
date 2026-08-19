# Changelog

## [Unreleased]

### Compatibility

- Added explicit Diablo II: Resurrected 3.3.93847 support to all 17 plugins.
- Retained Diablo II: Resurrected 3.2.92777 support.
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

- Charm Aura Trigger Fix now preserves the game's canonical charm eligibility
  rules instead of reactivating charms that should remain inactive.

### Documentation

- Updated the requirements for D2R 3.2.92777 and 3.3.93847.
- Corrected the Floating Damage credits and third-party notices.
- Credited Fr4nsson and D2R Damage Numbers for the original feature design.
- Credited locbones / D2RHUD-2.4 as the direct D3D12/ImGui implementation
  source.
- Added the original D2R Damage Numbers MIT license.

## [1.0.0] - 2026-08-18

- Initial public release of the RuffnecKk D2RLoader Suite.
