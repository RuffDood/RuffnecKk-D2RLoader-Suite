# Further SDK Services requests

## PluginSDK v4 audit — 2026-08-31

PluginSDK v4 at commit `6eb8f8b6192868214706bd6d528c5294f2f551b7`
adds `ServiceId::Http = 16`, `ServiceId::ItemInteraction = 17`, and
`ItemServiceV1::executeExistingItemTransaction`. The four requests below now
have these governed statuses:

| Request | v4 status | Workspace consequence |
|---|---|---|
| Loader-owned native publication transaction | **Open, optional hardening** | ISC12 now has an experimental same-thread startup caller using the official `D2RLoaderLoadPlugin` patching model. This request remains useful for a documented reusable cross-publisher contract, but no longer blocks the isolated ISC12 runtime spike. The former provisional id 16 remains invalid because v4 assigns it to `Http`. |
| Typed item interaction events | **Delivered for the proven V1 surfaces** | `ItemInteractionServiceV1` provides semantic `Activate` events for inventory, Cube, personal stash and the current custom page. Shared stash and authoritative gameplay remain outside V1. |
| Atomic edits and moves for existing items | **Delivered for the supported V1 operations** | `executeExistingItemTransaction` atomically debits, edits or moves up to 64 identity-preserving items, with the container restrictions documented below. |
| Plugin-provided service discovery | **Open** | Inter-DLL providers still require an independently versioned and unload-safe discovery contract. |

`HttpServiceV1`, richer lifecycle events, Hardcore/Softcore character-creation
metadata and duplicate-Unique creation are useful v4 additions but do not close
either remaining request. No RuffnecKk plugin was migrated as part of this
audit.

### D2RLoader 1.2 baseline — 2026-08-31

The official D2RLoader `1.2.0-beta` release dated 2026-08-30 adds official D2R
3.3 support and is the runtime baseline associated with PluginSDK v4. The v4
SDK explicitly preserves API-v2 and API-v3 plugin compatibility, so ISC12's
current API-v3 pin remains supported. Public v3/v4 `PluginContext`, Patch, Hook,
Lifecycle and low-level export headers are byte-identical.

The 1.2 changelog announces no NativePublication, load-phase, Patch/Hook or
unload contract. The public service registry still ends at
`ItemInteraction = 17`; therefore 1.2 does not close this request. Absence from
release notes is not binary proof of private Core internals. ISC12 nevertheless
uses the synchronous initial-load patching boundary for its disposable-profile
runtime spike and terminates on every post-write ambiguity. The SDK/conformance
work below remains valid as optional hardening for a later loader release.



## 1. Loader-owned native publication transaction

### Verified gap

The current header-only PluginSDK v4 at commit
`6eb8f8b6192868214706bd6d528c5294f2f551b7` exposes additive services through
`QueryService`, but its public `ServiceId` values stop at
`ItemInteraction = 17`. Patch and hook calls remain independent; Lifecycle,
ThreadService and the new item transaction do not exclude every D2R consumer
or serialize executable publishers. A plugin can preflight bytes, but cannot
prove that executable publication is quiescent, globally serialized with every
other publisher, or contained if a native write reports an uncertain result.

The minimum viable addition remains one optional synchronous service. Its
numeric ID must be officially reserved upstream. IDs 16 and 17 are already
assigned to `Http` and `ItemInteraction`; no local consumer may assume a
replacement value.

The following is a compilable header shape, not an assigned service-registry
entry. It deliberately contains no numeric `ServiceId` value:

