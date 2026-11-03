
#include "i_hate_tomls_client.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks{};
void Check(bool value, const char* message) { ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); } }
int acquireCount{}, released{}, registered{}, unregistered{}, listeners{}, removed{};
bool available{}, late{};

D2RL::Lifecycle::DataTablesLoadedListener loaded{};
bool __cdecl Get(const char*, char*, uint32_t, void*) noexcept { return true; }
bool __cdecl Set(const char*, const char*, char*, uint32_t, void*) noexcept { return true; }
uint64_t __cdecl Register(const D2RL::PluginContext*, const PluginMenu::ClientInfo*) noexcept { ++registered; return 9; }
void __cdecl Unregister(uint64_t) noexcept { ++unregistered; }
void __cdecl Changed(uint64_t, const char*) noexcept {}
void __cdecl Open(uint64_t) noexcept {}
PluginMenu::Service menu { sizeof(menu), 1, Register, Unregister, Changed, Open, nullptr };
auto __cdecl Acquire(const D2RL::PluginContext*, const D2RL::PluginCommunication::AcquireServiceRequest* request,
        D2RL::PluginCommunication::AcquiredService* result) noexcept -> D2RL::PluginCommunication::Result {
    ++acquireCount;
    if (std::strcmp(request->providerPluginId, "i-hate-tomls") || std::strcmp(request->name, "settings-menu")) return D2RL::PluginCommunication::Result::NotFound;
    if (late) return D2RL::PluginCommunication::Result::NotFound;
    result->table = &menu; result->handle = 7; result->tableSize = sizeof(menu); result->serviceVersion = 1;
    return D2RL::PluginCommunication::Result::Success;
}
auto __cdecl Release(const D2RL::PluginContext*, uint64_t) noexcept -> D2RL::PluginCommunication::Result { ++released; return D2RL::PluginCommunication::Result::Success; }
D2RL::PluginCommunicationService communication { sizeof(communication), 1, nullptr, Acquire, Release, nullptr, nullptr, nullptr };
auto __cdecl Listen(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedListener* listener, uint64_t* handle) noexcept -> D2RL::Lifecycle::Result { ++listeners; loaded = *listener; *handle = 12; return D2RL::Lifecycle::Result::Success; }
auto __cdecl Remove(const D2RL::PluginContext*, uint64_t) noexcept -> D2RL::Lifecycle::Result { ++removed; return D2RL::Lifecycle::Result::Success; }
D2RL::LifecycleServiceV1 lifecycle { sizeof(lifecycle), 1, Listen, Remove, nullptr, nullptr };
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, uint32_t, const void** result) noexcept -> D2RL::ServiceQueryResult {
    *result = nullptr;
    if (id == static_cast<D2RL::ServiceId>(21) && available) *result = &communication;
    if (id == D2RL::ServiceId::Lifecycle) *result = &lifecycle;
    return *result ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
}
int main() {
    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.queryService = Query;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.api = &api;
    PluginMenu::Connection connection;
    Check(!connection.Start(&context, "", Get, Set, nullptr) && !connection.Registered(), "missing hub is optional");
    connection.Stop();
    available = true;
    Check(connection.Start(&context, "", Get, Set, nullptr) && connection.Registered(), "register with provider");
    connection.Stop();
    Check(unregistered == 1 && released == 1, "unregister before releasing lease");

    // The table's required setter must be non-null before any registration.
    menu.registerClient = nullptr;
    Check(!connection.Start(&context, "", Get, Set, nullptr), "malformed provider rejected");
    connection.Stop(); menu.registerClient = Register;
    late = true;
    Check(!connection.Start(&context, "", Get, Set, nullptr), "late provider deferred");
    late = false;
    loaded.callback(&context, nullptr, loaded.userData);
    Check(connection.Registered(), "late provider joins after load");
    connection.Stop();
    Check(listeners == removed, "deferred listeners removed");
    Check(acquireCount >= 4 && registered == 2, "only compatible tables registered");
    std::printf("%d checks passed\n", checks);
}
