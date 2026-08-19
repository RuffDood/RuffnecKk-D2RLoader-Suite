#include <D2RLPlugin/api.h>

#include "native_contract.hpp"
#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace RuffnecKk::BulkSkillPointAllocation {
namespace {

struct GameStringView {
    const char* data{};
    std::size_t size{};
};

struct PendingConfirmationState {
    bool active{};
    std::uint16_t skillId{};
};

using SendFiveBytePacketFn = void(__fastcall*)(
    std::uint8_t, std::uint16_t, std::uint16_t) noexcept;
using IsVirtualKeyDownFn = std::uint32_t(__fastcall*)(
    std::int32_t) noexcept;
using GetLocalizedStringByKeyFn = const char*(__fastcall*)(
    const GameStringView*) noexcept;
using ShowAssignAllConfirmationFn = void(__fastcall*)(
    const void*) noexcept;

constexpr char AssignAllStatPointsConfirmationKey[] =
    "AssignAllStatPointsConfirmation";
constexpr char MissingStringKey[] = "strMissingString";
constexpr std::size_t LocalizedBufferSize = 4096;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Settings ActiveSettings{};
std::optional<std::filesystem::path> GameplaySource;
std::optional<std::filesystem::path> StringsSource;

const D2RL::LocalizationServiceV1* LocalizationService{};
const D2RL::SharedEventServiceV1* SharedEventService{};
D2RL::SharedEvents::ListenerHandle UiListenerHandle{
    D2RL::SharedEvents::InvalidHandle};

SendFiveBytePacketFn OriginalSendFiveBytePacket{};
IsVirtualKeyDownFn IsVirtualKeyDown{};
GetLocalizedStringByKeyFn OriginalGetLocalizedStringByKey{};
ShowAssignAllConfirmationFn ShowAssignAllConfirmation{};

SRWLOCK ConfirmationLock = SRWLOCK_INIT;
PendingConfirmationState PendingConfirmation{};
alignas(16) std::array<std::uint8_t,
    NativeContract::FakeStatWidgetSize> FakeStatWidget{};
thread_local bool OpeningSkillConfirmation{};
thread_local std::array<char, LocalizedBufferSize> ConfirmationPrompt{};
thread_local std::array<char, LocalizedBufferSize> MissingString{};
thread_local std::array<char, LocalizedBufferSize> PrimaryLocaleProbe{};
thread_local std::array<char, LocalizedBufferSize> SecondaryLocaleProbe{};

std::atomic<std::uint64_t> SingleClicks{};
std::atomic<std::uint64_t> CtrlBatches{};
std::atomic<std::uint64_t> ShiftAccepted{};
std::atomic<std::uint64_t> ShiftCancelled{};
std::atomic<std::uint64_t> NativeBulkPacketsSent{};
std::atomic_bool LocalizationFallbackLogged{};

enum ModifierMask : std::uint32_t {
    NativeCtrl = 1U << 0,
    NativeLeftCtrl = 1U << 1,
    NativeRightCtrl = 1U << 2,
    Win32Ctrl = 1U << 3,
    Win32LeftCtrl = 1U << 4,
    Win32RightCtrl = 1U << 5,
    PacketShift = 1U << 6,
};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-bulk-skill-point-allocation",
    .name = "Bulk Skill Point Allocation",
    .version = "1.3.3",
    .author = "RuffnecKk",
    .description =
        "Allocates configurable skill-point batches with Ctrl or all points with Shift.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto CancelPendingConfirmation() noexcept -> bool {
    AcquireSRWLockExclusive(&ConfirmationLock);
    const bool wasActive = PendingConfirmation.active;
    PendingConfirmation = {};
    ReleaseSRWLockExclusive(&ConfirmationLock);
    return wasActive;
}

