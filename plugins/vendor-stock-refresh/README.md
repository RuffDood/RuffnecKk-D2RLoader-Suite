# Vendor Stock Refresh 2.1.6 - gold height correction

The coin and gold amount move down together by 12 native UI units in the approved footer. Their horizontal positions, gap, font, dimensions and formatting remain unchanged. The same correction applies to the recognized revision-3 loose-panel anchor; custom anchors are preserved. Gambling restores the original positions.

Build ID `VSR-GOLD-216-2` replaces the unsuccessful dynamic-centering candidate. The extra text-measurement/draw hook has been removed. Seven offline tests pass. The revised DLL passed an authorized in-game visual check on D2R 3.3.93847 with D2RLoader 1.3.1-beta; the user confirmed the corrected height. It uses the unchanged corrected 2.1.5 candidate-2 MPQ. Gold height is visually confirmed; this does not add controller or multiplayer qualification. Nothing has been published.

## Previous 2.1.5 candidate and evidence

# Vendor Stock Refresh 2.1.5 - fix candidate

Current source builds 2.1.5. The refresh button targets the native repair-button size and reduces it only when measured free space requires it. Artwork compatibility checks remain intact. The companion packer now preserves the forward-slash virtual file names required by the resource lookup; a DLL + MPQ-only cold start on D2R 3.3.93847 / D2RLoader 1.3.1-beta loaded both backgrounds successfully, and the user confirmed the frame and button appearance. Replace both DLL and MPQ together. No loose data folder is required. The bounded diagnostic build identifies itself as `VSR-FIX-215-2`. Seven offline tests pass; remote custom layouts, controller, gambling, and click behavior still require their own checks.

Install globally under `<Game>/d2rloader/plugins/` or mod-locally under `<Game>/mods/<ModName>/d2rloader/plugins/`, with the packed MPQ beside its DLL. Preserve the existing configuration and layouts. Build with `VENDOR_LAYOUT_DIAGNOSTIC=ON` for the bounded diagnostic capture and seventh regression test.

## Historical 2.1.4 candidate instructions and evidence

# Vendor Stock Refresh 2.1.4 - companion MPQ candidate

This local candidate combines the DLL and panel artwork in one download. It reuses the approved revision-3 stone-and-gold footer and keeps the native refresh button centered at the same 116x116 size as the repair buttons. The DLL selects the companion artwork through the existing native background widgets; no stock vendor layout files are installed.

## Installation

Close the game and back up the existing plugin. Choose one installation location:

### Global installation

Extract the ZIP into `<Game>/d2rloader/`. The resulting files are:

```text
<Game>/d2rloader/plugins/d2rl-ruffneckk-vendor-stock-refresh.dll
<Game>/d2rloader/plugins/d2rl-ruffneckk-vendor-stock-refresh.mpq
```

### Mod-local installation

To load the plugin only with a particular mod, extract the ZIP into
`<Game>/mods/<ModName>/d2rloader/`. Create that directory if necessary. The resulting files are:

```text
<Game>/mods/<ModName>/d2rloader/plugins/d2rl-ruffneckk-vendor-stock-refresh.dll
<Game>/mods/<ModName>/d2rloader/plugins/d2rl-ruffneckk-vendor-stock-refresh.mpq
```

Replace `<ModName>` with your mod's folder name and launch that mod through
D2RLoader. The mod's `d2rloader/` directory sits beside `<ModName>.mpq`, not inside it.
The companion `.mpq` remains a packed file beside the DLL; do not extract its contents.

Keep the two files together in the same plugin folder. Choose global or mod-local
installation, rather than installing a duplicate pair in both locations. Preserve
the existing TOML configuration; D2RLoader creates the embedded default when
configuration is absent. A missing or mismatched MPQ causes the enabled plugin to
refuse loading before installing hooks.

The MPQ contains four namespaced sprites: shop and repair-shop backgrounds, each in HD and low-quality form. It contains no replacement stock JSON layouts. The framed panel targets the standard keyboard/mouse HD vendor layout. Controller, classic graphics and custom layouts retain the compact fallback. Gambling restores the original background and native gold/button positions.

## Upgrading from the separate 2.1.3 panel ZIP

The previous layout remains supported, but it is no longer required. To test the companion on a clean layout, restore the two vendor layout files from the backups made before installing the old panel ZIP. Preserve any unrelated mod customizations; do not blindly delete a mod's vendor layouts. The two old `panel/vendors/ruffneckk/vendorrefreshframe` sprites can be removed only if they belong to that prior installation.

## Validation status

This is an offline candidate, not a published or game-qualified release. Release x64 builds and all six CTests pass. Tests of the compiled DLL cover missing/corrupted companion files, acceptance of the matching file and rejection when the resource service is unavailable. Every MPQ texture was reopened and SHA-256 verified with StormLib, a second build produced an identical MPQ, and both ZIP entries were reopened and hash-verified.