```cpp
#pragma once

#include <D2RLPlugin/services.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace D2RL {

struct PluginContext;

namespace NativePublication {

enum class CallbackOutcome : uint32_t {
    Committed = 0,
    Rejected = 1,
    Poisoned = 2,
};

enum class Result : uint32_t {
    Committed = 0,
    Rejected = 1,
    Poisoned = 2,
    InvalidArgument = 3,
    Unavailable = 4,
    Busy = 5,
    WrongPhase = 6,
    OwnerInactive = 7,
    CallbackFault = 8,
};

using ValidateLeaseFn = bool(__cdecl*)(
    const PluginContext* context,
    uint64_t epoch,
    uintptr_t token) noexcept;

// Borrowed from Core. The pointer and every field expire when CallbackFn
// returns. A plugin never releases, destroys, persists, or transfers it.
struct LeaseViewV1 {
    uint32_t structSize;
    uint32_t flags;
    uint64_t epoch;
    uintptr_t token;
    ValidateLeaseFn validate;
};

using CallbackFn = CallbackOutcome(__cdecl*)(
    const PluginContext* context,
    const LeaseViewV1* lease,
    void* userData) noexcept;

struct RequestV1 {
    uint32_t structSize;
    uint32_t flags;
    CallbackFn callback;
    void* userData;
};

inline constexpr uint32_t LeaseViewV1Size =
    static_cast<uint32_t>(sizeof(LeaseViewV1));
inline constexpr uint32_t LeaseViewV1RequiredSize = LeaseViewV1Size;
inline constexpr uint32_t RequestV1Size =
    static_cast<uint32_t>(sizeof(RequestV1));
inline constexpr uint32_t RequestV1RequiredSize = RequestV1Size;

inline auto HasLeaseViewV1Field(
        const LeaseViewV1* lease,
        uint32_t fieldEndOffset) noexcept -> bool {
    return lease != nullptr && lease->structSize >= fieldEndOffset;
}

inline auto HasRequestV1Field(
        const RequestV1* request,
        uint32_t fieldEndOffset) noexcept -> bool {
    return request != nullptr && request->structSize >= fieldEndOffset;
}

using ExecuteFn = Result(__cdecl*)(
    const PluginContext* context,
    const RequestV1* request) noexcept;

static_assert(sizeof(CallbackOutcome) == sizeof(uint32_t));
static_assert(sizeof(Result) == sizeof(uint32_t));
static_assert(sizeof(void*) == 8);
static_assert(sizeof(uintptr_t) == 8);
static_assert(std::is_standard_layout_v<LeaseViewV1>);
static_assert(std::is_trivially_copyable_v<LeaseViewV1>);
static_assert(std::is_standard_layout_v<RequestV1>);
static_assert(std::is_trivially_copyable_v<RequestV1>);
static_assert(offsetof(LeaseViewV1, structSize) == 0);
static_assert(offsetof(LeaseViewV1, flags) == 4);
static_assert(offsetof(LeaseViewV1, epoch) == 8);
static_assert(offsetof(LeaseViewV1, token) == 16);
static_assert(offsetof(LeaseViewV1, validate) == 24);
static_assert(LeaseViewV1RequiredSize == 32);
static_assert(sizeof(LeaseViewV1) == 32);
static_assert(offsetof(RequestV1, structSize) == 0);
static_assert(offsetof(RequestV1, flags) == 4);
static_assert(offsetof(RequestV1, callback) == 8);
static_assert(offsetof(RequestV1, userData) == 16);
static_assert(RequestV1RequiredSize == 24);
static_assert(sizeof(RequestV1) == 24);

} // namespace NativePublication

struct NativePublicationServiceV1 {
    uint32_t serviceSize;
    uint32_t serviceVersion;
    NativePublication::ExecuteFn executeStartupTransaction;
};

inline constexpr uint32_t NativePublicationServiceV1Version = 1;
inline constexpr uint32_t NativePublicationServiceV1Size =
    static_cast<uint32_t>(sizeof(NativePublicationServiceV1));
inline constexpr uint32_t NativePublicationServiceV1RequiredSize =
    NativePublicationServiceV1Size;

inline auto HasNativePublicationServiceV1Field(
        const NativePublicationServiceV1* service,
        uint32_t fieldEndOffset) noexcept -> bool {
    return service != nullptr
        && service->serviceSize >= fieldEndOffset
        && service->serviceSize
            >= offsetof(NativePublicationServiceV1, serviceVersion)
                + sizeof(service->serviceVersion)
        && service->serviceVersion == NativePublicationServiceV1Version;
}

static_assert(std::is_standard_layout_v<NativePublicationServiceV1>);
static_assert(std::is_trivially_copyable_v<NativePublicationServiceV1>);
static_assert(offsetof(NativePublicationServiceV1, serviceSize) == 0);
static_assert(offsetof(NativePublicationServiceV1, serviceVersion) == 4);
static_assert(offsetof(
    NativePublicationServiceV1,
    executeStartupTransaction) == 8);
static_assert(NativePublicationServiceV1RequiredSize == 16);
static_assert(sizeof(NativePublicationServiceV1) == 16);

} // namespace D2RL
```

