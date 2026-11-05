#include "automap_hook_bridge.hpp"
#include "pyre_automap_witness.hpp"
#include "pyre_r5_automap_witness.hpp"
#include "celestial_automap_witness.hpp"
#include <atomic>

#include <algorithm>
#include <cstring>
#include <limits>

namespace RuffnecKk::MapSense::Detail {
namespace {

AutomapHookRoute Attached{};
void* AttachedCallback{};
std::atomic<ReturningAutomapPass> ReturningOriginal{};
std::atomic<AutomapPass> ReturningObserver{};

std::uint64_t __fastcall ReturningBridge(void* unit, void* context) noexcept {
    const auto original = ReturningOriginal.load(std::memory_order_acquire);
    if (original == nullptr) return 0;
    const auto result = original(unit, context);
    const auto observer = ReturningObserver.load(std::memory_order_acquire);
    if (observer != nullptr) observer(unit, context);
    return result;
}

auto Read(std::uintptr_t address, void* destination, std::size_t size) noexcept -> bool {
    SIZE_T read{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
        destination, size, &read) != FALSE && read == size;
}

auto Region(std::uintptr_t address, std::size_t size, bool executable,
        bool writable = false) noexcept -> bool {
    MEMORY_BASIC_INFORMATION info{};
    if (size == 0 || address > UINTPTR_MAX - size
        || VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)
        || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || address + size > reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize)
        return false;
    const auto protect = info.Protect & 0xFFU;
    const bool isExecutable = protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ
        || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
    const bool isWritable = protect == PAGE_READWRITE || protect == PAGE_WRITECOPY
        || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY;
    return (!executable || isExecutable) && (!writable || isWritable);
}

auto Matches(std::uintptr_t address, std::span<const std::uint8_t> expected) noexcept -> bool {
    std::array<std::uint8_t, 1024> bytes{};
    return expected.size() <= bytes.size() && Read(address, bytes.data(), expected.size())
        && std::equal(expected.begin(), expected.end(), bytes.begin());
}

// Accept only the two ordinary x64 jump encodings used by the witnessed hook
// path. Follow at most one relay; arbitrary executable pointers are insufficient.
auto JumpDestination(std::uintptr_t address, std::uintptr_t& destination) noexcept -> bool {
    std::array<std::uint8_t, 6> bytes{};
    if (!Region(address, bytes.size(), true) || !Read(address, bytes.data(), bytes.size()))
        return false;
    if (bytes[0] == 0xE9) {
        std::int32_t relative{};
        std::memcpy(&relative, bytes.data() + 1, sizeof(relative));
        destination = address + 5U + static_cast<std::uintptr_t>(relative);
        return Region(destination, 1, true);
    }
    if (bytes[0] == 0xFF && bytes[1] == 0x25) {
        std::int32_t relative{};
        std::memcpy(&relative, bytes.data() + 2, sizeof(relative));
        return Read(address + 6U + static_cast<std::uintptr_t>(relative),
            &destination, sizeof(destination)) && Region(destination, 1, true);
    }
    return false;
}

// All boundaries precede the first relative CALL. Reconstructing this prefix
// plus an exact jump back proves the original slot reaches the native function,
// rather than a third callback that would create an unknown chain or recursion.
auto NativeTrampoline(std::uintptr_t trampoline, std::uintptr_t entry,
        std::span<const std::uint8_t> expected) noexcept -> bool {
    constexpr std::array<std::size_t, 7> boundaries{5, 6, 10, 13, 18, 23, 26};
    for (const auto size : boundaries) {
        std::uintptr_t resume{};
        if (size >= expected.size() || !Region(trampoline, size, true)
            || !Matches(trampoline, expected.first(size))
            || !JumpDestination(trampoline + size, resume) || resume != entry + size)
            continue;
        return Matches(entry + size, expected.subspan(size));
    }
    return false;
}


} // namespace

auto ExchangeAutomapSlot(void** slot, void* expected, void* replacement) noexcept -> bool {
    return slot != nullptr && reinterpret_cast<std::uintptr_t>(slot) % alignof(void*) == 0
        && Region(reinterpret_cast<std::uintptr_t>(slot), sizeof(void*), false, true)
        && InterlockedCompareExchangePointer(slot, replacement, expected) == expected;
}

