#include "native_ui_state.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::MapSense {
namespace {

constexpr std::uintptr_t UiStateTableRva = 0x2A2ADA0;
constexpr std::uintptr_t OpenUiStateReadWitnessRva = 0x0CD7FB;
constexpr std::uintptr_t CloseUiStateWriteWitnessRva = 0x0C7DF1;
constexpr std::uintptr_t ToggleUiStateReadWitnessRva = 0x0CDE3C;
constexpr std::uintptr_t FindTopLevelPanelRva = 0x846170;
constexpr std::size_t WidgetVisibleOffset = 0x51U;

// The three independent witnesses prove the table RVA, its 32-entry bound,
// and native byte semantics without borrowing UI_GetState, whose entry may be
// owned by RemoteStash in the same complete plugin stack.
constexpr std::array<std::uint8_t, 9> OpenUiStateReadWitness{
    0x45, 0x0F, 0xB6, 0xBC, 0x1C, 0xA0, 0xAD, 0xA2, 0x02,
};
constexpr std::array<std::uint8_t, 19> CloseUiStateWriteWitness{
    0x48, 0x83, 0xFB, 0x20, 0x0F, 0x83, 0x0C, 0x04, 0x00,
    0x00, 0x42, 0xC6, 0x84, 0x23, 0xA0, 0xAD, 0xA2, 0x02, 0x00,
};
constexpr std::array<std::uint8_t, 9> ToggleUiStateReadWitness{
    0x45, 0x0F, 0xB6, 0xA4, 0x1D, 0xA0, 0xAD, 0xA2, 0x02,
};
constexpr std::array<std::uint8_t, 22> FindTopLevelPanelExpected{
    0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xF6, 0x9F,
    0xBF, 0x02, 0x48, 0x85, 0xC9, 0x0F, 0x85, 0xDD,
    0x95, 0x05, 0x00, 0x33, 0xC0, 0xC3,
};

constexpr std::array QuestPanelNames{
    "QuestLogPanelExpansion",
    "QuestLogPanelOriginal",
};

using FindTopLevelPanelFn = void*(__fastcall*)(const char*) noexcept;

const D2RL::PluginContext* Context{};
const volatile std::uint8_t* UiStateTable{};
FindTopLevelPanelFn FindTopLevelPanel{};
std::atomic_bool Active{};
std::atomic_bool QuestVisibilityKnown{};
std::atomic_bool QuestPanelVisible{};
std::atomic_uint32_t LastActiveMask{};
std::atomic_uint32_t LastBlockingPanelMask{};
std::atomic_uint64_t ReadFailures{};
std::atomic_uint64_t QuestVisibilityTick{};
std::atomic_uint64_t QuestVisibilityReadFailures{};

[[nodiscard]] auto CheckWitness(
        std::uintptr_t rva,
        const auto& expected) noexcept -> bool {
    return Context != nullptr
        && Context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()));
}

} // namespace

auto InitializeNativeUiState(
        const D2RL::PluginContext* context) noexcept -> bool {
    ShutdownNativeUiState();
    if (!D2RL::HasContext(context) || context->exeBase == 0U) return false;
    Context = context;
    if (!CheckWitness(OpenUiStateReadWitnessRva, OpenUiStateReadWitness)
        || !CheckWitness(
            CloseUiStateWriteWitnessRva,
            CloseUiStateWriteWitness)
        || !CheckWitness(
            ToggleUiStateReadWitnessRva,
            ToggleUiStateReadWitness)
        || !CheckWitness(FindTopLevelPanelRva, FindTopLevelPanelExpected)) {
        context->LogError(
            "MapSense: D2R native panel-state telemetry fingerprint mismatch; native automap clipping remains authoritative.");
        ShutdownNativeUiState();
        return false;
    }

    UiStateTable = reinterpret_cast<const volatile std::uint8_t*>(
        context->exeBase + UiStateTableRva);
    FindTopLevelPanel = reinterpret_cast<FindTopLevelPanelFn>(
        context->exeBase + FindTopLevelPanelRva);
    Active.store(true, std::memory_order_release);
    return true;
}

void ShutdownNativeUiState() noexcept {
    Active.store(false, std::memory_order_release);
    UiStateTable = nullptr;
    FindTopLevelPanel = nullptr;
    Context = nullptr;
    QuestVisibilityKnown.store(false, std::memory_order_relaxed);
    QuestPanelVisible.store(false, std::memory_order_relaxed);
    LastActiveMask.store(0U, std::memory_order_relaxed);
    LastBlockingPanelMask.store(0U, std::memory_order_relaxed);
    ReadFailures.store(0U, std::memory_order_relaxed);
    QuestVisibilityTick.store(0U, std::memory_order_relaxed);
    QuestVisibilityReadFailures.store(0U, std::memory_order_relaxed);
}