`RequestV1::flags` and `LeaseViewV1::flags` must both be zero in V1. The
consumer must size-gate every field before reading it and reject unknown flags.
Core accepts a request only when `HasRequestV1Field` covers
`RequestV1RequiredSize` and `callback` is non-null. A plugin reads
`executeStartupTransaction` only when `HasNativePublicationServiceV1Field`
covers `NativePublicationServiceV1RequiredSize`, and its callback proceeds only
when `HasLeaseViewV1Field` covers `LeaseViewV1RequiredSize` and `validate` is
non-null.
`token` is opaque: a plugin may only pass the unchanged value, with `epoch`, to
`validate`. It must not dereference, compare, log, serialize or manufacture the
token. The lease view is callback-scoped borrowed data. Core revokes it on
callback return; the plugin neither owns it nor calls any release operation.

### Normative Core behavior

#### Exact startup boundary and order

V1 is startup-only and synchronous. The sole legal caller is the active
owner's initial `D2RLoaderLoadPlugin` invocation, on the loader's startup
publisher thread, after the service query succeeds and before that export
returns. A call from a lifecycle/game callback, another thread, unload, reload,
or a second startup invocation returns `WrongPhase` without invoking the
callback.

Core must close one global startup barrier before its first executable
publisher and keep it closed until its last publisher completes. It must fix a
deterministic total publisher order before the first write, then run each Core
publisher, DLL-less JSON publisher, Patch/Hook publisher and plugin transaction
in that resolved order without interleaving. A transaction callback occupies
the active owner's position in that order and completes before
`executeStartupTransaction` returns. Only after every publisher has completed
successfully may Core publish global readiness and allow the first D2R consumer
to run. The barrier must remain closed through callback return, terminal-result
classification and any required owner pin. A plugin publishes its private
readiness only after `executeStartupTransaction` returns `Committed` and before
its initial `D2RLoaderLoadPlugin` export returns. Core retains the barrier
through that export and every later publisher, so a non-committed service result
never requires a plugin-private readiness rollback. The current PluginSDK v4
does not provide or prove this phase; these are requirements for the proposed
Core implementation.

Nested publication or a second concurrent publisher returns `Busy`. The active
callback may prepare private unpublished resources before its call, then use
only transaction-aware loader Patch/Hook paths. Those paths must recognize the
already-held gate and must not reacquire it. The callback must not defer work,
hand the lease to another thread, or return while a write or instruction-cache
flush is pending.

Each transaction-aware operation linearizes against Core's atomic
`Open -> Finalizing` transition. An operation admitted while `Open` completes
before result classification. Once finalization wins, a deferred or
wrong-thread call fails without touching target memory. This containment does
not relax the callback's obligation to join all work before returning.

#### Lease and mutation boundary

The lease is bound to the active plugin owner generation, callback thread and a
monotonic epoch. Core validates all three independently inside every
transaction-aware Patch/Hook path. A copied `LeaseViewV1` has no authority after
the callback returns.

Direct executable writes are outside the cooperative contract and forbidden to
participating publishers. This includes plugin `memcpy`/stores into executable
ranges, `WriteProcessMemory`, direct page protection plus stores, private Core
ordinals, third-party detour installers and any Patch/Hook entry point that
bypasses the active transaction. Core can refuse bypasses through its own
internal, DLL-less and Patch/Hook paths, but cannot honestly prevent an
arbitrary same-privilege native DLL from using `NtProtectVirtualMemory`, a
direct system call or another private writer. Such a DLL is outside the V1
threat model and must be excluded by compatibility policy unless a separate
enforcement mechanism is specified.

