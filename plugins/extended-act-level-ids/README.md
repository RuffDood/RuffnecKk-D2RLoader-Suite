# Extended Act Level IDs 1.0.0

Extended Act Level IDs allows new level rows to belong to any act through the
`Act` value in `levels.txt`.

D2R normally resolves an act from the contiguous ranges compiled from
`actinfo.txt`. As a result, a new Level ID appended after the Act V range is
treated as Act V even when its `levels.txt` row declares another act. This
plugin replaces that one decision with the authoritative compiled
`Levels.Act` value while preserving the original resolver as a fail-safe.

## Installation

Install the DLL in one scope only.

Global installation:

```text
<D2R>/d2rloader/plugins/d2rl-ruffneckk-extended-act-level-ids.dll
```

Mod-local installation:

```text
<D2R>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-extended-act-level-ids.dll
```

The plugin supports both scopes but refuses a duplicate global and mod-local
installation in the same process. It has no configuration file because it has
no modder-facing setting: the installed DLL is active, and removing it disables
the plugin. Restart D2RLoader after adding, removing, or replacing the DLL.

## Runtime contract

The plugin owns one central native hook. After each `DataTablesLoaded` event it
uses PluginSDK API v4 to copy the Classic, LoD, and RotW `Levels` tables into
plugin-owned immutable caches. It validates all of the following before using
them:

- the complete 48-byte native resolver fingerprint;
- the PluginSDK service versions;
- compiled `Levels` row size `0x18C`;
- the `Id` field through a service lookup round-trip for every row;
- the `Act` field at `+0x0D`, including the five vanilla act boundaries;
- every act value is between `0` and `4`.

An unsupported data context, table revision, missing Level ID, invalid act,
incomplete cache, signature mismatch, or hook ownership conflict never guesses
an answer. The original D2R resolver remains authoritative in those cases.
Build names are logged for diagnostics only and are not an allowlist.

The console command `extended-act-level-ids` reports cache state, table
revision, row counts, resolutions, fallbacks, and the diagnostic build name.
`extended-act-level-ids resolve <level-id>
[data-context]` calls the hooked central resolver and reports the zero-based
Act index; the optional data context defaults to RotW (`3`).

## Release candidate

`RuffnecKk-Extended-Act-Level-IDs-1.0.0.zip` is the planned public archive,
not a public release. The archive contains only the DLL. Keep this README
beside the archive when sharing it.

Back up saves and use a disposable character for tests involving custom level
data. Start D2RLoader with the normal complete plugin stack, then run:

```text
extended-act-level-ids
```

The status must report `active` and `cache=ready`. The decisive test requires a
mod with a new `levels.txt` row appended after the normal Act V range while its
`Act` value is set to `0`, `1`, `2`, or `3`. Run:

```text
extended-act-level-ids resolve <new-level-id>
```

The reported Act must match the row's zero-based `Act` value and the source
must be `Levels.txt`. If the custom area is playable, also test travel in both
directions, Town Portal, automap, save/reload, and host/joiner behavior. This
build does not create a new area, transition, waypoint, portal, or quest by
itself.

Please report the D2R and D2RLoader versions, installation scope, mod name,
Level ID and declared Act, both console outputs, gameplay result, and the fresh
D2RLoader/plugin logs. A successful cold start without an out-of-range level is
useful compatibility evidence but does not close the playable-area release
gate.

## Compatibility

Version 1.0.0 is built against D2RLoader 1.2.0-beta and PluginSDK API v4 commit
`4933e2c42cb2592958cd0df3b6dc5003102252d1`. Runtime qualification targets the
official D2R `3.3.93847` build. D2R `3.2.92777` is covered only by governed
byte-exact equivalence of every native surface used by the plugin.

The DLL is an autonomous member of the RuffnecKk D2RLoader Suite. It does not
modify, link, merge, or redistribute any eezstreet plugin.

## Credits

D2MOO documented the historical fixed-threshold behavior and explicitly noted
that the act should be looked up from `Levels.txt`. D2MOO is used as a semantic
reference only; no legacy 32-bit address, structure, or ABI is reused.

Implementation and D2R 3.3 integration: `RuffnecKk`.
