#include "hud/native_slider_panel.hpp"
#include "hud/slider_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>
namespace players_hud_slider { void RunDirectClickContract(); }
using namespace players_hud_slider;
namespace {
int checks{}, resourceAdds{}, resourceRemoves{}, panelAdds{}, panelRemoves{}, listenerAdds{}, listenerRemoves{};
bool available = true, queueWorks = true;
std::vector<std::string> layouts;
std::map<std::string, std::uint64_t> handles;
std::map<std::uint64_t, bool> visible;
std::map<std::uint64_t, bool> opened;
void Check(bool ok, const char* label) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); } }
void __cdecl Log(const D2RL::PluginContext*, const char*) noexcept {}
auto __cdecl AddResource(const D2RL::PluginContext*, const D2RL::Resources::ResourceRegistration* info, uint64_t* out) noexcept -> D2RL::Resources::Result {
    ++resourceAdds; *out = static_cast<uint64_t>(resourceAdds);
    if (std::string_view(info->path).ends_with(".json")) layouts.emplace_back(static_cast<const char*>(info->bytes), static_cast<std::size_t>(info->byteCount));
    return D2RL::Resources::Result::Success;
}
auto __cdecl RemoveResource(const D2RL::PluginContext*, uint64_t) noexcept -> D2RL::Resources::Result { ++resourceRemoves; return D2RL::Resources::Result::Success; }
auto __cdecl AddPanel(const D2RL::PluginContext*, const D2RL::Panels::PanelRegistration*, uint64_t* out) noexcept -> D2RL::Panels::Result {
    ++panelAdds; *out = static_cast<uint64_t>(panelAdds); return D2RL::Panels::Result::Success;
}
auto __cdecl RemovePanel(const D2RL::PluginContext*, uint64_t) noexcept -> D2RL::Panels::Result { ++panelRemoves; return D2RL::Panels::Result::Success; }
auto __cdecl Open(const D2RL::PluginContext*, uint64_t handle) noexcept -> D2RL::Panels::Result { opened[handle] = true; return D2RL::Panels::Result::Success; }
auto __cdecl Close(const D2RL::PluginContext*, uint64_t handle) noexcept -> D2RL::Panels::Result { opened[handle] = false; return D2RL::Panels::Result::Success; }
auto __cdecl Info(const D2RL::PluginContext*, uint64_t handle, D2RL::Panels::PanelInfo* info) noexcept -> D2RL::Panels::Result {
    info->presentationState = opened[handle] ? D2RL::Panels::PresentationState::Open : D2RL::Panels::PresentationState::Closed;
    return D2RL::Panels::Result::Success;
}
auto __cdecl Listen(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageListener*, uint64_t* out) noexcept -> D2RL::SharedEvents::Result {
    ++listenerAdds; *out = static_cast<uint64_t>(listenerAdds); return D2RL::SharedEvents::Result::Success;
}
auto __cdecl Unlisten(const D2RL::PluginContext*, uint64_t) noexcept -> D2RL::SharedEvents::Result { ++listenerRemoves; return D2RL::SharedEvents::Result::Success; }
auto __cdecl FindPanel(const D2RL::PluginContext*, const char*, uint64_t* out) noexcept -> D2RL::Widgets::Result { *out = 1; return D2RL::Widgets::Result::Success; }
auto __cdecl FindWidget(const D2RL::PluginContext*, uint64_t, const char* name, uint64_t* out) noexcept -> D2RL::Widgets::Result {
    auto [it, inserted] = handles.try_emplace(name, handles.size()+1); (void)inserted; *out = it->second; return D2RL::Widgets::Result::Success;
}
auto __cdecl Visible(const D2RL::PluginContext*, uint64_t handle, bool value) noexcept -> D2RL::Widgets::Result { visible[handle] = value; return D2RL::Widgets::Result::Success; }
auto __cdecl Queue(const D2RL::PluginContext* context, D2RL::Threads::Callback cb, void* data) noexcept -> D2RL::Threads::Result {
    if (!queueWorks) return D2RL::Threads::Result::Unavailable;
    cb(context, data); return D2RL::Threads::Result::Success;
}
D2RL::ResourceServiceV1 resources{};
D2RL::PanelServiceV1 panels{};
D2RL::SharedEventServiceV1 events{};
D2RL::WidgetServiceV1 widgets{};
D2RL::ThreadServiceV1 threads{};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, uint32_t, const void** out) noexcept -> D2RL::ServiceQueryResult {
    *out = nullptr; if (!available) return D2RL::ServiceQueryResult::Unavailable;
    if (id == D2RL::ServiceId::Resource) *out = &resources;
    if (id == D2RL::ServiceId::Panel) *out = &panels;
    if (id == D2RL::ServiceId::SharedEvent) *out = &events;
    if (id == D2RL::ServiceId::Widget) *out = &widgets;
    if (id == D2RL::ServiceId::Thread) *out = &threads;
    return *out ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
bool IsVisible(const char* name) { const auto i = handles.find(name); return i != handles.end() && visible[i->second]; }
}
int main() {
    resources.serviceSize = sizeof(resources); resources.serviceVersion = 1; resources.registerResource = AddResource; resources.unregisterResource = RemoveResource;
    panels.serviceSize = sizeof(panels); panels.serviceVersion = 1; panels.registerPanel = AddPanel; panels.unregisterPanel = RemovePanel; panels.getPanelInfo = Info; panels.openPanel = Open; panels.closePanel = Close;
    events.serviceSize = sizeof(events); events.serviceVersion = 1; events.registerUiMessageListener = Listen; events.unregisterUiMessageListener = Unlisten;
    widgets.serviceSize = sizeof(widgets); widgets.serviceVersion = 1; widgets.findPanel = FindPanel; widgets.findWidget = FindWidget; widgets.setWidgetVisible = Visible;
    threads.serviceSize = sizeof(threads); threads.serviceVersion = 1; threads.runOnUiThread = Queue;
    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.queryService = Query; api.logWarn = Log; api.logInfo = Log;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.api = &api;
    RunDirectClickContract();
    NativeSliderPanel panel; HudPlayerSliderConfig config;
    available = false; Check(!panel.Start(&context, config, 24), "missing optional services rejected"); available = true;
    for (int minimum : {1,4,24}) {
        config.minimumPlayers = minimum;
        Check(panel.Start(&context, config, 64), "production panel registration");
        Check(listenerAdds == 0, "HUD needs no native message listener");
        Check(layouts.size() == 2, "slider and button layouts registered");
        { std::ofstream file("hud-layout-"+std::to_string(minimum)+".json"); file << layouts[0]; }
        { std::ofstream file("hud-button-"+std::to_string(minimum)+".json"); file << layouts[1]; }
        Check(layouts[0].find("SettingsSliderWidget") == std::string::npos && layouts[0].find("OfflineDifficultyScaling") == std::string::npos, "no engine-setting binding");
        Check(layouts[0].find("SliderMarker25") == std::string::npos, "no selection above p24");
        panel.SetTooltipValue(64);
        Check(IsVisible("SliderMarker24") && !IsVisible("SliderTooltipValue24"), "out-of-range display uses endpoint marker without false p24 text");
        Check(IsVisible("ActualDigit36") && IsVisible("ActualDigit44"), "actual p64 digits shown");
        Check(panel.Open(), "cached panel reopens");
        panel.SetTooltipValue(minimum);
        Check(!IsVisible("ActualDigit36") && !IsVisible("ActualDigit44"), "old actual digits cleared");
        Check(IsVisible(("SliderTooltipValue"+std::to_string(minimum)).c_str()), "in-range label shown");
        queueWorks = false; panel.QueueTooltipValue(16); Check(!IsVisible("SliderTooltipValue16") || minimum == 16, "queue failure does not bypass UI thread"); queueWorks = true;
        panel.Stop(); layouts.clear(); handles.clear(); visible.clear();
        Check(resourceAdds == resourceRemoves && panelAdds == panelRemoves && listenerAdds == listenerRemoves, "all registrations cleaned");
    }
    std::printf("%d HUD panel/resource/display checks passed\n", checks);
}
