#include "sfilllocation_diagnostic.hpp"

#include <D2RLPlugin/api.h>

#include <array>
#include <cstdint>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t FirstNegativeIndexGuardWitnessRva = 0x3E1C32;
constexpr std::uintptr_t FirstNegativeIndexTailWitnessRva = 0x3E1D24;
constexpr std::uintptr_t FirstNegativeIndexDiagnosticCallRva = 0x3E1D2F;
constexpr std::uintptr_t SecondNegativeIndexBranchWitnessRva = 0x3E1F1A;
constexpr std::uintptr_t SecondNegativeIndexDiagnosticCallRva = 0x3E1F2B;

// movsxd index; load the signed lookup entry; and branch to the shared
// diagnostic tail only when that entry is negative.
constexpr std::array<std::uint8_t, 19U> FirstNegativeIndexGuardExpected{
    0x48, 0x63, 0xC3, 0x49, 0x63, 0x94, 0x84, 0x20,
    0xDF, 0xD0, 0x01, 0x85, 0xD2, 0x0F, 0x88, 0xDF,
    0x00, 0x00, 0x00,
};

// Diagnostic arguments; logger CALL; and the existing jump that skips the
// fill operation after the first negative-index path.
constexpr std::array<std::uint8_t, 18U> FirstNegativeIndexTailExpected{
    0x44, 0x8B, 0x47, 0x30, 0x48, 0x8D, 0x0D, 0x41,
    0xC2, 0x92, 0x01, 0xE8, 0x5C, 0xFE, 0x63, 0x00,
    0xEB, 0x4C,
};
constexpr std::array<std::uint8_t, 5U>
    FirstNegativeIndexDiagnosticCallExpected{
        0xE8, 0x5C, 0xFE, 0x63, 0x00,
    };

// test ecx,ecx; non-negative branch; diagnostic arguments; logger CALL; and
// the existing jump that skips the fill operation after the second path.
constexpr std::array<std::uint8_t, 27U> SecondNegativeIndexBranchExpected{
    0x85, 0xC9, 0x79, 0x17, 0x44, 0x8B, 0x47, 0x30,
    0x8B, 0xD1, 0x48, 0x8D, 0x0D, 0x45, 0xC0, 0x92,
    0x01, 0xE8, 0x60, 0xFC, 0x63, 0x00, 0xE9, 0xA3,
    0x00, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 5U>
    SecondNegativeIndexDiagnosticCallExpected{
    0xE8, 0x60, 0xFC, 0x63, 0x00,
};

bool Installed{};

} // namespace

auto ValidateSFillLocationDiagnosticSuppression(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    if (context->CheckExpectedBytes(
            FirstNegativeIndexGuardWitnessRva,
            FirstNegativeIndexGuardExpected.data(),
            static_cast<std::uint32_t>(
                FirstNegativeIndexGuardExpected.size()))
        && context->CheckExpectedBytes(
            FirstNegativeIndexTailWitnessRva,
            FirstNegativeIndexTailExpected.data(),
            static_cast<std::uint32_t>(
                FirstNegativeIndexTailExpected.size()))
        && context->CheckExpectedBytes(
            SecondNegativeIndexBranchWitnessRva,
            SecondNegativeIndexBranchExpected.data(),
            static_cast<std::uint32_t>(
                SecondNegativeIndexBranchExpected.size()))) {
        return true;
    }
    context->LogError(
        "MapSense: an sFillLocation diagnostic branch fingerprint mismatched or collided; plugin refused before installing native hooks.");
    return false;
}

auto InstallSFillLocationDiagnosticSuppression(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (Installed) return true;
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    if (!context->PatchNop(
            FirstNegativeIndexDiagnosticCallRva,
            FirstNegativeIndexDiagnosticCallExpected.data(),
            static_cast<std::uint32_t>(
                FirstNegativeIndexDiagnosticCallExpected.size()),
            static_cast<std::uint32_t>(
                FirstNegativeIndexDiagnosticCallExpected.size()))
        || !context->PatchNop(
            SecondNegativeIndexDiagnosticCallRva,
            SecondNegativeIndexDiagnosticCallExpected.data(),
            static_cast<std::uint32_t>(
                SecondNegativeIndexDiagnosticCallExpected.size()),
            static_cast<std::uint32_t>(
                SecondNegativeIndexDiagnosticCallExpected.size()))) {
        context->LogError(
            "MapSense: a tracked sFillLocation diagnostic suppression was refused; plugin remains inactive.");
        return false;
    }
    Installed = true;
    context->LogInfo(
        "MapSense: both tracked sFillLocation negative-index diagnostic suppressions installed.");
    return true;
}

void ShutdownSFillLocationDiagnosticSuppression() noexcept {
    Installed = false;
    // D2RLoader restores this registered patch after the plugin unload callback.
}

} // namespace RuffnecKk::MapSense
