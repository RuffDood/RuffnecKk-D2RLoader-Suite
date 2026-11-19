#pragma once

#include <D2RLPlugin/widgets.h>

namespace MapSeedSelector {

struct ScreenRect {
    float x;
    float y;
    float width;
    float height;
};

// Widget service ABI 1 appends optional fields after the Suite SDK's prefix.
// Field positions follow PluginSDK 03a5af9e0feb3f95f95e148e330e01f6635950df.
struct ExtendedWidgetService {
    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    D2RL::Widgets::FindPanelFn findPanel;
    D2RL::Widgets::FindWidgetFn findWidget;
    D2RL::Widgets::GetWidgetRectFn getWidgetRect;
    D2RL::Widgets::SetWidgetVisibleFn setWidgetVisible;
    D2RL::Widgets::SetWidgetEnabledFn setWidgetEnabled;
    D2RL::Widgets::DispatchUiActionFn dispatchUiAction;

    std::uintptr_t beforeDropdown[4];
    D2RL::Widgets::Result (__cdecl* getDropdownSelection)(
        const D2RL::PluginContext*, D2RL::Widgets::WidgetHandle, std::uint32_t*) noexcept;
    D2RL::Widgets::Result (__cdecl* setDropdownUpdateMessage)(
        const D2RL::PluginContext*, D2RL::Widgets::WidgetHandle, const char*, const char*) noexcept;
    std::uintptr_t beforeSetSelection[6];
    D2RL::Widgets::Result (__cdecl* setDropdownSelection)(
        const D2RL::PluginContext*, D2RL::Widgets::WidgetHandle, std::uint32_t) noexcept;
    std::uintptr_t beforeScreenRect[5];
    D2RL::Widgets::Result (__cdecl* getWidgetScreenRect)(
        const D2RL::PluginContext*, D2RL::Widgets::WidgetHandle, ScreenRect*) noexcept;
};

static_assert(offsetof(ExtendedWidgetService, beforeDropdown) == sizeof(D2RL::WidgetServiceV1));
static_assert(offsetof(ExtendedWidgetService, getDropdownSelection) == 88);
static_assert(offsetof(ExtendedWidgetService, setDropdownUpdateMessage) == 96);
static_assert(offsetof(ExtendedWidgetService, setDropdownSelection) == 152);
static_assert(offsetof(ExtendedWidgetService, getWidgetScreenRect) == 200);

inline constexpr auto WidgetServiceRequiredSize = sizeof(D2RL::WidgetServiceV1);
inline constexpr auto WidgetServiceSetDropdownSelectionFieldEnd =
    offsetof(ExtendedWidgetService, setDropdownSelection) + sizeof(ExtendedWidgetService::setDropdownSelection);
inline constexpr auto WidgetServiceGetWidgetScreenRectFieldEnd =
    offsetof(ExtendedWidgetService, getWidgetScreenRect) + sizeof(ExtendedWidgetService::getWidgetScreenRect);

inline auto HasWidgetServiceField(const ExtendedWidgetService* service, std::size_t end) noexcept -> bool {
    return service != nullptr && service->serviceVersion == D2RL::WidgetServiceV1Version
        && service->serviceSize >= end;
}

} // namespace MapSeedSelector