The 2.1.4 loading path still needs an in-game test with no loose vendor-layout overrides: appearance, refresh click, gambling transitions, controller, and texture-quality changes. The earlier visual approval applies to 2.1.3 revision 3 only. Target Loader integration is 1.3.1-beta ResourceService V1; this candidate does not expand previously established platform or multiplayer coverage.

The existing 2.1.3 revision-3 release remains available as rollback. No runtime installation or publication was performed for this candidate.

ZIP SHA-256: `7E1D3FFCB17FA48CA34CB104D40D839CDBC663D6E217008B62C85D4C22A0EAA9`.
DLL SHA-256: `583FCE9884243E60D8C7B2C2F663D7FF97117E2FE489BB3892F58A1D78DE70CD`.
MPQ SHA-256: `C06E2870F19275D14C31CC12870186FF6448E996D3B1885A018C059A0DDE23D5`.

## Building the companion

Use `tools/build-companion-assets.cjs <output-directory>` with Node.js, `sharp`, and `VENDOR_PANEL_REFERENCE_ROOT` pointing to a locally extracted native `hd/global/ui/panel` directory. This composites the committed approved footer with the native shop and forge backgrounds while preserving protected native pixels.

Build the Unicode shared StormLib v9.31 tooling from upstream commit `2aa6216c65ce68b88c46c0e4f12a38dc94c36b7a`, then run:

```text
python tools/pack-companion.py <StormLib.dll> <asset-directory> <output.mpq> src/companion_hash.hpp
```

The output must not already exist. The tool packs and verifies all four resources and emits the expected hash for the DLL. Rebuild the DLL after generating that header. StormLib is not shipped to players. Configure CMake with `VENDOR_COMPANION_TEST_MPQ` pointing to the matching MPQ to enable the sixth, compiled-DLL companion test. The first five tests remain available without an MPQ build input.

## Previous 2.1.3 release and qualification history

#### Vendor Stock Refresh 2.1.3

Refresh normal vendor stock without leaving the Trade screen. This update places
its native refresh button in a centered stone-and-gold frame below the gold display. Panel revision 3 preserves the original grid corners and outer frame around a continuous footer and keeps all three native buttons at 116 by 116 pixels, without a duplicate gold border.
Gambling keeps the original refresh behavior.

### Panel download

**[Download the 2.1.3 panel-assets ZIP](https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite/releases/download/vendor-stock-refresh-panel-v2.1.3-r3/RuffnecKk-vendor-stock-refresh-panel-v2.1.3-r3.zip)**

Use this artwork with Vendor Stock Refresh **2.1.3** from D2RLoader Hub.
The panel download contains the two vendor layouts and HD/low-quality sprites;
it does not contain the plugin DLL. Without these assets, the DLL uses its compact
button fallback.

1. Close the game and back up the vendor layouts you are replacing.
2. Extract the ZIP into `<Game>/mods/<Mod>/<Mod>.mpq/`, preserving its `data/` folder.
3. Install the 2.1.3 plugin in the global or selected mod's `d2rloader/plugins/`
   folder. Preserve your existing `ruffneckk-vendor-stock-refresh.toml`.
4. Launch that mod through D2RLoader and open a vendor's Trade panel.

Mods with customized vendor layouts must merge these layout changes rather than
blindly replace their existing layouts. Revision 3 passed asset, installed-file, startup and in-game appearance/alignment checks.
The full framed layout targets keyboard/mouse; controller mode uses the compact
fallback. The frame is hidden and native gold/button positions restored in gambling.

Panel ZIP SHA-256:
`90C4FDC2C441F2EDBC74B0A8927CD25E9E9167D213A3A3A2B79ED1D890D299BB`.

### Compatibility and verification

The exact tested combination is D2RLoader **1.3.1-beta** with Battle.net D2R
**3.3.93847**. Release build, five automated tests, installed hashes and plugin
initialization passed; D2R completed startup 24/24. The framed appearance and alignment were confirmed in game.
Hover/click behavior, gambling transitions, controller input and texture-quality
switching still require recorded in-game validation. Persistence, multiplayer,
Steam and CrossOver are not qualified for this update.

The test stack reported independent load failures for Advanced Item Tooltips
(mod-local), Doll Explosion and Scripted AI. This is not full-stack qualification.

### Source and assets

This directory contains the 2.1.3 source, tests, layout inputs, generated artwork
and runtime sprites. Build through the Suite CMake project. The component registers
five tests with names beginning `ruffneckk.vendor-stock-refresh`.

`node tools/build-layouts.cjs` regenerates the layouts. The artwork encoder
`tools/build-panel-assets.cjs` requires Node.js and `sharp`; the committed sprite
files do not require regeneration to build the DLL. Set `VENDOR_PANEL_REFERENCE_ROOT`
to a locally extracted native `hd/global/ui/panel` directory only when generating
preview composites. Native background and button sprites are not bundled here.

To roll back, close the game, restore the previous DLL and vendor layouts, and
restore or remove the two namespaced frame sprites according to your backup.
Preserve your configuration and unrelated mod files.
