# D2RLoader PluginSDK upstream pin

This directory vendors the minimal build surface required by RuffnecKk Suite
plugins from the official D2RLoader PluginSDK repository.

- Repository: https://github.com/D2RLoader/PluginSDK
- Tag: `v3`
- Commit: `4933e2c42cb2592958cd0df3b6dc5003102252d1`
- Upstream commit date: 2026-08-11
- License: MIT; see `LICENSE`

The following files are copied byte-for-byte from that commit:

- `include/D2RLPlugin/*.h`
- `cmake/D2RLPluginConfig.cmake`
- `cmake/D2RLPluginEmbedConfig.cmake`
- `cmake/D2RLPluginConfigResource.rc.in`
- `LICENSE`

The local `CMakeLists.txt` is a RuffnecKk integration adapter. It exposes the
same `D2RLPlugin::D2RLPlugin` interface target and the official
`d2rlplugin_embed_config` helper without vendoring upstream examples or tests.

Copyright (c) 2026 D2RLoader contributors. The vendored upstream files remain
subject to the included MIT license.
