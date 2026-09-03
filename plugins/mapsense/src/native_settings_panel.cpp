#include "native_settings_panel.hpp"
#include "native_settings_layout.hpp"

#include <D2RLPlugin/api.h>

#include <cstdint>
#include <string_view>

namespace RuffnecKk::MapSense {
namespace {

struct NativePanelState {
    const D2RL::PluginContext* context{};
    const D2RL::ResourceServiceV1* resources{};
    const D2RL::PanelServiceV1* panels{};
    const D2RL::SharedEventServiceV1* sharedEvents{};
    D2RL::Resources::RegistrationHandle resourceHandle{
        D2RL::Resources::InvalidHandle};
    D2RL::Panels::RegistrationHandle panelHandle{
        D2RL::Panels::InvalidHandle};
    D2RL::SharedEvents::ListenerHandle messageHandle{
        D2RL::SharedEvents::InvalidHandle};
    NativeSettingsPanelCallbacks callbacks{};
    bool initialized{};
};

NativePanelState State{};

auto ActionLogMessage(NativeSettingsAction action) noexcept -> const char* {
    switch (action) {
        case NativeSettingsAction::RevealLevel:
            return "MapSense: native Reveal Level control activated.";
        case NativeSettingsAction::RevealAct:
            return "MapSense: native Reveal Act control activated.";
        case NativeSettingsAction::ToggleRevealAll:
            return "MapSense: native Reveal All control activated.";
        case NativeSettingsAction::DisableRevealAll:
            return "MapSense: native Reveal All Off control activated.";
    }
    return "MapSense: native reveal control activated.";
}

auto __cdecl OnUiMessage(
        const D2RL::PluginContext* context,
        const D2RL::SharedEvents::UiMessageEvent* event,
        void*) noexcept -> D2RL::SharedEvents::UiMessageAction {
    if (!State.initialized
        || context == nullptr
        || context != State.context
        || event == nullptr
        || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
        || event->target == nullptr
        || event->command == nullptr
        || event->text == nullptr) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }

    const auto action = ClassifyNativeSettingsMessage(
        event->target,
        event->command,
        event->text);
    if (!action.has_value()) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }

    context->LogInfo(ActionLogMessage(*action));
    if (State.callbacks.onAction != nullptr) {
        State.callbacks.onAction(*action, State.callbacks.userData);
    }
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

void CleanupRegistrations() noexcept {
    if (State.context != nullptr
        && State.sharedEvents != nullptr
        && State.sharedEvents->unregisterUiMessageListener != nullptr
        && State.messageHandle != D2RL::SharedEvents::InvalidHandle) {
        (void)State.sharedEvents->unregisterUiMessageListener(
            State.context,
            State.messageHandle);
        State.messageHandle = D2RL::SharedEvents::InvalidHandle;
    }
    if (State.context != nullptr
        && State.panels != nullptr
        && State.panels->unregisterPanel != nullptr
        && State.panelHandle != D2RL::Panels::InvalidHandle) {
        (void)State.panels->unregisterPanel(
            State.context,
            State.panelHandle);
        State.panelHandle = D2RL::Panels::InvalidHandle;
    }
    if (State.context != nullptr
        && State.resources != nullptr
        && State.resources->unregisterResource != nullptr
        && State.resourceHandle != D2RL::Resources::InvalidHandle) {
        (void)State.resources->unregisterResource(
            State.context,
            State.resourceHandle);
        State.resourceHandle = D2RL::Resources::InvalidHandle;
    }
}

auto QueryServices() noexcept -> bool {
    if (State.context->QueryService(
            D2RL::ServiceId::Resource,
            D2RL::ResourceServiceV1Version,
            &State.resources) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasResourceServiceV1Field(
            State.resources,
            D2RL::ResourceServiceV1RequiredSize)
        || State.resources->registerResource == nullptr
        || State.resources->unregisterResource == nullptr) {
        State.context->LogWarn(
            "MapSense: D2RLoader ResourceService v1 is unavailable.");
        return false;
    }
    if (State.context->QueryService(
            D2RL::ServiceId::Panel,
            D2RL::PanelServiceV1Version,
            &State.panels) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasPanelServiceV1Field(
            State.panels,
            D2RL::PanelServiceV1RequiredSize)
        || State.panels->registerPanel == nullptr
        || State.panels->unregisterPanel == nullptr
        || State.panels->getPanelInfo == nullptr
        || State.panels->togglePanel == nullptr
        || State.panels->closePanel == nullptr) {
        State.context->LogWarn(
            "MapSense: D2RLoader PanelService v1 is unavailable.");
        return false;
    }
    if (State.context->QueryService(
            D2RL::ServiceId::SharedEvent,
            D2RL::SharedEventServiceV1Version,
            &State.sharedEvents) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasSharedEventServiceV1Field(
            State.sharedEvents,
            D2RL::SharedEventServiceV1RequiredSize)
        || State.sharedEvents->registerUiMessageListener == nullptr
        || State.sharedEvents->unregisterUiMessageListener == nullptr) {
        State.context->LogWarn(
            "MapSense: D2RLoader SharedEventService v1 is unavailable.");
        return false;
    }
    return true;
}

} // namespace

