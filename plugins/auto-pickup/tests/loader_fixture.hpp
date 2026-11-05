#pragma once
#include <D2RLPlugin/api.h>
#include <cstring>
#include <string>
namespace LoaderFixture {
inline std::string ConfigText;
inline bool Compatible{true};
inline unsigned Checks{}, Patches{}, InlineHooks{}, Listeners{};
inline bool LifecycleAvailable{true};
inline unsigned FailListener{}, Unsubscribed{};
inline void __cdecl Log(const D2RL::PluginContext*, const char*) noexcept {}
inline bool __cdecl Read(const D2RL::PluginContext*, char* out, std::uint32_t size, std::uint32_t* required) noexcept {
    *required = static_cast<std::uint32_t>(ConfigText.size() + 1);
    if (size < *required) return false;
    std::memcpy(out, ConfigText.c_str(), *required); return true;
}
inline bool __cdecl Check(const D2RL::PluginContext*, std::uint64_t, const void*, std::uint32_t) noexcept { ++Checks; return Compatible; }
inline bool __cdecl Patch(const D2RL::PluginContext*, std::uint64_t, const void*, std::uint32_t, std::uint64_t, std::uint32_t) noexcept { ++Patches; return true; }
inline bool __cdecl Hook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* hook) noexcept { ++InlineHooks; if(hook->rva != 0x3862D0)return false; if(hook->original)*hook->original=reinterpret_cast<void*>(1);return true; }
inline bool __cdecl Command(const D2RL::PluginContext*, const D2RL::ConsoleCommandRegistration*) noexcept { return true; }
inline D2RL::Threads::Result __cdecl Queue(const D2RL::PluginContext*, D2RL::Threads::Callback, void*) noexcept { return D2RL::Threads::Result::Success; }
inline D2RL::Lifecycle::Result __cdecl Subscribe(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEventListener*, D2RL::Lifecycle::ListenerHandle* handle) noexcept {
    ++Listeners;
    if (Listeners == FailListener) return D2RL::Lifecycle::Result::Unavailable;
    *handle = Listeners; return D2RL::Lifecycle::Result::Success;
}
inline D2RL::Lifecycle::Result __cdecl Unsubscribe(const D2RL::PluginContext*, D2RL::Lifecycle::ListenerHandle) noexcept { ++Unsubscribed; return D2RL::Lifecycle::Result::Success; }
inline const D2RL::ThreadServiceV1 Threads{D2RL::ThreadServiceV1Size, D2RL::ThreadServiceV1Version, nullptr, Queue};
inline const D2RL::LifecycleServiceV1 Lifecycle{D2RL::LifecycleServiceV1Size, D2RL::LifecycleServiceV1Version, nullptr, nullptr, Subscribe, Unsubscribe};
inline D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t, const void** service) noexcept {
    if (id == D2RL::ServiceId::Thread) { *service = &Threads; return D2RL::ServiceQueryResult::Success; }
    if (id == D2RL::ServiceId::Lifecycle && LifecycleAvailable) { *service = &Lifecycle; return D2RL::ServiceQueryResult::Success; }
    *service = nullptr; return D2RL::ServiceQueryResult::Unavailable;
}
inline D2RL::PluginApi Api{};
inline D2RL::PluginContext Context{};
inline auto Setup(const std::string& text, bool compatible) -> const D2RL::PluginContext* {
    ConfigText = text; Compatible = compatible; Checks = Patches = InlineHooks = Listeners = 0;
    LifecycleAvailable = true; FailListener = Unsubscribed = 0;
    Api = {}; Api.apiSize = D2RL::PluginApiSize;
    Api.logInfo = Log; Api.logWarn = Log; Api.logError = Log;
    Api.readConfig = Read; Api.checkExpectedBytes = Check; Api.patchWrite = Patch;
    Api.installInlineHook = Hook; Api.registerConsoleCommand = Command; Api.queryService = Query;
    Context = {}; Context.contextSize = D2RL::PluginContextSize; Context.apiVersion = D2RL_PLUGIN_API_VERSION;
    Context.api = &Api; Context.exeBase = 0x140000000; Context.loadScope = D2RL::LoadScope::Global;
    return &Context;
}
}
