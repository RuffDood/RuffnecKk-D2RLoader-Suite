#include <D2RLPlugin/api.h>
#include <D2RLPlugin/resources.h>
#include "companion_layout.hpp"
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
int wmain(int argc, wchar_t** argv) {
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
