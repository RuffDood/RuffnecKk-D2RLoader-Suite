#include <RuffnecKk/maps_stat_compat.hpp>
#include "maps_stat_witness.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <RuffnecKk/tracked_native_transform_d2rl.hpp>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace RuffnecKk::MapsStatCompat {
namespace {
auto Read(std::uintptr_t at, void* output, std::size_t size) noexcept -> bool {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(at),
        output, size, &count) != FALSE && count == size;
}
template<class T> auto ReadValue(std::uintptr_t at, T& output) noexcept -> bool {
    return Read(at, &output, sizeof(output));
}
template<class T> auto ReadOwned(std::uintptr_t base, std::uintptr_t rva, T& output) noexcept -> bool {
    if (base > UINTPTR_MAX - rva) return false;
    const auto at = base + rva;
    MEMORY_BASIC_INFORMATION region{};
    return VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region)) == sizeof(region)
        && reinterpret_cast<std::uintptr_t>(region.AllocationBase) == base
        && ReadValue(at, output);
}
auto IsCode(std::uintptr_t at, std::size_t size) noexcept -> bool {
    MEMORY_BASIC_INFORMATION region{};
    if (at > UINTPTR_MAX - size
        || VirtualQuery(reinterpret_cast<void*>(at), &region, sizeof(region)) != sizeof(region)
        || region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0
        || at + size > reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
        return false;
    const auto protection = region.Protect & 0xFFU;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
auto Match(std::uintptr_t at, std::span<const std::uint8_t> expected) noexcept -> bool {
    // All admitted profiles have bounded, complete code witnesses.
    std::array<std::uint8_t, 693> actual{};
    return !expected.empty() && expected.size() <= actual.size()
        && IsCode(at, expected.size()) && Read(at, actual.data(), expected.size())
        && std::equal(expected.begin(), expected.end(), actual.begin());
}
}

auto ValidateReimaginedStatRedirect(void*, std::uintptr_t mainImageBase,
        std::uint32_t nativeRva, std::uintptr_t slotAddress,
        std::uintptr_t actualTarget, std::uintptr_t expectedCoreTarget) noexcept -> bool {
    if (mainImageBase == 0 || nativeRva != NativeUnitStatRva
        || slotAddress == 0 || expectedCoreTarget == 0
        || !IsCode(expectedCoreTarget, 1)) return false;
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(actualTarget), &module)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    const auto owner = reinterpret_cast<std::uintptr_t>(
        GetProcAddress(module, "D2RLMapsOwnsStatReaderHook"));
    const MapsStatProfile* profile{};
    for (const auto& candidate : MapsStatProfiles) {
        if (base <= UINTPTR_MAX - candidate.readerRva
            && base <= UINTPTR_MAX - candidate.ownerRva
            && actualTarget == base + candidate.readerRva
            && owner == base + candidate.ownerRva
            && Match(actualTarget, candidate.readerWitness)
            && Match(owner, candidate.ownerWitness)) {
            // Rebuilds can retain entry RVAs while relocating internal state.
            // Select by both complete witnesses and refuse ambiguous evidence.
            if (profile != nullptr) return false;
            profile = &candidate;
        }
    }
    if (profile == nullptr) return false;

    // Validate the published ownership function's complete state without calling
    // third-party code during admission. Both saved originals must identify the
    // exact Core export independently validated by NativeStatCompat afterward.
    std::uintptr_t original{}, savedOriginal{}, savedSlot{}, ownerContext{}, ownerBase{}, ownerRva{};
    std::uint8_t active{};
    if (!ReadOwned(base, profile->originalStatSlotRva, original)
        || !ReadOwned(base, profile->redirectPreviousValueRva, savedOriginal)
        || !ReadOwned(base, profile->redirectSlotAddressRva, savedSlot)
        || !ReadOwned(base, profile->contextSlotRva, ownerContext)
        || ownerContext == 0 || ownerContext > UINTPTR_MAX - 0x18U
        || !ReadValue(ownerContext + 0x18U, ownerBase)
        || !ReadOwned(base, profile->nativeStatRvaSlotRva, ownerRva)
        || !ReadOwned(base, profile->statHookActiveRva, active)) return false;
    return original == expectedCoreTarget && savedOriginal == expectedCoreTarget
        && savedSlot == slotAddress && ownerBase == mainImageBase
        && ownerRva == nativeRva && active == 1U;
}

}
