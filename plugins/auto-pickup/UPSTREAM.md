# Auto Pickup integration provenance

This candidate copies D2R-Pickit from atsay714/D2R-Pickit at
`be89040f742666b6343c25811fc78b16f29ce468` (0.10.1, MIT).
It adds RuffnecKk's potion belt-column routing and unchanged AutoPickupRoute V1
contract from the local `plugin-dev/auto-pickup/experiments/item-type-filter`
candidate. Selection, scheduler and container behavior remain upstream-owned.
The copied native witnesses were compared against the governed captured
D2RLoader-hosted game image (canonical SHA-256
`CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`).
Historical upstream 93847 labels below are preserved as provenance; they are
not retail native-identity or cross-build compatibility claims.

## Original upstream record

# Upstream provenance

Current project author and maintainer: [atsay714](https://github.com/atsay714).

Native function signatures, pickup calls, action-table hooks, and parts of the
compatibility and selection infrastructure were adapted from RuffnecKk Auto
Pickup 2.0.0 and Potion Auto Pickup in RuffnecKk-D2RLoader-Suite.

- Source: https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite
- Pinned commit: `4d070a0ec7f3ea0d83bfdebc332f071cb0d21e2b`
- License: MIT, copyright 2026 RuffnecKk and contributors; retained in LICENSE.
- The radius reference of 4 comes from Potion Auto Pickup's config comment,
  not an independently verified recovery of vanilla auto-gold's full predicate.

The current plugins separate item selection from post-cast scheduling, use
generic identities and both scopes, check fingerprints before activation,
and cooperate over a versioned synchronous scan bridge. Auto Pickup Anything
has a limited nine-switch catalog. Teleport Auto Pickup schedules landing
scans using game/type/player-ID lookup rather than retaining native pointers.
Current changes and versions are documented in CHANGELOG.md.

The vendored PluginSDK headers/config helpers are from D2RLoader/PluginSDK,
tag `v3`, commit `4933e2c42cb2592958cd0df3b6dc5003102252d1`. Its original
MIT notice and file hashes remain in third_party/PluginSDK. The small SDK
CMake adapter came from the RuffnecKk suite. The SDK is included so this
repository and its source ZIP build independently.

`src/picker_sdk.hpp` is an authored adapter for the append-only Widget ABI 1
tail documented by official SDK 0.4.0, commit
`03a5af9e0feb3f95f95e148e330e01f6635950df`, `include/D2RLPlugin/widgets.h`.
It checks size/version/function availability without changing the vendored SDK
or the plugin's ABI 3 requirement. Panel TextEntry uses that revision's flag 4;
localization ABI 2 uses its unchanged table shape. Native panel layout fields
follow the official `examples/widget-localization` example. The new picker
code is authored here and does not copy another plugin's item database.

The Windows frontend in `src/picker_window.*` is authored here using Win32
common controls. The feasibility reference was Item Database's independently
owned overlay and message thread:
https://github.com/falendar85/d2rr-item-database . No source or item database
from that project is included. This path uses existing public loader services
and does not depend on MapSense's renderer or private D2RCore exports.

The settings cursor surface is authored here with documented Win32 layered
window, cursor and timer APIs. It draws a shared Windows cursor into an owned,
nonactivating, click-through popup rather than hooking game rendering/cursor
functions or changing global ShowCursor counts. Relevant public contracts:
[layered windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows),
[cursor display](https://learn.microsoft.com/en-us/windows/win32/menurc/using-cursors#displaying-a-cursor),
and [SetCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcursor).

`src/post_cast/` adapts this maintainer's Teleport Auto Pickup 0.3.4,
commit `693106e6f4e5aafac1d312c8d5451eea0c4b99c7`, into an internal module.
It replaces the optional external item scan with a direct call into the same
item selector, synchronizes live settings, and cancels old scheduling tickets on
settings changes. The independently distributed standalone plugin is retained.
Its original RuffnecKk attribution remains covered by LICENSE.

`src/walking_pickup.inc` reuses that session-bound scheduler and the existing
authoritative player lookup for movement updates. It uses walk/run/town-walk
player modes (2/3/6), with the legacy D2MOO Player.h enum as a semantic reference.
The current runtime's already admitted UnitMode and GetServerUnit functions
provide the actual values/lookup; no legacy native addresses are transplanted.
The local read-only 93847 capture at workspace `inspection/pickup-radius/`
witnesses `0x471977` saving the fourth pickup argument and `0x471A6B..0x471A73`
comparing UnitDistance against that argument. This supports the configurable
native radius; it does not certify every mod or live walking behavior.

The MIT native-stat compatibility adapter is copied unchanged from the standalone
project under `third_party/RuffnecKkNativeStats`; SOURCE.json records the upstream
pin and file hashes. Its original LICENSE is retained and included in packages.

`src/pickup_coexist.hpp` is an authored protocol shared byte-for-byte with
Teleport 0.3.5. Named RCDATA `D2R_PICKUP_COEXIST` holds three DWORDs: magic
0x5049434B, protocol 1, and role 1 (Pickit owner) or 2 (yielding standalone).
Unknown/missing markers retain conservative legacy behavior. Resource mappings
use Windows data/image-resource flags without executing the probed DLL.

The authored `src/interact_settings.hpp` settings-only ABI is byte-identical
in Auto Interact 0.2.0 and Pickit 0.9.4. No native game pointer crosses it.
The pinned ABI-3 SDK has no PluginCommunication declarations; this compatibility
path pins the already-loaded provider with GetModuleHandleEx only for each
synchronous UI call. Provider calls serialize with unload and authoritative
scans, use the provider's own loader config API, and reject stale drafts. It is
not the newer SDK communication/lease service and adds no mandatory dependency.


## Built-in Auto Interact (0.10.0)

Authored by atsay714, lifted from the local Auto Interact 0.3.0 test project.
Canonical maintained source is now `src/auto_interact/` in this repository.
It includes the reviewed 93847 native witnesses, source Class catalog, policy,
bounded retry engine and SDK lifecycle scheduler. The former standalone repo
is historical. No sibling checkout or external Auto Interact DLL is needed.
Settings use internal callbacks and one Pickit config instead of the old DLL ABI.

The MIT paired object-accessor validator and tests derive from RuffnecKk
MapSense at `4d070a0ec7f3ea0d83bfdebc332f071cb0d21e2b`.
`third_party/RuffnecKkObjectContract/` retains its LICENSE, SOURCE.json pins and
file hashes. The existing native-stat adapter is reused for key quantity, with
normal consumption left to the native opener. D2MOO remains semantic research
only; none of its code, addresses or 32-bit layouts is incorporated.
See `inspection/auto-interact-native-research.md` and the accompanying contract
manifest for capture provenance and historical qualification boundaries.

## Local Maps stat-reader compatibility correction - 2026-11-02

Reused the previously qualified Diablo MapSense consumer-owned redirect proof
from `plugin-dev/compatibility-check/builds/mapsense-maps0124-20261101/source`.
The proof is adapted under `src/stat_compat` for Auto Pickup and covers exact
Maps 0.12.1 and 0.12.4 callback/ownership witnesses. The native-stat adapter now
accepts an optional consumer proof of that redirect, while retaining all
independent Core graph, native tail, descriptor and imported-helper checks.
Gold and locked-chest readers opt in. Calls retain the native entry and Maps
callback. Unknown redirects still fail. No upstream/native fingerprints change.
The actual-DLL regression maps the reference without imports or initialization;
it calls only the witnessed callback with a synthetic original reader.
