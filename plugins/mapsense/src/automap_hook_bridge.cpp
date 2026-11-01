#include "automap_hook_bridge.hpp"
#include "pyre_automap_witness.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace RuffnecKk::MapSense::Detail {
namespace {

AutomapHookRoute Attached{};
AutomapPass AttachedCallback{};

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
    route = {};
    if (context == nullptr || context->exeBase == 0 || expected.size() != 32U) return false;
    const auto entry = context->exeBase + AutomapEntryRva;
    if (!Region(entry, expected.size(), true)) return false;
    if (Matches(entry, expected)) return true;
    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (context->QueryService(D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version, &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) return false;
    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize, .rva = AutomapEntryRva,
        .expected = expected.data(), .expectedSize = static_cast<std::uint32_t>(expected.size())};
    D2RL::Diagnostics::HookStatus status{.structSize = D2RL::Diagnostics::HookStatusSize};
    if (diagnostics->queryHookStatus(context, &query, &status) != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize
        || status.rva != AutomapEntryRva || status.size != expected.size()
        || status.state != D2RL::Diagnostics::ModificationState::Tracked
        || status.kind != D2RL::Diagnostics::ModificationKind::InlineHook
        || status.ownerCount != 1U
        || std::memcmp(status.ownerPluginId, "pyre-lootfilter\0", 16) != 0) return false;
    std::uintptr_t target{};
    if (!JumpDestination(entry, target)) return false;
    HMODULE module{};
    auto resolveModule = [&]() noexcept {
        return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(target), &module) != FALSE;
    };
    if (!resolveModule()
        || target != reinterpret_cast<std::uintptr_t>(module) + PyreAutomapCallbackRva) {
        if (!JumpDestination(target, target) || !resolveModule()) return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    if (target != base + PyreAutomapCallbackRva
        || !Region(target, PyreCallbackWitness.size(), true)
        || !Matches(target, PyreCallbackWitness)) return false;
    const auto slot = base + PyreAutomapOriginalSlotRva;
    MEMORY_BASIC_INFORMATION slotInfo{};
    void* original{};
    if (VirtualQuery(reinterpret_cast<void*>(slot), &slotInfo, sizeof(slotInfo)) != sizeof(slotInfo)
        || slotInfo.AllocationBase != module
        || slot % alignof(void*) != 0 || !Region(slot, sizeof(void*), false, true)
        || !Read(slot, &original, sizeof(original))
        || !NativeTrampoline(reinterpret_cast<std::uintptr_t>(original), entry, expected))
        return false;
    route = {reinterpret_cast<void**>(slot), original, module};
    return true;
}

auto InstallAutomapHookRoute(const D2RL::PluginContext* context,
        std::span<const std::uint8_t> expected, const AutomapHookRoute& route,
        AutomapPass callback, AutomapPass& original) noexcept -> bool {
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
        || current.ownerModule != route.ownerModule) return false;
    original = reinterpret_cast<AutomapPass>(route.original);
    if (!ExchangeAutomapSlot(route.slot, route.original, reinterpret_cast<void*>(callback)))
        return false;
    Attached = route;
    AttachedCallback = callback;
    context->LogInfo("MapSense: validated Pyre automap original-call bridge attached; Pyre retains the native hook.");
    return true;
}

void RestoreAutomapHookRoute() noexcept {
    if (Attached.slot == nullptr) return;
    (void)ExchangeAutomapSlot(Attached.slot,
        reinterpret_cast<void*>(AttachedCallback), Attached.original);
    Attached = {};
    AttachedCallback = nullptr;
}

} // namespace RuffnecKk::MapSense::Detail
