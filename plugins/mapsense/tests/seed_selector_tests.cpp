#include "seed_selector/map_seed_selector.hpp"

#include <RuffnecKk/live_settings_windows.hpp>

#include <array>
#include <cstring>
#include <iostream>
#include <vector>

extern "C" {
extern volatile long gMapSeedOverrideActive;
extern volatile long gSelectedMapSeed;
}

namespace {

constexpr std::size_t Initializer = 0x326E89;
constexpr std::size_t InitializerHook = 0x326E9E;
constexpr std::size_t Relay = 0x100;
constexpr auto InitialConfig = R"toml(config_version = 1
selected_seed = "Legacy"
[[seeds]]
name = "Legacy"
seed = 123456789
)toml";

bool RejectInitializer{};
std::array<std::uint64_t, 5> Hooks{};
std::size_t HookCount{};

auto __cdecl CheckBytes(const D2RL::PluginContext* context, std::uint64_t rva,
        const void* expected, std::uint32_t count) noexcept -> bool {
    if (RejectInitializer && rva == Initializer) return false;
    std::memcpy(reinterpret_cast<void*>(context->exeBase + rva), expected, count);
    return true;
}

auto __cdecl InstallHook(const D2RL::PluginContext* context,
        const D2RL::InlineHookRegistration* hook) noexcept -> bool {
    if (HookCount == Hooks.size() || hook->expectedSize < 5 || hook->target == nullptr
            || hook->original == nullptr) return false;
    auto* site = reinterpret_cast<unsigned char*>(context->exeBase + hook->rva);
    if (std::memcmp(site, hook->expected, hook->expectedSize) != 0) return false;
    Hooks[HookCount++] = hook->rva;
    *hook->original = site;
    std::memset(site, 0x90, hook->expectedSize);
    site[0] = 0xE9;
    if (hook->rva == InitializerHook) {
        constexpr auto displacement = static_cast<std::int32_t>(Relay)
            - static_cast<std::int32_t>(InitializerHook + 5);
        std::memcpy(site + 1, &displacement, sizeof(displacement));
        auto* relay = reinterpret_cast<unsigned char*>(context->exeBase + Relay);
        constexpr unsigned char indirectJump[]{0xFF, 0x25, 0, 0, 0, 0};
        std::memcpy(relay, indirectJump, sizeof(indirectJump));
        std::memcpy(relay + sizeof(indirectJump), &hook->target, sizeof(hook->target));
    }
    return true;
}

bool Check(bool result, const char* description) {
    if (!result) std::cerr << description << '\n';
    return result;
}

bool Run(const std::filesystem::path& config) {
    const auto legacy = config.parent_path() / L"map-seed-selector.toml";
    namespace Settings = RuffnecKk::LiveSettings;
    if (!Settings::SaveFile(legacy, InitialConfig)) return false;

    std::vector<unsigned char> image(0x540000);
    D2RL::PluginApi api{};
    api.apiSize = D2RL::PluginApiSize;
    api.checkExpectedBytes = CheckBytes;
    api.installInlineHook = InstallHook;
    D2RL::PluginContext context{};
    context.contextSize = sizeof(context);
    context.api = &api;
    context.exeBase = reinterpret_cast<std::uintptr_t>(image.data());
    context.pluginConfigPath = config.c_str();

    RejectInitializer = true;
    if (!Check(MapSeedSelector::Initialize(&context, "unused")
            && !MapSeedSelector::IsAvailable() && HookCount == 0,
            "A changed initializer must prevent seed hook installation.")) return false;

    RejectInitializer = false;
    if (!Check(MapSeedSelector::Initialize(&context, "unused")
            && MapSeedSelector::IsAvailable()
            && Hooks == std::array<std::uint64_t, 5>{
                0x4210CB, 0x532443, 0x532B74, 0x48AADB, InitializerHook}
            && MapSeedSelector::OwnsSeedInitializer(context.exeBase)
            && Settings::ReadFile(legacy) == InitialConfig,
            "The integrated picker must recognize its initializer and preserve the legacy config.")) {
        return false;
    }
    image[InitializerHook + 1] ^= 1;
    if (!Check(!MapSeedSelector::OwnsSeedInitializer(context.exeBase),
            "A later change to the integrated hook must invalidate the atlas witness.")) return false;
    image[InitializerHook + 1] ^= 1;
    image[Relay + 6] ^= 1;
    if (!Check(!MapSeedSelector::OwnsSeedInitializer(context.exeBase),
            "A relay redirected away from MapSense must invalidate the atlas witness.")) return false;
    image[Relay + 6] ^= 1;

    if (!Check(gMapSeedOverrideActive == 0, "The override must default to inactive.")) return false;
    MapSeedSelector::SetOfflineFlowActive(true);
    if (!Check(gMapSeedOverrideActive == 1 && gSelectedMapSeed == 123456789,
            "The Offline route must use the saved selection.")) return false;
    std::string message;
    if (!Check(MapSeedSelector::AddSeedEntry("New", "0xffffffff", message)
            && gMapSeedOverrideActive == 1
            && static_cast<std::uint32_t>(gSelectedMapSeed) == UINT32_MAX,
            "The picker must accept and activate a full-width hexadecimal seed.")) return false;
    const auto updated = Settings::ReadFile(legacy);
    if (!Check(updated.find("selected_seed = \"New\"") != std::string::npos
            && updated.find("seed = 4294967295") != std::string::npos
            && updated.find("name = \"Legacy\"") != std::string::npos,
            "Add Seed must save the new entry and selection together.")) return false;
    MapSeedSelector::SetOfflineFlowActive(false);
    if (!Check(gMapSeedOverrideActive == 0, "Leaving the Offline route must clear the override.")) {
        return false;
    }
    MapSeedSelector::SelectChoice(0);
    MapSeedSelector::SetOfflineFlowActive(true);
    return Check(gMapSeedOverrideActive == 0
            && Settings::ReadFile(legacy).find("selected_seed = \"character\"") != std::string::npos,
            "Off must preserve the game's map behavior and persist its selection.");
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path()
        / (L"mapsense-seed-tests-" + std::to_wstring(GetCurrentProcessId()));
    if (!std::filesystem::create_directory(directory)) return 1;
    const bool result = Run(directory / L"ruffneckk-mapsense.toml");
    MapSeedSelector::Shutdown();
    std::filesystem::remove(directory / L"map-seed-selector.toml");
    std::filesystem::remove(directory);
    return result ? 0 : 1;
}
