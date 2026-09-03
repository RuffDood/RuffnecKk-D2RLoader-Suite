# Armageddon-Hurricane CtC Fix 1.0.0

Lets Armageddon and Hurricane start correctly when triggered by native
chance-to-cast item effects.

## What it fixes

Diablo II: Resurrected normally fails to start these two persistent Druid
skills from the native item-effect path when the character or monster does not
already own the skill. Armageddon can also hit an internal assertion when its
`ItemEffect` field is blank.

This plugin supplies the missing native context only for Armageddon (skill 249)
and Hurricane (skill 250). Native duration, states, missiles, targeting and
damage remain in control of the game and the active mod's data.

## Install

Install the two files in one D2RLoader scope only.

Global installation:

```text
<D2R>/d2rloader/plugins/d2rl-ruffneckk-armageddon-hurricane-ctc-fix.dll
<D2R>/d2rloader/config/ruffneckk-armageddon-hurricane-ctc-fix.toml
```

Mod-local installation:

```text
<D2R>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-armageddon-hurricane-ctc-fix.dll
<D2R>/mods/<mod>/d2rloader/config/ruffneckk-armageddon-hurricane-ctc-fix.toml
```

Do not install the DLL in both scopes at once. The plugin refuses a duplicate
global/mod-local installation instead of installing the same hooks twice.

## Configure

The supplied TOML enables both fixes by default. Set either skill to `false` if
you want to disable only that correction. Set `enabled = false` to load the
plugin without activating its hooks.

`diagnostics.enabled` adds bounded activation details to the D2RLoader log.
The `armageddon-ctc-fix` console command always reports the loaded config and
runtime counters.

## Behavior and compatibility

- Ordinary casts and every other chance-to-cast skill stay on their native
  paths.
- Armageddon follows its caster and expires according to `skills.txt`; the
  plugin does not make it permanent.
- No table replacement, save migration or proprietary save data is required.
- The native fingerprint is validated before the first hook. A mismatch fails
  closed; version names are diagnostic and are not used as an allowlist.
- Runtime-tested on D2R 3.3.93847 with a mod-local, offline installation.
- D2R 3.2.92777 is covered by the governed byte-identical native surfaces; a
  duplicate runtime matrix was not required.
- The global layout is supported by the same DLL but was not runtime-tested for
  this release. Multiplayer was not qualified.

The previously tested 0.1.1 candidate DLL SHA-256 is
`FDA4D8905D60A5FDCDE12B734D4D272EE2FBDA7BF58E24C516A2A88CD5295F77`.

## Uninstall

Remove the DLL and TOML. No save or data migration is needed.

## Credits

Created by **RuffnecKk**.

Native behavior was understood with help from the D2MOO project at pinned
commit `19019806df7f3e877fa105b05395d1e3597e2316`. D2MOO was used as a semantic
reference; no 32-bit address or layout was transplanted.
