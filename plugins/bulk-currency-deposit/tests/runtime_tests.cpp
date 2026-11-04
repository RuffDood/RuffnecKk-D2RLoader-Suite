// Private native entry pointers need deterministic substitutes to exercise the
// actual batch and menu callbacks without a game or a second hook owner.
#include "../src/plugin.cpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks{};
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
struct FakeItem {
    std::array<std::uint8_t, 0x56> data{};
    FakeItem* next{};
    void* owner{};
    std::uint32_t id{}, code{};
    bool eligible{true}, blocked{};
};
FakeItem* first{};
int inventoryToken{}, playerToken{}, destinationToken{};
bool stashOpen = true, rejectTransfer{}, removeCubeAfterTransfer{};
FakeItem* cube{};
std::vector<std::uint32_t> transferredIds;
std::vector<std::uint8_t> transferredPages;
void* __fastcall First(void*) noexcept { return first; }
void* __fastcall Next(void* p) noexcept { return static_cast<FakeItem*>(p)->next; }
std::int32_t __fastcall UnitType(void*) noexcept { return 4; }
std::int32_t __fastcall UnitId(void* p) noexcept { return static_cast<std::int32_t>(static_cast<FakeItem*>(p)->id); }
void* __fastcall ItemData(void* p) noexcept { return static_cast<FakeItem*>(p)->data.data(); }
void* __fastcall Parent(void* p) noexcept { return static_cast<FakeItem*>(p)->owner; }
std::uint32_t __fastcall Code(void* p) noexcept { return static_cast<FakeItem*>(p)->code; }
std::int32_t __fastcall Blocked(void* p) noexcept { return static_cast<FakeItem*>(p)->blocked ? 1 : 0; }
bool __fastcall Eligible(void* p) noexcept { return static_cast<FakeItem*>(p)->eligible; }
std::int32_t __fastcall DataContext() noexcept { return 0; }
void* __fastcall Player(std::int32_t) noexcept { return &playerToken; }
void* __fastcall Inventory(void*) noexcept { return &inventoryToken; }
std::int32_t __fastcall UiOpen(std::int32_t) noexcept { return stashOpen ? 1 : 0; }
void* __fastcall Panel(const char*) noexcept { return nullptr; }
void* __fastcall Destination(void*) noexcept { return &destinationToken; }
void __fastcall Finish(std::int32_t, void*, std::int32_t, std::int32_t, bool) noexcept {}
bool __fastcall Transfer(void* p, void* destination, std::uint8_t target,
        std::uint8_t source, bool quick, void*) noexcept {
    auto& item = *static_cast<FakeItem*>(p);
    if (destination != &destinationToken || target != AdvancedStashPage || !quick
            || item.data[ItemDataInventoryPageOffset] != source) std::abort();
    transferredIds.push_back(item.id);
    transferredPages.push_back(source);
    if (removeCubeAfterTransfer && source == CubeInventoryPage)
        cube->data[ItemDataInventoryPageOffset] = AdvancedStashPage;
    return !rejectTransfer;
}
void ConnectNativeStubs() {
    GetFirstItem = First; GetNextItem = Next; GetUnitType = UnitType;
    GetUnitId = UnitId; GetItemData = ItemData; GetParentInventory = Parent;
    GetItemCode = Code; IsItemInteractionBlocked = Blocked; CanDepositToAdvancedStash = Eligible;
    GetLocalDataContext = DataContext; GetLocalPlayer = Player; GetUnitInventory = Inventory;
    IsUiStateOpen = UiOpen; FindTopLevelPanel = Panel; GetAdvancedStashDestination = Destination;
    TransferItemToInventoryPage = Transfer; FinishInventoryInteraction = Finish;
    CallbackRundown.Reset(); InputStopping = false;
}
void Link(std::vector<FakeItem>& items) {
    first = items.data();
    for (std::size_t i = 0; i < items.size(); ++i) {
        items[i].id = static_cast<std::uint32_t>(i + 1);
        items[i].owner = &inventoryToken;
        items[i].next = i + 1 < items.size() ? &items[i + 1] : nullptr;
    }
}
void Deposit() {
    ResetBatchState();
    transferredIds.clear(); transferredPages.clear();
    ProcessDepositRequest(GetTickCount64());
    Check(!BatchActive && !BatchSettings && PendingItemCount == 0, "batch state was retained");
}
bool Set(const char* key, const std::string& value) {
    char error[512]{};
    return SetLiveSetting(key, value.c_str(), error, sizeof(error), nullptr);
}
std::string Get(const char* key) {
    char value[4096]{};
    Check(GetLiveSetting(key, value, sizeof(value), nullptr), "getter failed");
    return value;
}

