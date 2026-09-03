#define NOMINMAX

#include <D2RLPlugin/api.h>

#include "extended_act_level_ids_policy.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RuffnecKk::ExtendedActLevelIds {
namespace {

using namespace ruffneckk::extended_act_level_ids;

constexpr wchar_t SingletonName[] =
    L"Local\\RuffnecKk.ExtendedActLevelIds.Singleton";

constexpr std::uintptr_t ResolveActFromLevelIdRva = 0x326710;
constexpr auto ResolveActFromLevelIdExpected = std::to_array<std::uint8_t>({
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,
    0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x8B,
    0xF2,0x0F,0xB6,0xD9,0xE8,0xB7,0x9F,0xFD,
    0xFF,0x0F,0xB6,0xCB,0x48,0x8B,0xF8,0xE8,
    0x5C,0xA3,0xFD,0xFF,0x8B,0x98,0x08,0x01,
    0x00,0x00,0x83,0xEB,0x01,0x78,0x43,0x90,
});

using ResolveActFromLevelIdFn = std::uint8_t(__fastcall*)(
    std::uint8_t dataContext,
    std::int32_t levelId) noexcept;

struct ActMap {
    std::uint64_t revision{};
    std::vector<ActEntry> entries;
};

const D2RL::PluginContext* Context{};
const D2RL::DataTableServiceV1* DataTables{};
std::string RuntimeBuildName{"unknown"};
HANDLE SingletonHandle{};
ResolveActFromLevelIdFn OriginalResolveActFromLevelId{};

std::atomic_bool Operational{};
std::atomic_bool CacheReady{};
std::atomic_uint64_t PublishedRevision{};
std::atomic_uint64_t ResolvedFromLevels{};
std::atomic_uint64_t OriginalFallbacks{};
std::atomic<std::shared_ptr<const ActMap>> ClassicCache{};
std::atomic<std::shared_ptr<const ActMap>> LodCache{};
std::atomic<std::shared_ptr<const ActMap>> RotwCache{};

template <typename Value>
Value ReadValue(const void* base, std::size_t offset) noexcept {
    Value result{};
    if (base) {
        const auto* bytes = static_cast<const std::uint8_t*>(base);
        std::memcpy(&result, bytes + offset, sizeof(result));
    }
    return result;
}

const char* BankName(D2RL::DataTables::Bank bank) noexcept {
    switch (bank) {
    case D2RL::DataTables::Bank::Classic: return "Classic";
    case D2RL::DataTables::Bank::Lod: return "Lod";
    case D2RL::DataTables::Bank::Rotw: return "RotW";
    default: return "Unknown";
    }
}

D2RL::DataTables::Bank BankForContext(
        std::uint8_t dataContext) noexcept {
    switch (dataContext) {
    case 1: return D2RL::DataTables::Bank::Classic;
    case 2: return D2RL::DataTables::Bank::Lod;
    case 3: return D2RL::DataTables::Bank::Rotw;
    default: return static_cast<D2RL::DataTables::Bank>(0);
    }
}

std::shared_ptr<const ActMap> LoadCache(
        std::uint8_t dataContext) noexcept {
    switch (dataContext) {
    case 1: return ClassicCache.load(std::memory_order_acquire);
    case 2: return LodCache.load(std::memory_order_acquire);
    case 3: return RotwCache.load(std::memory_order_acquire);
    default: return {};
    }
}

void ResetCaches() noexcept {
    CacheReady.store(false, std::memory_order_release);
    ClassicCache.store({}, std::memory_order_release);
    LodCache.store({}, std::memory_order_release);
    RotwCache.store({}, std::memory_order_release);
    PublishedRevision.store(0, std::memory_order_release);
}

bool AcquireSingleton() noexcept {
    SingletonHandle = CreateMutexW(nullptr, FALSE, SingletonName);
    if (!SingletonHandle) {
        Context->LogError(
            "ExtendedActLevelIds: process singleton could not be created.");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(SingletonHandle);
        SingletonHandle = nullptr;
        Context->LogError(
            "ExtendedActLevelIds: duplicate global/mod-local installation refused.");
        return false;
    }
    return true;
}

void ReleaseSingleton() noexcept {
    if (SingletonHandle) {
        CloseHandle(SingletonHandle);
        SingletonHandle = nullptr;
    }
}

bool ValidateRuntime() noexcept {
    const auto* base = Context
        ? reinterpret_cast<const std::uint8_t*>(Context->exeBase)
        : nullptr;
    if (base && std::memcmp(
            base + ResolveActFromLevelIdRva,
            ResolveActFromLevelIdExpected.data(),
            ResolveActFromLevelIdExpected.size()) == 0) {
        return true;
    }
    Context->LogError(
        "ExtendedActLevelIds: central act resolver fingerprint mismatch; plugin refused.");
    return false;
}

bool QueryServices(
        const D2RL::LifecycleServiceV1*& lifecycle) noexcept {
    const D2RL::DataTableServiceV1* tables{};
    if (Context->QueryService(
            D2RL::ServiceId::DataTable,
            D2RL::DataTableServiceV1Version,
            &tables) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceV1Field(
                tables,
                D2RL::DataTableServiceV1RequiredSize)) {
        Context->LogError(
            "ExtendedActLevelIds: DataTableServiceV1 is unavailable or incompatible.");
        return false;
    }
    DataTables = tables;

    const D2RL::LifecycleServiceV1* lifecycleService{};
    if (Context->QueryService(
            D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version,
            &lifecycleService) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(
                lifecycleService,
                D2RL::LifecycleServiceV1RequiredSize)) {
        Context->LogError(
            "ExtendedActLevelIds: LifecycleServiceV1 is unavailable or incompatible.");
        return false;
    }
    lifecycle = lifecycleService;
    return true;
}

std::shared_ptr<const ActMap> BuildBankCache(
        const D2RL::PluginContext* context,
        D2RL::DataTables::Bank bank,
        std::uint64_t revision,
        std::string& error) {
    D2RL::DataTables::TableView table{
        .structSize = D2RL::DataTables::TableViewSize,
    };
    const auto tableResult = DataTables->getTable(
        context,
        bank,
        D2RL::DataTables::TableId::Levels,
        &table);
    if (tableResult != D2RL::DataTables::Result::Success
            || table.revision != revision
            || table.rows == nullptr
            || table.rowCount == 0
            || table.rowSize != LevelsRowSize) {
        error = "invalid Levels table view";
        return {};
    }

    auto map = std::make_shared<ActMap>();
    map->revision = revision;
    map->entries.reserve(table.rowCount);
    for (std::uint32_t rowIndex = 0; rowIndex < table.rowCount; ++rowIndex) {
        D2RL::DataTables::RowView physical{
            .structSize = D2RL::DataTables::RowViewSize,
        };
        if (DataTables->getRow(
                context,
                bank,
                D2RL::DataTables::TableId::Levels,
                rowIndex,
                &physical) != D2RL::DataTables::Result::Success
                || physical.revision != revision
                || physical.row == nullptr
                || physical.rowIndex != rowIndex
                || physical.rowSize != LevelsRowSize) {
            error = "physical Levels row lookup failed";
            return {};
        }

        const auto levelId = ReadValue<std::int32_t>(
            physical.row,
            LevelsIdOffset);
        const auto act = ReadValue<std::uint8_t>(
            physical.row,
            LevelsActOffset);
        if (levelId < 0 || act > MaximumAct) {
            error = "Levels row contains an invalid Id or Act";
            return {};
        }

        D2RL::DataTables::RowView keyed{
            .structSize = D2RL::DataTables::RowViewSize,
        };
        if (DataTables->findRowById(
                context,
                bank,
                D2RL::DataTables::TableId::Levels,
                static_cast<std::uint32_t>(levelId),
                &keyed) != D2RL::DataTables::Result::Success
                || keyed.revision != revision
                || keyed.row != physical.row
                || keyed.rowIndex != rowIndex
                || keyed.rowSize != LevelsRowSize) {
            error = "Levels Id layout failed the service round-trip";
            return {};
        }
        map->entries.push_back({levelId, act});
    }

    std::sort(map->entries.begin(), map->entries.end());
    if (std::adjacent_find(
            map->entries.begin(),
            map->entries.end(),
            [](const ActEntry& left, const ActEntry& right) {
                return left.levelId == right.levelId;
            }) != map->entries.end()) {
        error = "Levels contains duplicate Id values";
        return {};
    }
    if (!HasValidAnchorActs(std::span<const ActEntry>(map->entries))) {
        error = "Levels Act layout failed the anchor check";
        return {};
    }
    return map;
}

void __cdecl OnDataTablesLoaded(
        const D2RL::PluginContext* context,
        const D2RL::Lifecycle::DataTablesLoadedEvent* event,
        void*) noexcept {
    CacheReady.store(false, std::memory_order_release);
    try {
        if (!context
                || !DataTables
                || !D2RL::Lifecycle::HasDataTablesLoadedEventField(
                    event,
                    D2RL::Lifecycle::DataTablesLoadedEventRequiredSize)) {
            ResetCaches();
            if (context) context->LogError(
                "ExtendedActLevelIds: invalid DataTablesLoaded event; original resolver retained.");
            return;
        }

        std::array<std::shared_ptr<const ActMap>, 3> maps;
        constexpr std::array<D2RL::DataTables::Bank, 3> banks{
            D2RL::DataTables::Bank::Classic,
            D2RL::DataTables::Bank::Lod,
            D2RL::DataTables::Bank::Rotw,
        };
        for (std::size_t index = 0; index < banks.size(); ++index) {
            std::string error;
            maps[index] = BuildBankCache(
                context,
                banks[index],
                event->revision,
                error);
            if (!maps[index]) {
                ResetCaches();
                const auto message = std::string(
                    "ExtendedActLevelIds: ") + BankName(banks[index])
                    + " cache rejected (" + error
                    + "); original resolver retained.";
                context->LogError(message.c_str());
                return;
            }
        }

        ClassicCache.store(maps[0], std::memory_order_release);
        LodCache.store(maps[1], std::memory_order_release);
        RotwCache.store(maps[2], std::memory_order_release);
        PublishedRevision.store(event->revision, std::memory_order_release);
        CacheReady.store(true, std::memory_order_release);

        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "ExtendedActLevelIds: Levels revision %llu accepted; rows Classic=%zu, LoD=%zu, RotW=%zu.",
            static_cast<unsigned long long>(event->revision),
            maps[0]->entries.size(),
            maps[1]->entries.size(),
            maps[2]->entries.size());
        context->LogInfo(message);
    } catch (const std::exception& exception) {
        ResetCaches();
        const auto message = std::string(
            "ExtendedActLevelIds: cache build failed (")
            + exception.what() + "); original resolver retained.";
        context->LogError(message.c_str());
    } catch (...) {
        ResetCaches();
        context->LogError(
            "ExtendedActLevelIds: cache build failed; original resolver retained.");
    }
}

