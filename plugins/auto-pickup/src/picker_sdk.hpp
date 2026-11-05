#pragma once
#include <D2RLPlugin/api.h>

namespace RuffnecKk::AutoPickup::Picker {
// Optional append-only Widget ABI 1 fields, pinned to official SDK 0.4.0,
// 03a5af9e0feb3f95f95e148e330e01f6635950df/widgets.h. Keep the authored
// adapter separate from the unchanged ABI 3 SDK. Never access a tail field
// until serviceSize, serviceVersion and the function itself are checked.
enum class WidgetEvent : std::uint32_t { Click = 1, InputChanged = 2, InputReturn = 3, DropdownChanged = 4 };
using WidgetResult = D2RL::Widgets::Result;
using WidgetHandle = D2RL::Widgets::WidgetHandle;
using ReservedWidgetFn = void(__cdecl*)() noexcept;
struct WidgetExtensions {
    D2RL::WidgetServiceV1 base;
    WidgetResult(__cdecl* getInputText)(const D2RL::PluginContext*, WidgetHandle, char*, std::uint32_t, std::uint32_t*) noexcept;
    WidgetResult(__cdecl* setText)(const D2RL::PluginContext*, WidgetHandle, const char*) noexcept;
    ReservedWidgetFn reserved72[15]; // tooltip through slider setter, unused
    WidgetResult(__cdecl* setWidgetEventMessage)(const D2RL::PluginContext*, WidgetHandle, WidgetEvent, const char*, const char*) noexcept;
};
static_assert(offsetof(WidgetExtensions, getInputText) == 56);
static_assert(offsetof(WidgetExtensions, setText) == 64);
static_assert(offsetof(WidgetExtensions, setWidgetEventMessage) == 192);
static_assert(sizeof(WidgetExtensions) == 200);
inline auto HasPickerWidgets(const WidgetExtensions* s) noexcept -> bool {
    return s && s->base.serviceVersion == 1 && s->base.serviceSize >= sizeof(WidgetExtensions)
        && s->base.findPanel && s->base.findWidget && s->base.setWidgetVisible && s->base.setWidgetEnabled
        && s->getInputText && s->setText && s->setWidgetEventMessage;
}
}