unsigned notifications{}, registrations{}, releases{};
bool menuAvailable = true, menuDeferred{};
PluginMenu::ClientInfo menuClient{};
D2RL::Lifecycle::DataTablesLoadedListener afterLoad{};
std::uint64_t __cdecl Register(const D2RL::PluginContext*, const PluginMenu::ClientInfo* info) noexcept {
    menuClient = *info; ++registrations; return 1;
}
void __cdecl Unregister(std::uint64_t) noexcept { menuClient = {}; }
void __cdecl Changed(std::uint64_t, const char*) noexcept { ++notifications; }
void __cdecl Open(std::uint64_t) noexcept {}
PluginMenu::Service menu{sizeof(menu), 1, Register, Unregister, Changed, Open, nullptr};
auto __cdecl Acquire(const D2RL::PluginContext*, const D2RL::PluginCommunication::AcquireServiceRequest*,
        D2RL::PluginCommunication::AcquiredService* out) noexcept -> D2RL::PluginCommunication::Result {
    if (!menuAvailable || menuDeferred) return D2RL::PluginCommunication::Result::NotFound;
    out->table = &menu; out->handle = 1; out->tableSize = sizeof(menu); out->serviceVersion = 1;
    return D2RL::PluginCommunication::Result::Success;
}
auto __cdecl Release(const D2RL::PluginContext*, std::uint64_t) noexcept -> D2RL::PluginCommunication::Result {
    ++releases; return D2RL::PluginCommunication::Result::Success;
}
D2RL::PluginCommunicationService communication{sizeof(communication), 1, nullptr, Acquire, Release, nullptr, nullptr, nullptr};
auto __cdecl Listen(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedListener* info,
        std::uint64_t* handle) noexcept -> D2RL::Lifecycle::Result {
    afterLoad = *info; *handle = 1; return D2RL::Lifecycle::Result::Success;
}
auto __cdecl Remove(const D2RL::PluginContext*, std::uint64_t) noexcept -> D2RL::Lifecycle::Result {
    afterLoad = {}; return D2RL::Lifecycle::Result::Success;
}
D2RL::LifecycleServiceV1 lifecycle{sizeof(lifecycle), 1, Listen, Remove, nullptr, nullptr};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t,
        const void** out) noexcept -> D2RL::ServiceQueryResult {
    *out = id == static_cast<D2RL::ServiceId>(21) && menuAvailable ? &communication
        : id == D2RL::ServiceId::Lifecycle ? static_cast<const void*>(&lifecycle) : nullptr;
    return *out ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}

