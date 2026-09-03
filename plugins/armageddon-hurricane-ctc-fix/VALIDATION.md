# Armageddon-Hurricane CtC Fix validation

The historical evidence below belongs to candidate 0.1.1. Version 1.0.0 must
be rebuilt and requalified under its final product identity before release.

Status as of **27 August 2026**: **version 0.1.1 release package prepared for
Vincent's review**. No game process was launched during packaging. The runtime
results below come from the explicitly approved D2R 3.3.93847 tests of the exact
DLL being packaged.

## Release artifacts

| File | Bytes | SHA-256 |
|---|---:|---|
| `d2rl-ruffneckk-armageddon-ctc-fix.dll` | 177152 | `FDA4D8905D60A5FDCDE12B734D4D272EE2FBDA7BF58E24C516A2A88CD5295F77` |
| `ruffneckk-armageddon-ctc-fix.toml` | 468 | `634114B3A719D50453EAB9B7BE0B6511B1C40DB9E7EAB4C75C56270C757155CD` |
| `ArmageddonCtCFix-0.1.1.zip` | 84879 | `3B4F00FEBFB116E55E8BB3DF4A3CC0679AF1ED1A238FF535565FDFC0EE8B486F` |

The ZIP contains only the DLL and TOML. `README.md` and this validation record
remain beside the archive for human review and are deliberately excluded.

## Static and native gates

| Gate | Result | Evidence |
|---|---|---|
| Release policy tests | passed | `armageddon-ctc-policy`, 1/1 |
| Strict TOML parser | passed | Valid defaults plus unknown-key and wrong-type rejection tests |
| Native fingerprint | passed | Every used RVA, signature and ABI witness is checked before the first hook; one-byte-negative test passes |
| Version policy | passed | No build-name or version allowlist |
| Release x64 build | passed | MSVC against PluginSDK `4933e2c42cb2592958cd0df3b6dc5003102252d1` |
| Reproducible binary | passed | Clean Release rebuild reproduces the gameplay-tested SHA-256 |
| Metadata | passed | Version `0.1.1`, author `RuffnecKk`, server/native-hook flags, no `ModScopedOnly` |
| Exports | passed | Exactly `D2RLoaderGetPluginInfo`, `D2RLoaderLoadPlugin`, and `D2RLoaderUnloadPlugin` |
| Hook ownership | passed | Selected entries are disjoint from Cast Triggers and the governed addon sources |
| Save contract | passed by design | Synthetic skill objects are stack-local and unlinked before return; no serialized plugin data |
| D2MOO credit | passed | README credits pinned commit `19019806df7f3e877fa105b05395d1e3597e2316` |

The governed corpus identifies these native surfaces with high confidence:

- `0x589930`: item-effect skill helper;
- `0x575DE0`: shared Armageddon/Hurricane start callback;
- `0x574E90`: Armageddon active callback;
- `0x575600`: Hurricane active callback;
- `0x33DD40`: highest-level skill resolver;
- `0x34B6E0`: unit skill-list resolver;
- `0x3351B0`: state predicate used for retained-seed cleanup.

## Runtime scope

| Case | D2R 3.3.93847 result |
|---|---|
| Mod-local installed-stack cold start | passed: 36 plugins and 18 patches loaded; startup completed |
| Blank-`ItemEffect` assertion absent | passed |
| Armageddon visible periodic meteors | passed |
| Armageddon damage | passed; low damage was expected from the level-1 fixture |
| Unrelated CtC control | passed: Chilling Armor triggered from the same event |
| Native Armageddon duration and caster tracking | passed |
| Natural-expiry retained-seed cleanup | passed |
| Hurricane visible state/effect | passed |

### Armageddon

A disposable Fallen fixture used 100% level-1 Chilling Armor and Armageddon on
being struck while Armageddon's `ItemEffect` remained blank. The Fallen
survived the triggering hit, Chilling Armor appeared, periodic Armageddon
meteors appeared and the meteors damaged the player. No
`SkillItemEffect.cpp:136` assertion occurred.

A separate ring fixture triggered level-20 Armageddon on the player. The effect
followed the character, which is native behavior. BKVince normally sets
Armageddon `Param1=999999`; a temporary test value of `250` frames proved that
the effect stopped after about ten seconds and that the plugin preserves the
data-driven duration.

With version 0.1.1, the post-expiry console counters reported:

```text
helper calls=1
record bridges=1
real-skill starts=0
synthetic starts=1
synthetic active calls=124
expired seed cleanups=1
retained seeds=0
```

This closes the retained-seed lifetime issue found in 0.1.0.

### Hurricane

The approved `QtyTester` ring fixture made Hurricane visibly surround the
player. No item-effect assertion occurred. The console counters reported:

```text
helper calls=3
record bridges=3
real-skill starts=0
synthetic starts=3
synthetic active calls=0
expired seed cleanups=0
retained seeds=0
```

Hurricane correctly needs no persistent Armageddon seed bridge.

## Restored runtime fixtures

All temporary runtime table edits were removed. The files were restored to the
exact state observed immediately before their respective tests:

| Runtime file | Restored SHA-256 |
|---|---|
| `cubemain.txt` | `6DC72AEAC17C2BD0A47C8045E90DC15C02E841AB3A1AB9BD60002651B0EF455A` |
| `skills.txt` | `2E09E2D8410DC1877EB93F48DCA78050C4C533DC1F8449FB6753FCF0ADCB8006` |

## Compatibility claims and exclusions

D2R 3.3.93847 is the runtime actually tested. D2R 3.2.92777 inherits native
coverage because the governed corpus proves every used surface byte-identical;
no duplicate gameplay matrix is claimed or required.

The global install layout, duplicate-scope refusal, native-cast regression,
retrigger behavior, multiplayer authority and a complete all-features Suite
matrix were not run. The five eezstreet plugins were present in the installed
stack, but that alone is not presented as proof that every feature was active.

## Repository-wide verification

The plugin build, policy test, archive inspection and cadastre schema validation
pass. The repository-wide `npm run verify` returns exit code 1 at workstream
ownership because the already dirty workspace contains numerous unassigned
files, including this not-yet-registered addon. The active mission pointer and
cadastre are valid; this repository hygiene state does not change the packaged
binary, but the workspace is not checkpoint-ready.

## Rollback

Remove the DLL and TOML. The plugin owns no table replacement, save payload or
migration.
