#include <Windows.h>
#include <RuffnecKk/iht/i_hate_tomls_client.h>
#include <RuffnecKk/live_settings_windows.hpp>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{}, notifications{}, registrations{}, releases{};
bool available = true, saveAllowed = true, deferred{};
PluginMenu::ClientInfo client{};
std::string schema, document;
D2RL::Lifecycle::DataTablesLoadedListener afterLoad{};
void Check(bool ok, const char* what) { ++checks; if (!ok) throw std::runtime_error(what); }
std::uint64_t __cdecl Register(const D2RL::PluginContext*, const PluginMenu::ClientInfo* info) noexcept {
    client = *info; schema = info->schemaToml; ++registrations; return 1;
}
void __cdecl Unregister(std::uint64_t) noexcept { client = {}; }
void __cdecl Changed(std::uint64_t, const char*) noexcept { ++notifications; }
void __cdecl Open(std::uint64_t) noexcept {}
PluginMenu::Service menu{sizeof(menu), 1, Register, Unregister, Changed, Open, nullptr};
auto __cdecl Acquire(const D2RL::PluginContext*, const D2RL::PluginCommunication::AcquireServiceRequest*,
        D2RL::PluginCommunication::AcquiredService* out) noexcept -> D2RL::PluginCommunication::Result {
    if (!available || deferred) return D2RL::PluginCommunication::Result::NotFound;
    out->table = &menu; out->handle = 1; out->tableSize = sizeof(menu); out->serviceVersion = 1;
    return D2RL::PluginCommunication::Result::Success;
}
auto __cdecl Release(const D2RL::PluginContext*, std::uint64_t) noexcept -> D2RL::PluginCommunication::Result {
    ++releases; return D2RL::PluginCommunication::Result::Success;
}
D2RL::PluginCommunicationService communication{sizeof(communication), 1, nullptr, Acquire, Release, nullptr, nullptr, nullptr};
auto __cdecl Listen(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedListener* info,
        std::uint64_t* handle) noexcept -> D2RL::Lifecycle::Result { afterLoad = *info; *handle = 1; return D2RL::Lifecycle::Result::Success; }
