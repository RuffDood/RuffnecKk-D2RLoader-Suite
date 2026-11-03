# PlayerX Scaling Tweaks third-party notices

## Players HUD Slider

The HUD panel, artwork and pointer interaction are adapted from
[MadMike616/players-hud-slider](https://github.com/MadMike616/players-hud-slider),
revision `9383558545ca9edefe7a4a9c809331ab20b61954`.
Copyright (c) 2026 MadMike616. MIT License.

The complete license is retained at `src/hud/LICENSE` and embedded in the DLL.
The four native sprites remain unchanged; their exact upstream Git blob hashes
are recorded in `src/hud/UPSTREAM.json`. PlayerX retains its own author identity,
native checks, settings ownership and game-thread application path. The standalone
upstream DLL is neither required nor redistributed.

## I Hate TOMLs client

The optional settings-menu adapter in `src/iht/i_hate_tomls_client.h` is adapted
from wild-healer/D2R-Tools revision `8b4e508ea88bcc3c2ab1b1e014a6acd9f3518120`.
Its original integration comments and attribution are retained.

The SDK-v3 communication declarations are from the official D2RLoader PluginSDK
revision `03a5af9e0feb3f95f95e148e330e01f6635950df`. Its MIT license is retained at
`src/iht/PluginSDK-LICENSE`. Services are optional and probed at runtime; the
plugin retains SDK-v3 metadata.

## Other dependencies and historical semantics

D2RLoader PluginSDK and toml++ retain their upstream MIT licenses in the Suite
third-party sources. D2MOO is used as a historical semantic reference for Diablo
II player-count and NoDrop rules; no 32-bit address or ABI is imported.