std::uint8_t __fastcall HookResolveActFromLevelId(
        std::uint8_t dataContext,
        std::int32_t levelId) noexcept {
    if (Operational.load(std::memory_order_acquire)
            && CacheReady.load(std::memory_order_acquire)
            && IsSupportedDataContext(dataContext)
            && BankForContext(dataContext)
                != static_cast<D2RL::DataTables::Bank>(0)) {
        const auto cache = LoadCache(dataContext);
        if (cache) {
            const auto act = FindAct(
                std::span<const ActEntry>(cache->entries),
                levelId);
            if (act) {
                ResolvedFromLevels.fetch_add(1, std::memory_order_relaxed);
                return *act;
            }
        }
    }
    OriginalFallbacks.fetch_add(1, std::memory_order_relaxed);
    return OriginalResolveActFromLevelId(dataContext, levelId);
}

std::string_view Trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool ParseInteger(
        std::string_view text,
        std::int32_t& value) noexcept {
    text = Trim(text);
    if (text.empty()) return false;
    const auto parsed = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value);
    return parsed.ec == std::errc{}
        && parsed.ptr == text.data() + text.size();
}

auto ResolveProbe(
        const D2RL::ConsoleCommandContext* command,
        std::string_view arguments) noexcept
        -> D2RL::ConsoleCommandResult {
    constexpr std::string_view verb{"resolve"};
    arguments = Trim(arguments);
    if (!arguments.starts_with(verb)
            || (arguments.size() > verb.size()
                && arguments[verb.size()] != ' ')) {
        return D2RL::ConsoleCommandResult::InvalidArguments;
    }
    arguments = Trim(arguments.substr(verb.size()));
    const auto separator = arguments.find_first_of(" \t");
    const auto levelText = separator == std::string_view::npos
        ? arguments
        : arguments.substr(0, separator);
    const auto contextText = separator == std::string_view::npos
        ? std::string_view{}
        : Trim(arguments.substr(separator + 1));

    std::int32_t levelId{};
    std::int32_t parsedContext{3};
    if (!ParseInteger(levelText, levelId)
            || (!contextText.empty()
                && !ParseInteger(contextText, parsedContext))
            || parsedContext < MinimumDataContext
            || parsedContext > MaximumDataContext
            || levelId < 0) {
        return D2RL::ConsoleCommandResult::InvalidArguments;
    }
    if (!Operational.load(std::memory_order_acquire)
            || !CacheReady.load(std::memory_order_acquire)
            || !Context
            || !Context->exeBase) {
        command->plugin->WriteConsoleMessage(
            "Extended Act Level IDs: resolver is not operational.",
            D2RL::ConsoleMessageKind::Error);
        return D2RL::ConsoleCommandResult::Failed;
    }

    const auto dataContext = static_cast<std::uint8_t>(parsedContext);
    const auto cache = LoadCache(dataContext);
    const auto cachedAct = cache
        ? FindAct(std::span<const ActEntry>(cache->entries), levelId)
        : std::nullopt;
    const auto resolver = reinterpret_cast<ResolveActFromLevelIdFn>(
        Context->exeBase + ResolveActFromLevelIdRva);
    const auto resolvedAct = resolver(dataContext, levelId);

    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "Extended Act Level IDs: Level Id %d resolved to Act index %u (Act %u), data context %u, source=%s.",
        levelId,
        static_cast<unsigned>(resolvedAct),
        static_cast<unsigned>(resolvedAct) + 1,
        static_cast<unsigned>(dataContext),
        cachedAct ? "Levels.txt" : "original resolver");
    Context->LogInfo(message);
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

