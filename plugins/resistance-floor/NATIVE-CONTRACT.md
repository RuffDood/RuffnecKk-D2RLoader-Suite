# Resistance Floor native contract

Target runtime: D2R 3.3.93847. The governed native corpus retains its historical
`d2r-3.2.92777` path because the verified executable identity is reused by the
current target.

## Owned mutations

| RVA | Contract | Scope |
|---|---|---|
| `0x4524C4` | first `MOV ECX, -100` lower-clamp immediate | Replaced by a five-byte jump to a near relay; resumes at `0x4524C9` before the original compare. |
| `0x4524E7` | second `MOV ECX, -100` lower-clamp immediate | Replaced by a five-byte jump to a near relay; resumes at `0x4524EC` before the original compare. |
| `0x14E729A` | four-value Character Screen `-100` operand | Writes the configured player floor only when native display synchronization is enabled. |

Both gameplay sites require their complete independent twelve-byte witnesses
before any mutation. After that full validation, D2RLoader's tracked write gate
receives the exact five-byte `MOV` witness because its safety-check size must
equal the five-byte jump size. The two jumps target private relays allocated
within signed `rel32` reach of D2R; each relay transfers to an assembly mid-hook
inside `d2rl-ruffneckk-resistance-floor.dll`.

The mid-hooks preserve all volatile integer and XMM registers, keep the native
stack aligned, read the proven defender `Unit*` from `[RSI+0x10]` and the
resistance stat id from `[R14+0x08]`, then provide only the lower-bound value in
`ECX` before the original compare executes. The supported stat ids are:

| Resistance | Stat id |
|---|---:|
| Physical | 36 |
| Magic | 37 |
| Fire | 39 |
| Lightning | 41 |
| Cold | 43 |
| Poison | 45 |

Any other stat, unknown unit type, malformed owner chain, cycle or excessive
owner depth falls back to vanilla `-100`.

## Unit classification

The classifier uses independently signed current-runtime entries:

- `UNITS_GetUnitType` at `0x34B9D0`;
- `D2GAME_GetMinionOwner(Unit*) -> Unit*` at `0x4A53C0`.

A direct player selects `players`. A bounded monster-owner chain that reaches
a player selects `companions`. A monster with no owner selects `monsters`. The
walk is cycle-safe and capped at eight nodes;
every uncertain result is fail-closed to the vanilla floor.

## Display contract

The native Fire, Lightning, Cold and Poison Character Screen values share the
operand at `0x14E729A`. Its full eighteen-byte witness at `0x14E728C` must match
before it is changed.

Physical and Magic have no native Character Screen slots. They remain covered
by the gameplay floor, but Resistance Floor does not create custom values for
them or depend on another plugin for display.

## Shared ownership boundary

Resistance Floor does not write the upper-cap operands at `0x4524D6`
(Physical) or `0x4524DE` (Magic and elemental). Those bytes remain owned by D2R
or the active compatible cap plugin.

No eezstreet plugin is modified, linked or redistributed. There is no
`ModScopedOnly` flag, proprietary merge key or dependency on PluginPack.

## Lifecycle and rollback

Runtime hot reload is unsupported for the first candidate. D2RLoader owns and
restores the tracked patches on process shutdown; the small relay allocation is
intentionally retained until process exit so a tracked jump can never target
freed memory during unload ordering. Removing the DLL and TOML followed by a
cold restart restores vanilla behavior.

## Provenance boundary

D2MOO supplies semantic legacy names and control-flow context only. No 32-bit
address, ordinal, structure layout or calling convention is reused in this D2R
plugin. Every RVA, witness and ABI above is governed for the current D2R corpus.
