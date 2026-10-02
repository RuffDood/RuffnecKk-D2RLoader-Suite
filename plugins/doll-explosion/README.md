# Doll Explosion

Doll Explosion delays undead Doll death blasts and makes their physical damage
configurable. The default damage ranges, delay and radius follow Project Diablo 2
(PD2) Season 13's damage model.

## Defaults

The default targets are the retail D2R undead Doll variants `bonefetish1` through
`bonefetish7`: MonStats IDs **212, 213, 214, 215, 216, 690 and 691**. Mods that use
different monster IDs can adjust `targets.monstats_ids` in the TOML.

| Difficulty | Physical damage | Delay | Radius |
| --- | ---: | ---: | ---: |
| Normal | 18–30 | 25 frames (1 second) | 4 native tiles |
| Nightmare | 54–96 | 25 frames (1 second) | 4 native tiles |
| Hell | 318–540 | 25 frames (1 second) | 4 native tiles |

The default `fixed` formula rolls within the selected difficulty's range.
Alternatively, `source_max_life_percent` rolls an integer percentage of the dying
Doll's maximum life. Its configured ranges are used only when that formula is
selected. Set `delay_frames = 0` for an immediate blast.

## Configuration and installation

Edit [ruffneckk-doll-explosion.toml](config/ruffneckk-doll-explosion.toml).
The DLL's presence enables the plugin. The console command `doll-explosion`
reports the effective configuration and explosion counters.

The plugin uses the first existing configuration in this order:

1. The active mod's `d2rloader/config` directory.
2. The DLL's current load-scope configuration directory.
3. The global game's `d2rloader/config` directory.

An absent configuration uses the embedded defaults and creates the TOML in the
current scope when possible. Invalid configurations refuse loading. Unknown keys,
duplicate monster IDs and unsafe bounds are errors.

Install `DollExplosion.dll` globally in `<D2R>/d2rloader/plugins/` or for one mod
in `<D2R>/mods/<mod>/d2rloader/plugins/`. Install only one copy. Existing DLL and
configuration names are preserved in this source integration.

## Source and validation status

This is **0.1.6 source integration**, with strict native fingerprints and shared
native stat-provider admission. It is not an individual Hub release or a new
Suite download. The build remains ineligible for public archives.

Version 0.1.6 fixes an assertion in the delayed visual constructor by resolving
the original Doll by GUID and class at expiry. Missing, changed or revived owners
safely suppress the event. The carrier remains the damage source; the native
dispatcher retains missile removal ownership.

The laboratory source passed 19 paired and 5 standalone checks. Player-directed
debug gameplay recorded four scheduled and completed explosions with zero
failures and no captured assertion. Exact damage/radius calibration, Save & Exit,
multiplayer and broad coexistence remain unqualified. See the retained
[diagnosis and bounded gameplay evidence](https://github.com/BRODIABLO/Diablo/blob/fbf63c0c/plugin-dev/doll-explosion/notes/delayed-owner-20261002.md).

When Cast Triggers owns native damage cleanup, Doll requires a compatible cleanup
ABI provider. The tested pairing used the isolated Cast 1.1.13 cleanup-ON
candidate. The Suite's default Cast build does not expose that candidate ABI;
unsupported cleanup ownership fails closed.

Build this directory standalone or through the Suite root. It uses the Suite's
vendored PluginSDK and common adapter. Tests cover configuration, native witnesses
and the real delayed callback with native stubs. Set
`DOLL_EXPLOSION_NATIVE_IMAGE` to a governed executable image to include the native
image witness check.

## Credits and provenance

The PD2 defaults were derived from the pinned Season 13 data chain: the death-skill
property selects `DollMeteor`, its difficulty levels select the fixed damage
ranges, its calculation supplies radius 4, and `dollmeteorcenter` supplies a
25-frame lifetime. The delay is a data-and-semantics inference rather than a
qualification of PD2's proprietary runtime path. No PD2 code or assets are copied.

The source originated in `BRODIABLO/Diablo`, `addons/DollExplosion`, at commit
`fbf63c0c`, with the subsequently approved retail/PD2 TOML comment revision.
Suite integration changes the build paths, source-contract path checks and
fallback configuration comments; the gameplay fix and native witnesses are
preserved. Laboratory evidence remains in Diablo's `plugin-dev/doll-explosion`.
