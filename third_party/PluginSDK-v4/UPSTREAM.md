# D2RLoader PluginSDK v4 upstream pin

This directory vendors the header surface required by the Suite components
that use PluginSDK v4 services. Components that do not require v4 continue to
build against the separately pinned v3 SDK for maximum loader compatibility.

- Repository: https://github.com/D2RLoader/PluginSDK
- Tag: `v4`
- Commit: `6eb8f8b6192868214706bd6d528c5294f2f551b7`
- Upstream commit date: 2026-08-30
- License: MIT; see `LICENSE`

The following files are copied byte-for-byte from that commit:

- `include/D2RLPlugin/*.h`
- `LICENSE`

The local `CMakeLists.txt` is a RuffnecKk integration adapter and is not an
upstream PluginSDK file.

Copyright (c) 2026 D2RLoader contributors. The vendored upstream files remain
subject to the included MIT license.
