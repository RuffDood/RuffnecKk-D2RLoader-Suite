#include <D2RLPlugin/api.h>

#include "resistance_floor_policy.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

extern "C" void ResistanceFloorFirstMidHook();
extern "C" void ResistanceFloorSecondMidHook();
extern "C" void* gResistanceFloorFirstContinuation{};
extern "C" void* gResistanceFloorSecondContinuation{};

namespace {

using namespace ruffneckk::resistance_floor;

constexpr wchar_t ConfigFileName[] = L"ruffneckk-resistance-floor.toml";
constexpr std::size_t MaximumOwnerDepth = 8;
constexpr std::size_t RelayStride = 32;
constexpr std::size_t RelayBytes = RelayStride * 2;
constexpr std::uint32_t FloorPatchSize = 5;

constexpr char DefaultConfig[] = R"toml(# Resistance Floor
# Choose how low each group can push its six damage resistances.

# File format version. Leave this value unchanged.
config_version = 3
enabled = true

[players]
# Your characters.
enabled = true
minimum_resistance = -1000

[companions]
# Your mercenaries, summons, pets, Revives and converted monsters.
enabled = true
minimum_resistance = -1000

[monsters]
# Enemies and other monsters that do not belong to a player.
# They keep the normal -100 minimum until you enable this option.
enabled = false
minimum_resistance = -1000

[character_screen]
# Lets Fire, Lightning, Cold and Poison display values lower than -100.
show_resistances_below_minus_100 = true

[troubleshooting]
# Adds usage counters to the resistance-floor console status.
show_usage_counters = false
)toml";

constexpr std::uintptr_t FirstFloorSiteRva = 0x4524C4;
constexpr std::uintptr_t FirstFloorContinuationRva = 0x4524C9;
constexpr std::uintptr_t SecondFloorSiteRva = 0x4524E7;
constexpr std::uintptr_t SecondFloorContinuationRva = 0x4524EC;
constexpr std::uintptr_t CharacterDisplayWitnessRva = 0x14E728C;
constexpr std::uintptr_t CharacterDisplayFloorOperandRva = 0x14E729A;
constexpr std::uintptr_t GetUnitTypeRva = 0x34B9D0;
constexpr std::uintptr_t GetMinionOwnerRva = 0x4A53C0;

