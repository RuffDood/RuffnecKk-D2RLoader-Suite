#pragma once
#include "picker_sdk.hpp"
#include "picker_layout.hpp"
#include "picker_catalog.hpp"
#include "picker_window.hpp"
#include <array>
#include <deque>
#include <mutex>
#include <unordered_set>

namespace RuffnecKk::AutoPickup::Picker {
class Interface {
public:
    using SnapshotFn = Config(*)();
    using CommitFn = void(*)(Config);
private:
    const D2RL::PluginContext* context{};
    const WidgetExtensions* widgets{};
    const D2RL::PanelServiceV1* panels{};
    const D2RL::ResourceServiceV1* resources{};
    const D2RL::SharedEventServiceV1* events{};
    const D2RL::LifecycleServiceV1* lifecycle{};
    const D2RL::DataTableServiceV1* tables{};
    const D2RL::ThreadServiceV1* threads{};
    const D2RL::InputServiceV1* input{};
    const D2RL::LocalizationServiceV1* localization{};
    D2RL::Resources::RegistrationHandle layout{};
    D2RL::Panels::RegistrationHandle panel{};
    D2RL::Input::ActionHandle action{};
    D2RL::SharedEvents::ListenerHandle messages{};
    D2RL::Lifecycle::ListenerHandle tableListener{};
    std::array<D2RL::Lifecycle::ListenerHandle, 2> sessionListeners{};
    std::mutex catalogMutex;
    std::vector<Entry> catalog;
    std::uint64_t catalogRevision{};
    std::uint64_t catalogEpoch{}, renderedEpoch{};
    Config renderedConfig;
    std::string renderedQuery;
    bool rowsReady{}, renderedFiltered{};
    std::string catalogStatus{"Item tables are not ready. Enter a game, then reopen this panel."};
    Config original, draft;
    SnapshotFn snapshot{};
    CommitFn commit{};
    WidgetHandle root{};
    bool editing{}, overridesOnly{}, options{}, delegated{};
    std::size_t page{};
    std::vector<Entry> matches;
    std::array<std::uint32_t, PageSize> visible{};
    std::string status;
    Window window;
    bool windowMode{};
    Reimagined::InteractSettings::Editor interact;
    bool interactTab{};
    std::mutex actionsMutex;
    std::deque<WindowAction> actions;
    bool actionsQueued{};
    template<class Service> auto Query(D2RL::ServiceId id, std::uint32_t version, const Service** out) -> bool {
        return context->QueryService(id, version, out) == D2RL::ServiceQueryResult::Success;
    }
    auto Find(const std::string& name) const -> WidgetHandle {
        WidgetHandle handle{};
        if (!root || widgets->base.findWidget(context, root, name.c_str(), &handle) != WidgetResult::Success) return 0;
        return handle;
    }
    void Text(const std::string& name, const std::string& text) const {
        if (const auto handle = Find(name)) widgets->setText(context, handle, text.c_str());
    }
    // Buttons carry a native child label. setText applies to the text widget,
    // not to ButtonWidget itself, so set row choice through a separate label.
    void Enable(const std::string& name, bool enabled) const {
        if (const auto handle = Find(name)) widgets->base.setWidgetEnabled(context, handle, enabled);
    }
    auto Bind(const std::string& name, WidgetEvent kind, const char* command) const -> bool {
        const auto handle = Find(name);
        return handle && widgets->setWidgetEventMessage(context, handle, kind, "AutoPickupPicker", command) == WidgetResult::Success;
    }
    auto IsOpen() const -> bool {
        if (windowMode) return window.IsOpen();
        D2RL::Panels::PanelInfo info{}; info.structSize = D2RL::Panels::PanelInfoSize;
        return panel && panels->getPanelInfo(context, panel, &info) == D2RL::Panels::Result::Success
            && info.presentationState == D2RL::Panels::PresentationState::Open;
    }
    auto SearchText() const -> std::string {
        if (windowMode) return window.SearchText();
        std::array<char, 512> value{}; std::uint32_t needed{};
        const auto handle = Find("Search");
        if (!handle || widgets->getInputText(context, handle, value.data(), static_cast<std::uint32_t>(value.size()), &needed) != WidgetResult::Success)
            return {};
        value.back() = 0; return value.data();
    }
    void Render() {
        if (!editing || !IsOpen()) return;
        std::vector<Entry> entries;
        std::string source;
        const auto query = SearchText();
        bool rebuild{};
        {
            std::lock_guard lock(catalogMutex);
            rebuild = !rowsReady || renderedEpoch != catalogEpoch || renderedQuery != query || renderedFiltered != overridesOnly || !SameSettings(renderedConfig, draft);
            if (rebuild) { entries = catalog; renderedEpoch = catalogEpoch; }
            source = catalogStatus;
        }
        if (rebuild) {
            matches = Search(std::move(entries), draft, query, overridesOnly);
            renderedConfig = draft; renderedQuery = query; renderedFiltered = overridesOnly; rowsReady = true;
        }
        if (windowMode) {
            window.Update(matches, draft, source, status, !SameSettings(original, draft), overridesOnly, delegated, interact.view, rebuild);
            return;
        }
        const auto count = interactTab ? Reimagined::InteractSettings::ToggleLabels.size() + 3 : options ? ToggleLabels.size() + 2 + 15 : matches.size();
        const auto pages = (std::max)(std::size_t{1}, (count + PageSize - 1) / PageSize);
        page = (std::min)(page, pages - 1);
        visible.fill(0);
        for (std::size_t i = 0; i < PageSize; ++i) {
            const auto index = page * PageSize + i;
            const auto suffix = std::to_string(i);
            const auto button = Find("Choice" + suffix);
            const auto label = Find("Item" + suffix);
            const bool present = index < count;
            if (button) widgets->base.setWidgetVisible(context, button, present);
            if (label) widgets->base.setWidgetVisible(context, label, present);
            if (!present) continue;
            if (interactTab) {
                visible[i] = static_cast<std::uint32_t>(index + 1);
                Enable("Choice" + suffix, interact.view.available);
                namespace S = Reimagined::InteractSettings;
                const auto& v = interact.view.values;
                const auto text = index < S::ToggleLabels.size() ? std::string(S::ToggleLabels[index]) + ": " + (v.flags & (1U << index) ? "On" : "Off")
                    : index == 7 ? "Auto Interact distance: " + (v.range ? std::to_string(v.range) : std::string("Normal reach"))
                    : index == 8 ? "Scan interval (ms): " + std::to_string(v.intervalMs) : "Retry cooldown (ms): " + std::to_string(v.retryMs);
                Text("Item" + suffix, text); continue;
            }
            if (options) {
                visible[i] = static_cast<std::uint32_t>(index + 1);
                const bool locked = delegated && ((index >= 11 && index < ToggleLabels.size()) || index == ToggleLabels.size() + 1);
                Enable("Choice" + suffix, !locked);
                if(index>=ToggleLabels.size()+2) {
                    const auto choice=index-ToggleLabels.size()-2;
                    Enable("Choice"+suffix,true);
                    Text("Item"+suffix,PotionRouting::Label(choice)+": "+(PotionRouting::ToggleValue(draft.potionRouting,choice)?"On":"Off"));continue;
                }
                const auto labelText = index < ToggleLabels.size() ? std::string(ToggleLabels[index]) + ": " + (ToggleValue(draft, index) ? "On" : "Off")
                    : std::string(index == ToggleLabels.size() ? "Pickup distance while moving: " : "Pickup distance after casting: ")
                        + std::to_string(index == ToggleLabels.size() ? draft.walkingRange : draft.teleportRange) + " (Change steps 1-20)";
                Text("Item" + suffix, labelText + (locked ? " (Disabled)" : ""));
                continue;
            }
            Enable("Choice" + suffix, true);
            const auto& row = matches[index];
            visible[i] = row.code;
            const auto choice = GetChoice(draft, row.code);
            const auto policy = choice == Choice::Include ? "Include" : choice == Choice::Exclude ? "Exclude"
                : draft.Selects(row.code) ? "Default: pickup" : "Default: leave";
            Text("Item" + suffix, row.name + " (" + CodeText(row.code) + ")\n" + policy);
        }
        Text("Source", interactTab ? "Auto Interact | Normal reach uses the usual interaction distance. Choose 1-20 to open eligible objects without moving." : options && delegated ? "Post-cast distance and options are disabled because an older Teleport Auto Pickup is installed. Update it to version 0.3.5 or later, or uninstall it, then restart the game." : source);
        Text("Page", std::to_string(count) + (options ? " settings | Page " : " matching items | Page ") + std::to_string(page + 1) + " / " + std::to_string(pages)
            + (overridesOnly ? " | Only custom item rules" : " | All items"));
        Text("Status", interactTab ? (interact.view.status.empty()
            ? "Walls still block interaction. Normal barrels require close range. Personal stashes, Evil Urns, exploding barrels, and quest objects stay manual." : interact.view.status)
            : status.empty() ? (options ? (delegated ? "" : "Item rules take priority over category settings.")
            : "Change cycles Default / Include / Exclude. Codes select every quality; exclusions win.") : status);
        Enable("Previous", page > 0); Enable("Next", page + 1 < pages);
        Enable("Save", interactTab ? interact.view.available && interact.view.dirty : !SameSettings(original, draft));
    }
    void BeginDraft() {
        original = snapshot();
        std::array<char, 65'536> text{}; std::uint32_t needed{}; std::string error; Config disk;
        if (context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &needed)
            && ParseConfig(text.data(), disk, error)) original = std::move(disk);
        draft = original; page = 0; status.clear(); editing = true;
        interact.Open();
    }
    void Open() {
        if (IsOpen()) { Close(); return; }
        if (windowMode) {
            BeginDraft();
            if (!window.Open()) { editing = false; return; }
            Render(); return;
        }
        if (panels->openPanel(context, panel) != D2RL::Panels::Result::Success) {
            context->LogWarn("AutoPickupAnything: item picker could not open its native panel."); return;
        }
        root = 0;
        if (widgets->base.findPanel(context, "auto-pickup-anything/ItemPicker", &root) != WidgetResult::Success || !root) return;
        BeginDraft();
        bool bound = Bind("Search", WidgetEvent::InputChanged, "Search") && Bind("Filter", WidgetEvent::Click, "Filter")
            && Bind("Previous", WidgetEvent::Click, "Previous") && Bind("Next", WidgetEvent::Click, "Next")
            && Bind("Save", WidgetEvent::Click, "Save") && Bind("Close", WidgetEvent::Click, "Close")
            && Bind("Options", WidgetEvent::Click, "Options");
        for (std::size_t i = 0; i < PageSize; ++i) {
            const auto name = "Choice" + std::to_string(i);
            bound = Bind(name, WidgetEvent::Click, name.c_str()) && bound;
        }
        if (!bound) {
            panels->closePanel(context, panel); editing = false;
            context->LogWarn("AutoPickupAnything: picker controls could not bind; config was not changed."); return;
        }
        Render();
    }
    void Close() {
        editing = false;
        if (windowMode) window.Close();
        else if (panel) panels->closePanel(context, panel);
    }
    static void WindowEvent(void* data, WindowAction event) noexcept {
        auto* self = static_cast<Interface*>(data);
        try {
            bool schedule{};
            {
                std::lock_guard lock(self->actionsMutex);
                if (self->actions.size() >= 256) return;
                self->actions.push_back(event);
                schedule = !self->actionsQueued; self->actionsQueued = true;
            }
            if (schedule) {
                if (self->threads->runOnUiThread(self->context, DrainActions, self) != D2RL::Threads::Result::Success) {
                    std::lock_guard lock(self->actionsMutex);
                    self->actionsQueued = false; self->actions.clear();
                }
            }
        } catch (...) {}
    }
    static void __cdecl DrainActions(const D2RL::PluginContext*, void* data) noexcept {
        auto* self = static_cast<Interface*>(data);
        try {
            std::deque<WindowAction> pending;
            { std::lock_guard lock(self->actionsMutex); pending.swap(self->actions); self->actionsQueued = false; }
            for (const auto& event : pending) {
                if (event.kind == WindowActionKind::Close) { self->Close(); continue; }
                if (!self->editing || !self->IsOpen()) continue;
                switch (event.kind) {
                case WindowActionKind::Tab: self->interactTab = event.code == 2; self->Render(); break;
                case WindowActionKind::InteractToggle: self->interact.Toggle(event.code, event.value != 0); self->Render(); break;
                case WindowActionKind::InteractNumber:
                    if (event.value >= 0) self->interact.Number(event.code, static_cast<std::uint32_t>(event.value));
                    self->Render(); break;
                case WindowActionKind::PotionToggle:
                    PotionRouting::SetToggle(self->draft.potionRouting,event.code,event.value!=0);
                    self->status="Unsaved potion routing changes";self->Render();break;
                case WindowActionKind::Search: self->Render(); break;
                case WindowActionKind::Filter: self->overridesOnly = !self->overridesOnly; self->Render(); break;
                case WindowActionKind::Save: self->Save(); break;
                case WindowActionKind::Toggle:
                    if (event.code < ToggleLabels.size() && (!self->delegated || event.code < 11)) {
                        SetToggle(self->draft, event.code, event.value != 0); self->status = "Unsaved changes"; self->Render();
                    }
                    break;
                case WindowActionKind::Range:
                    if (event.code <= 1 && event.value >= 1 && event.value <= 20 && (!self->delegated || event.code == 0)) {
                        (event.code ? self->draft.teleportRange : self->draft.walkingRange) = static_cast<std::uint32_t>(event.value);
                        self->status = "Unsaved changes"; self->Render();
                    }
                    break;
                case WindowActionKind::Choice:
                    if (std::any_of(self->matches.begin(), self->matches.end(), [&](const Entry& item) { return item.code == event.code; })
                        && event.choice >= Choice::Default && event.choice <= Choice::Exclude) {
                        SetChoice(self->draft, event.code, event.choice); self->status = "Unsaved changes"; self->Render();
                    }
                    break;
                case WindowActionKind::BulkChoice:
                    if (event.targets && event.targets->size() <= 200'000 && event.choice >= Choice::Default && event.choice <= Choice::Exclude) {
                        // Use the exact rows displayed when the button was clicked,
                        // even if a later search has already been queued.
                        std::unordered_set<std::uint32_t> known;
                        { std::lock_guard lock(self->catalogMutex); for (const auto& row : self->catalog) known.insert(row.code); }
                        for (const auto* list : {&self->draft.includeCodes, &self->draft.excludeCodes}) known.insert(list->begin(), list->end());
                        std::vector<std::uint32_t> targets;
                        for (auto code : *event.targets) if (code != PackItemCode("gld") && known.contains(code)) targets.push_back(code);
                        SetChoices(self->draft, targets, event.choice);
                        self->status = "Unsaved changes to " + std::to_string(targets.size()) + " search results"; self->Render();
                    }
                    break;
                default: break;
                }
            }
        } catch (...) {}
    }
    void Save() {
        if (interactTab) { interact.Save(); Render(); return; }
        std::array<char, 65'536> text{}; std::uint32_t needed{};
        if (!context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &needed)) {
            status = "Could not read config. Nothing was applied."; Render(); return;
        }
        if (std::find(text.begin(), text.end(), '\0') == text.end()) {
            status = "Config is too large. Nothing was applied."; Render(); return;
        }
        std::string updated, error;
        if (!SaveSettings(text.data(), original, draft, updated, error)) { status = error; Render(); return; }
        if (updated.size() >= text.size()) { status = "Item lists exceed the config size limit. Nothing was applied."; Render(); return; }
        Config applied;
        if (!ParseConfig(updated, applied, error)) { status = error; Render(); return; }
        // Save before publishing the scan policy. A refused write
        // leaves both gameplay and the previous config unchanged.
        if (!context->WriteConfig(updated.c_str())) { status = "Loader refused the config write. Nothing was applied."; Render(); return; }
        commit(std::move(applied)); original = draft;
        const auto runtime = snapshot();
        status = !runtime.enabled ? "Saved. Item pickup is off."
            : runtime.validateOnly ? "Saved. Validation mode is active; pickups remain disabled."
            : "Saved.";
        Render();
    }
    auto NameFor(std::uint32_t code) const -> std::string {
        const auto codeText = CodeText(code);
        if (!localization) return {};
        std::array<char, 1024> value{}; std::uint32_t needed{};
        const auto key = "d2r:" + codeText;
        if (localization->getStringByKey(context, key.c_str(), value.data(), static_cast<std::uint32_t>(value.size()), &needed) != D2RL::Localization::Result::Success)
            return {};
        value.back() = 0;
        return PlainName(value.data());
    }
    auto NameById(std::uint16_t id) const -> std::string {
        if (!localization || !localization->getStringById) return {};
        std::array<char, 1024> value{}; std::uint32_t needed{};
        if (localization->getStringById(context, id, value.data(), static_cast<std::uint32_t>(value.size()), &needed) != D2RL::Localization::Result::Success) return {};
        value.back() = 0; return PlainName(value.data());
    }
    void BuildCatalog() {
        std::vector<Entry> copied;
        std::unordered_set<std::uint32_t> seen;
        std::uint64_t revision{};
        bool rejected{};
        for (const auto bank : {D2RL::DataTables::Bank::Classic, D2RL::DataTables::Bank::Lod, D2RL::DataTables::Bank::Rotw}) {
            D2RL::DataTables::TableView view{}; view.structSize = D2RL::DataTables::TableViewSize;
            if (tables->getTable(context, bank, D2RL::DataTables::TableId::Items, &view) != D2RL::DataTables::Result::Success) continue;
            auto lookup = [&](std::uint32_t code, std::uint32_t index) {
                const auto codeText = CodeText(code);
                std::uint32_t zeroPadded{};
                for (std::size_t n = 0; n < codeText.size(); ++n) zeroPadded |= std::uint32_t{static_cast<unsigned char>(codeText[n])} << (8 * n);
                for (const auto candidate : {code, zeroPadded}) {
                    D2RL::DataTables::RowView row{}; row.structSize = D2RL::DataTables::RowViewSize;
                    if (tables->findRowByCode(context, bank, D2RL::DataTables::TableId::Items, candidate, &row) == D2RL::DataTables::Result::Success
                        && row.rowIndex == index && row.revision == view.revision && row.rowSize == view.rowSize
                        && row.row == static_cast<const std::uint8_t*>(view.rows) + std::size_t{index} * view.rowSize) return true;
                }
                return false;
            };
            const auto column = DiscoverCodeColumn(view, lookup);
            if (!column) { rejected = true; continue; }
            const auto nameColumn = localization && localization->getStringById ? DiscoverNameColumn(view, *column,
                [&](std::uint32_t code) { return NameFor(code); }, [&](std::uint16_t id) { return NameById(id); }) : std::optional<std::size_t>{};
            revision = (std::max)(revision, view.revision);
            for (std::uint32_t i = 0; i < view.rowCount; ++i) {
                std::uint32_t raw{};
                std::memcpy(&raw, static_cast<const std::uint8_t*>(view.rows) + std::size_t{i} * view.rowSize + *column, 4);
                if (!ValidCode(raw)) continue;
                const auto code = PackItemCode(CodeText(raw));
                if (code == PackItemCode("gld") || !lookup(code, i) || !seen.insert(code).second) continue;
                auto name = NameFor(code);
                if (name.empty() && nameColumn) {
                    std::uint16_t id{};
                    std::memcpy(&id, static_cast<const std::uint8_t*>(view.rows) + std::size_t{i} * view.rowSize + *nameColumn, 2);
                    name = NameById(id);
                }
                copied.push_back({code, name.empty() ? CodeText(code) : std::move(name), true});
            }
        }
        {
            std::lock_guard lock(catalogMutex);
            catalog = std::move(copied); catalogRevision = revision; ++catalogEpoch;
            catalogStatus = catalog.empty() ? "No verified item table is available. Saved overrides are still listed."
                : std::to_string(catalog.size()) + " base item types from loaded game tables | "
                    + (context->activeMod && *context->activeMod ? context->activeMod : "No active mod");
            if (rejected) catalogStatus += " | Some table layouts were unavailable";
        }
        threads->runOnUiThread(context, RefreshUi, this);
    }
    static void __cdecl RefreshUi(const D2RL::PluginContext*, void* data) noexcept {
        try { static_cast<Interface*>(data)->Render(); } catch (...) {}
    }
    static void __cdecl CatalogGame(const D2RL::PluginContext*, void* data) noexcept {
        auto* self = static_cast<Interface*>(data);
        try { self->BuildCatalog(); } catch (...) { self->context->LogWarn("AutoPickupAnything: item catalog could not be refreshed."); }
    }
    static void __cdecl TablesLoaded(const D2RL::PluginContext* ctx, const D2RL::Lifecycle::DataTablesLoadedEvent* event, void* data) noexcept {
        if (!D2RL::Lifecycle::HasDataTablesLoadedEventField(event, D2RL::Lifecycle::DataTablesLoadedEventRequiredSize)) return;
        CatalogGame(ctx, data);
    }
    static void __cdecl Session(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent* event, void* data) noexcept {
        auto* self = static_cast<Interface*>(data);
        if (!D2RL::Lifecycle::HasGameplayEventField(event, D2RL::Lifecycle::GameplayEventRequiredSize)) return;
        try {
            if (event->kind == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady)
                self->threads->runOnGameThread(self->context, CatalogGame, self);
            else self->threads->runOnUiThread(self->context, CloseUi, self);
        } catch (...) {}
    }
    static void __cdecl ToggleUi(const D2RL::PluginContext*, void* data) noexcept {
        try { static_cast<Interface*>(data)->Open(); } catch (...) {}
    }
    static void __cdecl CloseUi(const D2RL::PluginContext*, void* data) noexcept {
        try { static_cast<Interface*>(data)->Close(); } catch (...) {}
    }
    static auto __cdecl ToggleAction(const D2RL::PluginContext*, const D2RL::Input::ActionEvent* event, void* data) noexcept -> D2RL::Input::ActionResult {
        auto* self = static_cast<Interface*>(data);
        if (!D2RL::Input::HasActionEventField(event, D2RL::Input::ActionEventRequiredSize) || event->kind != D2RL::Input::ActionEventKind::Pressed)
            return D2RL::Input::ActionResult::Ignored;
        self->threads->runOnUiThread(self->context, ToggleUi, self);
        return D2RL::Input::ActionResult::Handled;
    }
    static auto Command(D2R::Game::Client*, const D2RL::ConsoleCommandContext*, void* data) noexcept -> D2RL::ConsoleCommandResult {
        auto* self = static_cast<Interface*>(data);
        return self->threads->runOnUiThread(self->context, ToggleUi, self) == D2RL::Threads::Result::Success
            ? D2RL::ConsoleCommandResult::Handled : D2RL::ConsoleCommandResult::Failed;
    }
    static auto __cdecl Message(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void* data) noexcept -> D2RL::SharedEvents::UiMessageAction {
        using Action = D2RL::SharedEvents::UiMessageAction;
        if (!event || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize || !event->target || !event->command
            || std::strcmp(event->target, "AutoPickupPicker") != 0) return Action::Continue;
        auto* self = static_cast<Interface*>(data);
        try {
            if (!self->editing || !self->IsOpen()) return Action::Consume;
            const std::string_view command(event->command);
            if (command == "Close") { self->panels->closePanel(self->context, self->panel); self->editing = false; }
            else if (command == "Save") self->Save();
            else if (command == "Search") { self->page = 0; self->Render(); }
            else if (command == "Filter") { self->overridesOnly = !self->overridesOnly; self->page = 0; self->Render(); }
            else if (command == "Options") {
                if (self->interactTab) { self->interactTab = false; self->options = false; }
                else if (self->options) self->interactTab = true;
                else self->options = true;
                self->page = 0; self->Render();
            }
            else if (command == "Previous") { if (self->page) --self->page; self->Render(); }
            else if (command == "Next") { ++self->page; self->Render(); }
            else if (command.size() == 7 && command.substr(0, 6) == "Choice" && command[6] >= '0' && command[6] <= '7') {
                const auto code = self->visible[command[6] - '0'];
                if (self->interactTab && code) {
                    const auto index = code - 1;
                    const auto& v = self->interact.view.values;
                    if (index < 7) self->interact.Toggle(index, !(v.flags & (1U << index)));
                    else if (index == 7) self->interact.Number(0, (v.range + 1) % 21);
                    else if (index == 8) self->interact.Number(1, v.intervalMs >= 1000 ? 50 : (std::min)(1000U, v.intervalMs + 50));
                    else if (index == 9) self->interact.Number(2, v.retryMs >= 60000 ? 1000 : (std::min)(60000U, v.retryMs + 1000));
                    self->Render(); return Action::Consume;
                }
                if (self->options && code) {
                    const auto index = static_cast<std::size_t>(code - 1);
                    if (index < ToggleLabels.size() && (!self->delegated || index < 11)) SetToggle(self->draft, index, !ToggleValue(self->draft, index));
                    else if (index == ToggleLabels.size() || (index == ToggleLabels.size() + 1 && !self->delegated)) {
                        auto& radius = index == ToggleLabels.size() ? self->draft.walkingRange : self->draft.teleportRange;
                        radius = radius % 20 + 1;
                    }
                    else if(index>=ToggleLabels.size()+2 && index<ToggleLabels.size()+17) {
                        const auto choice=index-ToggleLabels.size()-2;
                        PotionRouting::SetToggle(self->draft.potionRouting,choice,!PotionRouting::ToggleValue(self->draft.potionRouting,choice));
                    }
                    self->status = "Unsaved changes"; self->Render(); return Action::Consume;
                }
                if (code) {
                    const auto choice = GetChoice(self->draft, code);
                    SetChoice(self->draft, code, choice == Choice::Default ? Choice::Include : choice == Choice::Include ? Choice::Exclude : Choice::Default);
                    self->status = "Unsaved changes"; self->Render();
                }
            }
        } catch (...) { self->context->LogWarn("AutoPickupAnything: picker operation failed."); }
        return Action::Consume;
    }