auto InitializeNativeSettingsPanel(
        const D2RL::PluginContext* context,
        NativeSettingsPanelCallbacks callbacks) noexcept -> bool {
    if (State.context != nullptr || context == nullptr) return false;

    State.context = context;
    State.callbacks = callbacks;
    if (!QueryServices()) {
        State = {};
        return false;
    }

    const D2RL::Resources::ResourceRegistration resource{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags = 0,
        .path = NativeSettingsPanelResourcePath,
        .bytes = NativeSettingsPanelLayout,
        .byteCount = static_cast<std::uint64_t>(
            NativeSettingsPanelLayoutView.size()),
    };
    if (State.resources->registerResource(
            context,
            &resource,
            &State.resourceHandle) != D2RL::Resources::Result::Success
        || State.resourceHandle == D2RL::Resources::InvalidHandle) {
        context->LogWarn(
            "MapSense: the native reveal-controls resource could not be registered.");
        CleanupRegistrations();
        State = {};
        return false;
    }

    const D2RL::Panels::PanelRegistration panel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags = D2RL::Panels::PanelFlags::CloseOnEscape,
        .localId = NativeSettingsPanelLocalId,
    };
    if (State.panels->registerPanel(
            context,
            &panel,
            &State.panelHandle) != D2RL::Panels::Result::Success
        || State.panelHandle == D2RL::Panels::InvalidHandle) {
        context->LogWarn(
            "MapSense: the native reveal-controls panel could not be registered.");
        CleanupRegistrations();
        State = {};
        return false;
    }

    const D2RL::SharedEvents::UiMessageListener listener{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags = 0,
        .priority = 10000,
        .reserved = 0,
        .callback = OnUiMessage,
        .userData = nullptr,
    };
    if (State.sharedEvents->registerUiMessageListener(
            context,
            &listener,
            &State.messageHandle) != D2RL::SharedEvents::Result::Success
        || State.messageHandle == D2RL::SharedEvents::InvalidHandle) {
        context->LogWarn(
            "MapSense: the native reveal-controls listener could not be registered.");
        CleanupRegistrations();
        State = {};
        return false;
    }

    State.initialized = true;
    context->LogInfo(
        "MapSense: minimal native reveal controls registered.");
    return true;
}

void ShutdownNativeSettingsPanel() noexcept {
    State.initialized = false;
    CleanupRegistrations();
    State = {};
}

auto ToggleNativeSettingsPanel() noexcept -> bool {
    if (!State.initialized
        || State.context == nullptr
        || State.panels == nullptr
        || State.panelHandle == D2RL::Panels::InvalidHandle) {
        return false;
    }

    D2RL::Panels::PanelInfo before{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    if (State.panels->getPanelInfo(
            State.context,
            State.panelHandle,
            &before) != D2RL::Panels::Result::Success
        || before.presentationState
            == D2RL::Panels::PresentationState::Unknown) {
        State.context->LogWarn(
            "MapSense: the native reveal-controls state could not be read.");
        return false;
    }

    if (State.panels->togglePanel(
            State.context,
            State.panelHandle) != D2RL::Panels::Result::Success) {
        State.context->LogWarn(
            "MapSense: the native reveal-controls panel could not be toggled.");
        return false;
    }

    D2RL::Panels::PanelInfo after{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    if (State.panels->getPanelInfo(
            State.context,
            State.panelHandle,
            &after) != D2RL::Panels::Result::Success
        || after.presentationState
            == D2RL::Panels::PresentationState::Unknown) {
        State.context->LogWarn(
            "MapSense: the native reveal-controls state could not be confirmed.");
        (void)State.panels->closePanel(State.context, State.panelHandle);
        return false;
    }
    return true;
}

auto GetNativeSettingsPanelStatus() noexcept
        -> NativeSettingsPanelStatus {
    NativeSettingsPanelStatus status{
        .ready = State.initialized,
        .open = false,
    };
    if (!State.initialized
        || State.context == nullptr
        || State.panels == nullptr
        || State.panelHandle == D2RL::Panels::InvalidHandle) {
        return status;
    }

    D2RL::Panels::PanelInfo info{
        .structSize = D2RL::Panels::PanelInfoSize,
    };
    if (State.panels->getPanelInfo(
            State.context,
            State.panelHandle,
            &info) == D2RL::Panels::Result::Success) {
        status.open = info.presentationState
            == D2RL::Panels::PresentationState::Open;
    }
    return status;
}

} // namespace RuffnecKk::MapSense
