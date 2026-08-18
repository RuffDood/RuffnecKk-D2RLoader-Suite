#include <D2RLPlugin/api.h>

#include "policy.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace RuffnecKk::TransmuteHotkey {
namespace {

constexpr std::uint32_t SupportedBuild = 92777;
constexpr std::size_t MaximumConfigBytes = 65'536;

constexpr std::uintptr_t IntegratedCubeUpdateRva = 0x23ECD0;
constexpr std::uintptr_t StandaloneCubeUpdateRva = 0x2CDA90;
constexpr std::uintptr_t FindWidgetRva = 0x856220;

constexpr std::size_t IntegratedPanelOffset = 0x2A0;
constexpr std::size_t WidgetEnabledOffset = 0x50;
constexpr std::size_t WidgetVisibleOffset = 0x51;
constexpr std::size_t ButtonOnClickMessageOffset = 0x558;
constexpr std::size_t UiMessageEventOffset = 0x88;

constexpr std::array<std::uint8_t, 32> IntegratedCubeUpdateExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B,
    0xF9, 0x48, 0x8B, 0x89, 0xA0, 0x02, 0x00, 0x00,
    0x48, 0x85, 0xC9, 0x0F, 0x84, 0xA2, 0x01, 0x00,
    0x00, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x48, 0x8D,
};
constexpr std::array<std::uint8_t, 32> StandaloneCubeUpdateExpected{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x3D,
    0x97, 0xC6, 0x7C, 0x02, 0x00, 0x48, 0x8B, 0xF9,
    0x48, 0x89, 0x5C, 0x24, 0x30, 0x48, 0x89, 0x74,
    0x24, 0x40, 0x74, 0x0E, 0xB2, 0x01, 0xC6, 0x05,
};
constexpr std::array<std::uint8_t, 32> FindWidgetExpected{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
    0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B,
    0x41, 0x60, 0x48, 0x8D, 0x3C, 0xC3, 0x48, 0x3B,
};

