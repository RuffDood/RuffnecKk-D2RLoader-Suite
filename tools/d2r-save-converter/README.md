# D2R Save Converter 1.0.0

D2R Save Converter moves Diablo II: Resurrected characters and shared stashes
between the standard D2R save format and the ISC12 format. It can also migrate
compatible saves between mods that use different item or stat data.

Your original saves are never overwritten.

## Before you start

- Back up your characters and shared stashes.
- Close Diablo II: Resurrected before converting anything.
- Keep ISC12 installed when loading a 12-bit save.
- Disable ISC12 before loading a save converted back to standard D2R.

## Supported saves

- Character files (`.d2s`).
- Shared-stash files (`.d2i`).
- Standard D2R 9-bit to ISC12 12-bit, and back again.
- Migration between compatible mods without changing the 9-bit or 12-bit
  format.
- Format and mod migration together in one conversion.

When you select a character, the Converter can include shared stashes found
beside it. You can also select a shared stash directly or convert a complete
save folder.

## How to use it

1. Extract the **D2R Save Converter** folder from the ISC12 ZIP.
2. Double-click `D2RSaveConverter.exe`.
3. Select the save file or folder you want to convert.
4. Choose its current format and, when needed, its source mod.
5. Choose the target format and target mod.
6. Review the paths shown by the Converter, then confirm.

Converted saves are written to a new, clearly named folder beside the source.
Existing output folders are never reused. If any file cannot be converted
safely, the batch stops without writing partial results.

## Selecting a mod

For a modded save, select the installed mod folder or MPQ used to create it.
The Converter supports loose TXT data, folder-based MPQs and binary MPQ
archives. It reads binary MPQs without extracting the complete archive.

Mods that provide only compiled BIN tables, or that change the save format
through custom DLL code, cannot be converted automatically. The Converter
stops instead of guessing or deleting unsupported data.

## Loading the converted save

- A save converted to 12-bit requires ISC12.
- A save converted to standard 9-bit must be loaded without ISC12. Restore the
  mod's original ItemStatCost save plugin first if it used one.
- `ExtendedItemStats.dll` 0.3.14 is supported with ISC12. Other plugins that
  change the same ItemStatCost save format must not be mixed with ISC12.

The Converter does not install or modify a mod. It also does not copy an old
`.d2rl` sidecar; launch the converted save in its intended D2RLoader mod so a
current sidecar can be created.

Advanced users can run `D2RSaveConverter.exe --help` for command-line options.

## Credits

- **RuffnecKk**: design, integration and testing (assisted by AI).
- **prowner**: `@d2runewizard/d2s`, used under the ISC license.
- **tmo-gg**: `stormlib-js`, used under the MIT license.
- **D2MOO contributors**: reference material for Diablo II save-value encoding.
