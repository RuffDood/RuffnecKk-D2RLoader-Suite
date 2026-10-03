#pragma once

#include <D2RLPlugin/api.h>
#include <D2RLPlugin/diagnostics.h>
#include <RuffnecKk/tracked_native_transform.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace RuffnecKk::MapSense::Detail {

inline constexpr std::uintptr_t ClientUnitHashLookupRva = 0x09F270;
inline constexpr std::size_t ClientUnitHashLookupDisplacedBytes = 7U;
inline constexpr std::uintptr_t BuiltInClientUnitHashLookupRva = 0x3E2DB60;
// Captured Loader-hosted 1.3.1 code: reject id -1 via the original null-return
// branch; otherwise execute the two displaced instructions and rejoin +7.
// Exact branch displacements witness both destinations as well as the ABI.
inline constexpr std::array<std::uint8_t, 7> BuiltInClientLookupEntry{
    0xE9, 0xEB, 0xE8, 0xD8, 0x03, 0x90, 0x90};
inline constexpr std::array<std::uint8_t, 22> BuiltInClientLookupBody{
    0x41, 0x83, 0xF8, 0xFF, 0x0F, 0x84, 0x2C, 0x17,
    0x27, 0xFC, 0x48, 0x63, 0xC2, 0x48, 0x8B, 0x04,
    0xC1, 0xE9, 0x01, 0x17, 0x27, 0xFC};

template <std::size_t Size, typename Check>
[[nodiscard]] auto ValidateBuiltInClientUnitHashLookup(
        const std::array<std::uint8_t, Size>& original,
        Check&& check, bool independentLayoutMatches,
        bool entryExecutable, bool bodyExecutable) noexcept -> bool {
    static_assert(Size > ClientUnitHashLookupDisplacedBytes);
    return independentLayoutMatches && entryExecutable && bodyExecutable
        && check(ClientUnitHashLookupRva, BuiltInClientLookupEntry.data(),
            BuiltInClientLookupEntry.size())
        && check(BuiltInClientUnitHashLookupRva, BuiltInClientLookupBody.data(),
            BuiltInClientLookupBody.size())
        && check(ClientUnitHashLookupRva + ClientUnitHashLookupDisplacedBytes,
            original.data() + ClientUnitHashLookupDisplacedBytes,
            Size - ClientUnitHashLookupDisplacedBytes);
}
inline constexpr std::string_view CelestialEngineStabilityPluginId{
    "celestialrayone.engine-stability"};

[[nodiscard]] constexpr auto EvaluateClientUnitHashLookup(
        TrackedNativeTransform::Observation observation,
        bool pristineBytesMatch,
        bool untouchedTailMatches) noexcept
        -> TrackedNativeTransform::Admission {
    observation.structuralWitnessesMatch =
        observation.structuralWitnessesMatch && untouchedTailMatches;
    return TrackedNativeTransform::Evaluate(
        observation,
        pristineBytesMatch,
        CelestialEngineStabilityPluginId,
        TrackedNativeTransform::Kind::InlineHook);
}

[[nodiscard]] inline auto IsExecutableAddress(const void* address) noexcept
        -> bool {
    if (address == nullptr) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(address, &region, sizeof(region)) == 0U
            || region.State != MEM_COMMIT) {
        return false;
    }
    const auto protection = region.Protect & 0xFFU;
    return protection == PAGE_EXECUTE
        || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE
        || protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] constexpr auto ToTrackedState(
        D2RL::Diagnostics::ModificationState state) noexcept
        -> TrackedNativeTransform::State {
    switch (state) {
    case D2RL::Diagnostics::ModificationState::Unchanged:
        return TrackedNativeTransform::State::Unchanged;
    case D2RL::Diagnostics::ModificationState::Tracked:
        return TrackedNativeTransform::State::Tracked;
    case D2RL::Diagnostics::ModificationState::Untracked:
        return TrackedNativeTransform::State::Untracked;
    }
    return TrackedNativeTransform::State::Untracked;
}

