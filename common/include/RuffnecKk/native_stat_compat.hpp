#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <RuffnecKk/tracked_native_transform.hpp>

namespace RuffnecKk::NativeStatCompat {

struct AddressRange final {
    std::uintptr_t base{};
    std::size_t size{};

    [[nodiscard]] auto Contains(std::uintptr_t address, std::size_t length) const noexcept -> bool;
};

using ReadMemoryFn = bool(*)(void* userData, std::uintptr_t address, std::byte* output, std::size_t size) noexcept;
using ValidateUnwindFn = bool(*)(void* userData, std::uintptr_t imageBase,
    std::uint32_t functionRva, std::size_t functionSize, std::uint32_t unwindRva,
    bool hasUnwind) noexcept;
using ValidateExecutableFn = bool(*)(void* userData, std::uintptr_t address) noexcept;

struct MemoryReader final {
    void* userData{};
    ReadMemoryFn read{};
    ValidateUnwindFn validateUnwind{};
    ValidateExecutableFn validateExecutable{};
};

using ObserveTrackedTransformFn = bool(*)(void* userData, std::uintptr_t mainImageBase,
    std::uintptr_t targetAddress, std::span<const std::byte> expected,
    TrackedNativeTransform::Observation& observation) noexcept;
// Optional consumer-owned structural proof for a native import-slot redirect.
// This only admits the redirect: the complete underlying Core contract remains
// mandatory, and calls continue through the native entry, preserving the owner.
using ValidateProviderRedirectFn = bool(*)(void* userData, std::uintptr_t mainImageBase,
    std::uint32_t nativeRva, std::uintptr_t slotAddress,
    std::uintptr_t actualTarget, std::uintptr_t expectedCoreTarget) noexcept;
struct TransformDiagnostics final {
    void* userData{};
    ObserveTrackedTransformFn observe{};
    ValidateProviderRedirectFn validateProviderRedirect{};
};

struct DirectCallWitness final { std::uint32_t siteRva; std::uint32_t targetRva; };
struct IndirectCallWitness final { std::uint32_t siteRva; std::uint32_t slotRva; };
struct ReadOnlyWitness final { std::uint32_t dataRva; std::string_view expectedHex; };
struct UnwindWitness final { std::uint32_t rva; std::string_view expectedHex; };
struct FunctionWitness final {
    std::uint32_t rva;
    std::string_view expectedHex;
    UnwindWitness unwind;
    std::span<const DirectCallWitness> directCalls;
    std::span<const IndirectCallWitness> indirectCalls;
    std::span<const ReadOnlyWitness> readOnlyData;
    // Bytes displaced by the one explicitly admitted tracked inline-hook
    // owner. The untouched function tail and every independent witness remain
    // mandatory; this prefix is never ignored for pristine functions.
    std::uint32_t trackedPrefixBytes{};
};
struct ImportWitness final {
    std::uint32_t slotRva;
    std::uint32_t targetRva;
    std::string_view targetExpectedHex;
};
struct HelperWitness final {
    std::string_view name;
    std::uint32_t nativeRva;
    std::string_view canonicalExpectedHex;
    std::uint32_t patchSpan;
    std::string_view paddingExpectedHex;
    std::string_view unchangedNativeTailExpectedHex;
    std::uint32_t descriptorRva;
    std::uint32_t exportRva;
    std::span<const FunctionWitness> functions;
    std::span<const ImportWitness> imports;
};
struct AdmissionContract final {
    std::string_view sha256;
    std::span<const HelperWitness> helpers;
};

[[nodiscard]] auto Loader130StatAdmissionContract() noexcept -> const AdmissionContract&;

enum class Failure : std::uint8_t {
    None,
    InvalidArguments,
    ReadFailed,
    CanonicalMismatch,
    ProviderEncoding,
    ProviderPointer,
    ProviderWitness,
};

enum class Helper : std::uint32_t {
    GetUnitStat = 1U << 0,
    AddUnitStat = 1U << 1,
    MergeStatLists = 1U << 2,
    WeaponMastery = 1U << 3,
    GetUnitBaseStat = 1U << 4,
    SetUnitStat = 1U << 5,
    GetUnitAlignment = 1U << 6,
    All = (1U << 7) - 1U,
};
using HelperMask = std::uint32_t;
constexpr auto ToMask(Helper helper) noexcept -> HelperMask { return static_cast<HelperMask>(helper); }

class Adapter final {
public:
    [[nodiscard]] auto Bind(const MemoryReader& reader, AddressRange mainImage,
        AddressRange coreImage, const AdmissionContract& contract,
        HelperMask required = ToMask(Helper::All),
        TransformDiagnostics diagnostics = {}) noexcept -> bool;
    [[nodiscard]] auto BindCurrentProcess(std::uintptr_t mainImageBase,
        HelperMask required, const AdmissionContract& contract = Loader130StatAdmissionContract()) noexcept -> bool;
    [[nodiscard]] auto BindCurrentProcess(std::uintptr_t mainImageBase,
        HelperMask required, TransformDiagnostics diagnostics,
        const AdmissionContract& contract = Loader130StatAdmissionContract()) noexcept -> bool;
    void Reset() noexcept;

