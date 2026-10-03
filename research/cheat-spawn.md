# `spawn` cheat command — in-game test reference

This page is a practical reference for the internal server-side `spawn` debug
command. It was imported from `cheat_spawn.md` on 2026-07-28 for in-game
testing.

> [!CAUTION]
> The source build and runtime provenance of the original notes were not
> supplied. Its native addresses and function names are therefore not promoted
> as D2R 3.2.92777 evidence. Confirm the command on the runtime being tested and
> prefer the lowercase, comma-separated syntax below.

## Syntax

```text
spawn <item_code>[,<option>=<value>]...
spawn spawnmode
```

Arguments are **comma-separated**, not space-separated. The first token is the
item code; subsequent tokens are option/value pairs.

## Quick examples

The item codes in these examples exist in the current BKVince `misc.txt`:

```text
spawn hp5
spawn hp5,3
spawn gld,qty=100
spawn rin,typ=m,pre=1,suf=1
spawn rin,lvl=85,typ=r
spawn rin,pre=1,pre=2,pre=3,suf=4,suf=5
spawn hp5,inv=1
spawn rin,num=3
spawn spawnmode
```

- `hp5`: Super Healing Potion
- `gld`: Gold
- `rin`: ring

Use the three-character code from the active mod's item tables when testing
other items.

## Subcommand

### `spawnmode`

When the first argument is `spawnmode`, the imported analysis says the handler
clears its global spawn-mode flag and returns without spawning an item. In other
words, `spawn spawnmode` turns spawn mode off in the analyzed handler.

## Arguments

### Item code

The first argument is normally a three-character item code. The imported
analysis describes a lookup against the game's item data table using a packed
four-byte value padded with a space (`0x20`).

The source notes conflict on case handling: they describe a case-sensitive
packed comparison but also a lowercase normalization before lookup. Until that
behavior is confirmed on D2R 3.2.92777, use lowercase codes exactly as they
appear in the active mod's tables.

### Options

Options follow the item code and are separated with commas. The imported
analysis describes tokenization with `strtok` and `,` as the delimiter.

Token routing reported by the source:

- Tokens shorter than five characters are treated as bare repeat counts. Only
  all-digit tokens are accepted.
- Tokens from five through eighteen characters are treated as `key=value`
  pairs. The key is three characters followed by `=` and a value.

| Option | Effect described by the imported analysis |
| --- | --- |
| `num=<n>` | Spawns the item repeatedly. A value of `0` falls back to `1`. |
| `spc=<n>` | Sets a special item type, clamped to the range `0`–`127`. |
| `qty=<n>` | Sets the quantity of a stackable item after creation when greater than `0`. |
| `pre=<id>` | Adds a magic prefix ID. Up to three prefixes are tracked. |
| `suf=<id>` | Adds a magic suffix ID. Up to three suffixes are tracked. |
| `isd=<n>` | Passes item spawn data to the handler described in the source. |
| `msd=<n>` | Passes monster spawn data to the handler described in the source. |
| `nid=<n>` | Sets the item name ID. |
| `lvl=<n>` | Sets the item level. Without it, the imported analysis reports a global spawn-level default. |
| `typ=<type>` | Selects an item quality using the values below. |
| `inv=<n>` | Requests inventory placement. Without it, the item is reported to drop on the ground. |

These are the only option keys recognized by the imported parser analysis;
unknown tokens are reported as silently ignored.

### Bare repeat-count shorthand

A short all-digit token is the shorthand for `num`:

```text
spawn hp5,3
spawn hp5,num=3
```

Both forms request three Super Healing Potions.

## `typ` quality values

| Type | Quality ID | Quality |
| --- | ---: | --- |
| `m` | 4 | Magic |
| `r` | 6 | Rare |
| `s` | 5 | Set |
| `l` | 1 | Low Quality |
| `h` | 3 | High Quality |
| `u` | 7 | Unique |
| `n` | 2 | Normal |

When `typ` is omitted, the imported analysis reports quality `0`, meaning the
base/default item quality.

## Full command patterns

```text
# Base item
spawn <code>

# Repeat count
spawn <code>,<count>
spawn <code>,num=<count>

# Stack quantity
spawn <code>,qty=<quantity>

# Up to three prefixes and three suffixes
spawn <code>,pre=<id>,pre=<id>,pre=<id>,suf=<id>,suf=<id>,suf=<id>

# Item level and quality
spawn <code>,lvl=<level>,typ=<m|r|s|l|h|u|n>

# Special and spawn data
spawn <code>,spc=<0-127>,isd=<value>,msd=<value>,nid=<value>

# Inventory placement
spawn <code>,inv=<value>

# Disable spawn mode
spawn spawnmode
```

Options can be combined, subject to the parser's token-length limit:

```text
spawn rin,num=3,lvl=85,typ=m,pre=1,suf=1,inv=1
```

## Reported behavior

The source analysis describes the handler in this order:

1. Check for the `spawnmode` subcommand and clear the spawn-mode flag.
2. Copy and split the argument string on commas.
3. Parse each recognized `key=value` token or bare repeat count.
4. Look up the base item code in the item data table.
5. Create the base item from the populated configuration.
6. Apply `qty` to the created item when requested.
7. Place the item in the player's inventory when `inv` is present; otherwise,
   drop it on the ground.
8. Apply up to three collected prefixes and three collected suffixes.
9. Repeat the creation process `num` times; the reported default is one.

The imported notes report a return value of `1` on success and `0` on failure
or invalid arguments.

## Limits and validation status

- Item codes must exist in the item tables loaded by the active runtime.
- `num` controls the number of created item units; `qty` controls the stack
  quantity of one stackable item.
- Prefix and suffix arrays are reported as limited to three entries each.
- Extra prefix or suffix entries and unknown option keys are reported as
  silently ignored.
- Affix IDs, name IDs, item-level behavior and inventory placement still need
  direct runtime confirmation on D2R 3.2.92777.
- The native names and addresses from the original file remain ungoverned until
  independently matched against the verified 92777 workbench.
