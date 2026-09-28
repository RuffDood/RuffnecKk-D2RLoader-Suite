# RuffnecKk D2RLoader SDK map for D2R 3.2.92777

Hey Dimentio,

This package collects the reusable native evidence I have confirmed while
working on plugins and memory patches for build 92777.

## Start here

Read `UPSTREAM-PROPOSAL.md` first. It proposes one small v2-compatible
`TryGetItemInventoryPage` service.

The JSON files are supporting evidence. They are not intended to be merged into
D2RLoader as code.

## Snapshot

- D2R build: `3.2.92777`
- Image base: `0x140000000`
- Canonical image SHA-256:
  `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`
- Main map: 206 governed entries from 23 evidence groups
- Memory-patch map: 61 unique sites with zero expected-byte mismatches

Every RVA is pinned to this build and must be checked again before use on
another D2R version.

## Files

| File | Purpose |
|---|---|
| `UPSTREAM-PROPOSAL.md` | One focused SDK change with its contract and acceptance checks |
| `verified-rvas.json` | Functions, call-sites, branch-sites, patch-sites and known consumers |
| `verified-memory-patches.json` | Memory-patch RVAs, expected bytes and original validation status |
| `sdk-candidates.json` | Verified tables, fields, helpers and the shared-entry case |
| `verified-layouts.hpp` | Minimal compile-time checked C++ layout fragments |

The package contains no game binary, analysis cache or speculative full
structure.