using PanelUpdateFn = void(__fastcall*)(void*) noexcept;
using FindWidgetFn = void*(__fastcall*)(void*, const char*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
PanelUpdateFn OriginalIntegratedCubeUpdate{};
PanelUpdateFn OriginalStandaloneCubeUpdate{};
FindWidgetFn FindWidget{};

const D2RL::InputServiceV1* InputService{};
const D2RL::ThreadServiceV1* ThreadService{};
const D2RL::WidgetServiceV1* WidgetService{};
D2RL::Input::ActionHandle ActionHandle{D2RL::Input::InvalidHandle};

std::atomic<void*> IntegratedCubePanel{};
std::atomic<void*> StandaloneCubePanel{};
std::atomic_bool RequestPending{};
std::atomic<std::uint64_t> AcceptedRequests{};
std::atomic<std::uint64_t> DispatchedRequests{};
std::atomic<std::uint64_t> RefusedRequests{};
std::atomic<std::uint64_t> FailedRequests{};
std::atomic_bool FirstDispatchReported{};
std::atomic_bool FirstRefusalReported{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-transmute-hotkey",
    .name = "Transmute Hotkey",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description =
        "Triggers the Horadric Cube transmute action from a configurable hotkey.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

auto ReadConfiguration() noexcept -> bool {
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        Context->LogError(requiredSize > buffer.size()
            ? "TransmuteHotkey: configuration exceeds 65535 bytes."
            : "TransmuteHotkey: configuration could not be read.");
        return false;
    }

    Config parsed{};
    std::string error;
    if (!ParseConfig(std::string_view(buffer.data()), parsed, error)) {
        const auto message = std::string("TransmuteHotkey: invalid TOML (")
            + error + "); no hook or input action was registered.";
        Context->LogError(message.c_str());
        return false;
    }
    Settings = parsed;
    return true;
}

auto ValidateRuntime() noexcept -> bool {
    return Context->CheckExpectedBytes(
            IntegratedCubeUpdateRva,
            IntegratedCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(IntegratedCubeUpdateExpected.size()))
        && Context->CheckExpectedBytes(
            StandaloneCubeUpdateRva,
            StandaloneCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(StandaloneCubeUpdateExpected.size()))
        && Context->CheckExpectedBytes(
            FindWidgetRva,
            FindWidgetExpected.data(),
            static_cast<std::uint32_t>(FindWidgetExpected.size()));
}

auto QueryServices() noexcept -> bool {
    if (Context->QueryService(
            D2RL::ServiceId::Input,
            D2RL::InputServiceV1Version,
            &InputService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInputServiceV1Field(
            InputService, D2RL::InputServiceV1RequiredSize)) {
        Context->LogError(
            "TransmuteHotkey: D2RLoader Input service v1 is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version,
            &ThreadService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(
            ThreadService, D2RL::ThreadServiceV1RequiredSize)) {
        Context->LogError(
            "TransmuteHotkey: D2RLoader Thread service v1 is unavailable.");
        return false;
    }
    if (Context->QueryService(
            D2RL::ServiceId::Widget,
            D2RL::WidgetServiceV1Version,
            &WidgetService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasWidgetServiceV1Field(
            WidgetService, D2RL::WidgetServiceV1RequiredSize)) {
        Context->LogError(
            "TransmuteHotkey: D2RLoader Widget service v1 is unavailable.");
        return false;
    }
    return true;
}

auto IntegratedPanel(void* controller) noexcept -> void* {
    if (!controller) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            static_cast<std::uint8_t*>(controller) + IntegratedPanelOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

auto ButtonIsUsable(void* panel, void* button) noexcept -> bool {
    if (!panel || !button || !FindWidget) return false;
    __try {
        const auto* panelBytes = static_cast<const std::uint8_t*>(panel);
        const auto* buttonBytes = static_cast<const std::uint8_t*>(button);
        if (panelBytes[WidgetVisibleOffset] == 0
            || buttonBytes[WidgetEnabledOffset] == 0
            || buttonBytes[WidgetVisibleOffset] == 0) {
            return false;
        }
        const auto* message = buttonBytes + ButtonOnClickMessageOffset;
        const auto event = *reinterpret_cast<const std::uint64_t*>(
            message + UiMessageEventOffset);
        return event != 0 && FindWidget(panel, "convert") == button;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ObserveCubePanel(std::atomic<void*>& slot, void* panel) noexcept {
    void* button{};
    __try {
        button = panel && FindWidget ? FindWidget(panel, "convert") : nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        button = nullptr;
    }
    slot.store(
        ButtonIsUsable(panel, button) ? panel : nullptr,
        std::memory_order_release);
}

void __fastcall HookIntegratedCubeUpdate(void* controller) noexcept {
    OriginalIntegratedCubeUpdate(controller);
    ObserveCubePanel(IntegratedCubePanel, IntegratedPanel(controller));
}

void __fastcall HookStandaloneCubeUpdate(void* panel) noexcept {
    OriginalStandaloneCubeUpdate(panel);
    ObserveCubePanel(StandaloneCubePanel, panel);
}

auto CubePanelObserved() noexcept -> bool {
    return StandaloneCubePanel.load(std::memory_order_acquire) != nullptr
        || IntegratedCubePanel.load(std::memory_order_acquire) != nullptr;
}

auto DispatchForPanel(
    std::atomic<void*>& slot,
    const char* target,
    const char* surface
) noexcept -> bool {
    void* panel = slot.load(std::memory_order_acquire);
    if (!panel) return false;
    void* button{};
    __try {
        button = FindWidget(panel, "convert");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        button = nullptr;
    }
    if (!ButtonIsUsable(panel, button)) {
        slot.store(nullptr, std::memory_order_release);
        return false;
    }

    const D2RL::Widgets::UiAction action{
        .structSize = D2RL::Widgets::UiActionSize,
        .flags = 0,
        .target = target,
        .command = "Convert",
        .text = "",
    };
    const auto result = WidgetService->dispatchUiAction(Context, &action);
    if (result != D2RL::Widgets::Result::Success) {
        FailedRequests.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const auto count = DispatchedRequests.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (Settings.diagnostics && Context
        && !FirstDispatchReported.exchange(
            true, std::memory_order_relaxed)) {
        char message[220]{};
        std::snprintf(
            message,
            sizeof(message),
            "TransmuteHotkey: first SDK Widget convert action dispatched on %s (count=%llu).",
            surface,
            static_cast<unsigned long long>(count));
        Context->LogInfo(message);
    }
    return true;
}

void __cdecl DispatchOnUiThread(
    const D2RL::PluginContext*,
    void*
) noexcept {
    const bool dispatched = DispatchForPanel(
            StandaloneCubePanel,
            "HoradricCubePanelMessage",
            "standalone Cube")
        || DispatchForPanel(
            IntegratedCubePanel,
            "BankPanelMessage",
            "integrated Cube");
    if (!dispatched) {
        RefusedRequests.fetch_add(1, std::memory_order_relaxed);
        if (Settings.diagnostics && Context
            && !FirstRefusalReported.exchange(
                true, std::memory_order_acq_rel)) {
            Context->LogInfo(
                "TransmuteHotkey diagnostics: first accepted request was refused because the Cube button was no longer usable on the UI thread.");
        }
    }
    RequestPending.store(false, std::memory_order_release);
}

auto __cdecl OnInputAction(
    const D2RL::PluginContext*,
    const D2RL::Input::ActionEvent* event,
    void*
) noexcept -> D2RL::Input::ActionResult {
    if (!D2RL::Input::HasActionEventField(
            event, D2RL::Input::ActionEventRequiredSize)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind == D2RL::Input::ActionEventKind::Released) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (event->kind != D2RL::Input::ActionEventKind::Pressed
        || !CubePanelObserved()) {
        return D2RL::Input::ActionResult::Ignored;
    }

    bool expected{};
    if (!RequestPending.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return D2RL::Input::ActionResult::Ignored;
    }

    const auto result = ThreadService->runOnUiThread(
        Context, DispatchOnUiThread, nullptr);
    if (result != D2RL::Threads::Result::Success) {
        RequestPending.store(false, std::memory_order_release);
        FailedRequests.fetch_add(1, std::memory_order_relaxed);
        return D2RL::Input::ActionResult::Ignored;
    }

    AcceptedRequests.fetch_add(1, std::memory_order_relaxed);
    return D2RL::Input::ActionResult::Ignored;
}

auto RegisterInputAction() noexcept -> bool {
    const D2RL::Input::ActionRegistration registration{
        .structSize = D2RL::Input::ActionRegistrationSize,
        .flags = 0,
        .logicalId = "transmute-horadric-cube",
        .displayName = "Transmute Horadric Cube",
        .category = "RuffnecKk Suite",
        .defaultPrimary = {
            static_cast<D2RL::Input::Key>(Settings.hotkey.virtualKey),
            static_cast<D2RL::Input::Modifier>(Settings.hotkey.modifier),
        },
        .defaultSecondary = {
            D2RL::Input::Key::None,
            D2RL::Input::Modifier::None,
        },
        .callback = OnInputAction,
        .userData = nullptr,
    };
    const auto result = InputService->registerAction(
        Context, &registration, &ActionHandle);
    if (result == D2RL::Input::Result::Success
        && ActionHandle != D2RL::Input::InvalidHandle) {
        return true;
    }
    char message[220]{};
    std::snprintf(
        message,
        sizeof(message),
        "TransmuteHotkey: SDK v3 Input action registration failed with result %u.",
        static_cast<unsigned>(result));
    Context->LogError(message);
    ActionHandle = D2RL::Input::InvalidHandle;
    return false;
}

auto InstallObservationHooks() noexcept -> bool {
    if (!Context->InstallInlineHook(
            IntegratedCubeUpdateRva,
            IntegratedCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(IntegratedCubeUpdateExpected.size()),
            HookIntegratedCubeUpdate,
            &OriginalIntegratedCubeUpdate)) {
        Context->LogError(
            "TransmuteHotkey: integrated Cube observation hook failed.");
        return false;
    }
    if (!Context->InstallInlineHook(
            StandaloneCubeUpdateRva,
            StandaloneCubeUpdateExpected.data(),
            static_cast<std::uint32_t>(StandaloneCubeUpdateExpected.size()),
            HookStandaloneCubeUpdate,
            &OriginalStandaloneCubeUpdate)) {
        Context->LogError(
            "TransmuteHotkey: standalone Cube observation hook failed.");
        return false;
    }
    return true;
}

auto Status(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    char message[560]{};
    std::snprintf(
        message,
        sizeof(message),
        "Transmute Hotkey 1.0.0: enabled=%s; diagnostics=%s; configured=%s; input=SDK v3 action (service ABI v1); modifier-limit=one; mouse=unsupported; controller=unsupported; pass-through=always; accepted=%llu; dispatched=%llu; refused=%llu; failed=%llu.",
        Settings.enabled ? "true" : "false",
        Settings.diagnostics ? "true" : "false",
        Settings.hotkeyText.c_str(),
        static_cast<unsigned long long>(
            AcceptedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            DispatchedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            RefusedRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            FailedRequests.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetRuntime() noexcept {
    OriginalIntegratedCubeUpdate = nullptr;
    OriginalStandaloneCubeUpdate = nullptr;
    FindWidget = nullptr;
    InputService = nullptr;
    ThreadService = nullptr;
    WidgetService = nullptr;
    ActionHandle = D2RL::Input::InvalidHandle;
    IntegratedCubePanel.store(nullptr, std::memory_order_relaxed);
    StandaloneCubePanel.store(nullptr, std::memory_order_relaxed);
    RequestPending.store(false, std::memory_order_relaxed);
    AcceptedRequests.store(0, std::memory_order_relaxed);
    DispatchedRequests.store(0, std::memory_order_relaxed);
    RefusedRequests.store(0, std::memory_order_relaxed);
    FailedRequests.store(0, std::memory_order_relaxed);
    FirstDispatchReported.store(false, std::memory_order_relaxed);
    FirstRefusalReported.store(false, std::memory_order_relaxed);
    Settings = {};
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
    const D2RL::PluginContext* context
) noexcept -> bool {
    if (!D2RL::HasContext(context)
        || context->apiVersion < D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetRuntime();

    if (!Base) {
        context->LogError(
            "TransmuteHotkey: D2R executable base is unavailable.");
        return false;
    }
    if (!ReadConfiguration()) return false;
    if (context->modDataVersionBuild != 0
        && context->modDataVersionBuild != SupportedBuild) {
        context->LogError(
            "TransmuteHotkey: only D2R build 92777 is supported.");
        return false;
    }

    if (!Settings.enabled) {
        context->LogInfo(
            "Transmute Hotkey 1.0.0 by RuffnecKk disabled; no hook or input action was registered.");
    } else {
        if (!QueryServices() || !ValidateRuntime()) {
            context->LogError(
                "TransmuteHotkey: SDK service or 92777 preflight failed; activation refused.");
            return false;
        }
        FindWidget = At<FindWidgetFn>(FindWidgetRva);
        if (!RegisterInputAction() || !InstallObservationHooks()) return false;
        context->LogInfo(
            "Transmute Hotkey 1.0.0 by RuffnecKk active through SDK v3 Input, Thread, and Widget service ABI v1; keyboard input always passes through to D2R.");
    }

    if (!context->RegisterConsoleCommand(
            "transmute-hotkey",
            Status,
            "Show Transmute Hotkey SDK service status and counters.")) {
        context->LogWarn(
            "TransmuteHotkey: status command could not be registered.");
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context && Settings.diagnostics) {
        char message[280]{};
        std::snprintf(
            message,
            sizeof(message),
            "TransmuteHotkey diagnostics: stopped; accepted=%llu, dispatched=%llu, refused=%llu, failed=%llu.",
            static_cast<unsigned long long>(
                AcceptedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                DispatchedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                RefusedRequests.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                FailedRequests.load(std::memory_order_relaxed)));
        Context->LogInfo(message);
    }
    if (InputService && Context
        && ActionHandle != D2RL::Input::InvalidHandle) {
        (void)InputService->unregisterAction(Context, ActionHandle);
    }
    ResetRuntime();
    Base = nullptr;
    Context = nullptr;
}

} // namespace RuffnecKk::TransmuteHotkey
