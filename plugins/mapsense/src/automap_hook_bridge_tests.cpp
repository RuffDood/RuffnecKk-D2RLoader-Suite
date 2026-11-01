#include "automap_hook_bridge.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace RuffnecKk::MapSense::Detail;
namespace {
int Failures{}, NativeCalls{}, ObserverCalls{}, NativeHookInstalls{};
void* SeenUnit{};
void* SeenContext{};
AutomapPass Original{};
D2RL::Diagnostics::ModificationState State = D2RL::Diagnostics::ModificationState::Tracked;
D2RL::Diagnostics::ModificationKind Kind = D2RL::Diagnostics::ModificationKind::InlineHook;
std::uint32_t Owners = 1;
const char* Owner = "pyre-lootfilter";
bool DiagnosticsAvailable = true;
constexpr std::array<std::uint8_t, 32> Expected{
    0x48,0x89,0x6C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xFA,0x4C,0x8D,0x44,
    0x24,0x68,0x48,0x8D,0x54,0x24,0x60,0x48,0x8B,0xE9,0xE8,0xF1,0x01,0x00,0x00,0x84};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++Failures; } } while(false)
void __fastcall Observe(void* unit, void* context) noexcept {
    Original(unit, context);
    CHECK(NativeCalls == ObserverCalls + 1);
    SeenUnit = unit; SeenContext = context; ++ObserverCalls;
}
unsigned char __fastcall Native(void*, void*, void*) noexcept { ++NativeCalls; return 1; }
void Log(const D2RL::PluginContext*, const char*) noexcept {}
bool Install(const D2RL::PluginContext*, const D2RL::InlineHookRegistration* hook) noexcept {
    ++NativeHookInstalls;
    CHECK(hook->rva == AutomapEntryRva && hook->expectedSize == Expected.size());
    *hook->original = reinterpret_cast<void*>(&Observe);
    return true;
}
D2RL::Diagnostics::Result Query(const D2RL::PluginContext*,
        const D2RL::Diagnostics::HookQuery* query, D2RL::Diagnostics::HookStatus* status) noexcept {
    status->rva = query->rva; status->size = query->expectedSize;
    status->state = State; status->kind = Kind; status->ownerCount = Owners;
    std::memcpy(status->ownerPluginId, Owner, std::strlen(Owner) + 1U);
    return D2RL::Diagnostics::Result::Success;
}
D2RL::DiagnosticsServiceV1 Diagnostics{D2RL::DiagnosticsServiceV1Size,
    D2RL::DiagnosticsServiceV1Version, Query};