    [[nodiscard]] auto LastFailure() const noexcept -> Failure { return failure_; }
    [[nodiscard]] auto IsBound() const noexcept -> bool { return required_ != 0 && (admitted_ & required_) == required_; }
    [[nodiscard]] auto IsBound(Helper helper) const noexcept { return IsAdmitted(helper); }
    [[nodiscard]] auto IsAdmitted(Helper helper) const noexcept -> bool { return (admitted_ & ToMask(helper)) != 0; }

    [[nodiscard]] auto GetUnitStat(void* unit, std::int32_t stat, std::uint16_t layer) const noexcept -> std::int32_t;
    void AddUnitStat(void* unit, std::int32_t stat, std::int32_t delta, std::uint16_t layer) const noexcept;
    void MergeStatLists(void* target, void* source, std::int32_t mode) const noexcept;
    [[nodiscard]] auto ReadWeaponMastery(void* unit, void* weapon) const noexcept -> std::int32_t;
    [[nodiscard]] auto GetUnitBaseStat(void* unit, std::int32_t stat, std::uint16_t layer) const noexcept -> std::int32_t;
    [[nodiscard]] auto GetUnitAlignment(void* unit) const noexcept -> std::int32_t;
    void SetUnitStat(void* unit, std::int32_t stat, std::int32_t value, std::uint16_t layer) const noexcept;
    // A forwarding interceptor must preserve the Core's complete layer value.
    // ProviderWide forwards all 32 bits; the canonical public entry reproduces
    // its R9W ABI by forwarding only the explicit low 16 bits.
    void SetUnitStatWide(void* unit, std::int32_t stat, std::int32_t value, std::uint32_t layer) const noexcept;

private:
    enum class Route : std::uint8_t { None, Canonical, ProviderWide };
    using ReadLegacyFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::uint16_t) noexcept;
    using ReadWideFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::uint32_t) noexcept;
    using AddLegacyFn = void(__fastcall*)(void*, std::int32_t, std::int32_t, std::uint16_t) noexcept;
    using AddWideFn = void(__fastcall*)(void*, std::int32_t, std::int32_t, std::uint32_t) noexcept;
    using MergeWideFn = void(__fastcall*)(void*, void*, std::int32_t) noexcept;
    using CriticalFn = std::int32_t(__fastcall*)(void*, void*, void*, std::int32_t) noexcept;
    using AlignmentFn = std::int32_t(__fastcall*)(void*) noexcept;
    using SetLegacyFn = void(__fastcall*)(void*, std::int32_t, std::int32_t, std::uint16_t) noexcept;
    using SetWideFn = std::int32_t(__fastcall*)(void*, std::int32_t, std::int32_t, std::uint32_t) noexcept;

    std::array<std::uintptr_t, 7> entries_{};
    std::array<Route, 7> routes_{};
    Failure failure_{Failure::InvalidArguments};
    HelperMask admitted_{};
    HelperMask required_{};
};

} // namespace RuffnecKk::NativeStatCompat
