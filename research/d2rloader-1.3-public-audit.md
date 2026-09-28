# D2RLoader 1.3.0-beta public compatibility audit

Date: 2026-09-15. Implementation authorized by Vincent in the Loader update task.

## Result and limits

The active Suite catalog builds and passes its offline checks against the exact
public D2RLoader 1.3 artifact set. Ten plugins consume updated Core witnesses.
Public Loader 1.3 is installed and retained after the authorized cold start:
all 23 Suite DLLs and 17 Suite patches loaded, and the overlay rendered its first
frame. Gameplay, save/reload and multiplayer remain untested. Complete-stack
qualification is blocked by the pre-existing non-Suite Doll Explosion failure.
The historical promoted baseline remains 1.2.1; the installed runtime is 1.3.

The scope is the existing Suite 1.3.4 draft: 23 plugins and 17 patches. Prior
removals of ISC12 and Extended Act Level IDs are preserved. The loader's extended
save format does not establish migration compatibility for old ISC12 saves.

## Provenance

Official pages: https://d2rloader.net/download.html and
https://d2rloader.net/changelog.html. The latter dates this release September 15,
2026. A direct fresh request to the download page advertised the public ZIP and
its checksum; the web search cache initially returned the older download page.

Downloaded ZIP: `D2RLoader-1.3.0-beta.zip`, 46,946,021 bytes.
SHA-256: `E97D80722066BCB84F96D5BBC4C37BAE592E99C4B9A2C78DCAA35105E739F2A9`.
The local ZIP matches the official checksum exactly.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| D2RLoader.exe | 23590248 | B1066FF64B3168C0B7D25BB72A150FDE3F76D46A110C15B4893C4CCB1B696F89 |
| D2RCore.dll | 18345304 | AE1EA9B7F97AF5B89A550281E6A6C6B6E9C74E73AC8759E6558B40E751428CD0 |
| d2rloader.mpq | 10816549 | D5EBCD3488E3E0DE0CDF3938BD1DB2C6B4953129C9B651C949BDBEA969AF0BC6 |

Both PEs report `1.3.0-beta`. Both signatures expose signer thumbprint
`8001082E968FADAA58BF2F73EFA1810A11D0C28F`; Windows reports `UnknownError`.
The signer observation is not represented as successful Windows trust-chain
validation. Archive authenticity is supported separately by the official hash.
The MPQ is byte-identical to candidate.4; both PEs differ.

## Contract audit

All 334 preceding named Core exports retain their ordinals and move addresses.
`DeleteCharacterSaveFile` is added. Export names alone are not native ABI proof.

The native workbench passed canonical image, analysis image and index integrity
checks through `npm run re:d2r33 -- status`. The D2R corpus remains unchanged.
The Suite retains PluginSDK v3 commit
`4933e2c42cb2592958cd0df3b6dc5003102252d1` and v4 commit
`6eb8f8b6192868214706bd6d528c5294f2f551b7`.
All 63 vendored files listed by the two upstream hash manifests match.

The new service dispatcher preserves the candidate.4 service table sizes and
versions. Relative to promoted 1.2.1, Localization uses v2; the Suite already
contains its explicit v2 negotiation and v1 fallback. Seventeen published
services and unpublished service 18 remain present. Of 82 table callbacks, 81
have identical instruction structure after address relocation. The other,
ItemInteraction's callback at table offset 16, changes a private TLS member
offset from 0x570 to 0x590. No active Suite source consumes that service.
No new capability or SDK migration is needed by this task.

The archive's configuration template is retained for audit, not installed over
player settings. MPQ resource paths remain identical to candidate.4. The
compiled plugin metadata gate validates all active DLLs' supported API, role,
hybrid flags, author, manifest and exports. Static callback comparison does not
prove complete transitive behavior, thread scheduling or runtime lifetime;
those remain part of the runtime qualification gates.

## Native adaptations

