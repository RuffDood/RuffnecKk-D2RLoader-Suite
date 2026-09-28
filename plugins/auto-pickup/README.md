# Auto Pickup

Auto Pickup 2.0.0 sends configured potions to preferred belt columns and can
collect configured item codes through the game's normal inventory route.

The plugin is a standalone RuffnecKk D2RLoader Suite DLL. It can be installed
in either the global plugin directory or an active mod's plugin directory.

Potion families use their configured belt columns first, then their configured
inventory fallback codes. Entries in `[inventory_items]` are considered only
when no configured potion has a valid dedicated route, and use the native
destination selection unchanged.

When Stack Manager is present, Auto Pickup negotiates its optional versioned
route-advisor API. Stack Manager then selects an exact-code partial belt stack
or the next bottom-up slot and enforces its configured belt maximum. A ground
stack is never split implicitly: if the whole object does not fit, it follows
the configured inventory fallback or remains on the ground. If Stack Manager
is absent or inactive, Auto Pickup keeps its standalone native routing. A
loaded provider with an incompatible ABI disables potion routes fail-closed;
configured non-potion inventory items remain independent.

The configuration is independent and uses exact one-to-four character,
lowercase alphanumeric item codes. Its list order determines priority.

## Status

Static policy tests cover parser validation and routing rules. Runtime,
full-suite coexistence, and multiplayer validation remain separate gates.
