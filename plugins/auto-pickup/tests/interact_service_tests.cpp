#include <D2RLPlugin/api.h>
#include "native_contract.hpp"
#include "native.hpp"
#include "../src/interact_settings.hpp"
#include "service.hpp"
#include "../third_party/RuffnecKkObjectContract/native_object_interact_contract.hpp"
#include <Windows.h>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
namespace C = AutoInteract::NativeContract;
namespace O = RuffnecKk::MapSense::Detail;
namespace {
constexpr std::size_t ImageSize = 0x4100000;
std::uint8_t* image{};
std::string config;
unsigned hooks{}, patches{}, failSubscription{}, subscriptionCount{}, removed{}, originalCalls{};
bool missingThread{}, failHook{}, rejectQueue{}, failWrite{};
unsigned configWrites{};
void* hookTarget{};
std::vector<D2RL::Lifecycle::GameplayEventListener> listeners;
std::vector<std::pair<D2RL::Threads::Callback, void*>> queue;
std::array<std::uintptr_t, 8> observedArgs;
void __cdecl Log(const D2RL::PluginContext*, const char*) noexcept {}
bool __cdecl Write(const D2RL::PluginContext*, const char* text) noexcept { ++configWrites; if (failWrite) return false; config = text; return true; }
auto __cdecl Read(const D2RL::PluginContext*, char* out, std::uint32_t size, std::uint32_t* required) noexcept -> bool {
    *required = static_cast<std::uint32_t>(config.size() + 1);
    if (size < *required) return false;
    std::memcpy(out, config.c_str(), *required); return true;
}
auto __cdecl Check(const D2RL::PluginContext*, std::uint64_t rva, const void* expected, std::uint32_t size) noexcept -> bool {
    return rva <= ImageSize && size <= ImageSize - rva && std::memcmp(image + rva, expected, size) == 0;
}
auto __fastcall Original(void* game, void* player, void* skill, std::uint32_t mode, std::uint32_t type,
        std::uint32_t id, std::uint32_t a7, std::uint32_t a8) -> std::int32_t {
    ++originalCalls;
    observedArgs = {reinterpret_cast<std::uintptr_t>(game), reinterpret_cast<std::uintptr_t>(player), reinterpret_cast<std::uintptr_t>(skill), mode, type, id, a7, a8};
    return -123;
}
auto __cdecl Hook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* hook) noexcept -> bool {
    CHECK(hook->rva == C::PlayerActionRva && hook->expectedSize == C::PlayerActionBytes.size());
    CHECK(Check(nullptr, hook->rva, hook->expected, hook->expectedSize));
    if (failHook) return false;
    ++hooks; hookTarget = hook->target; *hook->original = reinterpret_cast<void*>(&Original); return true;
}
auto __cdecl Patch(const D2RL::PluginContext*, std::uint64_t, const void*, std::uint32_t, std::uint64_t, std::uint32_t) noexcept -> bool {
    ++patches; return false;
}
auto __cdecl Command(const D2RL::PluginContext*, const D2RL::ConsoleCommandRegistration*) noexcept -> bool { return true; }
auto __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback callback, void* data) noexcept -> D2RL::Threads::Result {
    if (rejectQueue) return D2RL::Threads::Result::Unavailable;
    queue.emplace_back(callback, data); return D2RL::Threads::Result::Success;
}
auto __cdecl Subscribe(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener* listener,
        D2RL::Lifecycle::ListenerHandle* handle) noexcept -> D2RL::Lifecycle::Result {
    ++subscriptionCount;
    if (subscriptionCount == failSubscription) return D2RL::Lifecycle::Result::Unavailable;
    listeners.push_back(*listener); *handle = subscriptionCount; return D2RL::Lifecycle::Result::Success;
}
auto __cdecl Unsubscribe(const D2RL::PluginContext*, D2RL::Lifecycle::ListenerHandle) noexcept -> D2RL::Lifecycle::Result { ++removed; return D2RL::Lifecycle::Result::Success; }
D2RL::ThreadServiceV1 threads{D2RL::ThreadServiceV1Size, 1, nullptr, Queue};
D2RL::LifecycleServiceV1 lifecycle{D2RL::LifecycleServiceV1Size, 1, nullptr, nullptr, Subscribe, Unsubscribe};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t, const void** service) noexcept -> D2RL::ServiceQueryResult {
    *service = nullptr;
    if (id == D2RL::ServiceId::Thread && !missingThread) *service = &threads;
    if (id == D2RL::ServiceId::Lifecycle) *service = &lifecycle;
    return *service ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
D2RL::PluginApi api;
D2RL::PluginContext context;
std::string defaults;
void Reset(bool foreignPickupHooks = false) {
    config = "enabled=true\n[item_codes]\ninclude=[]\nexclude=[]\n[auto_interact]\n" + defaults.substr(0, defaults.find("[d2rl]")) + "[d2rl]\nmatch=[]\n";
    hooks = patches = failSubscription = subscriptionCount = removed = originalCalls = 0;
    missingThread = failHook = rejectQueue = false; hookTarget = nullptr; listeners.clear(); queue.clear();
    failWrite = false; configWrites = 0;
    std::memset(image, 0, ImageSize);
    for (const auto& w : C::Witnesses) std::memcpy(image + w.rva, w.bytes.data(), w.bytes.size());
    std::memcpy(image + O::ObjectInteractGetterRva, O::ObjectInteractGetterBytes.data(), O::ObjectInteractGetterBytes.size());
    std::memcpy(image + O::ObjectInteractSetterRva, O::ObjectInteractSetterBytes.data(), O::ObjectInteractSetterBytes.size());
    for (auto [slot, rva] : C::Operations) {
        const auto value = reinterpret_cast<std::uintptr_t>(image) + rva;
        std::memcpy(image + 0x23A51C0 + slot * 8, &value, 8);
    }
    // Simulate pre-existing movement/cast owners in either load order. Auto
    // Interact must never validate, overwrite or restore these slots.
    if (foreignPickupHooks) for (unsigned i = 1; i <= 18; ++i) {
        const std::uint64_t owner = 0x1234000000000000ULL + i;
        std::memcpy(image + 0x1D2A790 + i * 8, &owner, 8);
    }
    api = {}; api.apiSize = D2RL::PluginApiSize;
    api.logInfo = api.logWarn = api.logError = Log; api.readConfig = Read; api.writeConfig = Write;
    api.checkExpectedBytes = Check; api.installInlineHook = Hook; api.patchWrite = Patch;
    api.registerConsoleCommand = Command; api.queryService = Query;
    context = {}; context.contextSize = D2RL::PluginContextSize; context.apiVersion = 3;
    context.api = &api; context.exeBase = reinterpret_cast<std::uintptr_t>(image); context.loadScope = D2RL::LoadScope::Global;
}
void Set(std::string_view from, std::string_view to) { auto p = config.find(from); CHECK(p != std::string::npos); config.replace(p, from.size(), to); }
}
int main(int argc, char** argv) {
    CHECK(argc == 2 || argc == 3);
    std::ifstream sourceFile(argv[1], std::ios::binary); defaults = {std::istreambuf_iterator<char>(sourceFile), {}}; CHECK(!defaults.empty());
    image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, ImageSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)); CHECK(image);
    auto load = [](const D2RL::PluginContext* c) { return AutoInteract::Start(c, false); };
    auto unload = [] { AutoInteract::Stop(); };
    namespace S = Reimagined::InteractSettings;
    auto getSettings = [](std::uint32_t version, std::uint32_t size) { return version==S::Version && size>=sizeof(S::Api) ? AutoInteract::SettingsApi() : nullptr; };
    for (bool foreign : {false, true}) {
        Reset(foreign); std::array<std::uint8_t, 18 * 8> slots;
        std::memcpy(slots.data(), image + 0x1D2A798, slots.size());
        CHECK(load(&context) && hooks == 1 && patches == 0 && listeners.size() == 3);
        auto hook = reinterpret_cast<AutoInteract::PlayerActionFn>(hookTarget);
        CHECK(hook(reinterpret_cast<void*>(1), nullptr, reinterpret_cast<void*>(3), 4, 5, 6, 7, 8) == -123);
        CHECK(originalCalls == 1 && observedArgs == (std::array<std::uintptr_t, 8>{1, 0, 3, 4, 5, 6, 7, 8}));
        D2RL::Lifecycle::GameplayEvent event{D2RL::Lifecycle::GameplayEventSize, 0, D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, 7, 1, 0, 0};
        listeners[2].callback(&context, &event, listeners[2].userData); CHECK(queue.size() == 1);
        auto stale = queue[0]; unload(); CHECK(removed == 3);
        stale.first(&context, stale.second); CHECK(queue.size() == 1);
        CHECK(hook(reinterpret_cast<void*>(1), nullptr, nullptr, 0, 0, 0, 0, 0) == -123 && originalCalls == 2); // unload leaves forwarding intact
        CHECK(std::memcmp(slots.data(), image + 0x1D2A798, slots.size()) == 0);
    }
    for (const auto& w : C::Witnesses) {
        // Key-only helpers are intentionally unnecessary with locked chests off.
        if (w.rva == C::ItemTypeRva || w.rva == C::ItemPageRva || w.rva == C::FirstInventoryItemRva || w.rva == C::NextInventoryItemRva) continue;
        Reset(); image[w.rva] ^= 1; CHECK(!load(&context) && hooks == 0 && listeners.empty()); unload();
    }
    for (auto [slot, rva] : C::Operations) { Reset(); image[0x23A51C0 + slot * 8] ^= 1; CHECK(!load(&context) && !hooks); unload(); }
    Reset(); image[O::ObjectInteractGetterRva + 62] ^= 1; CHECK(!load(&context) && !hooks); unload();
    Reset(); Set("validate_only = false", "validate_only = true"); CHECK(load(&context) && !hooks && listeners.empty()); unload();
    Reset(); Set("enabled = true", "enabled = false"); CHECK(load(&context) && !hooks && listeners.empty()); unload();
    Reset(); missingThread = true; CHECK(!load(&context) && !hooks && listeners.empty()); unload();
    for (unsigned i = 1; i <= 3; ++i) { Reset(); failSubscription = i; CHECK(!load(&context) && !hooks && removed == i - 1); unload(); }
    Reset(); failHook = true; CHECK(!load(&context) && !hooks && removed == 3); unload();
    Reset(); context.apiVersion = 2; CHECK(!load(&context) && !hooks); unload();
    Reset(); CHECK(load(&context));
    const auto* settings = getSettings(1, sizeof(S::Api)); CHECK(S::Valid(settings));
    S::Values before; CHECK(settings->read(&before) == S::Result::Success && before.state & S::Live);
    auto draft = before; draft.range = 2; draft.flags &= ~S::Enabled;
    char message[512]{};
    CHECK(settings->save(&before, &draft, message, sizeof(message)) == S::Result::Success && configWrites == 1);
    S::Values after; CHECK(settings->read(&after) == S::Result::Success && after.range == 2 && !(after.flags & S::Enabled));
    CHECK(settings->save(&before, &draft, message, sizeof(message)) == S::Result::Conflict && configWrites == 1);
    draft = after; draft.range = 3; const auto disk = config; failWrite = true;
    CHECK(settings->save(&after, &draft, message, sizeof(message)) == S::Result::WriteFailed && config == disk);
    failWrite = false; draft.range = 21; CHECK(settings->save(&after, &draft, message, sizeof(message)) == S::Result::Invalid);
    draft = after; draft.flags |= S::LockedChests; image[C::ItemPageRva] ^= 1;
    CHECK(settings->save(&after, &draft, message, sizeof(message)) == S::Result::Incompatible && config == disk);
    unload(); CHECK(settings->read(&after) == S::Result::Unavailable && getSettings(1, sizeof(S::Api)));
    Reset(); Set("enabled = true", "enabled = false"); CHECK(load(&context));
    settings = getSettings(1, sizeof(S::Api)); CHECK(settings && settings->read(&before) == S::Result::Success);
    draft = before; draft.flags |= S::Enabled;
    CHECK(settings->save(&before, &draft, message, sizeof(message)) == S::Result::RestartRequired && !hooks && configWrites == 1);
    unload();
    // Real file migration and legacy-DLL exclusion use production startup.
    namespace fs = std::filesystem;
    const auto fixture = fs::temp_directory_path() / ("pickit-migration-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    CHECK(fs::create_directory(fixture));
    const auto pickitPath = fixture / "auto-pickup-anything.toml", legacyPath = fixture / "auto-interact.toml";
    { std::ofstream file(legacyPath); file << "enabled=false\ninteract_range=20\nrocks=false\n"; }
    Reset(); config = "enabled=false\n[item_codes]\ninclude=[\"jew\"]\nexclude=[]\n";
    context.pluginConfigPath = pickitPath.c_str(); context.pluginDirectory = fixture.c_str();
    CHECK(load(&context) && configWrites == 1 && !hooks);
    settings = getSettings(1,sizeof(S::Api)); CHECK(settings->read(&before)==S::Result::Success && before.range==20 && !(before.flags&S::Enabled) && !(before.flags&S::Rocks));
    CHECK(config.find("include=[\"jew\"]")!=std::string::npos); unload();
    { std::ofstream file(legacyPath); file << "garbage=true\n"; }
    CHECK(load(&context) && configWrites==1); unload(); // migrated section wins
    { std::ofstream file(legacyPath); file << "enabled=false\ninteract_range=20\n"; }
    Reset(); config="enabled=false\n"; context.pluginConfigPath=pickitPath.c_str(); failWrite=true;
    CHECK(!load(&context) && config=="enabled=false\n" && !hooks); unload();
    const auto retired = fixture / "d2rl-auto-interact.dll";
    { std::ofstream file(retired); file << "old plugin"; }
    Reset(); context.pluginDirectory=fixture.c_str(); CHECK(!load(&context) && !hooks && listeners.empty());
    CHECK(getSettings(1,sizeof(S::Api))->read(&before)==S::Result::Success && (before.state&S::StartupBlocked)); unload();
    CHECK(fs::remove(retired) && fs::remove(legacyPath) && fs::remove(fixture));
    if (argc == 3) {
        Reset();
        std::ifstream captured(argv[2], std::ios::binary);
        captured.read(reinterpret_cast<char*>(image), ImageSize);
        CHECK(captured.gcount() == ImageSize);
        // This input is an independent capture, not copies of production
        // constants. Rebase only actual absolute table pointers for this map.
        std::uint64_t oldBase{};
        std::memcpy(&oldBase, image + 0x1D2A790 + 0x41 * 8, 8); oldBase -= 0x4B0F80;
        for (auto [slot, rva] : C::Operations) {
            std::uint64_t address{}; std::memcpy(&address, image + 0x23A51C0 + slot * 8, 8);
            address = address - oldBase + reinterpret_cast<std::uintptr_t>(image);
            std::memcpy(image + 0x23A51C0 + slot * 8, &address, 8);
        }
        Set("validate_only = false", "validate_only = true");
        CHECK(load(&context) && !hooks && listeners.empty()); unload();
        std::cout << "Independent live capture admission passed (no native function calls).\n";
    }
 VirtualFree(image, 0, MEM_RELEASE);
    std::cout << "Built-in service admission, observer forwarding, migration, internal settings and cleanup passed\n";
}