`common/src/native_stat_compat_contract.inc` now contains the exact public Core
functions, instruction references, read-only witnesses, native import
descriptors and unwind programs for all seven admitted stat helpers.

| Helper | Public export RVA | Public implementation RVA |
| --- | --- | --- |
| GetUnitStat | 0x7A99D0 | 0x3E2E40 |
| AddUnitStat | 0x7A9CC0 | 0x3E4150 |
| MergeStatLists | 0x7AA120 | 0x3E4C60 |
| WeaponMastery | 0x7AAC60 | 0x3DED90 |
| GetUnitBaseStat | 0x7A99C0 | 0x3E2D40 |
| SetUnitStat | 0x7A9C20 | 0x3E3E60 |
| GetUnitAlignment | 0x7AA7B0 | 0x3E2BF0 |

All fourteen bodies preserve instruction structure, internal branches,
registers, constants and non-address operands. Imported native target identities
and all seven unwind programs are preserved. Read-only exception evidence
separately verifies the retained `bad allocation` string and MSVC ThrowInfo,
catchable types, destructor and copy functions. Full replacement witnesses
remain exact; no wildcard or version-based admission was introduced.

Vendor Stock Refresh's 420-byte `SendClientGameplayPacket` provider moves to
0x79BD10. Its entry, body hash, forwarding instruction/slot, PDATA, unwind and
FuncInfo are updated from the public PE. Its forwarding ABI and branch structure
remain unchanged. Existing earlier public provider profiles remain intact.

Stack Manager's reader, cap and credit functions move to 0x3A26B0, 0x3A2640 and
0x79F1A0. Their complete body witnesses, imports and referenced limit cell are
updated; native target identities and non-address operands remain unchanged.

The candidate.4-specific fingerprints are replaced, not admitted by version.
Pre-edit sources and prior candidate artifacts remain available for rollback.
The stat adapter's canonical route remains admitted independently of Core.

## Active plugin matrix

Every row passes Release x64 compilation and compiled metadata inspection.
Every row requires public-release runtime testing.

| Plugin | Static disposition |
| --- | --- |
| Bulk Currency Deposit | Retest; no source adaptation required |
| Bulk Skill Point Allocation | Retest; existing Localization v2 adapter retained |
| Burn Damage Fix | Adapted: shared stat witnesses |
| Cast Triggers | Adapted: shared stat witnesses; Damage Cleanup stays off |
| Charm Aura Trigger Fix | Adapted: shared stat witnesses |
| Cube Quick Move | Retest; no source adaptation required |
| Enhanced Damage Min/Max Fix | Retest; no source adaptation required |
| Equipped Item to Cube | Retest; no source adaptation required |
| Ethereal Item Rules | Retest; no source adaptation required |
| Floating Damage | Adapted: shared stat witnesses |
| Item Durability | Retest; no source adaptation required |
| Larzuk Sockets | Adapted: shared stat witnesses |
| Mass Identify | Adapted: shared stat witnesses |
| Auto Pickup | Retest; current split-plugin API retained |
| Stack Manager | Adapted: shared stats and stash witnesses |
| Prevent Merc Death in Town | Adapted: shared stat witnesses |
| Progressive Affixes | Retest; no source adaptation required |
| Remote Stash | Retest; no source adaptation required |
| Repair Costs Cap | Retest; no source adaptation required |
| Vendor Stock Refresh | Adapted: packet-provider witnesses |
| Armageddon-Hurricane CtC Fix | Retest; current catalog source built |
| Resistance Floor | Retest; stale documentation-only test fixed |
| MapSense | Adapted: shared stat witnesses; current source retained |

All 17 retained JSON patches pass the catalog and native-write ownership checks.
Loader coexistence and actual application of every patch still require a fresh
runtime receipt. Historical DLLs excluded from the catalog are not declared
compatible by these results.

## Validation evidence

