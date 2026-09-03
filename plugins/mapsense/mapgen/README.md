# RuffnecKk MapSense map generator

This directory governs the source used to build the packaged
`RuffnecKkMapSenseMapgen.exe`. The helper is launched hidden and below normal
priority by MapSense; players do not launch it themselves.

## Reproducible dependency

1. Clone `https://github.com/jaenster/libd2.git` into `vendor/libd2`.
2. Check out commit `ac4d735e57fcab6a3c356f810bb256da95a93716`.
3. From that checkout, apply `../../libd2-mapsense.patch`.
4. Build with Zig 0.16.0:

   ```powershell
   zig build -Doptimize=ReleaseSafe
   ```

The patch is the complete diff used by this candidate. It adds the immutable
label/waypoint and automap-geometry surfaces consumed by MapSense; it does not
add any live D2R memory access. Outdoor automap geometry follows each room's
exact DT1 mask, waypoint/shrine/terrain LvlSub seed order, and near-room seam
ownership, then stops before collision rasterization. Protocol MS1 v2 emits
one collision-proven, player-width opening for each directed outdoor level
pair and refuses to invent an averaged seam anchor.

## Active-mod inputs

The helper embeds its pinned vanilla dataset as the fallback. MapSense may
overlay the active mod by appending repeatable roots to any `labels`,
`geometry`, or `geometry-binary` command:

```powershell
RuffnecKkMapSenseMapgen.exe labels 1337 2 4 109 `
  --excel-root "<mod>\data\global\excel" `
  --tiles-root "<mod>\data\global\tiles"
```

The Excel overlay covers `Levels`, `LvlPrest`, `LvlTypes`, `LvlMaze`,
`LvlSub`, `LvlWarp`, and `Objects`. DS1 files are resolved from the ordered
active tile roots first, then from the embedded vanilla assets. Conflicting
duplicate overrides, unsafe paths, malformed tables, and unresolved topology
fail closed. Level and preset enums are non-exhaustive, so an otherwise valid
custom numeric ID is generated without a per-mod code change.

The DLL passes these roots automatically when it starts the helper. This is a
developer interface; players still launch only D2R.

Binary geometry protocol MSA1 v2 records the native tile-array provenance and
the orientation-based vertical offset as separate `wallTree` and `raised`
bits. The distinction is required because D2R chooses the floor/wall owner tree
from the source tile array, not from `orientation >= 0x10`.

The packaged executable is a generated release artifact. Its hash must match
the value recorded in the MapSense mission before deployment or publication.
Run `verify-labels.ps1` against the built executable to exercise deterministic
MS1 and MSA1 v2 output, all five acts, exact waypoint coverage, valid floor/wall
provenance, unique physical seams and reciprocal one-subtile adjacency across
four governed seeds. The matrix also
pins the observed Spider Forest/Flayer Jungle crossing for seed `1395822899`
at `(5000, 4268)` / `(4999, 4268)` so the former averaged-label regression
cannot silently return.

Pass `-ActiveDataRoot <mod-data-root>` to add two mod-awareness gates. The
first requires the active dataset's real source-to-custom entrance and exact
waypoint coverage. The second rewrites that custom target to arbitrary
LevelId `733` in a temporary fixture and requires the same source coordinate,
proving that no BKVince or Rift identifier is hard-coded.