auto __cdecl Remove(const D2RL::PluginContext*, std::uint64_t) noexcept -> D2RL::Lifecycle::Result { return D2RL::Lifecycle::Result::Success; }
D2RL::LifecycleServiceV1 lifecycle{sizeof(lifecycle), 1, Listen, Remove, nullptr, nullptr};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t, const void** out) noexcept -> D2RL::ServiceQueryResult {
    *out = id == static_cast<D2RL::ServiceId>(21) && available ? &communication
        : id == D2RL::ServiceId::Lifecycle ? static_cast<const void*>(&lifecycle) : nullptr;
    return *out ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
bool __cdecl Read(const D2RL::PluginContext*, char* out, std::uint32_t size, std::uint32_t* required) noexcept {
    if (required) *required = static_cast<std::uint32_t>(document.size()+1);
    if (document.size() >= size) return false;
    std::memcpy(out, document.c_str(), document.size()+1); return true;
}
bool __cdecl Write(const D2RL::PluginContext*, const char* text) noexcept { if (!saveAllowed) return false; document = text; return true; }
std::string Get(const char* key) { char out[4096]{}; Check(client.get && client.get(key,out,sizeof(out),client.user), "production getter failed"); return out; }
bool Set(std::string_view key,const char* value) { const std::string name(key); char error[512]{}; const bool ok = client.set && client.set(name.c_str(),value,error,sizeof(error),client.user); if(!ok) std::cerr << "Rejected " << name << ": " << error << "\n"; return ok; }
std::string Bytes(const std::filesystem::path& p) { return RuffnecKk::LiveSettings::ReadFile(p); }
void Run(const wchar_t* dllPath, bool remote) {
    const auto module = LoadLibraryW(dllPath); Check(module != nullptr, "cannot map candidate DLL");
    using LoadFn = bool(__cdecl*)(const D2RL::PluginContext*) noexcept;
    using UnloadFn = void(__cdecl*)() noexcept;
    const auto load = reinterpret_cast<LoadFn>(GetProcAddress(module,"D2RLoaderLoadPlugin"));
    const auto unload = reinterpret_cast<UnloadFn>(GetProcAddress(module,"D2RLoaderUnloadPlugin"));
    Check(load && unload, "candidate exports missing");
    const auto root = std::filesystem::current_path() / (remote ? "remote-live-fixture" : "bulk-live-fixture");
    std::filesystem::create_directories(root);
    const auto config = root / (remote ? "ruffneckk-remote-stash.toml" : "BulkSkillPointAllocation.toml");
    document = remote ? "# player\r\nenabled = false\r\nhotkey_enabled = false\r\nhotkey = \"SHIFT+R\"\r\n[button]\r\nwidth = 128 # keep\r\n"
        : "# player\r\nenabled = false\r\nskillPointsPerCtrlClick = 5 # keep\r\nconfirmShiftAllocation = false\r\n[refund]\r\nbatch_points = 5\r\n";
    { std::ofstream f(config,std::ios::binary); f << document; }
    const auto path = config.wstring();
    D2RL::PluginApi api{}; api.apiSize = sizeof(api); api.queryService = Query; api.readConfig = Read; api.writeConfig = Write;
    D2RL::PluginContext context{}; context.contextSize = sizeof(context); context.apiVersion = 3; context.api = &api;
    context.exeBase = 1; context.loadScope = D2RL::LoadScope::Global; context.pluginConfigPath = path.c_str(); context.buildName = "fixture";
    available = false; Check(load(&context), "missing hub prevented disabled plugin loading"); unload();
    available = true; deferred = true; Check(load(&context), "late hub prevented plugin loading");
    Check(!client.set && afterLoad.callback, "late provider not deferred"); deferred = false;
    afterLoad.callback(&context,nullptr,afterLoad.userData); Check(client.set && client.get, "deferred page not registered");
    Check(schema.find("restart = true") != schema.npos, "master restart notice missing");
    const auto countBefore = notifications;
    const char* batch = remote ? "button.width" : "skillPointsPerCtrlClick";
    Check(Set(batch,remote ? "160" : "7"),"live scalar edit refused");
    Check(Get(batch) == (remote ? "160" : "7"),"running getter did not change");
    auto saved = remote ? document : Bytes(config);
    Check(saved.find("# player\r\n") == 0 && saved.find(" # keep\r\n") != saved.npos,"comments or CRLF lost");
    if (remote) {
        document = RuffnecKk::LiveSettings::Edit(document,"button.height","112");
        Check(Set("diagnostics.enabled","true"),"diagnostics edit refused");
        Check(Get("button.height")=="80","unrelated manual edit was silently applied");
        Check(document.find("height = 112")!=document.npos,"unrelated saved edit was lost");
    } else {
        const auto external = RuffnecKk::LiveSettings::Edit(Bytes(config),"refund.batch_points","25");
        { std::ofstream f(config,std::ios::binary); f << external; }
        Check(Set("diagnostics","true"),"diagnostics edit refused");
        Check(Get("refund.batch_points")=="5","unrelated saved refund batch was silently applied");
        Check(Bytes(config).find("batch_points = 25")!=std::string::npos,"unrelated saved value lost");
    }
    for (const auto* bad : {"0","999999999999","oops","3\nenabled=true","3 # injected"}) {
        const auto before = Get(batch); Check(!Set(batch,bad),"invalid edit accepted"); Check(Get(batch)==before,"invalid edit changed running settings");
    }
    if (remote) {
        saveAllowed = false; Check(!Set(batch,"192"),"failed SDK save accepted"); Check(Get(batch)=="160","failed SDK save changed state"); saveAllowed=true;
        Check(Set("hotkey","\"CTRL+F4\""),"hotkey edit rejected"); Check(Get("hotkey")=="\"CTRL+F4\"","hotkey getter stale");
    } else {
        const auto held = CreateFileW(config.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        Check(held!=INVALID_HANDLE_VALUE,"cannot lock fixture for failure case"); Check(!Set(batch,"9"),"locked-file save accepted"); CloseHandle(held);
        Check(Get(batch)=="7","failed atomic save changed state");
        Check(!Set("refund.single_removal","\"Ctrl+RightClick\""),"duplicate controls accepted");
        Check(Set("refund.batch_points","9"),"refund batch refused"); Check(Get("refund.batch_points")=="9","refund batch stale");
        Check(Set("confirmShiftAllocation","true"),"confirmation edit refused"); Check(Get("confirmShiftAllocation")=="true","confirmation getter stale");
    }
    for (unsigned i=0;i<50;++i) Check(Set("diagnostics" + std::string(remote ? ".enabled" : ""),"false"),"repeated edit failed");
    unsigned fields{};
    for(std::size_t at=0;(at=schema.find("key = \"",at))!=schema.npos;) {
        at+=7; const auto end=schema.find('"',at); Check(end!=schema.npos,"malformed schema key");
        const auto key=schema.substr(at,end-at); const auto value=Get(key.c_str());
        Check(Set(key,value.c_str()),"schema field rejected its own running value");
        Check(Get(key.c_str())==value,"schema round-trip changed value"); ++fields; at=end+1;
    }
    Check(fields==(remote?18U:8U),"settings schema lost a field");
    Check(Set("enabled","true"),"master preference refused"); Check(Get("enabled")=="true","saved master preference stale");
    Check(notifications==countBefore,"hub-originated setter requested redundant refresh");
    unload(); Check(!client.set,"page not unregistered on unload"); FreeLibrary(module);
    std::filesystem::remove(config); std::filesystem::remove(root);
}
}
int wmain(int argc,wchar_t** argv) {
    try { Check(argc==3,"expected both DLL paths"); Run(argv[1],true); Run(argv[2],false);
        Check(registrations==2 && releases>=2,"provider lease lifecycle mismatch");
        std::cout << checks << " production settings callback checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