Core owns a sticky TLS transaction bit named here `mutationAttempted`.
Preflight, fingerprint reads, lease validation, allocations, process-lifetime
reservation and construction of unpublished private relay bytes leave it
false. Core sets it to true **immediately before** the first underlying
transaction-aware operation that can actually modify a governed executable
byte or publish a hook target—not when that operation returns. It remains true
until the transaction reaches a terminal result. A no-op slot whose live bytes
already equal the requested bytes does not set it; the first real write does.

`Rejected` is legal only while Core proves `mutationAttempted == false`. A
rejection, false/ambiguous Patch or Hook result, lost lease or any other
uncertain result after the bit becomes true is promoted to `Poisoned`.

| Core observation | Before `mutationAttempted` | After `mutationAttempted` |
|---|---|---|
| Owner generation is inactive or becomes invalid | Return `OwnerInactive`; no executable mutation occurred. | Return/record `Poisoned`, retain quiescence and fail fast before any D2R consumer can resume. |
| A callback boundary fault is caught | Return `CallbackFault`; no executable mutation occurred. | Return/record `Poisoned`, retain quiescence and fail fast before any D2R consumer can resume. |

The callback type is `noexcept`: plugin code must not let a C++ exception cross
the ABI boundary. `noexcept` does not catch Windows structured exceptions. Core
must add and test its own SEH boundary for faults it can safely classify, such
as an access violation, and apply the table above. `std::terminate`, explicit
fail-fast, stack corruption and other non-recoverable process failures cannot
honestly be reported as `CallbackFault`; the process is already terminal.

`Poisoned` must log and terminate/fail-fast while quiescence remains held. The
enum remains observable for ABI tests and an injected fatal-handler test, but
production must never resume D2R consumers from that state. Only `Committed`
and a demonstrably pre-write `Rejected`, `OwnerInactive` or `CallbackFault` may
release transaction quiescence.

A committed executable publication is process-lifetime. Once
`mutationAttempted` is true and the callback returns `Committed`, Core must pin
the plugin module and loader-owned installed targets until process exit. It
must not call `D2RLoaderUnloadPlugin`, unmap the DLL, restore bytes or offer hot
reload; replacing that owner requires a cold restart. The plugin is responsible
for reserving its private relay/state allocations for process lifetime before
the first potentially modifying loader call. Private relays may be allocated,
populated, made executable and registered for unwind before the callback only
while no live D2R code can reach them; publishing a D2R call, pointer or
dispatch edge to them is a transaction mutation. Retaining an unpublished
private reservation after a clean pre-write rejection is an acceptable V1
tradeoff. The same module/resources remain pinned on `Poisoned` until fail-fast.
A callback that commits a true no-op without setting `mutationAttempted`
creates no executable lifetime dependency.

This is deliberately not a generic plugin-minted token or a thread-suspension
helper. The local D2RCore 1.1.0-beta binary contains thread suspend/resume and
native write machinery, but that proves only useful substrate—not a safe
boundary, no-new-thread guarantee, lock safety or public ownership contract.
Private Core ordinals and binary patches cannot substitute for a loader-owned
service.

### Required upstream slice

The SDK change remains additive: reserve a new service ID, add
`native_publication.h`, include it from `api.h`, list it in the SDK CMake public
headers, and add ABI tests and normative documentation. Existing API-v2,
API-v3 and API-v4 binaries remain compatible; an older loader returns
`UnknownService`, and the requesting plugin must refuse before any write.

The D2RLoader/Core implementation must add the service registry entry, exact
startup barrier and deterministic publisher order, owner/thread/epoch
validation, TLS `mutationAttempted`/poison state, transaction-aware Patch/Hook
paths, callback SEH containment, process-lifetime owner pinning and no-resume
poison handling. Tests must cover size gating, nonzero flags, wrong phase,
reentry, cross-owner serialization, lease expiry, wrong-thread handoff, clean
rejection, owner loss and callback faults on both sides of the mutation
boundary, mutate-then-reject, uncertain writes, cooperative-path enforcement,
transient unload refusal during the callback and permanent unload refusal after
a modifying commit. SDK declarations must not ship as supported until that Core
behavior is implemented and tested.

### Executable contribution kit