void SetPendingConfirmation(std::uint16_t skillId) noexcept {
    AcquireSRWLockExclusive(&ConfirmationLock);
    if (PendingConfirmation.active) {
        ShiftCancelled.fetch_add(1, std::memory_order_relaxed);
    }
    PendingConfirmation = {.active = true, .skillId = skillId};
    ReleaseSRWLockExclusive(&ConfirmationLock);
}

auto TakePendingConfirmation(
    PendingConfirmationState& output
) noexcept -> bool {
    AcquireSRWLockExclusive(&ConfirmationLock);
    output = PendingConfirmation;
    PendingConfirmation = {};
    ReleaseSRWLockExclusive(&ConfirmationLock);
    return output.active;
}

auto NativeKeyDown(std::int32_t virtualKey) noexcept -> bool {
    if (IsVirtualKeyDown == nullptr) return false;
    __try {
        return IsVirtualKeyDown(virtualKey) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto Win32KeyDown(std::int32_t virtualKey) noexcept -> bool {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0
        || (GetKeyState(virtualKey) & 0x8000) != 0;
}

auto ReadModifierMask(std::uint16_t packetExtra) noexcept -> std::uint32_t {
    std::uint32_t mask{};
    if (NativeKeyDown(VK_CONTROL)) mask |= NativeCtrl;
    if (NativeKeyDown(VK_LCONTROL)) mask |= NativeLeftCtrl;
    if (NativeKeyDown(VK_RCONTROL)) mask |= NativeRightCtrl;
    if (Win32KeyDown(VK_CONTROL)) mask |= Win32Ctrl;
    if (Win32KeyDown(VK_LCONTROL)) mask |= Win32LeftCtrl;
    if (Win32KeyDown(VK_RCONTROL)) mask |= Win32RightCtrl;
    if (packetExtra == AssignAllSkillPointsExtra) mask |= PacketShift;
    return mask;
}

void LogAllocation(
    const char* mode,
    std::uint16_t skillId,
    std::uint16_t outgoingExtra
) noexcept {
    if (!ActiveSettings.diagnostics || Context == nullptr) return;
    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "BulkSkillPointAllocation: mode=%s skill=%u packetExtra=%u.",
        mode,
        static_cast<unsigned>(skillId),
        static_cast<unsigned>(outgoingExtra));
    Context->LogInfo(message);
}

void BeginBulkAllocation(
    AllocationMode mode,
    std::uint16_t skillId
) noexcept {
    if (OriginalSendFiveBytePacket == nullptr) return;
    const auto requested = mode == AllocationMode::ShiftAll
        ? 1U
        : ActiveSettings.skillPointsPerCtrlClick;
    const auto extra = NativeSkillPacketExtra(mode, requested);
    LogAllocation(
        mode == AllocationMode::ShiftAll ? "shift-all" : "ctrl-batch",
        skillId,
        extra);
    OriginalSendFiveBytePacket(
        NativeContract::AllocateSkillOpcode,
        skillId,
        extra);
    NativeBulkPacketsSent.fetch_add(1, std::memory_order_relaxed);
}

template<std::size_t Capacity>
auto FetchLocalizedString(
    const char* key,
    std::array<char, Capacity>& buffer
) noexcept -> std::string_view {
    buffer.fill('\0');
    if (LocalizationService == nullptr
        || LocalizationService->getStringByKey == nullptr
        || key == nullptr) {
        return {};
    }
    std::uint32_t required{};
    const auto query = LocalizationService->getStringByKey(
        Context, key, nullptr, 0, &required);
    if (query != D2RL::Localization::Result::BufferTooSmall
        || required == 0 || required > Capacity) {
        return {};
    }
    const auto result = LocalizationService->getStringByKey(
        Context,
        key,
        buffer.data(),
        static_cast<std::uint32_t>(buffer.size()),
        &required);
    if (result != D2RL::Localization::Result::Success
        || required == 0 || required > Capacity
        || buffer[required - 1] != '\0') {
        buffer.fill('\0');
        return {};
    }
    const auto terminator = std::memchr(buffer.data(), '\0', required);
    if (terminator == nullptr) {
        buffer.fill('\0');
        return {};
    }
    return {
        buffer.data(),
        static_cast<std::size_t>(
            static_cast<const char*>(terminator) - buffer.data()),
    };
}

void CopyConfirmationPrompt(std::string_view prompt) noexcept {
    ConfirmationPrompt.fill('\0');
    const auto size = (std::min)(
        prompt.size(),
        ConfirmationPrompt.size() - 1);
    std::memcpy(ConfirmationPrompt.data(), prompt.data(), size);
}

void LogLocalizationFallbackOnce() noexcept {
    if (Context == nullptr
        || LocalizationFallbackLogged.exchange(
            true, std::memory_order_relaxed)) {
        return;
    }
    Context->LogWarn(
        "BulkSkillPointAllocation: active D2R locale could not be identified; using the configured English fallback.");
}

void ResolveConfirmationPrompt() noexcept {
    if (!ActiveSettings.shiftConfirmationKey.empty()) {
        const auto localized = FetchLocalizedString(
            ActiveSettings.shiftConfirmationKey.c_str(),
            ConfirmationPrompt);
        const auto missing = FetchLocalizedString(
            MissingStringKey,
            MissingString);
        if (IsUsableLocalizedString(
                localized,
                ActiveSettings.shiftConfirmationKey,
                missing)) {
            return;
        }
    }

    const auto primary = FetchLocalizedString(
        PrimaryLocaleProbeKey,
        PrimaryLocaleProbe);
    const auto secondary = FetchLocalizedString(
        SecondaryLocaleProbeKey,
        SecondaryLocaleProbe);
    if (const auto locale = DetectLocale(primary, secondary)) {
        CopyConfirmationPrompt(ActiveSettings.shiftConfirmations[*locale]);
        return;
    }

    CopyConfirmationPrompt(ActiveSettings.shiftConfirmationFallback);
    LogLocalizationFallbackOnce();
}

auto __fastcall HookGetLocalizedStringByKey(
    const GameStringView* key
) noexcept -> const char* {
    constexpr auto expectedLength =
        sizeof(AssignAllStatPointsConfirmationKey) - 1;
    if (OpeningSkillConfirmation
        && key != nullptr
        && key->data != nullptr
        && key->size == expectedLength
        && std::memcmp(
            key->data,
            AssignAllStatPointsConfirmationKey,
            expectedLength) == 0) {
        return ConfirmationPrompt.data();
    }
    return OriginalGetLocalizedStringByKey(key);
}

auto TryShowConfirmation() noexcept -> bool {
    if (ShowAssignAllConfirmation == nullptr) return false;
    __try {
        ShowAssignAllConfirmation(FakeStatWidget.data());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

auto ShowShiftConfirmation(std::uint16_t skillId) noexcept -> bool {
    ResolveConfirmationPrompt();
    SetPendingConfirmation(skillId);
    std::memcpy(
        FakeStatWidget.data() + NativeContract::FakeStatIndexOffset,
        &NativeContract::SkillConfirmationSentinel,
        sizeof(NativeContract::SkillConfirmationSentinel));
    OpeningSkillConfirmation = true;
    const bool shown = TryShowConfirmation();
    OpeningSkillConfirmation = false;
    if (!shown) CancelPendingConfirmation();
    return shown;
}

auto __cdecl OnUiMessage(
    const D2RL::PluginContext*,
    const D2RL::SharedEvents::UiMessageEvent* event,
    void*
) noexcept -> D2RL::SharedEvents::UiMessageAction {
    if (event == nullptr
        || event->structSize
            < D2RL::SharedEvents::UiMessageEventRequiredSize
        || !IsShiftConfirmationAcceptEvent(event->target, event->command)) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    PendingConfirmationState pending;
    if (!TakePendingConfirmation(pending)) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    ShiftAccepted.fetch_add(1, std::memory_order_relaxed);
    BeginBulkAllocation(AllocationMode::ShiftAll, pending.skillId);
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

void __fastcall HookSendFiveBytePacket(
    std::uint8_t opcode,
    std::uint16_t value,
    std::uint16_t extra
) noexcept {
    if (opcode != NativeContract::AllocateSkillOpcode) {
        OriginalSendFiveBytePacket(opcode, value, extra);
        return;
    }

    const auto modifierMask = ReadModifierMask(extra);
    const bool shiftPressed = (modifierMask & PacketShift) != 0;
    const bool controlPressed = (modifierMask & (
        NativeCtrl | NativeLeftCtrl | NativeRightCtrl
        | Win32Ctrl | Win32LeftCtrl | Win32RightCtrl)) != 0;
    const auto mode = ResolveMode(shiftPressed, controlPressed);
    if (mode == AllocationMode::Single) {
        if (CancelPendingConfirmation()) {
            ShiftCancelled.fetch_add(1, std::memory_order_relaxed);
        }
        SingleClicks.fetch_add(1, std::memory_order_relaxed);
        OriginalSendFiveBytePacket(opcode, value, extra);
        return;
    }

    if (mode == AllocationMode::ShiftAll) {
        if (!ActiveSettings.confirmShiftAllocation) {
            if (CancelPendingConfirmation()) {
                ShiftCancelled.fetch_add(1, std::memory_order_relaxed);
            }
            BeginBulkAllocation(mode, value);
            return;
        }
        if (!ShowShiftConfirmation(value) && Context != nullptr) {
            Context->LogError(
                "BulkSkillPointAllocation: Shift confirmation could not be shown; no points were allocated.");
        }
        return;
    }

    if (CancelPendingConfirmation()) {
        ShiftCancelled.fetch_add(1, std::memory_order_relaxed);
    }
    CtrlBatches.fetch_add(1, std::memory_order_relaxed);
    BeginBulkAllocation(mode, value);
}

auto ValidateServiceContracts() noexcept -> bool {
    if (!ActiveSettings.confirmShiftAllocation) return true;
    if (Context->QueryService(
            D2RL::ServiceId::Localization,
            D2RL::LocalizationServiceV1Version,
            &LocalizationService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLocalizationServiceV1Field(
            LocalizationService,
            D2RL::LocalizationServiceV1RequiredSize)
        || LocalizationService->getStringByKey == nullptr) {
        Context->LogError(
            "BulkSkillPointAllocation: Localization v1 is required when Shift confirmation is enabled.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version,
            &SharedEventService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceV1Field(
            SharedEventService,
            D2RL::SharedEventServiceV1RequiredSize)
        || SharedEventService->registerUiMessageListener == nullptr
        || SharedEventService->unregisterUiMessageListener == nullptr) {
        Context->LogError(
            "BulkSkillPointAllocation: SharedEvent v1 is required when Shift confirmation is enabled.");
        return false;
    }
    return true;
}

auto CheckBytes(
    std::uintptr_t rva,
    const auto& expected,
    const char* label
) noexcept -> bool {
    if (Context->CheckExpectedBytes(
            rva,
            expected.data(),
            static_cast<std::uint32_t>(expected.size()))) {
        return true;
    }
    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "BulkSkillPointAllocation: %s signature mismatch for D2R build 92777.",
        label);
    Context->LogError(message);
    return false;
}

auto ValidateNativeContract() noexcept -> bool {
    if (!CheckBytes(
            NativeContract::SendFiveBytePacketRva,
            NativeContract::SendFiveBytePacketExpected,
            "skill packet sender")) {
        return false;
    }
    if (!CheckBytes(
            NativeContract::IsVirtualKeyDownRva,
            NativeContract::IsVirtualKeyDownExpected,
            "key-state helper")) {
        return false;
    }
    if (!ActiveSettings.confirmShiftAllocation) return true;
    return CheckBytes(
            NativeContract::GetLocalizedStringByKeyRva,
            NativeContract::GetLocalizedStringByKeyExpected,
            "localization entry")
        && CheckBytes(
            NativeContract::ShowAssignAllConfirmationRva,
            NativeContract::ShowAssignAllConfirmationExpected,
            "confirmation builder");
}

void BindNativeFunctions() noexcept {
    IsVirtualKeyDown = At<IsVirtualKeyDownFn>(
        NativeContract::IsVirtualKeyDownRva);
    if (ActiveSettings.confirmShiftAllocation) {
        ShowAssignAllConfirmation = At<ShowAssignAllConfirmationFn>(
            NativeContract::ShowAssignAllConfirmationRva);
    }
}

auto RegisterUiListener() noexcept -> bool {
    if (!ActiveSettings.confirmShiftAllocation) return true;
    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 1'000,
        .reserved = 0,
        .callback = OnUiMessage,
        .userData = nullptr,
    };
    const auto result = SharedEventService->registerUiMessageListener(
        Context, &listener, &UiListenerHandle);
    if (result != D2RL::SharedEvents::Result::Success
        || UiListenerHandle == D2RL::SharedEvents::InvalidHandle) {
        UiListenerHandle = D2RL::SharedEvents::InvalidHandle;
        Context->LogError(
            "BulkSkillPointAllocation: Shift confirmation listener registration failed.");
        return false;
    }
    return true;
}

void UnregisterUiListener() noexcept {
    if (UiListenerHandle == D2RL::SharedEvents::InvalidHandle
        || SharedEventService == nullptr
        || SharedEventService->unregisterUiMessageListener == nullptr
        || Context == nullptr) {
        UiListenerHandle = D2RL::SharedEvents::InvalidHandle;
        return;
    }
    (void)SharedEventService->unregisterUiMessageListener(
        Context, UiListenerHandle);
    UiListenerHandle = D2RL::SharedEvents::InvalidHandle;
}

auto InstallHooks() noexcept -> bool {
    if (ActiveSettings.confirmShiftAllocation
        && !Context->InstallInlineHook(
            NativeContract::GetLocalizedStringByKeyRva,
            NativeContract::GetLocalizedStringByKeyExpected.data(),
            static_cast<std::uint32_t>(
                NativeContract::GetLocalizedStringByKeyExpected.size()),
            HookGetLocalizedStringByKey,
            &OriginalGetLocalizedStringByKey)) {
        Context->LogError(
            "BulkSkillPointAllocation: localization hook installation failed.");
        return false;
    }
    if (!Context->InstallInlineHook(
            NativeContract::SendFiveBytePacketRva,
            NativeContract::SendFiveBytePacketExpected.data(),
            static_cast<std::uint32_t>(
                NativeContract::SendFiveBytePacketExpected.size()),
            HookSendFiveBytePacket,
            &OriginalSendFiveBytePacket)) {
        Context->LogError(
            "BulkSkillPointAllocation: skill packet hook installation failed.");
        return false;
    }
    return true;
}

auto PathForLog(
    const std::optional<std::filesystem::path>& path
) -> std::string {
    if (!path) return "built-in defaults";
    const auto utf8 = path->u8string();
    return {
        reinterpret_cast<const char*>(utf8.data()),
        utf8.size(),
    };
}

void ResetState() noexcept {
    CancelPendingConfirmation();
    OpeningSkillConfirmation = false;
    ConfirmationPrompt.fill('\0');
    MissingString.fill('\0');
    PrimaryLocaleProbe.fill('\0');
    SecondaryLocaleProbe.fill('\0');
    GameplaySource.reset();
    StringsSource.reset();
    ActiveSettings.skillPointsPerCtrlClick =
        DefaultSkillPointsPerCtrlClick;
    ActiveSettings.confirmShiftAllocation = false;
    ActiveSettings.diagnostics = false;
    ActiveSettings.shiftConfirmationKey.clear();
    ActiveSettings.shiftConfirmationFallback.clear();
    for (auto& confirmation : ActiveSettings.shiftConfirmations) {
        confirmation.clear();
    }
    LocalizationFallbackLogged.store(false, std::memory_order_relaxed);
    LocalizationService = nullptr;
    SharedEventService = nullptr;
    UiListenerHandle = D2RL::SharedEvents::InvalidHandle;
    OriginalSendFiveBytePacket = nullptr;
    IsVirtualKeyDown = nullptr;
    OriginalGetLocalizedStringByKey = nullptr;
    ShowAssignAllConfirmation = nullptr;
    Base = nullptr;
    Context = nullptr;
}

void ResetCounters() noexcept {
    SingleClicks.store(0, std::memory_order_relaxed);
    CtrlBatches.store(0, std::memory_order_relaxed);
    ShiftAccepted.store(0, std::memory_order_relaxed);
    ShiftCancelled.store(0, std::memory_order_relaxed);
    NativeBulkPacketsSent.store(0, std::memory_order_relaxed);
}

} // namespace
} // namespace RuffnecKk::BulkSkillPointAllocation

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &RuffnecKk::BulkSkillPointAllocation::Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    using namespace RuffnecKk::BulkSkillPointAllocation;
    if (context == nullptr) return false;
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (Base == nullptr) {
        context->LogError(
            "BulkSkillPointAllocation: D2R executable base is unavailable.");
        ResetState();
        return false;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild == nullptr
        || (std::strcmp(runtimeBuild, "92777") != 0
            && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "BulkSkillPointAllocation: only D2R builds 92777 and 93847 are supported.");
        ResetState();
        return false;
    }

    try {
        auto loaded = LoadSettings(context);
        ActiveSettings = std::move(loaded.settings);
        GameplaySource = std::move(loaded.gameplaySource);
        StringsSource = std::move(loaded.stringsSource);
    } catch (const std::exception& exception) {
        const auto message = std::string(
            "BulkSkillPointAllocation: configuration refused; no service or hook registered: ")
            + exception.what() + ".";
        context->LogError(message.c_str());
        ResetState();
        return false;
    } catch (...) {
        context->LogError(
            "BulkSkillPointAllocation: configuration refused by an unknown error; no service or hook registered.");
        ResetState();
        return false;
    }

    if (!ActiveSettings.enabled) {
        try {
            const auto message = std::string(
                "BulkSkillPointAllocation 1.3.2 by RuffnecKk loaded disabled; no service or hook registered; gameplayConfig=")
                + PathForLog(GameplaySource) + ".";
            context->LogInfo(message.c_str());
        } catch (...) {
            context->LogInfo(
                "BulkSkillPointAllocation 1.3.2 by RuffnecKk loaded disabled; no service or hook registered.");
        }
        return true;
    }

    ResetCounters();
    if (!ValidateServiceContracts() || !ValidateNativeContract()) {
        ResetState();
        return false;
    }
    BindNativeFunctions();
    if (!RegisterUiListener()) {
        ResetState();
        return false;
    }
    if (!InstallHooks()) {
        UnregisterUiListener();
        // D2RLoader rolls back owner hooks after a failed load. Keep native
        // originals alive until that rollback completes so an in-flight UI
        // call cannot observe a cleared trampoline.
        return false;
    }

    try {
        const auto message = std::string(
            "BulkSkillPointAllocation 1.3.2 by RuffnecKk loaded; role=Client; ctrl=")
            + std::to_string(ActiveSettings.skillPointsPerCtrlClick)
            + "; shiftConfirmation="
            + (ActiveSettings.confirmShiftAllocation ? "enabled" : "disabled")
            + "; gameplayConfig=" + PathForLog(GameplaySource)
            + "; stringsConfig=" + PathForLog(StringsSource) + ".";
        context->LogInfo(message.c_str());
    } catch (...) {
        context->LogInfo(
            "BulkSkillPointAllocation 1.3.2 by RuffnecKk loaded.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace RuffnecKk::BulkSkillPointAllocation;
    UnregisterUiListener();
    ResetState();
}
