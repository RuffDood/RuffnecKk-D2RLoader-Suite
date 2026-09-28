# Automatic Materials Deposit

Deposit supported materials from your inventory into their assigned stash slots
with one action. Open the stash and press **Shift+D**, or use your mod's deposit
button. Change the shortcut in D2RLoader Controls under **Automatic Materials
Deposit**.

Version 1.2.0 removes the pause between items: all eligible items are requested
in the same UI operation. The game still validates each transfer, so materials
that cannot be deposited remain in your inventory. This is not an all-or-nothing
transaction; the server processes the native requests and controls the final
inventory state.

## Settings

The existing `ruffneckk-bulk-currency-deposit.toml` continues to apply. Configure
an optional Inventory button, its position and tooltip, and item-code inclusion
or exclusion lists. An empty inclusion list accepts everything supported by the
active mod's Advanced Stash registry. The plugin does not add unsupported item
types to that registry.

The optional Inventory button now defaults to **Deposit Materials**. Existing
custom tooltips are preserved; change `[button].tooltip` yourself if desired.
Mod-owned buttons control their own tooltip text.

The retired `deposit.item_delay_ms` key is accepted in older files but ignored.
New configurations omit it. There is no intentional delay between transfers.

## Existing installations and custom buttons

The displayed name replaces **Bulk Currency Deposit**. These identifiers remain
stable so an upgrade preserves saved Controls bindings and existing layouts:

- DLL: `d2rl-ruffneckk-bulk-currency-deposit.dll`
- Configuration: `ruffneckk-bulk-currency-deposit.toml`
- Plugin ID, Controls action ID and status command: `bulk-currency-deposit`
- Custom button command: `PanelManager:OpenPanel:RuffnecKkBulkCurrencyDeposit`

Global and mod-local installation are supported, with active-mod configuration
taking priority over the global fallback. The plugin remains a standalone
RuffnecKk component; no companion MPQ is required.

## Candidate validation

Version 1.2.0 is a development candidate. Local policy and build results are
recorded in the registered Diablo development home. In-game deposit behavior,
save/reload, coexistence with the complete active plugin stack and multiplayer
still require qualification for this version.

Author: **RuffnecKk**. D2MOO is credited for background knowledge of Diablo II
inventory mechanics; native D2R addresses and ABI evidence come from the
workspace's governed D2R corpus.