void Run(const std::filesystem::path& root) {
    const auto path = root / "ruffneckk-bulk-currency-deposit.toml";
    const std::string document = "# player config\r\n[deposit]\r\nenabled = true\r\n"
        "include_cube_materials = false # keep comment\r\n"
        "include_item_codes = [\r\n  \"r01\", # old list\r\n  \"r02\",\r\n] # keep array suffix\r\n"
        "[button]\r\nx = 7 # keep position\r\ntooltip = 'Custom materials'\r\n[d2rl]\r\nmatch = false\r\n";
    Check(RuffnecKk::LiveSettings::SaveFile(path, document), "fixture save failed");
    LoadedConfigPath = path.string(); Settings = LivePage::Parse(document); RunningSettings.Set(Settings);
    ConnectNativeStubs();
    std::vector<FakeItem> items(6);
    Link(items);
    items[0].code = *PackItemCode("r01");
    items[1].code = HoradricCubeItemCode; cube = &items[1];
    items[2].code = *PackItemCode("r02"); items[2].data[ItemDataInventoryPageOffset] = CubeInventoryPage;
    items[3].code = *PackItemCode("r02"); items[3].data[ItemDataInventoryPageOffset] = AdvancedStashPage;
    items[4].code = *PackItemCode("r02"); items[4].data[ItemDataInventoryPageOffset] = CubeInventoryPage;
    items[5].code = *PackItemCode("r03"); items[5].eligible = false;

    Deposit(); Check(transferredIds == std::vector<std::uint32_t>{1}, "default included cube or non-materials");
    Check(Set("deposit.include_cube_materials", "true"), "live enable failed");
    Check(Get("deposit.include_cube_materials") == "true", "live getter stale");
    Deposit(); Check(transferredIds == std::vector<std::uint32_t>({1, 3, 5}), "cube materials not connected to batch");
    Check(transferredPages == std::vector<std::uint8_t>({0, 3, 3}), "source page lost at native transfer");
    for (const auto page : {1, 3, 4, 255}) {
        cube->data[ItemDataInventoryPageOffset] = static_cast<std::uint8_t>(page);
        Deposit(); Check(transferredIds == std::vector<std::uint32_t>{1}, "cube outside inventory accepted");
    }
    cube->data[ItemDataInventoryPageOffset] = MainInventoryPage;
    cube->owner = &destinationToken; Deposit(); Check(transferredIds.size() == 1, "foreign cube accepted");
    cube->owner = &inventoryToken;
    cube->blocked = true; Deposit(); Check(transferredIds.size() == 1, "blocked cube accepted"); cube->blocked = false;
    removeCubeAfterTransfer = true; Deposit(); Check(transferredIds == std::vector<std::uint32_t>({1, 3}), "cube removal not revalidated");
    removeCubeAfterTransfer = false; cube->data[ItemDataInventoryPageOffset] = MainInventoryPage;

    BatchSettings = RunningSettings.Read(); Check(BuildBatch(&inventoryToken), "snapshot failed");
    const auto queued = PendingItems.back(); items[4].data[ItemDataInventoryPageOffset] = MainInventoryPage;
    Check(FindCurrentItem(&inventoryToken, queued) == nullptr, "stale source-page candidate accepted");
    items[4].data[ItemDataInventoryPageOffset] = CubeInventoryPage; ResetBatchState();
    items[2].owner = &destinationToken; Deposit(); Check(transferredIds == std::vector<std::uint32_t>({1, 5}), "foreign material accepted");
    items[2].owner = &inventoryToken; items[2].blocked = true;
    Deposit(); Check(transferredIds == std::vector<std::uint32_t>({1, 5}), "blocked material accepted"); items[2].blocked = false;
    rejectTransfer = true; Deposit(); Check(transferredIds.size() == 3, "ordinary native rejection stopped batch"); rejectTransfer = false;
    stashOpen = false; Deposit(); Check(transferredIds.empty(), "closed stash accepted"); stashOpen = true;

    Check(Set("deposit.exclude_item_codes", RuffnecKk::LiveSettings::Quote("[\"r02\"]")), "exclude edit failed");
    Deposit(); Check(transferredIds == std::vector<std::uint32_t>{1}, "live exclusions not applied to cube");
    Check(Set("deposit.exclude_item_codes", "\"[]\""), "exclude reset failed");
    Check(Set("deposit.include_item_codes", RuffnecKk::LiveSettings::Quote("[\"r02\"]")), "multiline include edit failed");
    Deposit(); Check(transferredIds == std::vector<std::uint32_t>({3, 5}), "live inclusion lost cube carry check");
    Check(Set("deposit.include_cube_materials", "false"), "live disable failed");
    Deposit(); Check(transferredIds.empty(), "live disable ignored");

    auto saved = RuffnecKk::LiveSettings::ReadFile(path);
    Check(saved.find("false # keep comment\r\n") != saved.npos && saved.find("] # keep array suffix\r\n") != saved.npos
        && saved.find("x = 7 # keep position\r\n") != saved.npos && saved.find("match = false\r\n") != saved.npos,
        "unrelated bytes/comments/CRLF lost");
    const auto before = RunningSettings.Read();
    for (const auto* invalid : {"1", "\"true\"", "[]", "true\n[button]", "true # injected"})
        Check(!Set("deposit.include_cube_materials", invalid), "invalid bool accepted");
    Check(!Set("deposit.include_item_codes", RuffnecKk::LiveSettings::Quote("[\"r01\",\"r01\"]")), "duplicate codes accepted");
    Check(!Set("deposit.include_item_codes", RuffnecKk::LiveSettings::Quote("[]\n[button]\nx = 99")), "array injection accepted");
    Check(!Set("unknown", "true"), "unknown key accepted");
    Check(RunningSettings.Read() == before && RuffnecKk::LiveSettings::ReadFile(path) == saved, "rejected edit mutated state");
    const auto held = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "cannot lock fixture");
    const auto accepted = Set("deposit.include_cube_materials", "true"); CloseHandle(held);
    Check(!accepted && RunningSettings.Read() == before && RuffnecKk::LiveSettings::ReadFile(path) == saved, "failed save published state");

    Check(Set("deposit.enabled", "false") && Get("deposit.enabled") == "false", "restart setting not saved");
    Check(Settings.enabled && RunningSettings.Read()->enabled, "master setting applied without restart");
    Check(Set("button.x", "99") && Get("button.x") == "99" && Settings.button.x == 7, "layout restart contract lost");
    Check(!Set("button.x", "99.0"), "float coordinate accepted");
    for (unsigned i = 0; i < 50; ++i)
        Check(Set("deposit.include_cube_materials", i % 2 ? "false" : "true"), "repeated live edit failed");
    Check(RunningSettings.Read()->includeCubeMaterials == false, "last live edit stale");
    const auto schema = toml::parse(LivePage::Schema);
    const auto* fields = schema["setting"].as_array(); Check(fields && fields->size() == 9, "schema lost fields");
    for (const auto& field : *fields) {
        const auto* table = field.as_table(); Check(table != nullptr, "schema entry is not a table");
        const auto key = (*table)["key"].value<std::string>(); if (!key) continue;
        const auto value = Get(key->c_str()); Check(Set(key->c_str(), value), "schema roundtrip failed");
        Check(Get(key->c_str()) == value, "schema getter changed after same-value edit");
    }

    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.queryService = Query;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.apiVersion = 3; context.api = &api;
    Context = &context;
    menuAvailable = false; StartSettingsMenu(); Check(!SettingsMenu.Registered(), "missing menu registered"); SettingsMenu.Stop();
    menuAvailable = true; menuDeferred = true; StartSettingsMenu();
    Check(!SettingsMenu.Registered() && afterLoad.callback, "late menu was not deferred");
    menuDeferred = false; afterLoad.callback(&context, nullptr, afterLoad.userData);
    Check(SettingsMenu.Registered() && menuClient.set == SetLiveSetting && menuClient.get == GetLiveSetting, "production callbacks not registered");
    char error[128]{};
    Check(menuClient.set("deposit.include_cube_materials", "true", error, sizeof(error), menuClient.user), "registered setter failed");
    Check(notifications == 0, "setter requested recursive menu refresh");
    SettingsMenu.Stop(); Check(!menuClient.set && releases == 1 && registrations == 1 && !afterLoad.callback, "menu lifecycle cleanup failed");
    Context = nullptr;
    CallbackRundown.Stop(); Check(!Set("deposit.include_cube_materials", "false"), "stopping plugin accepted callback");
    std::filesystem::remove(path);
}
}
int main() {
    try {
        const auto root = std::filesystem::current_path() / ("cube-live-fixture-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        Run(root); std::filesystem::remove(root);
        std::cout << checks << " cube batch and production settings callback checks passed.\n";
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
