#include "automap_visibility.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t RenderAutomapUnitRva = 0x0D76E0;
constexpr ULONGLONG VisibilityGraceMilliseconds = 120;

using RenderAutomapUnitFn = void(__fastcall*)(
    void* unit,
    void* automapContext) noexcept;

RenderAutomapUnitFn OriginalRenderAutomapUnit{};
std::atomic_bool Active{};
std::atomic_bool Diagnostics{};
std::atomic<ULONGLONG> LastRenderTick{};
std::atomic_uint64_t RenderPulses{};

__declspec(noinline) void __fastcall HookRenderAutomapUnit(
        void* unit,
        void* automapContext) noexcept {
    const auto original = OriginalRenderAutomapUnit;
    if (original != nullptr) original(unit, automapContext);
    if (!Active.load(std::memory_order_acquire)) return;
    LastRenderTick.store(GetTickCount64(), std::memory_order_release);
    RenderPulses.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

auto InitializeAutomapVisibility(
        const D2RL::PluginContext* context,
        bool diagnostics) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    Diagnostics.store(diagnostics, std::memory_order_release);
    LastRenderTick.store(0, std::memory_order_release);
    RenderPulses.store(0, std::memory_order_release);

    constexpr std::array<std::uint8_t, 31> expected{
        0x48, 0x89, 0x6C, 0x24, 0x10, 0x57, 0x48, 0x83,
        0xEC, 0x40, 0x48, 0x8B, 0xFA, 0x4C, 0x8D, 0x44,
        0x24, 0x68, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x48,
        0x8B, 0xE9, 0xE8, 0xF1, 0x01, 0x00, 0x00,
    };
    if (!context->InstallInlineHook(
            RenderAutomapUnitRva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()),
            HookRenderAutomapUnit,
            &OriginalRenderAutomapUnit)) {
        context->LogError(
            "MapSense: native automap visibility hook was refused.");
        return false;
    }
    Active.store(true, std::memory_order_release);
    if (Diagnostics.load(std::memory_order_acquire)) {
        context->LogInfo(
            "MapSense: native automap render-pulse observer installed.");
    }
    return true;
}

void ShutdownAutomapVisibility() noexcept {
    Active.store(false, std::memory_order_release);
    LastRenderTick.store(0, std::memory_order_release);
    // D2RLoader restores the hook after the unload callback. Keep the
    // trampoline valid so an already-running native call can finish safely.
}

void ResetAutomapVisibility() noexcept {
    LastRenderTick.store(0, std::memory_order_release);
}

auto IsNativeAutomapVisible() noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)) return false;
    const auto lastTick = LastRenderTick.load(std::memory_order_acquire);
    if (lastTick == 0) return false;
    const auto currentTick = GetTickCount64();
    return currentTick >= lastTick
        && (currentTick - lastTick) <= VisibilityGraceMilliseconds;
}

auto GetAutomapRenderPulses() noexcept -> std::uint64_t {
    return RenderPulses.load(std::memory_order_relaxed);
}

} // namespace RuffnecKk::MapSense