auto QualifyAutomapHookRoute(const D2RL::PluginContext* context,
        std::span<const std::uint8_t> expected, AutomapHookRoute& route) noexcept -> bool {
    const auto reject = [context](const char* message) noexcept {
        if (context != nullptr) context->LogWarn(message);
        return false;
    };
    route = {};
    if (context == nullptr || context->exeBase == 0 || expected.size() != 32U) return reject("MapSense ESRDIAG1: automap-route stage=invalid-context-or-witness-size.");
    const auto entry = context->exeBase + AutomapEntryRva;
    if (!Region(entry, expected.size(), true)) return reject("MapSense ESRDIAG1: automap-route stage=entry-not-executable.");
    if (Matches(entry, expected)) return true;
    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (context->QueryService(D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version, &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) return reject("MapSense ESRDIAG1: automap-route stage=diagnostics-service-unavailable.");
    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize, .rva = AutomapEntryRva,
        .expected = expected.data(), .expectedSize = static_cast<std::uint32_t>(expected.size())};
    D2RL::Diagnostics::HookStatus status{.structSize = D2RL::Diagnostics::HookStatusSize};
    if (diagnostics->queryHookStatus(context, &query, &status) != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize
        || status.rva != AutomapEntryRva || status.size != expected.size()
        || status.state != D2RL::Diagnostics::ModificationState::Tracked
        || status.kind != D2RL::Diagnostics::ModificationKind::InlineHook
        || status.ownerCount != 1U) return reject("MapSense ESRDIAG1: automap-route stage=hook-status-or-owner-not-admitted.");
    const bool pyre = std::memcmp(status.ownerPluginId, "pyre-lootfilter\0", 16) == 0;
    const bool celestial = std::memcmp(status.ownerPluginId,
        "celestialrayone.loot-filter\0", sizeof("celestialrayone.loot-filter")) == 0;
    if (!pyre && !celestial) return reject("MapSense: automap owner is not supported.");
    auto callbackRva = pyre ? PyreAutomapCallbackRva : CelestialAutomapCallbackRva;
    auto slotRva = pyre ? PyreAutomapOriginalSlotRva : CelestialAutomapOriginalSlotRva;
    std::span<const std::uint8_t> witness = pyre
        ? std::span<const std::uint8_t>(PyreCallbackWitness)
        : std::span<const std::uint8_t>(CelestialCallbackWitness);
    std::uintptr_t target{};
    if (!JumpDestination(entry, target)) return reject("MapSense ESRDIAG1: automap-route stage=entry-jump-unrecognized.");
    HMODULE module{};
    auto resolveModule = [&]() noexcept {
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target), &module) != FALSE;
    };
    if (!resolveModule()
        || (target != reinterpret_cast<std::uintptr_t>(module) + callbackRva
            && !(pyre && target == reinterpret_cast<std::uintptr_t>(module) + 0x84C0))) {
        if (!JumpDestination(target, target) || !resolveModule()) return reject("MapSense ESRDIAG1: automap-route stage=relay-or-module-unresolved.");
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    // The revision 5 profile is exclusive to the admitted Pyre owner.
    if (pyre && target == base + 0x84C0) {
        callbackRva = 0x84C0;
        slotRva = 0x19F900;
        witness = PyreR5CallbackWitness;
    }
    if (target != base + callbackRva
        || !Region(target, witness.size(), true)
        || !Matches(target, witness)) return reject("MapSense ESRDIAG1: automap-route stage=callback-bytes-or-location-not-admitted.");
    const auto slot = base + slotRva;
    MEMORY_BASIC_INFORMATION slotInfo{};
    void* original{};
    if (VirtualQuery(reinterpret_cast<void*>(slot), &slotInfo, sizeof(slotInfo)) != sizeof(slotInfo)
        || slotInfo.AllocationBase != module
        || slot % alignof(void*) != 0 || !Region(slot, sizeof(void*), false, true)
        || !Read(slot, &original, sizeof(original))
        || !NativeTrampoline(reinterpret_cast<std::uintptr_t>(original), entry, expected))
        return reject("MapSense ESRDIAG1: automap-route stage=original-slot-or-trampoline-not-admitted.");
    route = {reinterpret_cast<void**>(slot), original, module,
        pyre ? AutomapOwner::Pyre : AutomapOwner::Celestial};
    return true;
}

auto InstallAutomapHookRoute(const D2RL::PluginContext* context,
        std::span<const std::uint8_t> expected, const AutomapHookRoute& route,
        AutomapPass callback, AutomapPass& original, AutomapPass observationOnly) noexcept -> bool {
    if (Attached.slot != nullptr) return false;
    if (route.slot == nullptr) {
        return context->InstallInlineHook(AutomapEntryRva, expected.data(),
            static_cast<std::uint32_t>(expected.size()), callback, &original);
    }
    // Revalidate immediately before attaching. Publish the pass-through pointer
    // first so Pyre can never observe a callback with an uninitialized original.
    AutomapHookRoute current{};
    if (!QualifyAutomapHookRoute(context, expected, current)
        || current.slot != route.slot || current.original != route.original
        || current.ownerModule != route.ownerModule || current.owner != route.owner) return false;
    void* replacement = reinterpret_cast<void*>(callback);
    if (route.owner == AutomapOwner::Celestial) {
        if (observationOnly == nullptr) return false;
        ReturningOriginal.store(reinterpret_cast<ReturningAutomapPass>(route.original), std::memory_order_release);
        ReturningObserver.store(observationOnly, std::memory_order_release);
        replacement = reinterpret_cast<void*>(&ReturningBridge);
    } else {
        original = reinterpret_cast<AutomapPass>(route.original);
    }
    if (!ExchangeAutomapSlot(route.slot, route.original, replacement)) {
        if (route.owner == AutomapOwner::Celestial)
            ReturningObserver.store(nullptr, std::memory_order_release);
        return false;
    }
    Attached = route;
    AttachedCallback = replacement;
    context->LogInfo(route.owner == AutomapOwner::Celestial
        ? "MapSense: validated CelestialRayOne Loot Filter 1.5.3 automap bridge attached; loot filter retains the native hook."
        : "MapSense: validated Pyre automap original-call bridge attached; Pyre retains the native hook.");
    return true;
}

void RestoreAutomapHookRoute() noexcept {
    if (Attached.slot == nullptr) return;
    ReturningObserver.store(nullptr, std::memory_order_release);
    (void)ExchangeAutomapSlot(Attached.slot,
        AttachedCallback, Attached.original);
    Attached = {};
    AttachedCallback = nullptr;
}

} // namespace RuffnecKk::MapSense::Detail
