#pragma once
#include <D2RLPlugin/api.h>
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace RuffnecKk::MapSense::Detail {
// Startup-only observation of existing admitted witness ranges. No code is
// executed, no jump is followed and no diagnostic result grants admission.
template <std::size_t Size>
void DiagnosticReport(const D2RL::PluginContext* context, const char* name,
        std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected,
        bool admitted) noexcept {
    static_assert(Size > 0 && Size <= 128);
    char message[1200]{};
    std::snprintf(message, sizeof(message),
        "MapSense ESRDIAG1: check=%s rva=0x%llX size=%zu admission=%s.",
        name, static_cast<unsigned long long>(rva), Size, admitted ? "PASS" : "FAIL");
    context->LogInfo(message);
    if (admitted) return;
    std::array<std::uint8_t, Size> observed{};
    SIZE_T read{};
    const bool addressValid = context->exeBase != 0
        && rva <= std::numeric_limits<std::uintptr_t>::max() - context->exeBase
        && Size <= std::numeric_limits<std::uintptr_t>::max() - (context->exeBase + rva);
    const bool readable = addressValid && ReadProcessMemory(GetCurrentProcess(),
        reinterpret_cast<const void*>(context->exeBase + rva), observed.data(),
        Size, &read) != FALSE && read == Size;
    const DWORD readError = readable ? ERROR_SUCCESS : GetLastError();
    std::array<char, Size * 2 + 1> expectedHex{}, observedHex{};
    constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < Size; ++i) {
        expectedHex[i * 2] = digits[expected[i] >> 4];
        expectedHex[i * 2 + 1] = digits[expected[i] & 15];
        observedHex[i * 2] = readable ? digits[observed[i] >> 4] : '?';
        observedHex[i * 2 + 1] = readable ? digits[observed[i] & 15] : '?';
    }
    std::snprintf(message, sizeof(message),
        "MapSense ESRDIAG1: check=%s expected=%s observed=%s readable=%s read-error=%lu.",
        name, expectedHex.data(), observedHex.data(), readable ? "yes" : "no",
        static_cast<unsigned long>(addressValid ? readError : ERROR_INVALID_ADDRESS));
    context->LogWarn(message);
    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (context->QueryService(D2RL::ServiceId::Diagnostics,
            D2RL::DiagnosticsServiceV1Version, &diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceV1Field(diagnostics, D2RL::DiagnosticsServiceV1RequiredSize)
        || diagnostics->queryHookStatus == nullptr) {
        context->LogWarn("MapSense ESRDIAG1: hook ownership service unavailable; owner remains unknown.");
        return;
    }
    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize, .rva = rva,
        .expected = expected.data(), .expectedSize = static_cast<std::uint32_t>(Size)};
    D2RL::Diagnostics::HookStatus status{.structSize = D2RL::Diagnostics::HookStatusSize};
    const auto result = diagnostics->queryHookStatus(context, &query, &status);
    if (result != D2RL::Diagnostics::Result::Success
        || status.structSize < D2RL::Diagnostics::HookStatusRequiredSize
        || status.rva != rva || status.size != Size) {
        std::snprintf(message, sizeof(message),
            "MapSense ESRDIAG1: check=%s ownership query unavailable/invalid result=%u.",
            name, static_cast<unsigned>(result));
        context->LogWarn(message);
        return;
    }
    std::snprintf(message, sizeof(message),
        "MapSense ESRDIAG1: check=%s tracked-state=%u kind=%u owner-count=%u owner=%.64s (ownership is diagnostic only).",
        name, static_cast<unsigned>(status.state), static_cast<unsigned>(status.kind),
        status.ownerCount, status.ownerPluginId);
    context->LogWarn(message);
}

template <std::size_t Size>
auto DiagnosticWitness(const D2RL::PluginContext* context, const char* name,
        std::uintptr_t rva, const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    const bool matches = context->CheckExpectedBytes(rva, expected.data(), static_cast<std::uint32_t>(Size));
    DiagnosticReport(context, name, rva, expected, matches);
    return matches;
}
} // namespace RuffnecKk::MapSense::Detail