constexpr std::array<std::uint8_t, 12> FirstFloorSiteExpected{
    0xB9, 0x9C, 0xFF, 0xFF, 0xFF, 0x3B,
    0xD9, 0x0F, 0x4F, 0xCB, 0xEB, 0x51,
};
constexpr std::array<std::uint8_t, 12> SecondFloorSiteExpected{
    0xB9, 0x9C, 0xFF, 0xFF, 0xFF, 0x3B,
    0xD9, 0x0F, 0x4F, 0xCB, 0x8B, 0xD8,
};
constexpr std::array<std::uint8_t, 18> CharacterDisplayWitnessExpected{
    0x8D, 0x4F, 0x4B, 0x83, 0xF9, 0x5F,
    0x7C, 0x05, 0xB9, 0x5F, 0x00, 0x00,
    0x00, 0xB8, 0x9C, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 4> VanillaFloorOperandExpected{
    0x9C, 0xFF, 0xFF, 0xFF,
};
constexpr std::array<std::uint8_t, 32> GetUnitTypeExpected{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75,
    0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
};
constexpr std::array<std::uint8_t, 36> GetMinionOwnerExpected{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xD9, 0xE8, 0x02, 0x66, 0xEA, 0xFF, 0x83, 0xF8,
    0x01, 0x0F, 0x85, 0x9E, 0x00, 0x00, 0x00, 0x48,
    0x85, 0xDB, 0x74, 0x0D, 0x48, 0x8B, 0xCB, 0xE8,
    0xEC, 0x65, 0xEA, 0xFF,
};

using GetUnitTypeFn = std::int32_t(__fastcall*)(void*) noexcept;
using GetMinionOwnerFn = void*(__fastcall*)(void*) noexcept;

const D2RL::PluginContext* Context{};
std::uint8_t* Base{};
Config Settings{};
std::string LoadedConfigPath{"embedded defaults"};
void* RelayPage{};
GetUnitTypeFn GetUnitType{};
GetMinionOwnerFn GetMinionOwner{};
std::atomic_bool Operational{};
std::atomic<std::uint64_t> PlayerSelections{};
std::atomic<std::uint64_t> PlayerOwnedSelections{};
std::atomic<std::uint64_t> MonsterSelections{};
std::atomic<std::uint64_t> VanillaFallbacks{};

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "resistance-floor",
    .name = "Resistance Floor",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Lets configured units fall below the vanilla resistance floor.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

void TraceLoad(const char* message, bool reset = false) noexcept {
    if (!Context || !Context->pluginLogPath
            || Context->pluginLogPath[0] == L'\0' || !message) {
        return;
    }
    const auto handle = CreateFileW(
        Context->pluginLogPath,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        reset ? CREATE_ALWAYS : OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) return;
    if (!reset) {
        (void)SetFilePointer(handle, 0, nullptr, FILE_END);
    }
    char line[1024]{};
    const auto length = std::snprintf(
        line,
        sizeof(line),
        "[%lu] %s\r\n",
        static_cast<unsigned long>(GetCurrentProcessId()),
        message);
    if (length > 0) {
        DWORD written{};
        const auto bounded = static_cast<DWORD>(std::min<std::size_t>(
            static_cast<std::size_t>(length), sizeof(line) - 1));
        (void)WriteFile(handle, line, bounded, &written, nullptr);
    }
    CloseHandle(handle);
}

template<class Function>
auto At(std::uintptr_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

template<std::size_t Size>
auto Matches(
        std::uintptr_t rva,
        const std::array<std::uint8_t, Size>& expected) noexcept -> bool {
    return Base != nullptr
        && std::memcmp(Base + rva, expected.data(), expected.size()) == 0;
}

auto ConfigCandidates() -> std::vector<std::filesystem::path> {
    std::filesystem::path activeModConfigDirectory;
    std::filesystem::path scopeConfigDirectory;
    if (Context && Context->activeMod && Context->activeMod[0] != '\0'
            && Context->modSupportDirectory
            && Context->modSupportDirectory[0] != L'\0') {
        activeModConfigDirectory =
            std::filesystem::path(Context->modSupportDirectory) / L"config";
    }
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        scopeConfigDirectory =
            std::filesystem::path(Context->pluginConfigPath).parent_path();
    }
    std::error_code currentPathError;
    const auto currentPath = std::filesystem::current_path(currentPathError);
    const auto globalConfigDirectory = currentPathError
        ? std::filesystem::path{}
        : currentPath / L"d2rloader" / L"config";
    return BuildConfigCandidates(
        activeModConfigDirectory,
        scopeConfigDirectory,
        globalConfigDirectory,
        ConfigFileName);
}

auto MaterializeDefaultConfig(
        const std::vector<std::filesystem::path>& candidates) noexcept -> bool {
    std::filesystem::path path;
    if (Context && Context->pluginConfigPath
            && Context->pluginConfigPath[0] != L'\0') {
        path = std::filesystem::path(
            Context->pluginConfigPath).parent_path() / ConfigFileName;
    } else if (!candidates.empty()) {
        path = candidates.front();
    }
    if (path.empty()) return false;
    try {
        std::error_code directoryError;
        std::filesystem::create_directories(path.parent_path(), directoryError);
        if (directoryError) return false;
        const auto handle = CreateFileW(
            path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const auto ok = WriteFile(
            handle, DefaultConfig, static_cast<DWORD>(sizeof(DefaultConfig) - 1),
            &written, nullptr) != FALSE
            && written == sizeof(DefaultConfig) - 1;
        CloseHandle(handle);
        if (!ok) {
            (void)DeleteFileW(path.c_str());
            return false;
        }
        LoadedConfigPath = path.string();
        return true;
    } catch (...) {
        return false;
    }
}

auto LoadConfig() noexcept -> bool {
    Settings = {};
    LoadedConfigPath = "embedded defaults";
    const auto candidates = ConfigCandidates();
    for (const auto& path : candidates) {
        std::error_code statusError;
        if (!std::filesystem::is_regular_file(path, statusError)) continue;
        try {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open()) {
                throw std::runtime_error("configuration file cannot be opened");
            }
            const std::string text{
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()};
            Config parsed{};
            std::string error;
            if (!ParseToml(text, parsed, error)) {
                throw std::invalid_argument(error);
            }
            Settings = parsed;
            LoadedConfigPath = path.string();
            return true;
        } catch (const std::exception& exception) {
            const auto message = std::string("ResistanceFloor: invalid ")
                + path.string() + " (" + exception.what() + ").";
            Context->LogError(message.c_str());
            TraceLoad(message.c_str());
            return false;
        }
    }
    if (MaterializeDefaultConfig(candidates)) {
        const auto message = std::string(
            "ResistanceFloor: created default configuration at ")
            + LoadedConfigPath + ".";
        Context->LogInfo(message.c_str());
    } else {
        Context->LogWarn(
            "ResistanceFloor: no TOML was found or created; embedded defaults are active.");
    }
    return true;
}

auto ValidateCoreRuntime() noexcept -> bool {
    struct Check {
        bool matched;
        const char* label;
    };
    const Check checks[]{
        {Matches(FirstFloorSiteRva, FirstFloorSiteExpected),
         "first resistance-floor clamp"},
        {Matches(SecondFloorSiteRva, SecondFloorSiteExpected),
         "second resistance-floor clamp"},
        {Matches(GetUnitTypeRva, GetUnitTypeExpected), "UNITS_GetUnitType"},
        {Matches(GetMinionOwnerRva, GetMinionOwnerExpected),
         "D2GAME_GetMinionOwner"},
    };
    for (const auto& check : checks) {
        if (check.matched) continue;
        char message[256]{};
        std::snprintf(
            message, sizeof(message),
            "ResistanceFloor: %s signature mismatch; plugin refused.",
            check.label);
        Context->LogError(message);
        TraceLoad(message);
        return false;
    }
    if (Settings.display.syncCharacterScreen
            && !Matches(
                CharacterDisplayWitnessRva,
                CharacterDisplayWitnessExpected)) {
        Context->LogError(
            "ResistanceFloor: Character Screen resistance signature mismatch; plugin refused.");
        TraceLoad(
            "ResistanceFloor: Character Screen resistance signature mismatch; plugin refused.");
        return false;
    }
    return true;
}

auto AllocateNear(void* hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto aligned = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity;
            delta < 0x70000000ULL; delta += granularity) {
        if (aligned > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(
                reinterpret_cast<std::uintptr_t>(hint), candidate)) {
            break;
        }
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(candidate), size,
                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

auto WriteAbsoluteJump(std::uint8_t* destination, const void* target) noexcept
        -> bool {
    if (!destination || !target) return false;
    destination[0] = 0xFF;
    destination[1] = 0x25;
    destination[2] = destination[3] = destination[4] = destination[5] = 0;
    const auto address = reinterpret_cast<std::uint64_t>(target);
    std::memcpy(destination + 6, &address, sizeof(address));
    return true;
}

auto InstallFloorRelays() noexcept -> bool {
    RelayPage = AllocateNear(Base + FirstFloorSiteRva, RelayBytes);
    if (!RelayPage) {
        Context->LogError(
            "ResistanceFloor: no executable relay page was available within rel32 reach.");
        TraceLoad(
            "ResistanceFloor: no executable relay page was available within rel32 reach.");
        return false;
    }
    auto* relays = static_cast<std::uint8_t*>(RelayPage);
    if (!WriteAbsoluteJump(
            relays,
            reinterpret_cast<const void*>(&ResistanceFloorFirstMidHook))
            || !WriteAbsoluteJump(
                relays + RelayStride,
                reinterpret_cast<const void*>(&ResistanceFloorSecondMidHook))) {
        return false;
    }
    DWORD previousProtection{};
    if (!VirtualProtect(
            relays, RelayBytes, PAGE_EXECUTE_READ, &previousProtection)) {
        Context->LogError(
            "ResistanceFloor: relay page protection could not be finalized.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relays, RelayBytes);

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(relays);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress
            || !CanEncodeRel32(baseAddress + FirstFloorSiteRva, relayAddress)
            || !CanEncodeRel32(
                baseAddress + SecondFloorSiteRva,
                relayAddress + RelayStride)) {
        Context->LogError(
            "ResistanceFloor: relay displacement validation failed.");
        return false;
    }
    const auto relayRva = relayAddress - baseAddress;
    gResistanceFloorFirstContinuation = Base + FirstFloorContinuationRva;
    gResistanceFloorSecondContinuation = Base + SecondFloorContinuationRva;
    if (!Context->PatchJmpRel32(
            FirstFloorSiteRva,
            FirstFloorSiteExpected.data(),
            FloorPatchSize,
            relayRva,
            FloorPatchSize)) {
        Context->LogError(
            "ResistanceFloor: first resistance-floor relay was refused.");
        TraceLoad(
            "ResistanceFloor: first resistance-floor relay was refused.");
        return false;
    }
    TraceLoad("First resistance-floor relay installed.");
    if (!Context->PatchJmpRel32(
            SecondFloorSiteRva,
            SecondFloorSiteExpected.data(),
            FloorPatchSize,
            relayRva + RelayStride,
            FloorPatchSize)) {
        Context->LogError(
            "ResistanceFloor: second resistance-floor relay was refused.");
        TraceLoad(
            "ResistanceFloor: second resistance-floor relay was refused.");
        return false;
    }
    TraceLoad("Second resistance-floor relay installed.");
    return true;
}

auto InstallCharacterDisplayPatch() noexcept -> bool {
    if (!Settings.display.syncCharacterScreen) return true;
    const auto floor = SelectConfiguredFloor(
        Settings, UnitClass::Player, FireResistanceStat);
    if (floor == VanillaFloor) return true;
    if (!Context->PatchWriteU32(
            CharacterDisplayFloorOperandRva,
            VanillaFloorOperandExpected.data(),
            static_cast<std::uint32_t>(VanillaFloorOperandExpected.size()),
            static_cast<std::uint32_t>(floor))) {
        Context->LogError(
            "ResistanceFloor: Character Screen floor operand patch was refused.");
        TraceLoad(
            "ResistanceFloor: Character Screen floor operand patch was refused.");
        return false;
    }
    TraceLoad("Character Screen resistance-floor operand installed.");
    return true;
}

auto ClassifyDefender(void* defender) noexcept -> UnitClass {
    if (!defender || !GetUnitType || !GetMinionOwner) return UnitClass::Unknown;
    const auto initialType = GetUnitType(defender);
    if (initialType == 0) return UnitClass::Player;
    if (initialType != 1) return UnitClass::Unknown;

    std::array<void*, MaximumOwnerDepth> visited{};
    void* current = defender;
    for (std::size_t depth = 0; depth < MaximumOwnerDepth; ++depth) {
        for (std::size_t index = 0; index < depth; ++index) {
            if (visited[index] == current) return UnitClass::Unknown;
        }
        visited[depth] = current;
        void* owner = GetMinionOwner(current);
        if (!owner) return UnitClass::Monster;
        const auto ownerType = GetUnitType(owner);
        if (ownerType == 0) return UnitClass::PlayerOwned;
        if (ownerType != 1) return UnitClass::Unknown;
        current = owner;
    }
    return UnitClass::Unknown;
}

void CountSelection(UnitClass unitClass, std::int32_t floor) noexcept {
    if (!Settings.diagnostics) return;
    if (floor == VanillaFloor || unitClass == UnitClass::Unknown) {
        VanillaFallbacks.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    switch (unitClass) {
    case UnitClass::Player:
        PlayerSelections.fetch_add(1, std::memory_order_relaxed);
        break;
    case UnitClass::PlayerOwned:
        PlayerOwnedSelections.fetch_add(1, std::memory_order_relaxed);
        break;
    case UnitClass::Monster:
        MonsterSelections.fetch_add(1, std::memory_order_relaxed);
        break;
    default:
        VanillaFallbacks.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    char message[1024]{};
    std::snprintf(
        message,
        sizeof(message),
        "Resistance Floor 1.0.0: active=%s; players=%s/%d; companions=%s/%d; monsters=%s/%d; character-screen=%s; usage-counters=%s; selections=%llu/%llu/%llu; vanilla=%llu; config=%s.",
        Operational.load(std::memory_order_acquire) ? "true" : "false",
        Settings.players.enabled ? "on" : "off", Settings.players.floor,
        Settings.playerOwnedUnits.enabled ? "on" : "off",
        Settings.playerOwnedUnits.floor,
        Settings.monsters.enabled ? "on" : "off", Settings.monsters.floor,
        Settings.display.syncCharacterScreen ? "on" : "off",
        Settings.diagnostics ? "on" : "off",
        static_cast<unsigned long long>(
            PlayerSelections.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            PlayerOwnedSelections.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            MonsterSelections.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            VanillaFallbacks.load(std::memory_order_relaxed)),
        LoadedConfigPath.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    PlayerSelections.store(0, std::memory_order_relaxed);
    PlayerOwnedSelections.store(0, std::memory_order_relaxed);
    MonsterSelections.store(0, std::memory_order_relaxed);
    VanillaFallbacks.store(0, std::memory_order_relaxed);
}

} // namespace

extern "C" std::int32_t __fastcall ResistanceFloorSelectFloor(
        void* defender,
        std::int32_t resistanceStat) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !IsSupportedResistanceStat(resistanceStat)) {
        if (Settings.diagnostics) {
            VanillaFallbacks.fetch_add(1, std::memory_order_relaxed);
        }
        return VanillaFloor;
    }
    const auto unitClass = ClassifyDefender(defender);
    const auto floor = SelectConfiguredFloor(
        Settings, unitClass, resistanceStat);
    CountSelection(unitClass, floor);
    return floor;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    ResetState();
    TraceLoad("Resistance Floor 1.0.0 load started.", true);
    if (!Base) {
        TraceLoad("Load refused: D2R executable base is unavailable.");
        return false;
    }
    if (!LoadConfig()) {
        TraceLoad("Load refused: configuration validation failed.");
        return false;
    }
    TraceLoad("Configuration validated.");
    if (!context->RegisterConsoleCommand(
            "resistance-floor",
            Status,
            "Show the active Resistance Floor settings and usage counters.")) {
        context->LogWarn(
            "ResistanceFloor: optional status command was not registered.");
    }
    if (!Settings.enabled) {
        context->LogInfo(
            "Resistance Floor 1.0.0 by RuffnecKk loaded disabled; no patch was installed.");
        return true;
    }
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (!runtimeBuild
            || (std::strcmp(runtimeBuild, "92777") != 0
                && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "ResistanceFloor: only governed D2R builds 92777 and 93847 are supported.");
        TraceLoad("Load refused: unsupported D2R build identity.");
        return false;
    }
    TraceLoad("Governed D2R build accepted.");
    if (!ValidateCoreRuntime()) return false;
    TraceLoad("Core runtime signatures validated.");

    GetUnitType = At<GetUnitTypeFn>(GetUnitTypeRva);
    GetMinionOwner = At<GetMinionOwnerFn>(GetMinionOwnerRva);
    if (!InstallFloorRelays() || !InstallCharacterDisplayPatch()) return false;
    Operational.store(true, std::memory_order_release);
    TraceLoad("Gameplay and native display mutations are operational.");

    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Resistance Floor 1.0.0 by RuffnecKk active for D2R %s; players=%d; companions=%d; monsters=%s/%d; Character Screen=%s; installation=%s; TOML=%s.",
        runtimeBuild,
        SelectConfiguredFloor(Settings, UnitClass::Player, FireResistanceStat),
        SelectConfiguredFloor(
            Settings, UnitClass::PlayerOwned, FireResistanceStat),
        Settings.monsters.enabled ? "enabled" : "disabled",
        Settings.monsters.floor,
        Settings.display.syncCharacterScreen ? "enabled" : "disabled",
        context->loadScope == D2RL::LoadScope::Mod ? "mod-local" : "global",
        LoadedConfigPath.c_str());
    context->LogInfo(message);
    TraceLoad(message);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Operational.store(false, std::memory_order_release);
    GetUnitType = nullptr;
    GetMinionOwner = nullptr;
    Context = nullptr;
    Base = nullptr;
}
