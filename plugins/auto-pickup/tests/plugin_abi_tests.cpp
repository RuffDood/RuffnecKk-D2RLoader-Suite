#include <D2RLPlugin/api.h>
#include <Windows.h>
#include "pickup_bridge.hpp"
#include <iostream>
#include <string_view>

namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
using InfoFn = const D2RL::PluginInfo*(__cdecl*)() noexcept;
void Inspect(HMODULE module, bool anything) {
    if (!module) { CHECK(module != nullptr); return; }
    const auto infoFn = reinterpret_cast<InfoFn>(GetProcAddress(module, "D2RLoaderGetPluginInfo"));
    CHECK(infoFn != nullptr);
    if (infoFn) {
        const auto* info = infoFn();
        CHECK(info && info->infoSize >= sizeof(D2RL::PluginInfo));
        if (info) CHECK(!D2RL::HasFlag(info->flags, D2RL::PluginFlags::ModScopedOnly));
        // IDs are character-history keys, not public branding. Pin both.
        if (info) CHECK(std::string_view(info->id) == (anything ? "ruffneckk-auto-pickup" : "teleport-auto-pickup"));
    }
    CHECK(GetProcAddress(module, "AutoInteractGetSettingsApi") == nullptr); // Settings are internal now.
    CHECK(GetProcAddress(module, "D2RLoaderLoadPlugin") != nullptr);
    CHECK(GetProcAddress(module, "D2RLoaderUnloadPlugin") != nullptr);
    const auto getApi = reinterpret_cast<Reimagined::PickupBridge::GetApiFn>(GetProcAddress(module, Reimagined::PickupBridge::ExportName));
    CHECK((getApi != nullptr) == anything);
    if (getApi) {
        CHECK(getApi(99, sizeof(Reimagined::PickupBridge::ApiV1)) == nullptr);
        CHECK(getApi(Reimagined::PickupBridge::Version, 1) == nullptr);
        const auto* api = getApi(Reimagined::PickupBridge::Version, sizeof(Reimagined::PickupBridge::ApiV1));
        CHECK(Reimagined::PickupBridge::ValidApi(api));
        if (api) CHECK(!api->scan(reinterpret_cast<void*>(1))); // Inactive provider must not dereference caller data.
        const auto getRangeApi = reinterpret_cast<Reimagined::PickupBridge::GetRangeApiFn>(getApi);
        const auto* rangeApi = getRangeApi(Reimagined::PickupBridge::RangeVersion, sizeof(Reimagined::PickupBridge::ApiV2));
        CHECK(Reimagined::PickupBridge::ValidApi(rangeApi));
        CHECK(getRangeApi(Reimagined::PickupBridge::RangeVersion, sizeof(Reimagined::PickupBridge::ApiV2) - 1) == nullptr);
        if (rangeApi) CHECK(!rangeApi->scan(reinterpret_cast<void*>(1), 8));
    }
}
}
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    // Loading metadata never calls D2RLoaderLoadPlugin or patches a game.
    auto anything = LoadLibraryA(argv[1]);
    Inspect(anything, true);
    if (anything) FreeLibrary(anything);
    if (argc == 3) {
        auto teleport = LoadLibraryA(argv[2]); // Teleport on its own.
        Inspect(teleport, false);
        anything = LoadLibraryA(argv[1]); // Both, Teleport first.
        Inspect(anything, true); Inspect(teleport, false);
        if (anything) FreeLibrary(anything);
        Inspect(teleport, false);
        if (teleport) FreeLibrary(teleport);
        anything = LoadLibraryA(argv[1]); // Both, Anything first.
        teleport = LoadLibraryA(argv[2]);
        Inspect(anything, true); Inspect(teleport, false);
        if (teleport) FreeLibrary(teleport);
        Inspect(anything, true);
        if (anything) FreeLibrary(anything);
    }
    std::cout << "DLL metadata, independent load/unload, optional bridge ABI and inactive-provider checks; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
