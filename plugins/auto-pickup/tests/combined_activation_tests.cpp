#include <D2RLPlugin/api.h>
#include "native_contract.hpp"
#include "native.hpp"
#include "action_hooks.hpp"
#include <RuffnecKk/native_stat_compat.hpp>
namespace FixturePickup {
#include "../src/native.hpp"
}
#include "../third_party/RuffnecKkObjectContract/native_object_interact_contract.hpp"
#include <Windows.h>
#include <filesystem>
#include "../src/policy.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
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
bool strict{};
unsigned hooks{}, patches{}, failSubscription{}, subscriptionCount{}, removed{}, originalCalls{};
bool missingThread{}, failHook{}, rejectQueue{}, failWrite{};
unsigned configWrites{};
void* hookTarget{};
std::vector<D2RL::Lifecycle::GameplayEventListener> listeners;
std::vector<std::pair<D2RL::Threads::Callback, void*>> queue;
std::array<std::uintptr_t, 8> observedArgs;
void __cdecl Log(const D2RL::PluginContext*, const char* message) noexcept { if (strict) std::cerr << message << "\n"; }
bool __cdecl Write(const D2RL::PluginContext*, const char* text) noexcept { ++configWrites; if (failWrite) return false; config = text; return true; }
auto __cdecl Read(const D2RL::PluginContext*, char* out, std::uint32_t size, std::uint32_t* required) noexcept -> bool {
    *required = static_cast<std::uint32_t>(config.size() + 1);
    if (size < *required) return false;
    std::memcpy(out, config.c_str(), *required); return true;
}
auto __cdecl Check(const D2RL::PluginContext*, std::uint64_t rva, const void* expected, std::uint32_t size) noexcept -> bool {
    const auto okay = !strict || (rva <= ImageSize && size <= ImageSize-rva && std::memcmp(image+rva,expected,size)==0);
    if(!okay) std::cerr<<"Capture mismatch RVA 0x"<<std::hex<<rva<<std::dec<<"\n";
    return okay;
}
auto __fastcall Original(void* game, void* player, void* skill, std::uint32_t mode, std::uint32_t type,
        std::uint32_t id, std::uint32_t a7, std::uint32_t a8) -> std::int32_t {
    ++originalCalls;
    observedArgs = {reinterpret_cast<std::uintptr_t>(game), reinterpret_cast<std::uintptr_t>(player), reinterpret_cast<std::uintptr_t>(skill), mode, type, id, a7, a8};
    return -123;
}
auto __cdecl Hook(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* hook) noexcept -> bool {
    if(hook->rva==0x3862D0) { if(hook->original)*hook->original=reinterpret_cast<void*>(1);return true; }
    CHECK(hook->rva == C::PlayerActionRva && hook->expectedSize == C::PlayerActionBytes.size());
    CHECK(Check(nullptr, hook->rva, hook->expected, hook->expectedSize));
    if (failHook) return false;
    ++hooks; hookTarget = hook->target; *hook->original = reinterpret_cast<void*>(&Original); return true;
}
auto __cdecl Patch(const D2RL::PluginContext*, std::uint64_t, const void*, std::uint32_t, std::uint64_t, std::uint32_t) noexcept -> bool {
    ++patches; return true;
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
auto __cdecl HookStatus(const D2RL::PluginContext*, const D2RL::Diagnostics::HookQuery* query, D2RL::Diagnostics::HookStatus* status) noexcept -> D2RL::Diagnostics::Result {
    if(!strict || query->rva!=FixturePickup::PickupRva) return D2RL::Diagnostics::Result::Unavailable;
    // The capture was taken with Auto Deposit active. Model its SDK ownership
    // report; retain the captured hooked entry rather than replacing its bytes.
    status->state=D2RL::Diagnostics::ModificationState::Tracked;status->kind=D2RL::Diagnostics::ModificationKind::InlineHook;status->ownerCount=1;
    std::memcpy(status->ownerPluginId,"d2rl-auto-deposit",17);return D2RL::Diagnostics::Result::Success;
}
D2RL::DiagnosticsServiceV1 diagnostics{D2RL::DiagnosticsServiceV1Size,1,HookStatus};
auto __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id, std::uint32_t, const void** service) noexcept -> D2RL::ServiceQueryResult {
    *service = nullptr;
    if (id == D2RL::ServiceId::Diagnostics && strict) *service = &diagnostics;
    if (id == D2RL::ServiceId::Thread && !missingThread) *service = &threads;
    if (id == D2RL::ServiceId::Lifecycle) *service = &lifecycle;
    return *service ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
D2RL::PluginApi api;
D2RL::PluginContext context;
std::string defaults;
void Reset(bool foreignPickupHooks = false) {
    config = defaults;
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
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;std::memcpy(image,&dos,sizeof(dos));
    IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(ImageSize);std::memcpy(image+0x80,&nt,sizeof(nt));
    const auto& helper=RuffnecKk::NativeStatCompat::Loader130StatAdmissionContract().helpers[0];
    auto nibble=[](char ch) {return ch<='9'?ch-'0':(ch|0x20)-'a'+10;};
    for(std::size_t i=0;i<helper.canonicalExpectedHex.size()/2;++i) image[helper.nativeRva+i]=static_cast<std::uint8_t>((nibble(helper.canonicalExpectedHex[i*2])<<4)|nibble(helper.canonicalExpectedHex[i*2+1]));
    constexpr std::array<std::uint8_t,28> cap{0x48,0x83,0xEC,0x28,0x45,0x33,0xC0,0x41,0x8D,0x50,0x0C,0xE8,0xF0,0x9C,0xFA,0xFF,0x69,0xC0,0x10,0x27,0,0,0x48,0x83,0xC4,0x28,0xC3,0xCC};
    std::memcpy(image+0x34B320,cap.data(),cap.size());
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
int main(int argc,char** argv) {
    CHECK(argc==3 || argc==4);
    std::ifstream file(argv[2],std::ios::binary);defaults={std::istreambuf_iterator<char>(file),{}};CHECK(!defaults.empty());
    image=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,ImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));CHECK(image);
    const auto module=LoadLibraryA(argv[1]);CHECK(module);
    const auto load=reinterpret_cast<D2RL::LoadPluginFn>(GetProcAddress(module,D2RL_PLUGIN_LOAD_EXPORT));
    const auto unload=reinterpret_cast<D2RL::UnloadPluginFn>(GetProcAddress(module,D2RL_PLUGIN_UNLOAD_EXPORT));CHECK(load && unload);
    CHECK(!GetProcAddress(module,"AutoInteractGetSettingsApi"));
    const auto resource=FindResourceW(module,MAKEINTRESOURCEW(1002),RT_RCDATA);CHECK(resource);
    std::ifstream defaultFile(std::filesystem::path(argv[2]).parent_path().parent_path()/"config/ruffneckk-auto-pickup.toml",std::ios::binary);
    const std::string shipped{std::istreambuf_iterator<char>(defaultFile),{}};
    CHECK(!shipped.empty() && SizeofResource(module,resource)==shipped.size());
    CHECK(std::memcmp(LockResource(LoadResource(module,resource)),shipped.data(),shipped.size())==0);
    RuffnecKk::AutoPickup::Config empty;std::string why;
    CHECK(RuffnecKk::AutoPickup::ParseConfig(shipped,empty,why));
    CHECK(!empty.enabled && !empty.walkingEnabled && !empty.teleportEnabled && !empty.teleportGold && !empty.teleportItems && !empty.interact.enabled);
    for(const auto& row:RuffnecKk::AutoPickup::Catalog)CHECK(!empty.Selects(RuffnecKk::AutoPickup::PackItemCode(row.code)));
    for(const auto& route:empty.potionRouting.families)CHECK(!route.count && !route.inventoryFallback);
    Reset();CHECK(load(&context) && patches==18 && hooks==1 && listeners.size()==9);unload();CHECK(removed==9);
    Reset();Set("validate_only = false","validate_only = true");CHECK(load(&context) && !patches && !hooks && listeners.empty());unload();
    Reset(); failHook=true;CHECK(load(&context) && patches==18 && !hooks);unload(); // opening failure preserves item pickup
    if(argc==4) {
        Reset();std::ifstream captured(argv[3],std::ios::binary);captured.read(reinterpret_cast<char*>(image),ImageSize);CHECK(captured.gcount()==ImageSize);
        IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;std::memcpy(image,&dos,sizeof(dos));
        IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(ImageSize);std::memcpy(image+0x80,&nt,sizeof(nt));
        // Normalize relocation-only tables to this fixture base. Code bytes
        // remain from the independent capture; mutation APIs are test doubles.
        std::uint64_t first{};std::memcpy(&first,image+0x23A51C0+3*8,8);const auto oldBase=first-0x58F1D0;
        for(auto [slot,rva]:C::Operations) {const auto value=reinterpret_cast<std::uintptr_t>(image)+rva;std::memcpy(image+0x23A51C0+slot*8,&value,8);}
        (void)oldBase;
        for(unsigned op=1;op<=18;++op) {const auto value=reinterpret_cast<std::uintptr_t>(image)+FixturePickup::TriggerHandlerRvas[op];std::memcpy(image+FixturePickup::ServerPacketTableRva+op*8,&value,8);}
        // GetUnitStat in this capture jumps into D2RCore, whose external code
        // is absent from the image. Exercise the adapter's canonical native
        // branch here; its loader branches have separate contract tests.
        const auto& stat=RuffnecKk::NativeStatCompat::Loader130StatAdmissionContract().helpers[0];
        auto nibble=[](char ch) {return ch<='9'?ch-'0':(ch|0x20)-'a'+10;};
        for(std::size_t i=0;i<stat.canonicalExpectedHex.size()/2;++i) image[stat.nativeRva+i]=static_cast<std::uint8_t>((nibble(stat.canonicalExpectedHex[i*2])<<4)|nibble(stat.canonicalExpectedHex[i*2+1]));
        strict=true;CHECK(load(&context) && patches==18 && hooks==1 && listeners.size()==9);unload();
        std::cout<<"Combined DLL capture admission passed (table relocations and external stat thunk normalized; no gameplay calls).\n";
    }
    FreeLibrary(module);VirtualFree(image,0,MEM_RELEASE);std::cout<<"Single-DLL pickup plus opening lifecycle passed\n";
}
