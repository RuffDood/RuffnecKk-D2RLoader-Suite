# Build 93847 argument-enriched RVA reference intake

Received from the user on 2026-09-21 as
`D2R_RVA_reference_build_93847_with_arguments.md`. The [original document](D2R_RVA_reference_build_93847_with_arguments.md)
is preserved byte-for-byte. Its [source manifest](rva-93847-source.json) records
the SHA-256 and provenance. Author and upstream archive identities were not supplied.

## Evidence status

This is reference material, not operational instructions or verified native
evidence. All imported contracts remain unverified source claims, including those
labelled "Exact", "production", "probed" or "qualified".

The document contains 226 distinct D2R addresses. At intake, 36 overlap the current
registry and 190 are absent. The [generated inventory](rva-93847-inventory.generated.md)
lists every row, source line and matching registry name; address overlap alone does
not establish semantic or ABI agreement.

The document's final source note says its per-entry Source column is retained in
a separate TSV edition. That TSV and the audited source archives were not supplied.
Preserve this provenance gap until primary sources become available.

## Known conflict

At source line 344, `D2R+0x858510` names opacity in XMM2 in the register contract
but calls it the fourth argument in XMM3 in the description. The callsite row at
line 346 repeats the XMM2 contract. Neither alternative is admitted by this intake.
Resolve the disagreement with caller/register and callee evidence before using it.

Loader/core helper and relay addresses are explicitly version-sensitive in the
source. Do not promote them as stable retail entries or assume current hook ownership.

## Integration

The original reference and inventory are discoverable through the workbench README.
No imported row is automatically inserted into `known-rvas.json`. For a concrete
consumer, check existing evidence, prove each relevant surface against the governed
corpus and current provider, and then add or enrich the registry with precise
sources and an [argument contract](../argument-contracts.md).

The workbench status check during intake verified the canonical image, analysis
image and index. This establishes corpus integrity, not correctness of the
imported addresses or their claimed runtime behavior.