D2RL::ServiceQueryResult Service(const D2RL::PluginContext*, D2RL::ServiceId,
        std::uint32_t, const void** result) noexcept {
    *result = &Diagnostics;
    return DiagnosticsAvailable ? D2RL::ServiceQueryResult::Success : D2RL::ServiceQueryResult::Unavailable;
}
void AbsoluteJump(std::uint8_t* at, void* target) {
    const std::array<std::uint8_t, 6> op{0xFF,0x25,0,0,0,0};
    std::memcpy(at, op.data(), op.size()); std::memcpy(at+6, &target, sizeof(target));
}
void Write(void* address, const void* bytes, std::size_t size) {
    DWORD before{}, ignored{};
    CHECK(VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &before));
    std::memcpy(address, bytes, size);
    CHECK(VirtualProtect(address, size, before, &ignored));
    FlushInstructionCache(GetCurrentProcess(), address, size);
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    // No DllMain, imports, game process or plugin initialization is run.
    // Only the isolated automap callback is executed, with its ready flag false.
    const auto module = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    CHECK(module != nullptr); if (!module) return 2;
    auto* image = reinterpret_cast<std::uint8_t*>(module);
    auto* fixture = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0xE0000,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(fixture != nullptr); if (!fixture) return 2;
    auto* entry = fixture + AutomapEntryRva;
    auto* trampoline = fixture + 0x1000;
    auto** slot = reinterpret_cast<void**>(image + PyreAutomapOriginalSlotRva);
    const auto savedSlot = *slot;
    D2RL::PluginApi api{.apiSize = D2RL::PluginApiSize,
        .logInfo = Log, .installInlineHook = Install, .queryService = Service};
    D2RL::PluginContext context{.contextSize = D2RL::PluginContextSize, .apiVersion = D2RL_PLUGIN_API_VERSION,
        .api = &api, .exeBase = reinterpret_cast<std::uintptr_t>(fixture)};
    AutomapHookRoute route{};
    std::memcpy(entry, Expected.data(), Expected.size());
    CHECK(QualifyAutomapHookRoute(&context, Expected, route) && route.slot == nullptr);
    AutomapPass pristineOriginal{};
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, pristineOriginal));
    CHECK(NativeHookInstalls == 1);

    // Real native prologue, then a bounded synthetic body and epilogue. Its
    // displaced prefix and resume address reproduce the hook's trampoline ABI.
    std::memcpy(trampoline, Expected.data(), 18);
    AbsoluteJump(trampoline + 18, entry + 18);
    entry[32] = 0xC0; // complete TEST AL,AL
    const std::array<std::uint8_t, 11> epilogue{
        0x48,0x8B,0x6C,0x24,0x58,0x48,0x83,0xC4,0x40,0x5F,0xC3};
    std::memcpy(entry + 33, epilogue.data(), epilogue.size());
    AbsoluteJump(entry + 0x210, reinterpret_cast<void*>(&Native));
    AbsoluteJump(entry, image + PyreAutomapCallbackRva);
    std::memset(entry + 14, 0x90, 4);
    *slot = trampoline;
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(route.slot == slot && route.original == trampoline);
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original));
    CHECK(NativeHookInstalls == 1); // existing canonical hook is never replaced
    CHECK(*slot == reinterpret_cast<void*>(&Observe));

    // Supply only the CFG dispatch stub needed by this isolated callback.
    fixture[0] = 0xFF; fixture[1] = 0xE0; // JMP RAX
    void* savedDispatch{};
    std::memcpy(&savedDispatch, image + 0x9D398, sizeof(savedDispatch));
    void* dispatch = fixture;
    Write(image + 0x9D398, &dispatch, sizeof(dispatch));
    auto callback = reinterpret_cast<AutomapPass>(image + PyreAutomapCallbackRva);
    callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560));
    CHECK(NativeCalls == 1 && ObserverCalls == 1);
    CHECK(SeenUnit == reinterpret_cast<void*>(0x1230) && SeenContext == reinterpret_cast<void*>(0x4560));
    RestoreAutomapHookRoute();
    CHECK(*slot == trampoline);
    callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560));
    CHECK(NativeCalls == 2 && ObserverCalls == 1);

    // Loader's ordinary five-byte entry jump through a nearby absolute relay.
    // The first native instruction is exactly five bytes; the rest stays native.
    std::memcpy(entry, Expected.data(), Expected.size());
    entry[0] = 0xE9;
    const auto relative = static_cast<std::int32_t>((fixture + 0x800) - (entry + 5));
    std::memcpy(entry + 1, &relative, sizeof(relative));
    AbsoluteJump(fixture + 0x800, image + PyreAutomapCallbackRva);
    std::memcpy(trampoline, Expected.data(), 5);
    AbsoluteJump(trampoline + 5, entry + 5);
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original));
    // Reset counters so the callback's exactly-once assertion covers this path.
    NativeCalls = 0; ObserverCalls = 0;
    callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560));
    CHECK(NativeCalls == 1 && ObserverCalls == 1);
    RestoreAutomapHookRoute();
    CHECK(*slot == trampoline);

    // Competing owners, modifications and unavailable diagnostics are rejected.
    Owner = "other-plugin"; CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Owner = "pyre-lootfilter"; Owners = 2; CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Owners = 1; Kind = D2RL::Diagnostics::ModificationKind::BytePatch;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Kind = D2RL::Diagnostics::ModificationKind::InlineHook;
    State = D2RL::Diagnostics::ModificationState::Untracked;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    State = D2RL::Diagnostics::ModificationState::Tracked; DiagnosticsAvailable = false;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); DiagnosticsAvailable = true;
    entry[25] ^= 1; CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); entry[25] ^= 1;
    trampoline[0] ^= 1; CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); trampoline[0] ^= 1;
    const auto savedCallbackByte = image[PyreAutomapCallbackRva + 570];
    const auto badCallbackByte = static_cast<std::uint8_t>(savedCallbackByte ^ 1);
    Write(image + PyreAutomapCallbackRva + 570, &badCallbackByte, 1);
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Write(image + PyreAutomapCallbackRva + 570, &savedCallbackByte, 1);
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    *slot = reinterpret_cast<void*>(&Native);
    CHECK(!InstallAutomapHookRoute(&context, Expected, route, Observe, Original));
    CHECK(*slot == reinterpret_cast<void*>(&Native)); // no clobber on attachment race
    *slot = trampoline;
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original));
    *slot = reinterpret_cast<void*>(&Native); RestoreAutomapHookRoute();
    CHECK(*slot == reinterpret_cast<void*>(&Native)); // no clobber on restoration race
    CHECK(!ExchangeAutomapSlot(nullptr, nullptr, nullptr));
    *slot = savedSlot;
    Write(image + 0x9D398, &savedDispatch, sizeof(savedDispatch));
    VirtualFree(fixture, 0, MEM_RELEASE); FreeLibrary(module);
    std::printf("automap bridge: %s\n", Failures ? "FAILED" : "PASS");
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