auto Status(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    const auto arguments = command->args
        ? Trim(std::string_view(command->args, command->argsLength))
        : std::string_view{};
    if (!arguments.empty()) return ResolveProbe(command, arguments);
    const auto classic = ClassicCache.load(std::memory_order_acquire);
    const auto lod = LodCache.load(std::memory_order_acquire);
    const auto rotw = RotwCache.load(std::memory_order_acquire);
    char message[768]{};
    std::snprintf(
        message,
        sizeof(message),
        "Extended Act Level IDs 1.0.0: %s; cache=%s; revision=%llu; rows=%zu/%zu/%zu; Levels resolutions=%llu; original fallbacks=%llu; build=%s.",
        Operational.load(std::memory_order_acquire) ? "active" : "inactive",
        CacheReady.load(std::memory_order_acquire) ? "ready" : "not ready",
        static_cast<unsigned long long>(
            PublishedRevision.load(std::memory_order_acquire)),
        classic ? classic->entries.size() : 0,
        lod ? lod->entries.size() : 0,
        rotw ? rotw->entries.size() : 0,
        static_cast<unsigned long long>(
            ResolvedFromLevels.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            OriginalFallbacks.load(std::memory_order_relaxed)),
        RuntimeBuildName.c_str());
    command->plugin->WriteConsoleMessage(message);
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Operational.store(false, std::memory_order_release);
    ResetCaches();
    ResolvedFromLevels.store(0, std::memory_order_relaxed);
    OriginalFallbacks.store(0, std::memory_order_relaxed);
    DataTables = nullptr;
    OriginalResolveActFromLevelId = nullptr;
}

} // namespace
} // namespace RuffnecKk::ExtendedActLevelIds

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-extended-act-level-ids",
    .name = "Extended Act Level IDs",
    .version = "1.0.0",
    .author = "RuffnecKk",
    .description = "Allows new levels to belong to any act.",
    .flags = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    using namespace RuffnecKk::ExtendedActLevelIds;
    Context = context;
    ResetState();
    if (!Context || !Context->exeBase) return false;
    if (!AcquireSingleton()) return false;

    const auto* runtimeBuild = D2RL::GetBuildName(Context);
    RuntimeBuildName = runtimeBuild ? runtimeBuild : "unknown";
    if (!Context->RegisterConsoleCommand(
            "extended-act-level-ids",
            Status,
            "Show status or resolve a cached Level ID.")) {
        Context->LogWarn(
            "ExtendedActLevelIds: status command could not be registered.");
    }
    if (!ValidateRuntime()) {
        ReleaseSingleton();
        return false;
    }

    const D2RL::LifecycleServiceV1* lifecycle{};
    if (!QueryServices(lifecycle)) {
        ReleaseSingleton();
        return false;
    }
    const D2RL::Lifecycle::DataTablesLoadedListener listener{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
        .flags = 0,
        .callback = OnDataTablesLoaded,
        .userData = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle listenerHandle{
        D2RL::Lifecycle::InvalidHandle};
    if (lifecycle->registerDataTablesLoadedListener(
            Context,
            &listener,
            &listenerHandle) != D2RL::Lifecycle::Result::Success
            || listenerHandle == D2RL::Lifecycle::InvalidHandle) {
        Context->LogError(
            "ExtendedActLevelIds: DataTablesLoaded listener registration failed.");
        ReleaseSingleton();
        return false;
    }

    if (!Context->InstallInlineHook(
            ResolveActFromLevelIdRva,
            ResolveActFromLevelIdExpected.data(),
            static_cast<std::uint32_t>(
                ResolveActFromLevelIdExpected.size()),
            HookResolveActFromLevelId,
            &OriginalResolveActFromLevelId)) {
        Context->LogError(
            "ExtendedActLevelIds: central resolver hook is already owned or unavailable.");
        ReleaseSingleton();
        return false;
    }

    Operational.store(true, std::memory_order_release);
    const auto message = std::string(
        "Extended Act Level IDs 1.0.0 by RuffnecKk active; native fingerprint accepted; build=")
        + RuntimeBuildName + ".";
    Context->LogInfo(message.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace RuffnecKk::ExtendedActLevelIds;
    Operational.store(false, std::memory_order_release);
    ResetCaches();
    ReleaseSingleton();
}