A separate PluginSDK worktree now carries an uncommitted review branch
`proposal/native-publication-v1`, based exactly on v4 commit
`6eb8f8b6192868214706bd6d528c5294f2f551b7`. It deliberately assigns no service
ID, is not included by `api.h`, and is not installed or exported by root CMake.
The draft ABI fixes `LeaseViewV1`, `RequestV1` and service sizes at 32/24/16
bytes and includes a portable authority model plus a Core-only conformance
matrix.

Release MSVC `/W4 /WX`, the focused contract CTest, 100 repeated executions and
the untouched root PluginSDK v4 contract CTest pass. An install smoke contains
36 normal SDK files and no `native_publication.h`. This proves only the public
ABI and portable state transitions. An independent final review reports no
remaining ABI/conformance blocker; the matrix also makes null service execute
and null lease validate rejection explicit. No numeric registry assignment,
private Core implementation, upstream commit, pull request or runtime authority
is claimed.

If ISC12 later adopts this optional service, it will consume it through one
full-set callback with this exact
order: preflight G0, G10 and the complete codec plan before the first write;
reserve every relay/state allocation once for process lifetime; commit G0, then
G10, then the codec groups G9, G2, G4, G1 and G3; revalidate the borrowed lease
at every required boundary; then return `Committed` without publishing
readiness. Only after the service itself returns `Committed` does the initial
plugin-load caller publish all readiness and operational flags, still under the
Core startup barrier. The current experimental candidate instead performs this
same transaction synchronously inside its same-thread initial
`D2RLoaderLoadPlugin` window and fail-fasts on every post-write ambiguity. That
local startup path is source-built but runtime publication remains untested;
the proposed service is therefore optional hardening, not an ISC12 prerequisite.

## 2. Typed item interaction events

### Status in PluginSDK v4

Delivered as `ServiceId::ItemInteraction = 17` and
`ItemInteractionServiceV1`. The loader emits a semantic `Activate` event on the
UI thread before normal handling and supplies generation-safe item/player
handles, the actual container, page and cell, keyboard modifiers and the active
keyboard/mouse or controller source. Priority ordering and `Consume` provide
the requested ordered interception boundary.

V1 deliberately covers normal inventory, Cube, personal stash and current
custom-page grids only. It does not emit shared-stash, vendor, trade, corpse,
ground, equipment, cursor or belt interactions, and it does not perform an
authoritative server mutation.

The read-only adoption audit identifies MassID as the strongest current
consumer: the service can replace its native client gesture seam and expose a
controller-aware activation, while the existing host request, server-side
validation and shared-stash path must remain. Transmogrify, Readable Items and
PSpell Framework can later share the same client boundary, subject to the same
authority and surface limits. No implementation changed during this audit.

### Original request retained for provenance

Input actions describe bindings, while tooltip listeners describe text.
Neither reports a semantic interaction with one specific item.

```cpp
D2RL::ItemInteractionListenerRegistration registration{
    .structSize = sizeof(registration),
    .priority = 100,
    .callback = [](const D2RL::ItemInteractionEvent* event,
                   void*) noexcept {
        if (event->phase != D2RL::InteractionPhase::Pressed) {
            return D2RL::InteractionResult::Continue;
        }

        if (event->action == D2RL::ItemAction::SecondaryClick
                && D2RL::HasModifier(
                    event->modifiers,
                    D2RL::InputModifier::Shift)) {
            QueueItemAction(
                event->item,
                event->surface,
                event->generation);

            return D2RL::InteractionResult::Consume;
        }

        return D2RL::InteractionResult::Continue;
    },
};

itemInteractions->registerListener(ctx, &registration);
```

The event should provide a generation-safe `ItemHandle`, originating UI
surface, logical action or physical button, active modifiers, and interaction
phase.

Returning `Consume` should stop lower-priority listeners and normal handling.
The event should run on a documented UI boundary, remain independent from
tooltip generation, and unregister automatically on unload.

## 3. Atomic edits and moves for existing items

### Status in PluginSDK v4

Delivered as the appended
`ItemServiceV1::executeExistingItemTransaction` function. A transaction accepts
up to 64 tagged `Debit`, `Edit` and `Move` operations, validates the entire set
before mutation, supports swap/chain placement through a temporary occupancy
model, preserves handles and native identity, and restores moved items and
edited values when a native placement or postcondition fails.

