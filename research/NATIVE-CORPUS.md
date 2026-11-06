# D2RLoader native corpus identity

The workspace's native reference is the **captured D2RLoader-hosted game image**,
whose documented upstream game target is **3.1.1.4**.
It is not an independently captured Blizzard retail D2R 3.2 or 3.3 executable.
Use this identity when explaining RVAs, layouts, signatures and ABI findings.

## Verified capture

The original capture on 2026-07-17 read selected sections from the running
`D2RLoader.exe` process, PID `40036`, at image base `0x140000000`. The process
path was `C:\games\Diablo II Resurrected\D2RLoader.exe`; the recorded module
size was `65495040` bytes. Its launch log records `D2RCore 1.0.1-beta`, mod
`D2RMM`, and no external plugins or memory patches.

`scripts/migrate-bkvince/dump-d2r-process-image.py` used the installed `D2R.exe`
as a PE template, then replaced selected section bytes with `ReadProcessMemory`
results from that Loader process. A template filename or installed CASC build
does not identify the retail ancestry of those executing code bytes.

| Artifact | SHA-256 |
| --- | --- |
| Reconstructed canonical PE | `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715` |
| Analysis PE | `673E8C0B2E89563E75525B24D137098EFD07B2DB4ED42ADEC56AA1ADDF0E63AB` |
| Captured `.text` | `35AA4478F59B766189F79C14E26A1A49CEFF1666126AB0CB496276485D398E37` |

The analysis PE preserves captured code but uses disk-sourced `.pdata`, `_RDATA`
and `.rodata`. Those sections have separate provenance. In particular, a
`.pdata` boundary is a query aid: bound a logical function by its actual code
and control flow when metadata splits it or includes adjacent functions.

The hashes identify reconstructed artifacts, not retail releases or every
runtime patch state. A captured branch to a Loader/Core relay also requires
evidence for its target and the current provider; the dump alone is incomplete.

## Version labels and backports

The installed environment reported `3.2.92777` during capture. Later records use
`3.3.93847` and Steam `3.3.93787`. Treat those as installation, data or historical
test labels unless a separate native comparison establishes more.

The directory `d2r-3.2.92777`, artifact filenames, `re:d2r32` / `re:d2r33` command
aliases, old registry `build` fields and imported `93847` documents retain their
historical identifiers. They do not establish retail native identity or retail
cross-build equivalence. Read dated findings and generated contract scope labels
under this correction. Preserve original imported documents and recorded tests.

The [official release notes](https://d2rloader.net/changelog.html), checked on
2026-10-16, explicitly describe a 3.2 Herald backport and changes to native
limits, saves and game messages. They support Loader-provided behavior changes;
they do not establish that every change is only a data edit.

The official wiki's [Modders page](https://wiki.d2rloader.net/en/modders),
checked on 2026-10-16, explicitly identifies **3.1.1.4** as D2RLoader's game
target and distinguishes it from Blizzard's 3.3.0 data guide. This verifies the
documented base target; it is not a claim that the captured image is an untouched
retail 3.1.1.4 PE. Use the Loader-hosted base target plus actual backport/Core
state when describing the effective runtime.

Vincent reports stable base RVAs across Loader updates. Reuse matching native
surfaces accordingly; neither the wiki nor the retained capture is a universal
guarantee for every future release. Core provider RVAs and active hook behavior
remain separate from the base-image RVA map.

## Applying the reference

1. Run `npm run re:d2r33 -- status` to verify the existing corpus. Both legacy
   command aliases query the same captured image by default.
2. Describe a location as `captured D2RLoader game image + RVA`. Older `D2R+RVA`
   notation refers to that image in this workspace; it does not identify the
   separate `D2RCore.dll` module or the outer launcher's on-disk PE layout.
3. Reuse verified base-image RVAs when their required surfaces match. An
   installed retail update is not by itself a reason to redump or rebase them.
4. Record Loader/Core artifacts, SDK contracts, active hooks/providers, mod data
   and runtime results separately. Stable base RVAs do not prove unchanged
   instructions, provider RVAs, ABI, layouts, ownership or behavior.
5. Admit native access by the required fingerprints and contracts. Neither a
   retail build label nor a Loader release number is a compatibility allowlist.
   Create another workbench only after a materially different native image is
   evidenced, not just a different installation label.

## Reproducible provenance sources

- Original local session `019f72e7-904c-7403-89ee-7b41919fe107`, archived as
  `rollout-2026-07-17T21-46-51-019f72e7-904c-7403-89ee-7b41919fe107.jsonl.zst`:
  line 1596 contains the launch/Core/environment log; line 1600 identifies the
  process, PID and image base; lines 1642 and 1650 record reconstruction hashes
  and captured sections. These records were re-read on 2026-10-16.
- Capture implementation (`scripts/migrate-bkvince/dump-d2r-process-image.py` in the laboratory):
  PE template, read-only process access and section replacement.
- [Workbench manifest](d2r/workbench.json): artifact/section hashes and
  separate capture provenance.
- Hydration implementation (`scripts/reverse-engineering/d2r32.py` in the laboratory):
  `command_hydrate` preserves captured code while supplying non-code metadata.

This correction supersedes earlier assertions that this corpus proves native
identity between Blizzard retail builds 92777 and 93847. Individual byte-level
findings remain evidence for the identified captured image.