auto RefreshNativeUiPanelVisibilityOnUiThread() noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)
        || FindTopLevelPanel == nullptr) {
        return false;
    }

    bool visible{};
    __try {
        for (const auto* const name : QuestPanelNames) {
            auto* const panel = FindTopLevelPanel(name);
            if (panel != nullptr
                && *(static_cast<const volatile std::uint8_t*>(panel)
                    + WidgetVisibleOffset) != 0U) {
                visible = true;
                break;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        QuestVisibilityKnown.store(false, std::memory_order_release);
        QuestPanelVisible.store(false, std::memory_order_release);
        QuestVisibilityTick.store(0U, std::memory_order_release);
        QuestVisibilityReadFailures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    QuestPanelVisible.store(visible, std::memory_order_release);
    QuestVisibilityTick.store(
        static_cast<std::uint64_t>(GetTickCount64()),
        std::memory_order_release);
    QuestVisibilityKnown.store(true, std::memory_order_release);
    return true;
}

void ResetNativeUiPanelVisibility() noexcept {
    QuestVisibilityKnown.store(false, std::memory_order_release);
    QuestPanelVisible.store(false, std::memory_order_release);
    QuestVisibilityTick.store(0U, std::memory_order_release);
}

auto HasBlockingNativeUiPanel() noexcept -> bool {
    NativeUiStateStatus status{};
    if (!AcquireNativeUiStateStatus(status)) return true;
    return status.blockingPanelMask != 0U;
}

auto AcquireNativeUiStateStatus(
        NativeUiStateStatus& status) noexcept -> bool {
    status = {};
    if (!Active.load(std::memory_order_acquire) || UiStateTable == nullptr) {
        return false;
    }

    std::array<std::uint8_t, NativeUiStateCount> states{};
    __try {
        for (std::size_t state = 0U; state < states.size(); ++state) {
            states[state] = UiStateTable[state];
            if (states[state] > 1U) {
                ReadFailures.fetch_add(1U, std::memory_order_relaxed);
                return false;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        ReadFailures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    const auto nativeActiveMask = NativeUiStateMask(states);
    const auto currentTick = static_cast<std::uint64_t>(GetTickCount64());
    const auto questVisibilityTick = QuestVisibilityTick.load(
        std::memory_order_acquire);
    const auto questVisibilityKnown =
        QuestVisibilityKnown.load(std::memory_order_acquire)
        && questVisibilityTick != 0U && currentTick >= questVisibilityTick
        && currentTick - questVisibilityTick
            <= NativeUiPanelVisibilityLifetimeMilliseconds;
    const auto questPanelVisible = questVisibilityKnown
        && QuestPanelVisible.load(std::memory_order_acquire);
    const auto activeMask = questPanelVisible
        ? nativeActiveMask
            | (std::uint32_t{1U} << NativeUiQuestPanelState)
        : nativeActiveMask;
    const auto blockingMask = NativeUiBlockingPanelMask(states)
        | (questPanelVisible
            ? (std::uint32_t{1U} << NativeUiQuestPanelState)
            : 0U);
    const auto retainAutomapProjection =
        ShouldRetainNativeAutomapProjectionForQuest(
            nativeActiveMask,
            questVisibilityKnown,
            questPanelVisible,
            questVisibilityTick,
            currentTick);
    LastActiveMask.store(activeMask, std::memory_order_relaxed);
    LastBlockingPanelMask.store(blockingMask, std::memory_order_relaxed);
    status = {
        .active = true,
        .questVisibilityKnown = questVisibilityKnown,
        .questPanelVisible = questPanelVisible,
        .retainAutomapProjection = retainAutomapProjection,
        .activeMask = activeMask,
        .blockingPanelMask = blockingMask,
        .readFailures = ReadFailures.load(std::memory_order_relaxed),
        .questVisibilityReadFailures =
            QuestVisibilityReadFailures.load(std::memory_order_relaxed),
    };
    return true;
}

auto GetNativeUiStateStatus() noexcept -> NativeUiStateStatus {
    return {
        .active = Active.load(std::memory_order_acquire),
        .questVisibilityKnown =
            QuestVisibilityKnown.load(std::memory_order_acquire),
        .questPanelVisible = QuestPanelVisible.load(std::memory_order_acquire),
        .activeMask = LastActiveMask.load(std::memory_order_relaxed),
        .blockingPanelMask = LastBlockingPanelMask.load(
            std::memory_order_relaxed),
        .readFailures = ReadFailures.load(std::memory_order_relaxed),
        .questVisibilityReadFailures =
            QuestVisibilityReadFailures.load(std::memory_order_relaxed),
    };
}

} // namespace RuffnecKk::MapSense
