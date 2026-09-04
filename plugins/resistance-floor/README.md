# Resistance Floor 1.0.2

Lets configured units fall below D2R's vanilla `-100` resistance floor.

## Install

Copy the DLL and TOML to either the global or mod-local D2RLoader folders. Do
not install the same plugin in both scopes.

```text
d2rloader/plugins/d2rl-ruffneckk-resistance-floor.dll
d2rloader/config/ruffneckk-resistance-floor.toml
```

The same layout is valid below `mods/<mod>/d2rloader/` for a mod-local install.
A cold restart is required after changing the TOML or replacing the DLL.

## Behavior

The default configuration lets resistance fall as low as `-1000` for:

- players;
- hirelings, summons, pets, Revives, converted monsters and other units whose
  bounded owner chain reaches a player;
- Physical, Magic, Fire, Lightning, Cold and Poison resistance.

Ordinary monsters keep the vanilla `-100` minimum by default. Set
`monsters.enabled = true` if their resistance should also be allowed to fall
as low as the value in `monsters.minimum_resistance`.

The four native Character Screen values—Fire, Lightning, Cold and Poison—use
the configured player floor. Physical and Magic resistance remain covered by
the gameplay change, but D2R has no native Character Screen values for them and
this plugin does not add a custom display.

At `-1000`, a matching damage type can reach eleven times its pre-resistance
damage. This is an intentional consequence of the configured floor.

Use the `resistance-floor` D2RLoader console command to inspect the active
installation, minimum values, Character Screen support and optional usage
counters.

## Configuration

The TOML groups settings by the characters they affect. Every group can be
enabled independently and accepts a whole-number `minimum_resistance` from
`-1000` through `-100`:

```toml
[players]
enabled = true
minimum_resistance = -1000

[companions]
enabled = true
minimum_resistance = -1000

[monsters]
enabled = false
minimum_resistance = -1000
```

`companions` covers your hirelings, summons, pets, Revives, converted monsters
and other creatures that belong to a player.

Character Screen options are grouped separately:

```toml
[character_screen]
show_resistances_below_minus_100 = true
```

Version 1.0.2 uses `config_version = 3`. Replace an earlier candidate TOML
instead of mixing removed settings with this format.

Unknown keys, missing required settings and out-of-range values make the plugin
refuse activation instead of guessing. Existing upper resistance caps remain
owned by D2R or the active compatible cap plugin; Resistance Floor changes only
the lower bound.

## Compatibility

- Runtime identities are diagnostic only; the DLL loads from its complete
  native fingerprint rather than a build or channel allowlist.
- Battle.net qualification of the exact 1.0.2 artifact is pending.
- Steam 3.3.93787 remains pending until the exact 1.0.2 artifact passes the
  complete Suite qualification.
- Loader: governed D2RLoader/PluginSDK v3 baseline.
- Scope: global or mod-local; the plugin is not mod-scoped-only.
- Ownership: two independently signed lower-clamp sites and the native local
  Character Screen floor operand.
- Coexistence: does not modify any eezstreet DLL or the upper-cap operands used
  by PluginPack.

## Credits

- Author: `RuffnecKk`.
- D2MOO is credited as a semantic reference for Diablo II unit, minion-owner
  and resistance behavior. No D2MOO address, structure layout or 32-bit ABI is
  reused by this D2R plugin.