public:
    auto Start(const D2RL::PluginContext* ctx, SnapshotFn read, CommitFn apply, bool standalone = false, bool preferNative = false, const Reimagined::InteractSettings::Api* interactApi = nullptr) -> bool {
        interact.Bind(interactApi);
        context = ctx; snapshot = read; commit = apply; delegated = standalone;
        if (!Query(D2RL::ServiceId::Lifecycle, 1, &lifecycle) || !D2RL::HasLifecycleServiceV1Field(lifecycle, D2RL::LifecycleServiceV1RequiredSize)
            || !lifecycle->registerDataTablesLoadedListener || !lifecycle->unregisterDataTablesLoadedListener
            || !lifecycle->registerGameplayEventListener || !lifecycle->unregisterGameplayEventListener
            || !Query(D2RL::ServiceId::DataTable, 1, &tables) || !D2RL::HasDataTableServiceV1Field(tables, D2RL::DataTableServiceV1RequiredSize)
            || !tables->getTable || !tables->findRowByCode
            || !Query(D2RL::ServiceId::Thread, 1, &threads) || !D2RL::HasThreadServiceV1Field(threads, D2RL::ThreadServiceV1RequiredSize)
            || !threads->runOnUiThread || !threads->runOnGameThread) { Stop(); return false; }
        // ABI 2 localization uses the same function table layout as V1 but
        // respects plugin namespaces and active mod string overrides.
        if (!Query(D2RL::ServiceId::Localization, 2, &localization) || !localization || localization->serviceVersion != 2
            || localization->serviceSize < D2RL::LocalizationServiceV1Size || !localization->getStringByKey) localization = nullptr;
        // Use the checkbox frontend consistently, including on newer runtimes.
        // The older paged panel remains a fallback if the owned window fails.
        windowMode = !preferNative && window.Start(WindowEvent, this);
        const bool native = !windowMode && Query(D2RL::ServiceId::Widget, 1, &widgets) && HasPickerWidgets(widgets)
            && Query(D2RL::ServiceId::Panel, 1, &panels) && D2RL::HasPanelServiceV1Field(panels, D2RL::PanelServiceV1TogglePanelFieldEnd)
            && panels->registerPanel && panels->unregisterPanel && panels->getPanelInfo && panels->openPanel && panels->closePanel
            && Query(D2RL::ServiceId::Resource, 1, &resources) && D2RL::HasResourceServiceV1Field(resources, D2RL::ResourceServiceV1RequiredSize)
            && resources->registerResource && resources->unregisterResource
            && Query(D2RL::ServiceId::SharedEvent, 1, &events) && D2RL::HasSharedEventServiceV1Field(events, D2RL::SharedEventServiceV1RequiredSize)
            && events->registerUiMessageListener && events->unregisterUiMessageListener;
        if (native) {
        const auto json = PanelLayout();
        const D2RL::Resources::ResourceRegistration resource{D2RL::Resources::ResourceRegistrationSize, 0,
            "data/global/ui/layouts/auto-pickup-anything/ItemPickerhd.json", json.data(), json.size()};
        const D2RL::Panels::PanelRegistration registration{D2RL::Panels::PanelRegistrationSize,
            static_cast<D2RL::Panels::PanelFlags>(0x00000001U | 0x00000004U), "ItemPicker"}; // CloseOnEscape | TextEntry (SDK 0.4)
        const D2RL::SharedEvents::UiMessageListener listener{D2RL::SharedEvents::UiMessageListenerSize, 0, 0, 0, Message, this};
        if (resources->registerResource(context, &resource, &layout) != D2RL::Resources::Result::Success
            || !layout || panels->registerPanel(context, &registration, &panel) != D2RL::Panels::Result::Success || !panel
            || events->registerUiMessageListener(context, &listener, &messages) != D2RL::SharedEvents::Result::Success || !messages) {
            if (messages) events->unregisterUiMessageListener(context, messages);
            if (panel) panels->unregisterPanel(context, panel);
            if (layout) resources->unregisterResource(context, layout);
            messages = 0; panel = 0; layout = 0;
        }
        }
        if (!windowMode && !panel) {
            windowMode = preferNative && window.Start(WindowEvent, this);
            if (!windowMode) { Stop(); return false; }
        }
        const D2RL::Lifecycle::DataTablesLoadedListener data{D2RL::Lifecycle::DataTablesLoadedListenerSize, 0, TablesLoaded, this};
        if (lifecycle->registerDataTablesLoadedListener(context, &data, &tableListener) != D2RL::Lifecycle::Result::Success || !tableListener) { Stop(); return false; }
        constexpr std::array kinds{D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, D2RL::Lifecycle::GameplayEventKind::GameLeft};
        for (std::size_t i = 0; i < kinds.size(); ++i) {
            const D2RL::Lifecycle::GameplayEventListener session{D2RL::Lifecycle::GameplayEventListenerSize, 0, kinds[i], 0, Session, this};
            if (lifecycle->registerGameplayEventListener(context, &session, &sessionListeners[i]) != D2RL::Lifecycle::Result::Success || !sessionListeners[i]) { Stop(); return false; }
        }
        if (Query(D2RL::ServiceId::Input, 1, &input) && D2RL::HasInputServiceV1Field(input, D2RL::InputServiceV1RequiredSize)
            && input->registerAction && input->unregisterAction) {
            const D2RL::Input::ActionRegistration binding{D2RL::Input::ActionRegistrationSize, 0, "item-picker", "Auto Pickup Settings", "Auto Pickup Anything",
                {D2RL::Input::Key::P, D2RL::Input::Modifier::Control}, {D2RL::Input::Key::None, D2RL::Input::Modifier::None}, ToggleAction, this};
            if (input->registerAction(context, &binding, &action) != D2RL::Input::Result::Success) action = 0;
        } else input = nullptr;
        D2RL::ConsoleCommandRegistration command{}; command.registrationSize = D2RL::ConsoleCommandRegistrationSize;
        command.name = "auto-pickup-settings"; command.description = "Open the item picker."; command.callback = Command; command.userData = this;
        if (!context->RegisterConsoleCommand(command) && !action) { Stop(); return false; }
        context->LogInfo(windowMode ? "AutoPickupAnything: scrollable Win32 item picker ready (Ctrl+P)."
            : "AutoPickupAnything: native item picker ready (Ctrl+P).");
        return true;
    }
    void Stop() noexcept {
        window.Stop();
        if (context) {
            if (action && input) input->unregisterAction(context, action);
            if (messages && events) events->unregisterUiMessageListener(context, messages);
            if (lifecycle) {
                if (tableListener) lifecycle->unregisterDataTablesLoadedListener(context, tableListener);
                for (const auto handle : sessionListeners) if (handle) lifecycle->unregisterGameplayEventListener(context, handle);
            }
            if (panel && panels) panels->unregisterPanel(context, panel);
            if (layout && resources) resources->unregisterResource(context, layout);
        }
        action = 0; messages = 0; tableListener = 0; sessionListeners.fill(0); panel = 0; layout = 0;
        interact.Bind(nullptr);
        root = 0; editing = false; windowMode = false; context = nullptr; interactTab = false; options = false; rowsReady = false;
        { std::lock_guard lock(actionsMutex); actions.clear(); actionsQueued = false; }
        std::lock_guard lock(catalogMutex); catalog.clear(); catalogRevision = 0;
    }
};
}
