Hey Dimentio,

I cleaned up my 92777 SDK map. It now contains the full high-confidence set I
have governed so far: 206 unique functions, call-sites, patch sites and data
access points, plus the table offsets and verified structure fragments. I also
included my separate memory-patch inventory: 61 unique sites whose expected
bytes all match the canonical 92777 image, with the status of each site kept
intact.

That includes Remote Stash, Advanced Item Tooltips 3.2.0, my Cube interactions,
Charm Zone, the charm aura and enhanced damage fixes, the accepted PluginPack
work, MassID 1.0.0 and the other reusable 92777 discoveries.

One API case is now especially concrete. CubeOutputQuantity 1.0.2 hooks
`ITEMS_GetInvPage` at `0x36CFE0`. The old MassID 0.2.9 depended on that entry
still having its vanilla prologue, so it refused to load after the hook.

The native function is small: it calls `UNITS_GetItemData` at `0x34A500` and returns the page byte at `ItemData +0x55`. MassID 1.0.0 uses that lower-level accessor instead, so it no longer touches or validates the hooked entry.

Both plugins now load together on 92777.

I added the function, field, conflict and runtime result to the 92777 SDK notes. For the API, I think there are two separate useful pieces: a normal item-page accessor for plugins that only need the value, and a loader-owned callback list for plugins that really need to intercept the same function.

The package contains a short README, SDK candidates, minimal verified C++
layouts, the complete machine-readable RVA map and the separate memory-patch
map. No binary dumps or speculative structures.

I also included one focused upstream proposal: an optional v2-compatible
`TryGetItemInventoryPage` service using one reserved API slot. The other API
ideas can stay separate.
