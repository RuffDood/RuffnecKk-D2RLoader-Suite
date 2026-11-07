#include "automap_hook_bridge.hpp"
#include "celestial_automap_witness.hpp"
#include <array>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cstdlib>

using namespace RuffnecKk::MapSense::Detail;
namespace {
int Failures{}, NativeCalls{}, ObserverCalls{}, NativeHookInstalls{}, IconCalls{}, DrawCalls{};
void* IconUnit{};
void* DrawContext{};
void __fastcall RecordIconStub(void* unit) noexcept { ++IconCalls; IconUnit = unit; }
void __fastcall DrawIconsStub(void* context) noexcept { ++DrawCalls; DrawContext = context; }
void* SeenUnit{};
void* SeenContext{};
AutomapPass Original{};
D2RL::Diagnostics::ModificationState State = D2RL::Diagnostics::ModificationState::Tracked;
D2RL::Diagnostics::ModificationKind Kind = D2RL::Diagnostics::ModificationKind::InlineHook;
std::uint32_t Owners = 1;
const char* Owner = "celestialrayone.loot-filter";
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
constexpr std::uint64_t Sentinel = 0xFEDCBA9876543201ULL;
void* NativeUnit{};
std::uint64_t __fastcall Native(void* unit, void*, void*) noexcept { ++NativeCalls; NativeUnit = unit; return Sentinel; }
void __fastcall ObserveOnly(void* unit, void* context) noexcept {
    CHECK(NativeCalls == ObserverCalls + 1);
    SeenUnit = unit; SeenContext = context; ++ObserverCalls;
}
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
    if (argc != 2 && argc != 3) return 2;
    const bool revision182 = argc == 3 && std::wcscmp(argv[2], L"1.8.2") == 0;
    if (argc == 3 && !revision182) return 2;
    const auto callbackRva = revision182 ? 0x32850U : CelestialAutomapCallbackRva;
    const auto slotRva = revision182 ? 0xD4180U : CelestialAutomapOriginalSlotRva;
    const auto recordRva = revision182 ? 0x40760U : 0x3A900U;
    const auto drawRva = revision182 ? 0x2E740U : 0x29650U;
    const auto readyRva = revision182 ? 0xD4132U : 0xC903AU;
    const auto itemDetailRva = revision182 ? 0xD4136U : 0xC903DU;
    const auto playerReferenceRva = revision182 ? 0xD41E8U : 0xC90F0U;
    const auto playerMarkerRva = revision182 ? 0xD41F0U : 0xC90F8U;
    const auto previousMarkerRva = revision182 ? 0xD24E8U : 0xC75D0U;
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
    auto** slot = reinterpret_cast<void**>(image + slotRva);
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
    AbsoluteJump(entry, image + callbackRva);
    std::memset(entry + 14, 0x90, 4);
    *slot = trampoline;
    const bool qualified = QualifyAutomapHookRoute(&context, Expected, route);
    CHECK(qualified);
    if (!qualified) {
        VirtualFree(fixture, 0, MEM_RELEASE);
        FreeLibrary(module);
        return EXIT_FAILURE;
    }
    CHECK(route.slot == slot && route.original == trampoline);
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original, ObserveOnly));
    CHECK(NativeHookInstalls == 1); // existing canonical hook is never replaced
    CHECK(*slot != trampoline && *slot != reinterpret_cast<void*>(&Observe));

    auto callback = reinterpret_cast<ReturningAutomapPass>(image + callbackRva);
    CHECK(callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560)) == Sentinel);
    CHECK(NativeUnit == reinterpret_cast<void*>(0x1230));
    CHECK(NativeCalls == 1 && ObserverCalls == 1);
    CHECK(SeenUnit == reinterpret_cast<void*>(0x1230) && SeenContext == reinterpret_cast<void*>(0x4560));
    RestoreAutomapHookRoute();
    CHECK(*slot == trampoline);
    CHECK(callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560)) == Sentinel);
    CHECK(NativeCalls == 2 && ObserverCalls == 1);

    // Loader's ordinary five-byte entry jump through a nearby absolute relay.
    // The first native instruction is exactly five bytes; the rest stays native.
    std::memcpy(entry, Expected.data(), Expected.size());
    entry[0] = 0xE9;
    const auto relative = static_cast<std::int32_t>((fixture + 0x800) - (entry + 5));
    std::memcpy(entry + 1, &relative, sizeof(relative));
    AbsoluteJump(fixture + 0x800, image + callbackRva);
    std::memcpy(trampoline, Expected.data(), 5);
    AbsoluteJump(trampoline + 5, entry + 5);
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original, ObserveOnly));
    // Reset counters so the callback's exactly-once assertion covers this path.
    NativeCalls = 0; ObserverCalls = 0;
    CHECK(callback(reinterpret_cast<void*>(0x1230), reinterpret_cast<void*>(0x4560)) == Sentinel);
    CHECK(NativeCalls == 1 && ObserverCalls == 1);
    RestoreAutomapHookRoute();
    CHECK(*slot == trampoline);

    // Execute the actual provider callback's active item path. Only its downstream
    // icon routines are substituted in this private, non-initialized fixture;
    // the callback under test and its complete byte witness remain unchanged.
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original, ObserveOnly));
    std::array<std::uint8_t, 14> savedRecord{}, savedDraw{}, recordStub{}, drawStub{};
    std::memcpy(savedRecord.data(), image + recordRva, savedRecord.size());
    std::memcpy(savedDraw.data(), image + drawRva, savedDraw.size());
    AbsoluteJump(recordStub.data(), reinterpret_cast<void*>(&RecordIconStub));
    AbsoluteJump(drawStub.data(), reinterpret_cast<void*>(&DrawIconsStub));
    Write(image + recordRva, recordStub.data(), recordStub.size());
    Write(image + drawRva, drawStub.data(), drawStub.size());
    image[readyRva] = 1; image[itemDetailRva] = 0;
    *reinterpret_cast<std::uint64_t*>(image + playerReferenceRva) = 1;
    *reinterpret_cast<std::uint64_t*>(image + playerMarkerRva) = 1;
    *reinterpret_cast<std::uint64_t*>(image + previousMarkerRva) = 0;
    std::array<std::uint32_t, 8> item{}; item[0] = 4;
    NativeCalls = 0; ObserverCalls = 0;
    CHECK(callback(item.data(), reinterpret_cast<void*>(0x4560)) == Sentinel);
    CHECK(NativeCalls == 1 && ObserverCalls == 1 && IconCalls == 1 && DrawCalls == 1);
    CHECK(IconUnit == item.data() && DrawContext == reinterpret_cast<void*>(0x4560));
    CHECK(callback(item.data(), reinterpret_cast<void*>(0x4560)) == Sentinel);
    CHECK(NativeCalls == 2 && ObserverCalls == 2 && IconCalls == 2 && DrawCalls == 1);
    RestoreAutomapHookRoute();
    image[readyRva] = 0;
    Write(image + recordRva, savedRecord.data(), savedRecord.size());
    Write(image + drawRva, savedDraw.data(), savedDraw.size());

    // Competing owners, modifications and unavailable diagnostics are rejected.
    Owner = "other-plugin"; CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Owner = "celestialrayone.loot-filter"; Owners = 2; CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Owners = 1; Kind = D2RL::Diagnostics::ModificationKind::BytePatch;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Kind = D2RL::Diagnostics::ModificationKind::InlineHook;
    State = D2RL::Diagnostics::ModificationState::Untracked;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    State = D2RL::Diagnostics::ModificationState::Tracked; DiagnosticsAvailable = false;
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); DiagnosticsAvailable = true;
    entry[25] ^= 1; CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); entry[25] ^= 1;
    trampoline[0] ^= 1; CHECK(!QualifyAutomapHookRoute(&context, Expected, route)); trampoline[0] ^= 1;
    const auto savedCallbackByte = image[callbackRva + 397];
    const auto badCallbackByte = static_cast<std::uint8_t>(savedCallbackByte ^ 1);
    Write(image + callbackRva + 397, &badCallbackByte, 1);
    CHECK(!QualifyAutomapHookRoute(&context, Expected, route));
    Write(image + callbackRva + 397, &savedCallbackByte, 1);
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    *slot = reinterpret_cast<void*>(&Native);
    CHECK(!InstallAutomapHookRoute(&context, Expected, route, Observe, Original, ObserveOnly));
    CHECK(*slot == reinterpret_cast<void*>(&Native)); // no clobber on attachment race
    *slot = trampoline;
    CHECK(QualifyAutomapHookRoute(&context, Expected, route));
    CHECK(InstallAutomapHookRoute(&context, Expected, route, Observe, Original, ObserveOnly));
    *slot = reinterpret_cast<void*>(&Native); RestoreAutomapHookRoute();
    CHECK(*slot == reinterpret_cast<void*>(&Native)); // no clobber on restoration race
    CHECK(!ExchangeAutomapSlot(nullptr, nullptr, nullptr));
    *slot = savedSlot;

    VirtualFree(fixture, 0, MEM_RELEASE); FreeLibrary(module);
    std::printf("Celestial %s automap bridge: %s\n", revision182 ? "1.8.2" : "1.5.3",
        Failures ? "FAILED" : "PASS");
    return Failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
