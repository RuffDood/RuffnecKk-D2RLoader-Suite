# PlayerX Scaling Tweaks

PlayerX Scaling Tweaks is a configurable RuffnecKk D2RLoader plugin that keeps a
minimum difficulty baseline while allowing life, experience, monster offense
to use independent caps and optionally makes `/players X` count like X nearby
party members for NoDrop. Its optional Battle.net simulation disables
artificial player-count controls and uses connected players as the only
dynamic source.

Current status: **1.2.0 candidate with an independent NoDrop minimum.**
The new minimum requires in-game qualification. In 1.1.1, new-game p16 startup and
commands through p64 were confirmed in BKVince with D2RLoader 1.3.1-beta on
Battle.net D2R 3.3.93847. Shift+1/p1 and Shift+2/p2 were confirmed during this
test cycle. Higher counts and multiplayer remain unqualified.

Version 1.1.1 also removes the final native setter's rejection of counts above
8 when an extended maximum is configured. Earlier candidates updated the
Offline Difficulty setting, but the final setter still kept the previous count
instead of applying p16. Commands, startup values and shortcuts share this path.
Set `player-count.maximum-command-players = 16` to allow up to p16. The default
remains p8; Battle.net simulation continues to lock artificial difficulty to p1.

Version 1.0.1 adds `[battle-net-simulation]` and declares the API v3 shared
execution role required by its combined local-control and gameplay behavior.

## Starting count and shortcuts

The included TOML keeps Loader's starting value (`start-game-players = 0`) and
adds exactly two shortcuts: **Shift+1 sets p1**, and **Shift+2 sets p2**.

To start every new game at p16, edit your existing `[player-count]` section:

```toml
minimum-scaling-players = 1
maximum-command-players = 16
start-game-players = 16
```

The starting value is applied once when your local player becomes ready. Later
commands and shortcuts remain in effect until you leave that game. Zero or an
omitted setting leaves Loader's starting value alone.

The TOML explains how to copy a `[[player-count.hotkeys]]` block to add a shortcut.
Use a unique combination of A-Z, 0-9 or F1-F24, with an optional Shift, Ctrl or
Alt modifier. Counts must be within your configured minimum and maximum; if you
raise the minimum, adjust or remove shortcuts below it. Remove all hotkey blocks
to disable shortcuts. Restart D2R after changing the TOML.

Shortcuts are registered in Loader's Controls menu. Its saved bindings can
override their defaults; changing a shortcut's keys or count in TOML creates a
new action. Loader suppresses these shortcuts during text entry and key binding.
The applied count is reported in Loader's console.

Existing TOMLs remain valid: missing startup/hotkey settings add no controls.
The new controls require Loader Lifecycle v1 and Input v1 (Input only when
hotkeys are configured). A missing service or failed registration refuses the
plugin with an error instead of silently skipping the requested feature.
Battle.net simulation disables both features. Native `/players` game-mode
restrictions still apply; multiplayer behavior has not been qualified.

## Default player experience

The included TOML uses these defaults:

| Setting | Default | Concrete effect |
|---|---:|---|
| Minimum scaling count | 1 | Monster scaling keeps its vanilla p1 baseline. |
| Maximum `/players` command | 8 | The command keeps its vanilla p8 ceiling. |
| Monster life cap | Unlimited | HP follows the effective player count. |
| Monster experience cap | Unlimited | XP follows the effective player count. |
| Monster offense cap | Unlimited | Physical damage and Attack Rating follow the native Nightmare/Hell factor. |
| NoDrop party simulation | Disabled | NoDrop keeps the native nearby-party formula. |
| Battle.net simulation | Disabled | `/players` and Offline Difficulty keep their native behavior. |

The configuration is
[`ruffneckk-playerx-scaling-tweaks.toml`](config/ruffneckk-playerx-scaling-tweaks.toml). Its comments
are the player-facing template and explain every value.

The strict configuration range is 1â€“65,535. Values above p8 are extended mod
values and remain runtime-unqualified until they are measured in game.

## Battle.net simulation

```toml
[battle-net-simulation]
enabled = true
```

When enabled, `/players` is unavailable, Offline Difficulty is reset and
locked to p1, and the artificial player-count value is ignored by gameplay.
Connected players become the only dynamic source. The configurable minimum
and independent channel caps still apply after that real count is obtained.

For Battle.net-style defaults, keep the minimum at 1, all channel caps at 0,
and NoDrop party simulation disabled. Battle.net simulation takes priority if
the NoDrop simulation option is also true.

