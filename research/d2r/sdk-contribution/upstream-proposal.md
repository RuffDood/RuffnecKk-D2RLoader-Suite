# Proposal: item inventory-page accessor

Target: D2RLoader Plugin SDK v2 and D2R build 92777

Prepared by RuffnecKk

## Problem

`CubeOutputQuantity 1.0.2` hooks `ITEMS_GetInvPage` at `0x36CFE0`. An older
MassID build expected the same entry to remain vanilla and refused to load.
MassID 1.0.0 fixed the collision privately, but each plugin should not need its
own RVA and item-data layout just to read the inventory page.

## Proposed change

```cpp
bool context->TryGetItemInventoryPage(void* item, std::uint8_t* page);
```

- Return `true` and write the page when the active build is supported.
- Return `false` when the service or build is unsupported.
- Write `0xFF` for a null pointer, a non-item unit or missing item data.
- Do not hook or call `ITEMS_GetInvPage`.

## Verified 92777 path

| Surface | RVA or offset |
|---|---:|
| `UNITS_GetUnitType` | `0x34B9D0` |
| `UNITS_GetItemData` | `0x34A500` |
| inventory page | `D2ItemDataStrc +0x55` |

The SDK v2 inspected at commit
`efcfaaa52eeec9e379b3fc2aad1013bb3dddc970` can use optional slot
`reserved3[0]` without moving existing fields or changing `PluginApiSize`.
Older loaders leave the slot null, so plugins can keep their current fallback.

## Acceptance

- Existing v2 plugins still load without rebuilding.
- An older loader reports the service as unavailable.
- Valid and invalid results match native 92777 behavior.
- CubeOutputQuantity and MassID remain active together.

That is the whole proposal. Listener chains, table views and a general collision
registry can be discussed separately.
