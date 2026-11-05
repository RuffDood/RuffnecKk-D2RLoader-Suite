#include "loader_fixture.hpp"
#include "../src/auto_interact/settings.hpp"
#include "picker_ui.hpp"
#include <iostream>
#include <map>
#include <vector>
using namespace RuffnecKk::AutoPickup;
using namespace RuffnecKk::AutoPickup::Picker;
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
Config live;
std::string search, saved, layoutText;
bool open{}, failWrite{}, failBind{}, failPanel{};
unsigned writes{}, commits{}, unregistered{};
std::map<std::string, WidgetHandle> ids;
std::map<WidgetHandle, std::string> labels;
std::map<WidgetHandle, bool> enabled;
std::vector<std::pair<D2RL::Threads::Callback, void*>> queued;
D2RL::SharedEvents::UiMessageListener messageListener{};
D2RL::Lifecycle::DataTablesLoadedListener dataListener{};
D2RL::ConsoleCommandRegistration command{};
D2RL::Input::ActionRegistration binding{};
unsigned registeredActions{}, removedActions{};
std::array<std::array<std::uint32_t, 5>, 20> rows{};
const D2RL::PluginContext* ctx{};
auto Snapshot() -> Config { return live; }
void Commit(Config config) { live = std::move(config); ++commits; }
void Drain() { while (!queued.empty()) { auto pending = std::move(queued); queued.clear(); for (auto [callback, data] : pending) callback(ctx, data); } }
auto __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback callback, void* data) noexcept -> D2RL::Threads::Result {
    queued.emplace_back(callback, data); return D2RL::Threads::Result::Success;
}
auto __cdecl FindPanel(const D2RL::PluginContext*, const char*, WidgetHandle* out) noexcept -> WidgetResult { *out = 100; return WidgetResult::Success; }
auto __cdecl FindWidget(const D2RL::PluginContext*, WidgetHandle, const char* name, WidgetHandle* out) noexcept -> WidgetResult {
    auto [it, added] = ids.try_emplace(name, ids.size() + 1); (void)added; *out = it->second; return WidgetResult::Success;
}
auto __cdecl Visible(const D2RL::PluginContext*, WidgetHandle, bool) noexcept -> WidgetResult { return WidgetResult::Success; }
auto __cdecl Enable(const D2RL::PluginContext*, WidgetHandle id, bool value) noexcept -> WidgetResult { enabled[id] = value; return WidgetResult::Success; }
auto __cdecl Text(const D2RL::PluginContext*, WidgetHandle id, const char* text) noexcept -> WidgetResult { labels[id] = text; return WidgetResult::Success; }
auto __cdecl InputText(const D2RL::PluginContext*, WidgetHandle, char* out, std::uint32_t size, std::uint32_t* required) noexcept -> WidgetResult {
    *required = static_cast<std::uint32_t>(search.size() + 1);
    if (size < *required) return static_cast<WidgetResult>(10);
    std::memcpy(out, search.c_str(), *required); return WidgetResult::Success;
}
auto __cdecl Bind(const D2RL::PluginContext*, WidgetHandle, WidgetEvent, const char*, const char*) noexcept -> WidgetResult {
    return failBind ? WidgetResult::Unavailable : WidgetResult::Success;
}
auto __cdecl RegisterResource(const D2RL::PluginContext*, const D2RL::Resources::ResourceRegistration* r, D2RL::Resources::RegistrationHandle* out) noexcept -> D2RL::Resources::Result {
    layoutText.assign(static_cast<const char*>(r->bytes), static_cast<std::size_t>(r->byteCount)); *out = 1; return D2RL::Resources::Result::Success;
}
auto __cdecl RemoveResource(const D2RL::PluginContext*, D2RL::Resources::RegistrationHandle) noexcept -> D2RL::Resources::Result { ++unregistered; return D2RL::Resources::Result::Success; }
auto __cdecl RegisterPanel(const D2RL::PluginContext*, const D2RL::Panels::PanelRegistration*, D2RL::Panels::RegistrationHandle* out) noexcept -> D2RL::Panels::Result {
    if (failPanel) return D2RL::Panels::Result::Unavailable;
    *out = 2; return D2RL::Panels::Result::Success;
}
auto __cdecl RemovePanel(const D2RL::PluginContext*, D2RL::Panels::RegistrationHandle) noexcept -> D2RL::Panels::Result { ++unregistered; open = false; return D2RL::Panels::Result::Success; }
auto __cdecl Info(const D2RL::PluginContext*, D2RL::Panels::RegistrationHandle, D2RL::Panels::PanelInfo* out) noexcept -> D2RL::Panels::Result {
    out->presentationState = open ? D2RL::Panels::PresentationState::Open : D2RL::Panels::PresentationState::Closed; return D2RL::Panels::Result::Success;
}
auto __cdecl Open(const D2RL::PluginContext*, D2RL::Panels::RegistrationHandle) noexcept -> D2RL::Panels::Result { open = true; return D2RL::Panels::Result::Success; }
auto __cdecl Close(const D2RL::PluginContext*, D2RL::Panels::RegistrationHandle) noexcept -> D2RL::Panels::Result { open = false; return D2RL::Panels::Result::Success; }
auto __cdecl RegisterMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageListener* listener, D2RL::SharedEvents::ListenerHandle* out) noexcept -> D2RL::SharedEvents::Result {
    messageListener = *listener; *out = 3; return D2RL::SharedEvents::Result::Success;
}
auto __cdecl RemoveMessage(const D2RL::PluginContext*, D2RL::SharedEvents::ListenerHandle) noexcept -> D2RL::SharedEvents::Result { ++unregistered; return D2RL::SharedEvents::Result::Success; }
auto __cdecl RegisterData(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedListener* listener, D2RL::Lifecycle::ListenerHandle* out) noexcept -> D2RL::Lifecycle::Result {
    dataListener = *listener; *out = 4; return D2RL::Lifecycle::Result::Success;
}
auto __cdecl RemoveData(const D2RL::PluginContext*, D2RL::Lifecycle::ListenerHandle) noexcept -> D2RL::Lifecycle::Result { ++unregistered; return D2RL::Lifecycle::Result::Success; }
auto __cdecl GetTable(const D2RL::PluginContext*, D2RL::DataTables::Bank bank, D2RL::DataTables::TableId, D2RL::DataTables::TableView* out) noexcept -> D2RL::DataTables::Result {
    if (bank != D2RL::DataTables::Bank::Rotw) return D2RL::DataTables::Result::Unavailable;
    out->rows = rows.data(); out->rowSize = sizeof(rows[0]); out->rowCount = static_cast<std::uint32_t>(rows.size()); out->revision = 2; return D2RL::DataTables::Result::Success;
}
auto __cdecl ByCode(const D2RL::PluginContext*, D2RL::DataTables::Bank, D2RL::DataTables::TableId, std::uint32_t code, D2RL::DataTables::RowView* out) noexcept -> D2RL::DataTables::Result {
    for (std::uint32_t i = 0; i < rows.size(); ++i) if (rows[i][3] == code) { out->row = rows[i].data(); out->rowIndex = i; out->rowSize = sizeof(rows[0]); out->revision = 2; return D2RL::DataTables::Result::Success; }
    return D2RL::DataTables::Result::NotFound;
}
auto __cdecl Name(const D2RL::PluginContext*, const char* key, char* out, std::uint32_t size, std::uint32_t* required) noexcept -> D2RL::Localization::Result {
    const std::string name = std::string_view(key) == "d2r:gmo" ? "Chaos Onyx" : "Rune " + std::string(key + 4);
    *required = static_cast<std::uint32_t>(name.size() + 1);
    if (size < *required) return D2RL::Localization::Result::BufferTooSmall;
    std::memcpy(out, name.c_str(), *required); return D2RL::Localization::Result::Success;
}
bool __cdecl RegisterCommand(const D2RL::PluginContext*, const D2RL::ConsoleCommandRegistration* value) noexcept { command = *value; return true; }
auto __cdecl RegisterAction(const D2RL::PluginContext*, const D2RL::Input::ActionRegistration* value, D2RL::Input::ActionHandle* out) noexcept -> D2RL::Input::Result {
    binding = *value; ++registeredActions; *out = 50; return D2RL::Input::Result::Success;
}
auto __cdecl RemoveAction(const D2RL::PluginContext*, D2RL::Input::ActionHandle) noexcept -> D2RL::Input::Result { ++removedActions; return D2RL::Input::Result::Success; }
bool __cdecl Write(const D2RL::PluginContext*, const char* value) noexcept { ++writes; if (failWrite) return false; saved = value; LoaderFixture::ConfigText = saved; return true; }
WidgetExtensions widgets{};
D2RL::PanelServiceV1 panels{};
D2RL::ResourceServiceV1 resources{};
D2RL::SharedEventServiceV1 events{};
D2RL::LifecycleServiceV1 lifecycle{};
D2RL::DataTableServiceV1 tables{};
D2RL::ThreadServiceV1 threads{};
D2RL::LocalizationServiceV1 localization{};
D2RL::InputServiceV1 input{};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t, const void** out) noexcept -> D2RL::ServiceQueryResult {
    switch (id) {
        case D2RL::ServiceId::Widget: *out = &widgets; break;
        case D2RL::ServiceId::Panel: *out = &panels; break;
        case D2RL::ServiceId::Resource: *out = &resources; break;
        case D2RL::ServiceId::SharedEvent: *out = &events; break;
        case D2RL::ServiceId::Lifecycle: *out = &lifecycle; break;
        case D2RL::ServiceId::DataTable: *out = &tables; break;
        case D2RL::ServiceId::Thread: *out = &threads; break;
        case D2RL::ServiceId::Localization: *out = &localization; break;
        case D2RL::ServiceId::Input: *out = &input; break;
        default: *out = nullptr; return D2RL::ServiceQueryResult::Unavailable;
    }
    return D2RL::ServiceQueryResult::Success;
}
void Send(const char* name) {
    const D2RL::SharedEvents::UiMessageEvent event{sizeof(event), 0, 0, 0, "AutoPickupPicker", name, nullptr};
    CHECK(messageListener.callback(ctx, &event, messageListener.userData) == D2RL::SharedEvents::UiMessageAction::Consume);
}
}
namespace S=Reimagined::InteractSettings;
auto ReadInteract(S::Values* out) noexcept -> S::Result { Config c;std::string error;if(!ParseConfig(LoaderFixture::ConfigText,c,error))return S::Result::Unavailable;*out=AutoInteract::ValuesFor(c.interact);return S::Result::Success; }
auto SaveInteract(const S::Values* expected,const S::Values* draft,char* error,std::uint32_t capacity) noexcept -> S::Result {
    std::string output,why;AutoInteract::Config applied;
    if(!AutoInteract::SaveSettings(LoaderFixture::ConfigText,*expected,*draft,output,applied,why))return S::Result::Conflict;
    if(!Write(ctx,output.c_str()))return S::Result::WriteFailed;
    snprintf(error,capacity,"Saved Auto Interact settings.");return S::Result::Success;
}
const S::Api interactApi{sizeof(S::Api),S::Version,ReadInteract,SaveInteract};
int main() {
    ctx = LoaderFixture::Setup("[item_codes]\ninclude=[]\nexclude=[]\n[d2rl]\nmatch=[]\n", true);
    LoaderFixture::Context.activeMod = "ExampleMod";
    LoaderFixture::Api.queryService = Query; LoaderFixture::Api.writeConfig = Write; LoaderFixture::Api.registerConsoleCommand = RegisterCommand;
    widgets.base.serviceSize = sizeof(widgets); widgets.base.serviceVersion = 1; widgets.base.findPanel = FindPanel; widgets.base.findWidget = FindWidget;
    widgets.base.setWidgetVisible = Visible; widgets.base.setWidgetEnabled = Enable; widgets.getInputText = InputText; widgets.setText = Text; widgets.setWidgetEventMessage = Bind;
    panels.serviceSize = sizeof(panels); panels.serviceVersion = 1; panels.registerPanel = RegisterPanel; panels.unregisterPanel = RemovePanel;
    panels.getPanelInfo = Info; panels.openPanel = Open; panels.closePanel = Close;
    resources.serviceSize = sizeof(resources); resources.serviceVersion = 1; resources.registerResource = RegisterResource; resources.unregisterResource = RemoveResource;
    events.serviceSize = sizeof(events); events.serviceVersion = 1; events.registerUiMessageListener = RegisterMessage; events.unregisterUiMessageListener = RemoveMessage;
    lifecycle = {sizeof(lifecycle), 1, RegisterData, RemoveData, LoaderFixture::Subscribe, LoaderFixture::Unsubscribe};
    tables = {sizeof(tables), 1, GetTable, nullptr, nullptr, ByCode}; threads = {sizeof(threads), 1, Queue, Queue}; localization = {sizeof(localization), 2, nullptr, Name};
    input.serviceSize = sizeof(input); input.serviceVersion = 1; input.registerAction = RegisterAction; input.unregisterAction = RemoveAction;
    for (unsigned i = 0; i < rows.size(); ++i) rows[i][3] = PackItemCode(i == 0 ? "gmo" : "r" + std::to_string(i));
    Interface picker;
    CHECK(picker.Start(ctx, Snapshot, Commit, false, true, &interactApi));
    CHECK(layoutText.find("InputTextBoxWidget") != std::string::npos);
    D2RL::Lifecycle::DataTablesLoadedEvent loaded{sizeof(loaded), 0, 2};
    dataListener.callback(ctx, &loaded, dataListener.userData); Drain();
    CHECK(command.callback(nullptr, nullptr, command.userData) == D2RL::ConsoleCommandResult::Handled); Drain(); CHECK(open);
    CHECK(labels[ids["Source"]].find("ExampleMod") != std::string::npos);
    CHECK(labels[ids["Page"]].find("Page 1 / 3") != std::string::npos);
    Send("Next"); CHECK(labels[ids["Page"]].find("Page 2 / 3") != std::string::npos);
    search = "gmo"; Send("Search"); CHECK(labels[ids["Item0"]].find("Chaos Onyx (gmo)") != std::string::npos);
    CHECK(labels[ids["Page"]].find("1 matching") != std::string::npos);
    Send("Choice8"); CHECK(!enabled[ids["Save"]]); // reject a row outside the bounded viewport
    Send("Choice0"); CHECK(enabled[ids["Save"]]);
    failWrite = true; Send("Save"); CHECK(writes == 1 && commits == 0 && live.includeCodes.empty());
    failWrite = false; Send("Save"); CHECK(writes == 2 && commits == 1 && live.includeCodes == std::vector{PackItemCode("gmo")});
    Send("Choice0"); Send("Close"); CHECK(!open && commits == 1); // unsaved changes are discarded
    command.callback(nullptr, nullptr, command.userData); Drain();
    Send("Choice0"); LoaderFixture::ConfigText = "enabled=true\n[vanilla]\nrunes=true\n[item_codes]\ninclude=[\"jew\"]\nexclude=[]\n";
    Send("Save"); CHECK(writes == 2 && commits == 1 && labels[ids["Status"]].find("outside") != std::string::npos);
    Send("Close"); command.callback(nullptr, nullptr, command.userData); Drain();
    Send("Options");
    CHECK(labels[ids["Item0"]].find("Enable item pickup: On") != std::string::npos);
    Send("Choice0"); Send("Choice2"); Send("Save");
    CHECK(!live.enabled && !live.vanilla[1] && writes == 3 && commits == 2);
    // Reopen while Pickit is off. The settings provider remains independent.
    Send("Close"); command.callback(nullptr, nullptr, command.userData); Drain();
    Send("Options"); // General -> Auto Interact
    CHECK(labels[ids["Source"]].find("Auto Interact") != std::string::npos && enabled[ids["Choice0"]]);
    Send("Choice2"); Send("Save");
    CHECK(writes == 4 && commits == 2 && !live.enabled); // same TOML, only the Auto Interact section saved
    CHECK(labels[ids["Status"]].find("Saved Auto Interact") != std::string::npos);
    picker.Stop(); CHECK(!open && unregistered == 4);
    failPanel = true;
    CHECK(picker.Start(ctx, Snapshot, Commit, false, true, &interactApi)); CHECK(unregistered == 5); // resource cleanup, then Win32 fallback
    CHECK(registeredActions == 2 && binding.callback && binding.userData == &picker);
    picker.Stop(); CHECK(removedActions == 2);
    failPanel = false; widgets.base.serviceSize = sizeof(D2RL::WidgetServiceV1);
    CHECK(picker.Start(ctx, Snapshot, Commit, false, true, &interactApi)); // installed 56-byte ABI registers hotkey through the Win32 frontend
    CHECK(registeredActions == 3 && command.callback && command.userData == &picker);
    CHECK(binding.defaultPrimary.key == D2RL::Input::Key::P && binding.defaultPrimary.modifier == D2RL::Input::Modifier::Control);
    CHECK(std::string_view(binding.displayName) == "Auto Pickup Settings");
    const D2RL::Input::ActionEvent pressed{sizeof(D2RL::Input::ActionEvent), 0, 50, D2RL::Input::ActionEventKind::Pressed, 0, binding.defaultPrimary};
    CHECK(binding.callback(ctx, &pressed, binding.userData) == D2RL::Input::ActionResult::Handled && queued.size() == 1);
    queued.clear(); // Model loader cancellation on unload without displaying the test overlay.
    picker.Stop(); CHECK(removedActions == 3);
    widgets.base.serviceSize = sizeof(widgets); failBind = true;
    CHECK(picker.Start(ctx, Snapshot, Commit, false, true, &interactApi)); command.callback(nullptr, nullptr, command.userData); Drain(); CHECK(!open);
    picker.Stop();
    std::cout << "Picker UI lifecycle, mod table search, paging, draft discard, failed writes and external-edit conflicts; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