All local evidence is under
`analysis-cache/d2rloader-release-intake/d2rloader-1.3-public/`.

- `audit/build.log`: complete Release x64 build with `/WX` enabled.
- `audit/ctest.log`: 47 of 50 checks initially passed. Two assertions still
  pinned candidate.4 addresses; Resistance Floor referenced a previously moved
  VALIDATION.md. Address assertions were updated from PE evidence, and the stale
  documentation dependency removed while retaining product metadata checks.
- `audit/ctest-corrected.log`: all four targeted checks pass after correction;
  together with unchanged passing checks, all 50 checks pass.
- `audit/replay/result.log`: all seven exact PE provider routes, seven individual
  helper masks, 99 corruptions, unwind association, failed-rebind reset and the
  canonical route pass. The fixture models loader-installed descriptors and
  relays offline; it is not evidence of live hook installation.
- `audit/compiled-plugin-metadata.json`: actual versions, roles and SHA-256 for
  all 23 DLLs, inspected with the same C++ metadata gate used by
  Build-NativePlugins.ps1. That wrapper enumerates legacy laboratory sources,
  so the authoritative Suite was built with its CMake root and the inspector
  compiled separately. No laboratory source was substituted.
- `audit/core-delta.json`, `audit/relocation-proof.json`,
  `audit/service-callback-delta.json`: exact artifact and contract comparisons.
- `audit/source-before/`: exact pre-edit files, including uncommitted work.

## Authorized deployment and retained runtime

On September 15, Vincent explicitly authorized installing public Loader 1.3,
synchronizing the Suite, one cold start, and keeping the update if successful.
The BKVince runtime at `C:/Games/Diablo II Resurrected` now contains the three
public Loader artifacts and all 23 Suite DLLs / 17 Suite patches from the audited
build. D2R is 3.3.93847, build key `623f7a1f73eabb08ccb2b2046e3f9164`.

The governed synchronizer applied 15 replacements and one new patch, with 24
files already identical. No opposite-scope collisions occurred. Player configs
were excluded from copying; the Loader reported its normal configuration sync.
The existing Armageddon CtC filename now contains Armageddon-Hurricane CtC Fix,
avoiding a duplicate owner. Third-party plugins and resources remain installed.

| Check | Result |
| --- | --- |
| Cold start | Passed: startup 24/24 complete at 07:37:27 EDT |
| Suite DLLs | Passed: 23/23 compiled names and versions appear in fresh load lines |
| Suite patches | Passed: 17/17 applied; existing Normal Area Scaling adds an eighteenth |
| eezstreet pack | All five plugins loaded |
| Rendering coexistence | MapSense host initialized; Floating Damage rendered its first frame |
| Complete installed stack | Blocked: 39 loaded, one failed; existing non-Suite Doll Explosion refuses its corpse-explosion fingerprint |
| Gameplay, save/reload, multiplayer | Not run |
| Final synchronization | Passed: 43 installed artifact hashes match audited sources |
| Shutdown | Owned game-host PID 33716 closed gracefully; lease released as updated |

The Doll Explosion failure also appears in the retained preflight log, so it is
not a new failure introduced by this update. It was not disabled to obtain a
cleaner result. Full-stack baseline promotion remains blocked; the historical
1.2.1 promotion record is not the installed runtime version.

Previous plugin binaries and individual deployment receipts are retained under
each registered component home. Previous preview.8 Loader artifacts are retained
under `analysis-cache/d2rloader-release-intake/d2rloader-1.3.0-beta-preview.8/backups/20260915-public13/`.
Public 1.3 remains installed as requested; no previous Loader was reinstalled.

The composite receipt, exact backup paths, final hashes, preflight and fresh logs
are in `analysis-cache/runtime-deployments/loader/20260915-public13-suite/receipt.json`
and its adjacent files. Source backups remain under the intake audit directory.
No commit, push, publication or baseline promotion was performed.