The current native evidence proves an immobile p1 control, not whether the
slider is visually greyed or hidden.

## NoDrop nearby-party simulation

### Independent NoDrop minimum

Set an optional minimum effective count used only for NoDrop:

```toml
[no-drop]
minimum-effective-players = 1
players-command-simulates-nearby-party = false
```

The default of `1`, including when omitted from an older TOML, preserves the
existing calculation. Choose another value to raise only the NoDrop minimum.
For example, `5` gives solo play at least the NoDrop effect of five nearby
living party members. It does not set `/players 5` or change monster HP, XP,
physical damage or Attack Rating. Higher effective NoDrop counts still apply.

This minimum is independent of the command minimum, command maximum and
monster-channel caps. It applies after the native monster-count cap, and works
with or without nearby-party simulation. In Battle.net simulation, it remains
active while real players supply the dynamic count and command simulation is
disabled. The valid configuration range is 1–65,535. Restart after editing.

### Nearby-party simulation

D2R combines the two inputs as:

```text
effective NoDrop players = nearby + (players command - nearby) / 2
```

The division uses integer truncation. With one nearby member (the player),
`/players 4` therefore produces an effective NoDrop count of 2. Four living
party members in the same area produce an effective count of 4.

The optional mode makes the accepted `/players` count act as the nearby-party
source for this calculation:

```toml
[no-drop]
players-command-simulates-nearby-party = true
```

With that setting, solo `/players 4` produces an effective NoDrop count of 4
and solo `/players 8` produces 8. A larger real nearby-party count is never
reduced. PlayerX Scaling Tweaks also prevents the monster's persistent player-count
stat from lowering the simulated result; the native probability calculation
itself remains intact.

## Scaling switches

Setting a monster channel's `enabled` value to `false` does not erase the
baseline. It freezes that channel at
`player-count.minimum-scaling-players`. A `maximum-players` value of `0` means
unlimited scaling. Every non-zero maximum must be at least the configured
baseline: with a p4 baseline, `4` freezes the channel at p4, `5` or higher caps
it above the baseline, and `1..3` is invalid.

D2R couples its player-count monster physical damage bonus with monster Attack
Rating in Nightmare and Hell. The `[monster-offense-scaling]` section controls
that whole native offense factor. Normal difficulty keeps its native rule.

`[no-drop].players-command-simulates-nearby-party = false` uses native NoDrop
party inputs. A configured independent minimum still applies; with its default
of 1, the original NoDrop calculation remains untouched.

## Installation and ownership

The final plugin will support either of the normal D2RLoader scopes:

- global: `<D2R>/d2rloader/plugins/`;
- mod-local: `<D2R>/mods/<mod>/d2rloader/plugins/`.

Only one scope may load the DLL in a given D2R process. Separate local D2R
processes can each load it. Configuration lookup prefers the active mod, then
the plugin's scope, then the global config directory. A TOML that exists but is
invalid refuses the plugin instead of silently using other values.

PlayerX Scaling Tweaks must be the only owner of its native surfaces. Before runtime
deployment in BKVince:

- remove the old `ruffneckk-player-difficulty-overrides.json` patch;
- keep PluginPack `misc.playersCommandLimit` at 8;
- keep `misc.monsterHpPlayerCountCap` and
  `misc.monsterExperiencePlayerCountCap` at 0.

These values keep `plugin-misc.dll` and its unrelated features installed while
preventing its optional player-scaling hooks from competing with this plugin.

## Compatibility and rollback

The plugin makes no save-format changes. Removing the DLL and TOML restores
the native engine behavior, provided no retired overlapping patch is restored
at the same time.

Compatibility is decided only by the complete native byte fingerprint used by
the plugin. D2R build names and distribution channels are diagnostic, never an
allowlist. The current static evidence covers the governed native surface shared
by D2R 3.2.92777 and Battle.net 3.3.93847. Steam 3.3.93787 remains admissible
but unqualified until its byte-exact native evidence exists.

The plugin has a shared execution role. For TCP/IP, host and clients must use
the same plugin and configuration before compatibility can be claimed. A
client-side installation cannot change a remote host that does not run it.

## Credits

Created by **RuffnecKk**.

Thanks to **D2MOO** for the semantic reference used to understand the historical
monster offense and NoDrop algorithms. No D2MOO address, structure layout or
32-bit ABI is used in the D2R plugin.

The pinned eezstreet D2RL-Plugins implementation was also audited for the
existing `/players`, monster-life and monster-experience ownership surfaces.
No eezstreet DLL is modified, linked or redistributed.
