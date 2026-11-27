# Enhanced Damage Min/Max Fix

An independently versioned RuffnecKk D2RLoader plugin with global or mod-local installation. It restores off-weapon Enhanced Damage when the same item also adds minimum or maximum damage. Weapons keep vanilla behavior. Preserve existing compatible configuration when updating.

## Diagnostic line

Game checks judge this plugin from its log. They depend on the line below, so treat its phrase and fields as a contract. A change to them must update the checks that match it.

- **Flag:** `[diagnostics] enabled` in `ruffneckk-enhanced-damage-min-max-fix.toml`. The default is `false`, and the shipped configuration keeps it off. The plugin reads the flag at load, so restart the game after changing it.
- **Line:** with the flag on, each restored off-weapon update writes one information line through D2RLoader:

  ```text
  Enhanced Damage Min/Max Fix 1.2.4 restored an off-weapon update (stat=17, retained=0, evaluated=35).
  ```

- **Phrase:** `restored an off-weapon update`. Checks match this phrase and the fields. They never match the `Enhanced Damage Min/Max Fix <version>` prefix, which changes with each release.
- **Fields:**
  - `stat` is the corrected stat ID: `17` for maximum Enhanced Damage (`item_maxdamage_percent`) or `18` for minimum (`item_mindamage_percent`).
  - `retained` is the value the game kept after suppressing the update.
  - `evaluated` is the value the plugin restored. It always differs from `retained`, because an equal value is not corrected.
- **Cap:** the plugin writes at most 8 of these lines per load. Later corrections still apply, but they are not logged. The `enhanced-damage-min-max-fix` console command shows counters for every restored update. It writes to the console, not the log.
- **Flag off:** no line is written, and the cap is not consumed.

`tests/policy_tests.cpp` pins the exact text, the fields, the cap and the flag-off behavior. The format and the gate live in `src/policy.hpp`, and `src/plugin.cpp` calls them.
