#include "reimagined_stat_compat.hpp"
#include "reimagined_stat_witness.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
#include <RuffnecKk/tracked_native_transform_d2rl.hpp>
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

namespace RuffnecKk::MapSense::Detail {
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
            && owner == base + candidate.ownerRva) {
            profile = &candidate;
            break;
        }
    }
    if (profile == nullptr || !Match(actualTarget, profile->readerWitness)
        || !Match(owner, profile->ownerWitness)) return false;

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

void LogStatAdmissionFailure(const D2RL::PluginContext* context) noexcept {
    constexpr std::array helpers{
        std::pair{RuffnecKk::NativeStatCompat::Helper::GetUnitStat, NativeUnitStatRva},
        std::pair{RuffnecKk::NativeStatCompat::Helper::GetUnitAlignment, std::uint32_t{0x2F4190}}};
    for (const auto& [helper, rva] : helpers) {
        RuffnecKk::NativeStatCompat::Adapter adapter{};
        auto diagnostics = RuffnecKk::TrackedNativeTransform::D2RLDiagnosticsContext(context);
        diagnostics.validateProviderRedirect = ValidateReimaginedStatRedirect;
        const auto passed = adapter.BindCurrentProcess(context->exeBase,
            RuffnecKk::NativeStatCompat::ToMask(helper), diagnostics);
        std::array<std::uint8_t, 6> entry{};
        std::uintptr_t slot{}, target{};
        if (Read(context->exeBase + rva, entry.data(), entry.size())
            && entry[0] == 0xFF && entry[1] == 0x25) {
            std::int32_t displacement{};
            std::memcpy(&displacement, entry.data() + 2, sizeof(displacement));
            slot = context->exeBase + rva + 6U + static_cast<std::uintptr_t>(displacement);
            (void)ReadValue(slot, target);
        }
        char message[320]{};
        std::snprintf(message, sizeof(message),
            "MapSense stat admission: helper=%s accepted=%s failure=%u entry-rva=0x%X bytes=%02X%02X%02X%02X%02X%02X slot=0x%llX target=0x%llX.",
            helper == RuffnecKk::NativeStatCompat::Helper::GetUnitStat ? "GetUnitStat" : "GetUnitAlignment",
            passed ? "yes" : "no", static_cast<unsigned>(adapter.LastFailure()), rva,
            entry[0], entry[1], entry[2], entry[3], entry[4], entry[5],
            static_cast<unsigned long long>(slot), static_cast<unsigned long long>(target));
        context->LogWarn(message);
    }
}
}
