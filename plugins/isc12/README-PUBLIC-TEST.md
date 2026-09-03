# ISC12 0.2.1 Test version
ISC12 converts D2R serialized `ItemStatCost` IDs from
9 bits to 12 bits, hence the naming `ISC12`. This increases `ItemStatCost.txt` capacity from 511 to
4,095 rows.

This test package is intended only for fresh characters and fresh shared
stashes created while ISC12 is active. Existing 9-bit `.d2s` characters and
`.d2i` shared stashes are not supported by this test package.

Back up or isolate your existing saves before testing. Saves created with
ISC12 must not be loaded after removing the plugin.

## Included

- `d2rl-ruffneckk-isc12.dll`

ISC12 is active when its DLL is installed. It has no configuration file.



## Installing ISC12

Install the DLL in exactly one D2RLoader scope.

For one mod:

```text
<Diablo II Resurrected>/mods/<mod>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

Globally:

```text
<Diablo II Resurrected>/d2rloader/plugins/d2rl-ruffneckk-isc12.dll
```

Do not install the same DLL both globally and inside a mod.

If the mod already loads `ExtendedItemStats.dll` 0.3.14, it may remain
installed. ISC12 verifies that exact version and requires it to be the sole
owner of all six full-item transport hooks before delegating transport to it.
Other ExtendedItemStats versions and other ItemStatCost serialization plugins
are not qualified; ISC12 refuses an unknown, partial or mixed provider.

Before starting the game, make sure the selected mod save folder contains no
existing 9-bit character or shared-stash file that D2R could load. Create both
the test character and its shared stash from scratch with ISC12 already active.

## Credits

ISC12 is created by RuffnecKk (Assisted by AI).

Thanks to D2RLoader and PluginSDK for the plugin environment, and to D2MOO for
historical ItemStatCost and stat-list knowledge.
