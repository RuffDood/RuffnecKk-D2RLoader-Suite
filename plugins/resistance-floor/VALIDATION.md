# Resistance Floor validation

The completed evidence below belongs to candidate 0.3.0. Version 1.0.0 must be
rebuilt and requalified as the exact final artifact before release.

Validation date: 2026-08-25
Targets: Diablo II: Resurrected 3.2.92777 and 3.3.93847
Plugin SHA-256: `A3685451B8E5D1119AD10F75F5EE70760C0387E8FA42E8E2A540C0C929F642BB`
Configuration SHA-256: `063AF09EAF5BBF5ADCD4E11A88FA1C2B1F93AD13826789E04B25A10C037224B0`
Candidate ZIP SHA-256: `6724100183AD4EE79FE2D8E86972EE64CD33034E8CB5557CC07BFA8477653C71`

## Automated gates

- Clean Release x64 build: PASS, zero compiler warnings.
- Policy and source-contract test suite: PASS, 1/1.
- Byte reproducibility: PASS; two builds, including one clean build, produced
  the same plugin hash.
- Player-facing configuration version 3 names, strict TOML types, required
  settings, unknown-key rejection and bounds `[-1000, -100]`: PASS.
- PE contract: PASS; x64, ASLR, high-entropy ASLR, NX and exactly the three
  D2RLoader exports.
- Governed RVA JSON parse and workbench status: PASS.
- PluginSDK commit: `4933e2c42cb2592958cd0df3b6dc5003102252d1`.
- Cross-plugin display dependency: none.
- Candidate archive contents: PASS; DLL and TOML only. README remains beside
  the archive for human review.

## Runtime provenance

The installed runtime matches the governed baseline exactly:

- version: `3.3.93847`;
- Build Key: `623f7a1f73eabb08ccb2b2046e3f9164`;
- `.build.info` SHA-256:
  `2EBCAD0521DBF038D5A7FE5395E96B4BEF6D4F0774F7B1F840E03C3DE9CB067A`;
- `D2R.exe` SHA-256:
  `E1F5436E3D9687F644EF16938B1B183D1FDEF434F18CF66D852CF68F48CC8936`.

The source and final mod-local runtime copies of both candidate files are
byte-identical.

## Cold-start matrix

No installed plugin or PluginPack feature was disabled. The installed
inventory includes all five eezstreet plugins and the active RuffnecKk Suite.

| Installation | Status | Evidence |
|---|---|---|
| Mod-local BKVince 0.3.0 | PENDING | Fresh full-stack cold start with `config_version = 3`. |
| Global plus mod-local duplicate | prior PASS / not rerun | Candidate 0.1.0 proved that the mod-local instance wins and the global duplicate is skipped. Scope-selection code is unchanged in 0.3.0. |
| Global only | prior PASS / not rerun | Candidate 0.1.0 proved global-only loading and global TOML selection. Scope-selection code is unchanged in 0.3.0. |

The final runtime installation is mod-local under BKVince. Its DLL and dedicated
TOML will be checked byte-for-byte against the 0.3.0 sources after deployment.
The D2RLoader-generated companion TOML is also refreshed so removed settings do
not remain visible. No installed plugin or PluginPack feature may be disabled.

An initial diagnostic run was correctly refused because D2RLoader requires a
tracked `jmp-rel32` safety-check size equal to the five-byte patch size. The
final implementation retains the independent full twelve-byte preflight
witness, then supplies the exact five-byte `MOV` witness to the tracked write.
The fresh mod-local row will use the final byte-identical 0.3.0 candidate; the two
scope-retention rows explicitly preserve their earlier 0.1.0 provenance.

## Functional gameplay matrix

| Case | Status | Expected proof |
|---|---|---|
| Player resistance floor below `-100` | external PASS | Tester confirmed that resistance affects actual in-game damage below `-100`, not only the displayed value. |
| Hireling, summon, pet and Revive ownership | not run | Owned unit receives configured floor; ordinary monster remains at `-100`. |
| Monster opt-in | external PASS | Tester confirmed that configured monsters are functionally affected below `-100`. |
| Native Fire/Lightning/Cold/Poison display | external PASS | Tester confirmed that the native display passes below `-100`. |
| Eleven-times damage edge and integer safety | not run | Controlled highest-damage cases without overflow or instability. |
| Solo, host and client authority | not run | Separate observations for each network role. |

The external functional confirmation was reported by Vincent on 25 August
2026. Monster Display's separate hardcoded display floor remains owned by that
plugin; yinyin confirmed that Monster Display will be updated independently.
Vincent accepted Resistance Floor as functional and ready for inclusion in the
next RuffnecKk D2RLoader Suite release. Rows still marked `not run` remain open
for the release-specific qualification matrix.

## Scope boundaries

- No TXT or TSV edit.
- No upper resistance-cap mutation.
- No eezstreet binary modification or redistribution.
- No ROADMAP edit, per the implementation instruction.