V1 moves cover normal inventory, Cube, personal stash and the current custom
page. Equipment, cursor, belt, shared stash, trade, corpse and ground moves are
rejected. A debit must leave a positive quantity, while atomic edits are limited
to durability, identified state and item level.

The read-only adoption audit identifies AutoSort as the strongest current
consumer for normal inventory, Cube, personal-stash and current custom-page
plans. Its shared-stash proxy scope and authoritative client-to-host request
still require a separate design, so the existing planner/packet path is not yet
removable. MassID may use atomic identified-state edits plus a tome debit, but
the last-charge case cannot be represented by a debit that must stay positive;
its current authoritative path therefore cannot be replaced wholesale. Gear
Swap Inventory remains outside V1 because equipment and experimental BodyLoc
13–20 moves are unsupported. No implementation changed during this audit.

### Original request retained for provenance

`ItemServiceV1` can edit one existing item, while its transaction model
consumes inputs and creates outputs. It cannot yet atomically edit several
existing items or move existing items between stock containers while
preserving identity.

```cpp
D2RL::ExistingItemTransaction transaction{
    .structSize = sizeof(transaction),
};

transaction.edit(itemA)
    .setIdentified(true);

transaction.edit(itemB)
    .setDurability(24);

transaction.debitStack(tome, 2);

transaction.move(
    equippedItem,
    D2RL::StockContainer::Equipment,
    D2RL::StockContainer::Cube,
    D2RL::PlacementMode::FindFreeSpace);

D2RL::ExistingItemTransactionResult result{};
const auto status = items->executeExistingItemTransaction(
    ctx,
    player,
    &transaction,
    &result);
```

Before committing, the Loader should revalidate every handle, player authority,
source container, destination, placement, stack debit, and edit.

Failure should change nothing. Success should preserve item handles, runtime
identity, seeds, socket contents, and untouched state, then use the normal
inventory, replication, and save paths.

A first version could support inventory, cube, equipment, cursor, and personal
or shared stash. Trade, vendors, corpses, ground items, and remote contexts
could remain unsupported until their ownership rules are proven.

## 4. Plugin-provided service discovery

### Status in PluginSDK v4

Open. V4 adds loader-owned services only; it does not add publication, query or
availability subscription for plugin-provided interfaces.

Plugins can expose versioned C APIs, but consumers must currently know the
provider module and export names and manually manage availability, load order,
ABI checks, and unload safety.

```cpp
struct DamagePolicyV1 {
    std::uint32_t structSize;
    std::uint32_t version;

    DamageResult(__cdecl* resolve)(
        const DamageRequest* request) noexcept;
};

const DamagePolicyV1 policy{
    .structSize = sizeof(policy),
    .version = 1,
    .resolve = &ResolveDamage,
};

D2RL::PluginServiceRegistration service{
    .structSize = sizeof(service),
    .name = "example.damage-policy",
    .version = 1,
    .interfaceSize = sizeof(policy),
    .interface = &policy,
};

serviceRegistry->publish(ctx, &service);
```

A consumer could query it without naming or linking to the provider DLL:

```cpp
const DamagePolicyV1* policy{};

if (serviceRegistry->query(
        ctx,
        "example.damage-policy",
        1,
        sizeof(DamagePolicyV1),
        reinterpret_cast<const void**>(&policy))
        == D2RL::PluginServiceResult::Success) {
    const auto result = policy->resolve(&request);
}
```

An optional availability subscription would also allow consumers loaded first
to discover providers loaded later:

```cpp
serviceRegistry->subscribe(
    ctx,
    "example.damage-policy",
    &OnProviderAvailable,
    &OnProviderUnavailable);
```

The Loader should track provider and consumer ownership, block new calls during
unload, wait for active calls to finish, invalidate the service, notify
subscribers, and remove registrations automatically.

For every accepted boundary, I would keep the API v3 model: optional service
queries, size-gated structures, safe handles, documented thread and multiplayer
authority, deterministic unload cleanup, and no partial state after failure.
