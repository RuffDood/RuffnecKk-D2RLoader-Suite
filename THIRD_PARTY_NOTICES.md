# Third-Party Notices

This file records third-party software, documentation, and prior work used by
the RuffnecKk D2RLoader Suite. It does not replace or broaden any upstream
license or permission. The repository `LICENSE` applies only where RuffnecKk
has the right to apply it.

## D2RLoader PluginSDK v3

The Suite targets and vendors the minimal build surface of the
[D2RLoader PluginSDK](https://github.com/D2RLoader/PluginSDK):

- tag: `v3`;
- commit: `4933e2c42cb2592958cd0df3b6dc5003102252d1`;
- upstream commit date: 2026-08-11;
- license: MIT, copyright 2026 D2RLoader contributors;
- retained paths: `third_party/PluginSDK/include/D2RLPlugin/`,
  `third_party/PluginSDK/cmake/`, and `third_party/PluginSDK/LICENSE`.

The retained upstream files and their SHA-256 values are listed in
`third_party/PluginSDK/UPSTREAM-SHA256.txt`. The adjacent `CMakeLists.txt` is a
RuffnecKk integration adapter, not an upstream PluginSDK file. All Suite DLLs
use the PluginSDK ABI; the complete MIT text is retained at
`third_party/PluginSDK/LICENSE`.

## D2RHUD-2.4 / Floating Damage

Floating Damage contains a reduced and adapted derivative of
[locbones/D2RHUD-2.4](https://github.com/locbones/D2RHUD-2.4) at commit
`b9373f8508282948ceb3e2b56f892d9eba475744`.

The derivative is contained in these Suite paths:

- `plugins/floating-damage/src/d3d12_renderer.cpp` and `.hpp`;
- `plugins/floating-damage/src/floating_damage.cpp` and `.hpp`; and
- the D2RHUD-facing integration in `plugins/floating-damage/src/plugin.cpp`.

RuffnecKk changes include the PluginSDK v3 lifecycle, governed D2R
3.2.92777 hook and structure evidence, TOML configuration, fail-closed
validation, renderer diagnostics, native projection cache, 4K/2K renderer
reset handling, Kodia font index 12 support, and removal of bundled mod fonts.
The public binary containing this work is
`d2rl-ruffneckk-floating-damage.dll`.

## Dear ImGui 1.91.5

Floating Damage statically links selected sources from
[Dear ImGui](https://github.com/ocornut/imgui):

- tag: `v1.91.5`;
- commit: `f401021d5a5d56fe2304056c391e78f81c8d4b8f`;
- license: MIT, copyright 2014-2024 Omar Cornut;
- compiled source: core ImGui plus the Win32 and DirectX 12 backends;
- public artifact: `d2rl-ruffneckk-floating-damage.dll`.

The complete upstream license text is retained at
`third_party/notices/IMGUI-LICENSE.txt` (SHA-256
`0EBF8FEB061536BDEBD7C6A365B64808AE3479048B1C44113E19037680C5F2BE`).

## MinHook 1.3.4

Floating Damage statically links
[MinHook](https://github.com/TsudaKageyu/minhook):

- tag: `v1.3.4`;
- commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`;
- license: two-clause BSD;
- copyright: 2009-2017 Tsuda Kageyu, with the bundled HDE32/HDE64 portions
  copyright 2008-2009 Vyacheslav Patkov;
- public artifact: `d2rl-ruffneckk-floating-damage.dll`.

The complete upstream license and both HDE notices are retained
at `third_party/notices/MINHOOK-LICENSE.txt` (SHA-256
`2BFFF5FA29FC5D1CB2745ACFAAD9D930E983F5C8DD97E0EC5735D9199CF8F416`).

## nlohmann/json 3.12.0

Larzuk Sockets vendors the single-header distribution of
[nlohmann/json](https://github.com/nlohmann/json):

- tag: `v3.12.0`;
- tag commit: `55f93686c01528224f448c19128836e7df245f72`;
- retained header: `plugins/larzuk-sockets/third_party/nlohmann/json.hpp`;
- retained header SHA-256:
  `5F09D1EEBE9B3557F21DF155869AF13BCDE79B74A0748F82CA946E8CC088AAA0`;
- primary license: MIT, copyright 2013-2025 Niels Lohmann and contributors;
- public artifact: `d2rl-ruffneckk-larzuk-sockets.dll`.

The header also retains Google Abseil utility code under Apache License 2.0
and Florian Loitsch's Grisu2 implementation under MIT. Complete license texts
are retained beside the header as `LICENSE.MIT` and `LICENSE.APACHE-2.0`.
Bulk Skill Point Allocation uses its own bounded parser and does not contain
or link nlohmann/json.

## eezstreet/d2rdoc

[eezstreet/d2rdoc](https://github.com/eezstreet/d2rdoc) is the primary
documentation reference used for current Diablo II: Resurrected data tables
and TXT headers.

- consulted revision: `7a0fbf622b3fc94be642b5cffbe26df50e4bd668`;
## D2MOO

[D2MOO](https://github.com/ThePhrozenKeep/D2MOO) and its contributors provided
semantic reference material for historical Diablo II engine behavior. The
workspace reference is pinned to commit
`19019806df7f3e877fa105b05395d1e3597e2316`.

The governed missions and retained source documentation currently confirm
D2MOO consultation for these Suite components:

- Item Durability;
- Unified Ethereal Item Rules;
- Repair Costs Cap;
- Vendor Stock Refresh;
- Potion Auto Pick Up;
- Mass Identify;
- Cube Quick Move;
- Prevent Merc Death in Town;
- Remote Stash;
- Floating Damage;
- Larzuk Sockets;
- Bulk Skill Point Allocation;
- Gamble Screen Limit; and
- Quantity Display Fix.

This is a knowledge credit, not native evidence. No D2MOO address, 32-bit ABI,
or legacy structure layout is reused as proof for D2R 3.2.92777. Native RVAs,
signatures, callbacks, structure fields, and calling conventions are
independently established against the governed 64-bit game image.

## Memory patch provenance

All Suite memory patch JSON manifests are RuffnecKk-authored ports for D2R
3.2.92777. The JSON files contain strict expected bytes and do not contain
third-party program source. `manifests/native-writes-3.2.92777.json` is the
central write index. The table below records the conceptual lineage and the
local governed evidence for every shipped patch feature.

| Suite artifact | Concept and prior source | Governed 92777 evidence / credit |
|---|---|---|
| `ruffneckk-gamble-screen-limit.json` | RuffnecKk Gamble Screen Limit add-on | D2MOO semantic credit |
| `ruffneckk-quantity-display-fix.json` | RuffnecKk Qty Display Issue add-on | D2MOO semantic credit |
## Distribution requirements

- Preserve this notice with public source distributions.
- Reproduce the relevant dependency notices with any binary distribution that
  contains the corresponding dependency or derivative.

- Do not redistribute eezstreet's five PluginPack DLLs as Suite artifacts.
- No Kodia or other mod font is redistributed by Floating Damage. The plugin
  reads `data/hd/ui/fonts/kodia.ttf` from the installed mod as font index 12
  and otherwise falls back to index 0.