[[nodiscard]] constexpr auto ToTrackedKind(
        D2RL::Diagnostics::ModificationKind kind) noexcept
        -> TrackedNativeTransform::Kind {
    switch (kind) {
    case D2RL::Diagnostics::ModificationKind::BytePatch:
        return TrackedNativeTransform::Kind::BytePatch;
    case D2RL::Diagnostics::ModificationKind::InlineHook:
        return TrackedNativeTransform::Kind::InlineHook;
    case D2RL::Diagnostics::ModificationKind::Multiple:
        return TrackedNativeTransform::Kind::Multiple;
    case D2RL::Diagnostics::ModificationKind::Unknown:
        return TrackedNativeTransform::Kind::Unknown;
    }
    return TrackedNativeTransform::Kind::Unknown;
}

template <std::size_t Size>
[[nodiscard]] inline auto ValidateClientUnitHashLookup(
        const D2RL::PluginContext* context,
        const std::array<std::uint8_t, Size>& expected,
        bool independentUnitLayoutWitnessesMatch) noexcept -> bool {
    static_assert(Size > ClientUnitHashLookupDisplacedBytes);
    // This is a complete byte-witnessed native implementation, not a plugin
    // owner exception. Loader version and diagnostics ownership do not select it.
    if (ValidateBuiltInClientUnitHashLookup(expected,
            [context](std::uintptr_t rva, const std::uint8_t* bytes,
                    std::size_t size) noexcept {
                return context->CheckExpectedBytes(rva, bytes,
                    static_cast<std::uint32_t>(size));
            }, independentUnitLayoutWitnessesMatch,
            IsExecutableAddress(reinterpret_cast<const void*>(
                context->exeBase + ClientUnitHashLookupRva)),
            IsExecutableAddress(reinterpret_cast<const void*>(
                context->exeBase + BuiltInClientUnitHashLookupRva)))) {
        return true;
    }
    const auto strictPristine = [&]() noexcept {
        const auto pristineBytesMatch = context->CheckExpectedBytes(
            ClientUnitHashLookupRva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
        if (!pristineBytesMatch) {
            context->LogWarn(
                "MapSense: client unit lookup at RVA 0x09F270 differs from the admitted original or tracked Engine Stability hook; native lookup admission refused.");
        }
        return EvaluateClientUnitHashLookup(
            {TrackedNativeTransform::State::Unchanged,
                TrackedNativeTransform::Kind::Unknown,
                0U,
                {},
                IsExecutableAddress(
                    reinterpret_cast<const std::uint8_t*>(context->exeBase)
                    + ClientUnitHashLookupRva),
                independentUnitLayoutWitnessesMatch},
            pristineBytesMatch,
            pristineBytesMatch)
            == TrackedNativeTransform::Admission::Pristine;
    };

    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (context->QueryService(
            D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version,
            &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(
            diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) {
        return strictPristine();
    }

    const D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .flags = 0U,
        .rva = ClientUnitHashLookupRva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
        .reserved = 0U,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(context, &query, &status)
            != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize
        || status.rva != ClientUnitHashLookupRva
        || status.size != expected.size()) {
        return strictPristine();
    }
    if (status.state == D2RL::Diagnostics::ModificationState::Unchanged) {
        return strictPristine();
    }
    const auto untouchedTailMatches = context->CheckExpectedBytes(
        ClientUnitHashLookupRva + ClientUnitHashLookupDisplacedBytes,
        expected.data() + ClientUnitHashLookupDisplacedBytes,
        static_cast<std::uint32_t>(
            expected.size() - ClientUnitHashLookupDisplacedBytes));
    const auto ownerEnd = std::find(
        std::begin(status.ownerPluginId),
        std::end(status.ownerPluginId),
        '\0');
    const std::string_view owner{
        status.ownerPluginId,
        static_cast<std::size_t>(ownerEnd - std::begin(status.ownerPluginId))};
    return EvaluateClientUnitHashLookup(
        {ToTrackedState(status.state),
            ToTrackedKind(status.kind),
            status.ownerCount,
            owner,
            IsExecutableAddress(
                reinterpret_cast<const std::uint8_t*>(context->exeBase)
                + ClientUnitHashLookupRva),
            independentUnitLayoutWitnessesMatch},
        false,
        untouchedTailMatches)
        != TrackedNativeTransform::Admission::Rejected;
}

} // namespace RuffnecKk::MapSense::Detail
