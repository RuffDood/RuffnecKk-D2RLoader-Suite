#include <D2RLPlugin/api.h>
#include <D2RLPlugin/resources.h>
#include "companion_layout.hpp"
#include "move_only_compatibility.hpp"
#include <vector>
#include <algorithm>
#include <Windows.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" bool D2RLoaderLoadPlugin(const D2RL::PluginContext*) noexcept;
extern "C" void D2RLoaderUnloadPlugin() noexcept;
using namespace RuffnecKk::VendorStockRefresh;
#define CHECK(e) do { if (!(e)) return __LINE__; } while (false)
namespace {
std::string lastError;
bool resourceAvailable = true;
void __cdecl Log(const D2RL::PluginContext*, const char* text) noexcept { lastError = text; }
bool __cdecl ReadConfig(const D2RL::PluginContext*, char* out, std::uint32_t capacity,
                       std::uint32_t* required) noexcept {
    constexpr char text[] = "[plugin]\nenabled = true\n[diagnostics]\nenabled = false\n";
    *required = sizeof(text);
    if (capacity < sizeof(text)) return false;
    std::memcpy(out, text, sizeof(text));
    return true;
}
D2RL::ServiceQueryResult __cdecl Query(const D2RL::PluginContext*, D2RL::ServiceId id,
                                      std::uint32_t version, const void** out) noexcept {
    static const D2RL::ResourceServiceV1 service{D2RL::ResourceServiceV1Size, 1, nullptr, nullptr, nullptr};
    if (!resourceAvailable || id != D2RL::ServiceId::Resource || version != 1)
        return D2RL::ServiceQueryResult::Unavailable;
    *out = &service;
    return D2RL::ServiceQueryResult::Success;
}
}
int TestMoveOnlyCompatibility() {
    namespace M = MoveOnlyCompatibility;
    constexpr std::uintptr_t game = 0x10000000, owner = 0x20000000, relay = 0x30000000;
    constexpr auto entry = game + CompanionNative::AssignStringRva;
    constexpr auto trampoline = relay + 0x100;
    std::vector<std::uint8_t> native(114), provider(0x30000), relays(0x200);
    const auto read = [&](std::uintptr_t address, void* out, std::size_t size) noexcept {
        for (const auto& region : {std::pair{entry, &native}, std::pair{owner, &provider}, std::pair{relay, &relays}}) {
            if (address >= region.first && address - region.first <= region.second->size()
                && size <= region.second->size() - (address - region.first)) {
                std::memcpy(out, region.second->data() + (address - region.first), size); return true;
            }
        }
        return false;
    };
    std::copy(CompanionNative::AssignStringBytes.begin(), CompanionNative::AssignStringBytes.end(), native.begin());
    CHECK(M::Validate(read, game, 0));
    const auto vanilla = native;
    native[0] = 0xE9;
    const auto displacement = static_cast<std::int32_t>(relay - (entry + 5));
    std::memcpy(native.data() + 1, &displacement, 4);
    CHECK(!M::Validate(read, game, 0));
    const std::array<std::uint8_t, 6> jump{0xFF, 0x25, 0, 0, 0, 0};
    std::copy(jump.begin(), jump.end(), relays.begin());
    const auto hook = owner + M::HookRva;
    std::memcpy(relays.data() + 6, &hook, 8);
    std::copy(M::HookBytes.begin(), M::HookBytes.end(), provider.begin() + M::HookRva);
    std::copy(M::LoadFlagBytes.begin(), M::LoadFlagBytes.end(), provider.begin() + M::LoadFlagRva);
    std::copy(M::FlagAddressBytes.begin(), M::FlagAddressBytes.end(), provider.begin() + M::FlagAddressRva);
    std::copy(M::MemoryOrderBytes.begin(), M::MemoryOrderBytes.end(), provider.begin() + M::MemoryOrderRva);
    std::memcpy(provider.data() + M::GameBaseSlot, &game, 8);
    std::memcpy(provider.data() + M::OriginalSlot, &trampoline, 8);
    std::copy_n(vanilla.begin(), 5, relays.begin() + 0x100);
    std::copy(jump.begin(), jump.end(), relays.begin() + 0x105);
    const auto continuation = entry + 5;
    std::memcpy(relays.data() + 0x10B, &continuation, 8);
    // Regression: old full-body equality rejects this exact valid hook pattern.
    CHECK(native != vanilla);
    CHECK(M::Validate(read, game, owner));
    CHECK(!M::Validate(read, game, owner + 1));
    for (const auto offset : {M::HookRva, M::HookRva + 0x4D, M::HookRva + 0xAF,
            M::LoadFlagRva, M::FlagAddressRva, M::MemoryOrderRva, M::GameBaseSlot, M::OriginalSlot}) {
        provider[offset] ^= 1; CHECK(!M::Validate(read, game, owner)); provider[offset] ^= 1;
    }
    for (const std::size_t offset : {0u, 6u, 0x100u, 0x105u, 0x10Bu}) {
        relays[offset] ^= 1; CHECK(!M::Validate(read, game, owner)); relays[offset] ^= 1;
    }
    native[20] ^= 1; CHECK(!M::Validate(read, game, owner)); native[20] ^= 1;
    auto truncated = [&](std::uintptr_t address, void* out, std::size_t size) noexcept {
        return address != owner + M::HookRva && read(address, out, size);
    };
    CHECK(!M::Validate(truncated, game, owner));
    CHECK(!M::Validate(read, (std::numeric_limits<std::uintptr_t>::max)(), owner));
    // Exercise the same companion dispatch used by ValidateRuntime.
    CHECK(CompanionNative::Validate([&](std::uintptr_t rva, const std::uint8_t*, std::uint32_t) {
        return rva != CompanionNative::AssignStringRva || M::Validate(read, game, owner);
    }));
    return 0;
}
int wmain(int argc, wchar_t** argv) {
    CHECK(TestMoveOnlyCompatibility() == 0);
    CHECK(SameImagePath("PANEL\\Vendors\\VendorShop_BG", NativeBackgrounds[0]));
    CHECK(!SameImagePath("Controller/Panel/Vendor/V2/VendorShop_BG", NativeBackgrounds[0]));
    CHECK(IsDesktopBackground({{0, 0, 1162, 1507}, 1.0F}));
    CHECK(!IsDesktopBackground({{-2, 100, 1162, 1507}, 1.0F}));
    CHECK(!IsDesktopBackground({{0, 0, 320, 432}, 1.0F}));
    const auto placement = CenterInPanelSlot(CompanionSlot, {{877, 1277, 116, 116}, 1.0F});
    CHECK(placement.valid && placement.geometry.rect.x == 520 && placement.geometry.rect.y == 1352);
    CHECK(placement.geometry.scale == 1.0F);
    CHECK(argc == 2);
    const auto temp = std::filesystem::temp_directory_path()
        / (L"vendor-companion-test-" + std::to_wstring(GetCurrentProcessId()));
    CHECK(std::filesystem::create_directory(temp));
    const auto dll = (temp / L"d2rl-ruffneckk-vendor-stock-refresh.dll").wstring();
    const auto mpq = temp / L"d2rl-ruffneckk-vendor-stock-refresh.mpq";
    D2RL::PluginApi api{};
    api.apiSize = D2RL::PluginApiSize;
    api.logError = Log; api.logInfo = Log; api.logWarn = Log;
    api.readConfig = ReadConfig; api.queryService = Query;
    D2RL::PluginContext ctx{};
    ctx.contextSize = D2RL::PluginContextSize;
    ctx.apiVersion = D2RL_PLUGIN_API_VERSION;
    ctx.api = &api; ctx.pluginPath = dll.c_str();
    // No executable base or hook API: these cases must fail before native access.
    CHECK(!D2RLoaderLoadPlugin(&ctx));
    CHECK(lastError.find("missing/mismatched") != std::string::npos);
    D2RLoaderUnloadPlugin();
    std::filesystem::copy_file(argv[1], mpq);
    CHECK(!D2RLoaderLoadPlugin(&ctx));
    CHECK(lastError.find("executable base is unavailable") != std::string::npos);
    D2RLoaderUnloadPlugin();
    resourceAvailable = false;
    CHECK(!D2RLoaderLoadPlugin(&ctx));
    CHECK(lastError.find("missing/mismatched") != std::string::npos);
    D2RLoaderUnloadPlugin();
    resourceAvailable = true;
    {
        std::fstream corrupt(mpq, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(100); corrupt.put('\xA5');
    }
    CHECK(!D2RLoaderLoadPlugin(&ctx));
    CHECK(lastError.find("missing/mismatched") != std::string::npos);
    D2RLoaderUnloadPlugin();
    std::filesystem::remove(mpq);
    std::filesystem::remove(temp);
    return 0;
}
