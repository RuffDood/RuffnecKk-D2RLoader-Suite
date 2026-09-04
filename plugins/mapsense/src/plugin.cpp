#include <D2RLPlugin/api.h>

#include <Windows.h>
#include <shellapi.h>

#include "d3d12_imgui_host.hpp"
#include "external_label_provider.hpp"
#include "imgui_settings_panel.hpp"
#include "mapsense_config.hpp"
#include "mapsense_data_catalog.hpp"
#include "navigation_engine.hpp"
#include "navigation_resolver.hpp"
#include "native_automap_marker.hpp"
#include "native_automap_missile.hpp"
#include "native_automap_poi.hpp"
#include "native_ui_state.hpp"
#include "native_settings_policy.hpp"
#include "overlay_host_api.hpp"
#include "reveal_engine.hpp"
#include "sfilllocation_diagnostic.hpp"
#include "ui_localization.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace RuffnecKk::MapSense {
namespace {

constexpr std::size_t MaximumConfigBytes = 65'536;
constexpr std::uint32_t RevealReplayRetryLimit = 8U;
constexpr std::uint32_t RevealReplayRetryDelayMilliseconds = 250U;
constexpr std::uint32_t RevealReplayInitialDelayMilliseconds = 750U;
constexpr std::uint32_t ProgressiveRevealStepDelayMilliseconds = 8U;
constexpr std::uint32_t ProgressiveRevealSettlementDelayMilliseconds = 50U;
constexpr std::uint32_t ProgressiveRevealSettlementRetryLimit = 8U;
constexpr std::uint32_t ProgressiveRevealUiQueueRetryLimit = 8U;
constexpr std::uint32_t NativeAtlasCellsPerUiPump = 4'096U;
constexpr std::uint32_t NativeAtlasPumpDelayMilliseconds = 1U;
constexpr std::uint32_t NavigationRefreshRetryLimit = 8U;
constexpr std::uint32_t NavigationRefreshRetryDelayMilliseconds = 250U;
constexpr std::uint64_t DynamicNavigationRefreshIntervalMilliseconds = 1'000U;
constexpr std::uint64_t NativeUiPanelRefreshIntervalMilliseconds = 50U;

enum class Action {
    RevealZone,
    RevealAct,
    ArmRevealAll,
    ToggleRevealAll,
    DisableRevealAll,
    ToggleMenu,
};

std::array ActionTokens{
    Action::ToggleRevealAll,
    Action::ToggleMenu,
};

const D2RL::PluginContext* Context{};
const D2RL::InputServiceV1* InputService{};
const D2RL::LifecycleServiceV1* LifecycleService{};
const D2RL::ThreadServiceV1* ThreadService{};
Config Settings{};
std::atomic_bool Operational{};
std::atomic_bool FeaturesEnabled{true};
std::atomic_bool HostApiAvailable{};
std::atomic_bool GameplayReady{};
std::atomic_bool MenuExpanded{};
std::atomic_bool MarkerAvailable{};
std::atomic_bool PoiRuntimeAvailable{};
std::atomic_bool PoiAvailable{};
std::atomic_uint64_t SessionEpoch{1};
std::atomic_uint64_t CurrentSessionGeneration{};
std::atomic_uint32_t ActiveRevealMapSeed{};
std::atomic_int32_t ActiveRevealMapDifficulty{UnknownRevealDifficulty};
std::atomic_uint64_t LastDynamicNavigationRefreshTick{};
std::atomic_uint64_t LastNativeUiPanelRefreshRequestTick{};
std::atomic_bool NativeUiPanelRefreshQueued{};
std::vector<NavigationLineSnapshot> NavigationLineSnapshots;
std::vector<NativeAutomapMarkerSnapshot> MarkerSnapshots;
std::vector<NativeAutomapMissileSnapshot> MissileSnapshots;
std::vector<NativeAutomapPoiSnapshot> PoiSnapshots;
std::atomic<std::shared_ptr<const MapSenseDataCatalog>> DataCatalog{};
std::array<D2RL::Input::ActionHandle, ActionTokens.size()> ActionHandles{};
std::array<D2RL::Lifecycle::ListenerHandle, 5> LifecycleHandles{};

struct QueuedAction {
    Action action{};
    std::uint64_t sessionEpoch{};
};

constexpr std::size_t ActionQueueCapacity = 16;
std::array<QueuedAction, ActionQueueCapacity> ActionQueue{};
std::size_t ActionQueueHead{};
std::size_t ActionQueueSize{};
bool ActionDrainScheduled{};
std::atomic_flag ActionQueueLock = ATOMIC_FLAG_INIT;
HANDLE HostRetryStopEvent{};
HANDLE HostRetryWorker{};
std::mutex ConfigSaveMutex;
std::string PendingConfigSave;
bool ConfigSaveDrainScheduled{};
std::mutex DataCatalogLoadMutex;
std::mutex HostUiTaskMutex;
D3D12ImGuiUiTaskCallback PendingHostUiTask{};
void* PendingHostUiTaskData{};
bool HostUiTaskScheduled{};

struct NavigationRefreshState {
    std::uint64_t sessionGeneration{};
    std::int32_t levelId{UnknownNavigationLevelId};
    std::uint32_t retriesRemaining{};
    bool refreshRevealedActPoiDefinitions{};
    bool hasPendingTarget{};
    bool callbackQueued{};
};

std::mutex NavigationRefreshMutex;
NavigationRefreshState PendingNavigationRefresh{};
PTP_TIMER NavigationRefreshTimer{};

std::mutex RevealReplayMutex;
RevealPersistenceState RevealPersistence{};
RevealReplayRequestState PendingRevealReplay{};
PTP_TIMER RevealReplayTimer{};

enum class ProgressiveRevealPhase : std::uint8_t {
    Discover,
    Capture,
    Settle,
};

struct ProgressiveRevealState final {
    std::uint64_t sessionGeneration{};
    std::int32_t difficulty{UnknownRevealDifficulty};
    std::int32_t act{-1};
    ProgressiveRevealGraphState graph{};
    ProgressiveRevealPhase phase{ProgressiveRevealPhase::Discover};
    std::uint32_t settlementRetriesRemaining{};
    std::uint32_t uiQueueRetriesRemaining{};
    bool active{};
    bool callbackQueued{};
    bool failed{};
    std::uint64_t startedAt{};
    std::uint64_t maximumStepMilliseconds{};
    std::uint64_t maximumDiscoveryStepMilliseconds{};
    std::uint64_t maximumCaptureStepMilliseconds{};
    std::uint64_t namesReadyElapsedMilliseconds{};
    std::uint64_t capturedLevels{};
};

ProgressiveRevealState ProgressiveReveal{};
PTP_TIMER ProgressiveRevealTimer{};

std::mutex NativeAtlasPumpMutex;
PTP_TIMER NativeAtlasPumpTimer{};
bool NativeAtlasPumpRequested{};
bool NativeAtlasPumpCallbackQueued{};

[[nodiscard]] auto ProgressiveRevealLevelIdLocked() noexcept
        -> std::int32_t {
    if (ProgressiveReveal.phase == ProgressiveRevealPhase::Discover
        || ProgressiveReveal.phase == ProgressiveRevealPhase::Capture) {
        return ProgressiveReveal.graph.Current();
    }
    return UnknownRevealLevelId;
}
std::atomic_uint64_t AutomaticLevelRevealRequests{};
std::atomic_uint64_t AutomaticLevelRevealAccepted{};
std::atomic_uint64_t AutomaticLevelRevealRejected{};
std::atomic_uint64_t AutomaticLevelRevealDuplicateRefusals{};

class QueueLockGuard {
public:
    QueueLockGuard() noexcept {
        while (ActionQueueLock.test_and_set(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
    ~QueueLockGuard() {
        ActionQueueLock.clear(std::memory_order_release);
    }

    QueueLockGuard(const QueueLockGuard&) = delete;
    auto operator=(const QueueLockGuard&) -> QueueLockGuard& = delete;
};

auto TrimAndLower(const D2RL::ConsoleCommandContext* command) -> std::string {
    std::string text;
    if (command != nullptr && command->args != nullptr) {
        text.assign(command->args, command->argsLength);
    }
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1);
    for (char& character : text) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

auto LoadConfig(Config& config) noexcept -> bool {
    if (Context == nullptr) return false;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t requiredSize{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &requiredSize)) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense: configuration could not be read (required bytes=%u, limit=%zu).",
            requiredSize,
            buffer.size());
        Context->LogError(message);
        return false;
    }

    std::size_t length{};
    while (length < buffer.size() && buffer[length] != '\0') ++length;
    if (length == buffer.size()) {
        Context->LogError(
            "MapSense: configuration is not null-terminated or exceeds 64 KiB.");
        return false;
    }
    try {
        config = ParseConfig(std::string_view(buffer.data(), length));
        return true;
    } catch (const std::exception& exception) {
        const auto message = std::string("MapSense: invalid configuration (")
            + exception.what() + ").";
        Context->LogError(message.c_str());
        return false;
    } catch (...) {
        Context->LogError(
            "MapSense: invalid configuration (unknown parser failure).");
        return false;
    }
}

[[nodiscard]] constexpr auto DataCatalogFamilyStateName(
        DataCatalogFamilyState state) noexcept -> const char* {
    switch (state) {
        case DataCatalogFamilyState::Unavailable: return "unavailable";
        case DataCatalogFamilyState::ActiveTxt: return "active-txt";
        case DataCatalogFamilyState::VanillaFallbackTxt:
            return "vanilla-fallback";
        case DataCatalogFamilyState::BinaryOnlyConflict:
            return "binary-only-conflict";
        case DataCatalogFamilyState::Invalid: return "invalid";
    }
    return "unknown";
}

void LogDataCatalogLoad(
        const MapSenseDataCatalogLoadResult& result) noexcept {
    if (Context == nullptr) return;
    for (const auto& diagnostic : result.diagnostics) {
        const auto family = DataCatalogFamilyName(diagnostic.family);
        char message[1'024]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense data catalog [%.*s/%s]: %s",
            static_cast<int>(family.size()),
            family.data(),
            diagnostic.code.c_str(),
            diagnostic.message.c_str());
        switch (diagnostic.severity) {
            case DataCatalogDiagnosticSeverity::Info:
                if (Settings.diagnostics) Context->LogInfo(message);
                break;
            case DataCatalogDiagnosticSeverity::Warning:
                Context->LogWarn(message);
                break;
            case DataCatalogDiagnosticSeverity::Error:
                // A single unavailable family must not disable Reveal or any
                // other independently validated MapSense capability.
                Context->LogError(message);
                break;
        }
    }
    if (result.catalog == nullptr) {
        Context->LogWarn(
            "MapSense: the immutable TXT/localization catalog is unavailable; localized labels and data-driven objects are disabled while other features remain active.");
        return;
    }
    for (const auto& status : result.catalog->FamilyStatuses()) {
        if (!Settings.diagnostics && status.Available()) continue;
        const auto family = DataCatalogFamilyName(status.family);
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense data catalog: family=%.*s state=%s rows=%zu localized=%zu unresolved=%zu.",
            static_cast<int>(family.size()),
            family.data(),
            DataCatalogFamilyStateName(status.state),
            status.rowCount,
            status.localizedNameCount,
            status.unresolvedNameCount);
        if (status.Available()) {
            Context->LogInfo(message);
        } else {
            Context->LogWarn(message);
        }
    }
}

[[nodiscard]] auto CommandLineEnablesTxtMode() noexcept -> bool {
    int argumentCount{};
    auto** const arguments = CommandLineToArgvW(
        GetCommandLineW(),
        &argumentCount);
    if (arguments == nullptr) return false;
    auto enabled = false;
    for (int index = 1; index < argumentCount; ++index) {
        if (CompareStringOrdinal(
                arguments[index],
                -1,
                L"-txt",
                -1,
                TRUE) == CSTR_EQUAL) {
            enabled = true;
            break;
        }
    }
    LocalFree(arguments);
    return enabled;
}

[[nodiscard]] auto HasActiveTxtFamily(
        const MapSenseDataCatalog& catalog) noexcept -> bool {
    return std::any_of(
        catalog.FamilyStatuses().begin(),
        catalog.FamilyStatuses().end(),
        [](const auto& status) {
            return status.state == DataCatalogFamilyState::ActiveTxt;
        });
}

[[nodiscard]] auto BuildNativeAutomapPoiCollectionMask() noexcept
        -> std::uint32_t {
    if (!FeaturesEnabled.load(std::memory_order_acquire)
            || !Settings.objects.enabled) {
        return 0U;
    }
    std::uint32_t mask{};
    const auto enable = [&mask](
            bool enabled,
            AutomapPoiCollection collection) noexcept {
        if (enabled) mask |= AutomapPoiCollectionBit(collection);
    };
    enable(Settings.objects.exitLabels.enabled,
        AutomapPoiCollection::ExitLabels);
    enable(Settings.objects.waypointLabels.enabled,
        AutomapPoiCollection::WaypointLabels);
    enable(Settings.objects.shrineLabels.enabled,
        AutomapPoiCollection::ShrineLabels);
    enable(Settings.objects.chests.enabled,
        AutomapPoiCollection::Chests);
    enable(Settings.objects.superChests.enabled,
        AutomapPoiCollection::SuperChests);
    enable(Settings.objects.armorRacks.enabled,
        AutomapPoiCollection::ArmorRacks);
    enable(Settings.objects.weaponRacks.enabled,
        AutomapPoiCollection::WeaponRacks);
    return mask;
}

void ApplyNativeAutomapPoiCollectionSettings() noexcept {
    SetNativeAutomapPoiCollectionMask(
        BuildNativeAutomapPoiCollectionMask());
}

[[nodiscard]] auto HasResolvedCatalogLocalization(
        const MapSenseDataCatalog& catalog) noexcept -> bool {
    return catalog.HasLocalizationService()
        && catalog.HasVerifiedPlayerFacingLocalization();
}

[[nodiscard]] auto EnsureLocalizedDataCatalogReady(
        std::uint64_t sessionGeneration) noexcept -> bool {
    if (DataCatalog.load(std::memory_order_acquire) != nullptr) return true;

    std::scoped_lock lock(DataCatalogLoadMutex);
    if (DataCatalog.load(std::memory_order_acquire) != nullptr) return true;

    auto catalogLoad = MapSenseDataCatalog::Load(Context);
    LogDataCatalogLoad(catalogLoad);
    if (catalogLoad.catalog != nullptr
        && HasActiveTxtFamily(*catalogLoad.catalog)
        && !CommandLineEnablesTxtMode()) {
        if (Context != nullptr) {
            Context->LogWarn(
                "MapSense: active mod TXT catalogs require the D2R -txt launch argument; localized labels and data-driven objects are disabled to prevent TXT/BIN divergence.");
        }
        catalogLoad.catalog.reset();
    }
    if (catalogLoad.catalog == nullptr) return false;

    // D2RLoader publishes LocalizationService before D2R has populated its
    // language tables. A catalog built during plugin load therefore resolves
    // every entry to technical keys such as ShrId9. LocalPlayerReady is the
    // first safe lifecycle point, and this guard leaves the catalog pending
    // if a future runtime still reports no resolved player-facing string.
    if (!HasResolvedCatalogLocalization(*catalogLoad.catalog)) {
        if (Context != nullptr) {
            Context->LogWarn(
                "MapSense: D2R localization tables are not ready yet; labels and data-driven objects remain pending and will retry on the next player/level lifecycle event.");
        }
        return false;
    }

    if (RefreshUiLanguage(Context) && Context != nullptr) {
        const auto language = UiLanguageCode(CurrentUiLanguage());
        char message[128]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense: ImGui localization selected %.*s.",
            static_cast<int>(language.size()),
            language.data());
        Context->LogInfo(message);
    }

    auto catalog = std::move(catalogLoad.catalog);
    DataCatalog.store(catalog, std::memory_order_release);
    (void)BindNavigationResolverCatalog(catalog);
    const auto poiAvailable =
        PoiRuntimeAvailable.load(std::memory_order_acquire)
        && BindNativeAutomapPoiCatalog(catalog);
    PoiAvailable.store(poiAvailable, std::memory_order_release);
    if (poiAvailable) {
        ResetNativeAutomapPoiSession(sessionGeneration);
        ApplyNativeAutomapPoiCollectionSettings();
    } else if (Context != nullptr) {
        Context->LogWarn(
            "MapSense: localized names are ready, but native object collection is unavailable; boss names remain independently available.");
    }
    if (Context != nullptr) {
        Context->LogInfo(
            "MapSense: the localized TXT catalog is ready after D2R language initialization.");
    }
    return true;
}

[[nodiscard]] auto ResolveActiveLevelAct(
        std::int32_t levelId) noexcept -> std::int32_t {
    const auto catalog = DataCatalog.load(std::memory_order_acquire);
    if (catalog != nullptr) {
        const auto* const level = catalog->FindLevel(levelId);
        if (level != nullptr && level->act >= 0
            && level->act
                < static_cast<std::int32_t>(RevealPersistenceActCount)) {
            return level->act;
        }
    }
    return RevealActForLevelId(levelId);
}

void WriteOutcome(
        const D2RL::PluginContext* context,
        RevealOutcome outcome,
        Action action) noexcept {
    if (context == nullptr) return;
    const char* message = "MapSense: reveal request failed; enter a game and try again.";
    D2RL::ConsoleMessageKind kind = D2RL::ConsoleMessageKind::Warning;
    switch (outcome) {
        case RevealOutcome::Complete:
            if (action == Action::RevealAct) {
                message = "MapSense: current act revealed; distant names are being collected passively.";
            } else {
                message = "MapSense: current level revealed and remembered for new games at this difficulty.";
            }
            kind = D2RL::ConsoleMessageKind::Output;
            break;
        case RevealOutcome::Armed:
            message = "MapSense: Reveal Map enabled for this seed and difficulty.";
            kind = D2RL::ConsoleMessageKind::Output;
            break;
        case RevealOutcome::Disarmed:
            message = "MapSense: Reveal Map disabled; D2R keeps any native cells already revealed.";
            kind = D2RL::ConsoleMessageKind::Output;
            break;
        case RevealOutcome::Unavailable:
            break;
    }
    context->WriteConsoleMessage(message, kind);
    if (kind == D2RL::ConsoleMessageKind::Warning) {
        context->LogWarn(message);
    } else {
        context->LogInfo(message);
    }
}

auto ExecuteAction(Action action) noexcept -> RevealOutcome {
    switch (action) {
        case Action::RevealZone: return RevealCurrentZone();
        case Action::RevealAct: return RevealCurrentAct();
        case Action::ArmRevealAll: return ArmRevealAll();
        case Action::ToggleRevealAll: return ToggleRevealAll();
        case Action::DisableRevealAll: return DisableRevealAll();
        case Action::ToggleMenu: return RevealOutcome::Unavailable;
    }
    return RevealOutcome::Unavailable;
}

[[nodiscard]] constexpr auto IsRevealAllArmAction(
        Action action) noexcept -> bool {
    return action == Action::ArmRevealAll
        || action == Action::ToggleRevealAll;
}

[[nodiscard]] constexpr auto RevealActionName(Action action) noexcept
        -> const char* {
    switch (action) {
        case Action::RevealZone: return "reveal-zone";
        case Action::RevealAct: return "reveal-act";
        case Action::ArmRevealAll: return "arm-reveal-all";
        case Action::ToggleRevealAll: return "toggle-reveal-all";
        case Action::DisableRevealAll: return "disable-reveal-all";
        case Action::ToggleMenu: return "toggle-menu";
    }
    return "unknown";
}

constexpr auto IsIdempotent(Action action) noexcept -> bool {
    return action == Action::RevealZone
        || action == Action::RevealAct
        || action == Action::ArmRevealAll;
}

auto RequestNavigationRefresh(
    std::uint64_t sessionGeneration,
    std::int32_t levelId,
    bool refreshRevealedActPoiDefinitions = false) noexcept -> bool;
auto ExecuteTrackedRevealAction(Action action) noexcept -> RevealOutcome;
auto RequestRememberedRevealForCurrentSession(
    std::int32_t targetLevelId = UnknownRevealLevelId,
    std::uint32_t delayMilliseconds = 0U,
    bool automapObserved = false) noexcept -> bool;
auto HasPendingRevealWorkForLevel(
    std::int32_t levelId) noexcept -> bool;

void __cdecl DrainActionsOnUi(
        const D2RL::PluginContext* context,
        void*) noexcept {
    for (;;) {
        QueuedAction request{};
        {
            QueueLockGuard lock;
            if (ActionQueueSize == 0) {
                ActionDrainScheduled = false;
                return;
            }
            request = ActionQueue[ActionQueueHead];
            ActionQueueHead = (ActionQueueHead + 1) % ActionQueueCapacity;
            --ActionQueueSize;
        }
        if (!Operational.load(std::memory_order_acquire) || context == nullptr) {
            continue;
        }
        if (request.sessionEpoch
            != SessionEpoch.load(std::memory_order_acquire)) {
            continue;
        }
        if (request.action == Action::ToggleMenu) {
            if (!GameplayReady.load(std::memory_order_acquire)) {
                SetD3D12ImGuiMenuOpen(false);
                continue;
            }
            bool expanded = MenuExpanded.load(std::memory_order_acquire);
            while (!MenuExpanded.compare_exchange_weak(
                expanded,
                !expanded,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            }
            SetD3D12ImGuiMenuOpen(true);
            continue;
        }
        if (!FeaturesEnabled.load(std::memory_order_acquire)
                && request.action != Action::DisableRevealAll) {
            context->WriteConsoleMessage(
                "MapSense: features are disabled; open the MapSense panel to enable them.",
                D2RL::ConsoleMessageKind::Warning);
            continue;
        }
        const auto outcome = ExecuteTrackedRevealAction(request.action);
        WriteOutcome(context, outcome, request.action);
        const auto revealed = request.action == Action::RevealZone;
        const auto accepted = outcome == RevealOutcome::Complete
            || outcome == RevealOutcome::Armed;
        if (revealed && accepted
            && GameplayReady.load(std::memory_order_acquire)) {
            const auto sessionGeneration = CurrentSessionGeneration.load(
                std::memory_order_acquire);
            if (sessionGeneration != 0U
                && !RequestNavigationRefresh(
                    sessionGeneration,
                    UnknownNavigationLevelId,
                    false)
                && Settings.diagnostics) {
                context->LogWarn(
                    "MapSense navigation: the post-reveal exact-endpoint refresh could not be queued.");
            }
        }
    }
}

auto QueueAction(Action action) noexcept -> bool {
    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr) {
        return false;
    }

    const auto epoch = SessionEpoch.load(std::memory_order_acquire);
    bool scheduleDrain{};
    {
        QueueLockGuard lock;
        if (IsIdempotent(action)) {
            for (std::size_t index = 0; index < ActionQueueSize; ++index) {
                const auto& queued = ActionQueue[
                    (ActionQueueHead + index) % ActionQueueCapacity];
                if (queued.action == action && queued.sessionEpoch == epoch) {
                    return true;
                }
            }
        }
        if (ActionQueueSize == ActionQueueCapacity) return false;
        const auto tail = (ActionQueueHead + ActionQueueSize) % ActionQueueCapacity;
        ActionQueue[tail] = {.action = action, .sessionEpoch = epoch};
        ++ActionQueueSize;

        if (!ActionDrainScheduled) {
            ActionDrainScheduled = true;
            scheduleDrain = true;
        }
    }

    if (scheduleDrain) {
        if (ThreadService->runOnUiThread(Context, DrainActionsOnUi, nullptr)
            != D2RL::Threads::Result::Success) {
            QueueLockGuard lock;
            ActionQueueHead = 0;
            ActionQueueSize = 0;
            ActionDrainScheduled = false;
            return false;
        }
    }
    return true;
}

auto __cdecl OnAction(
        const D2RL::PluginContext* context,
        const D2RL::Input::ActionEvent* event,
        void* userData) noexcept -> D2RL::Input::ActionResult {
    if (!Operational.load(std::memory_order_acquire)
        || context == nullptr || userData == nullptr
        || !D2RL::Input::HasActionEventField(
            event, D2RL::Input::ActionEventRequiredSize)
        || event->kind != D2RL::Input::ActionEventKind::Pressed) {
        return D2RL::Input::ActionResult::Ignored;
    }
    const auto* token = static_cast<const Action*>(userData);
    const auto action = *token;
    const auto virtualKey = static_cast<std::uint32_t>(event->binding.key);
    const bool virtualKeyAccepted =
        MayTriggerMapSenseActionForVirtualKey(virtualKey);
    if (Settings.diagnostics) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense diagnostic Controls action: action=%u key=0x%02X modifier=%u accepted-by-key-policy=%d gameplay-ready=%d.",
            static_cast<unsigned>(action),
            static_cast<unsigned>(virtualKey),
            static_cast<unsigned>(event->binding.modifier),
            virtualKeyAccepted ? 1 : 0,
            GameplayReady.load(std::memory_order_acquire) ? 1 : 0);
        context->LogInfo(message);
    }
    if (!virtualKeyAccepted) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (action == Action::ToggleMenu
        && !GameplayReady.load(std::memory_order_acquire)) {
        return D2RL::Input::ActionResult::Ignored;
    }
    if (QueueAction(action)) {
        return D2RL::Input::ActionResult::Handled;
    }
    context->LogWarn("MapSense: the reveal action could not be queued on the UI thread.");
    return D2RL::Input::ActionResult::Ignored;
}

void CancelPendingNavigationRefresh(bool clearQueuedCallback = false) noexcept {
    std::scoped_lock lock(NavigationRefreshMutex);
    PendingNavigationRefresh.sessionGeneration = 0U;
    PendingNavigationRefresh.levelId = UnknownNavigationLevelId;
    PendingNavigationRefresh.retriesRemaining = 0U;
    PendingNavigationRefresh.refreshRevealedActPoiDefinitions = false;
    PendingNavigationRefresh.hasPendingTarget = false;
    if (clearQueuedCallback) {
        PendingNavigationRefresh.callbackQueued = false;
    }
    if (NavigationRefreshTimer != nullptr) {
        SetThreadpoolTimer(NavigationRefreshTimer, nullptr, 0U, 0U);
    }
}

void __cdecl RetryNavigationOnUi(
    const D2RL::PluginContext*,
    void*) noexcept;

void ScheduleNavigationRefreshTimer(
        std::uint32_t delayMilliseconds) noexcept {
    std::scoped_lock lock(NavigationRefreshMutex);
    if (NavigationRefreshTimer == nullptr) return;
    const auto boundedDelay = std::max(delayMilliseconds, 1U);
    ULARGE_INTEGER dueTime{};
    dueTime.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(boundedDelay) * 10'000LL);
    FILETIME due{
        .dwLowDateTime = dueTime.LowPart,
        .dwHighDateTime = dueTime.HighPart,
    };
    SetThreadpoolTimer(NavigationRefreshTimer, &due, 0U, 0U);
}

void ArmPendingNavigationRefresh(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        std::uint32_t delayMilliseconds,
        bool refreshRevealedActPoiDefinitions) noexcept {
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        const bool preserveActLabelRefresh =
            PendingNavigationRefresh.sessionGeneration == sessionGeneration
            && PendingNavigationRefresh.refreshRevealedActPoiDefinitions
            && (PendingNavigationRefresh.hasPendingTarget
                || PendingNavigationRefresh.callbackQueued);
        PendingNavigationRefresh.sessionGeneration = sessionGeneration;
        PendingNavigationRefresh.levelId = levelId;
        PendingNavigationRefresh.retriesRemaining
            = NavigationRefreshRetryLimit;
        PendingNavigationRefresh.refreshRevealedActPoiDefinitions
            = refreshRevealedActPoiDefinitions || preserveActLabelRefresh;
        PendingNavigationRefresh.hasPendingTarget = true;
    }
    ScheduleNavigationRefreshTimer(delayMilliseconds);
}

auto TryArmPendingNavigationRefresh(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        std::uint32_t delayMilliseconds) noexcept -> bool {
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        if (PendingNavigationRefresh.hasPendingTarget
            || PendingNavigationRefresh.callbackQueued) {
            return false;
        }
        PendingNavigationRefresh.sessionGeneration = sessionGeneration;
        PendingNavigationRefresh.levelId = levelId;
        PendingNavigationRefresh.retriesRemaining
            = NavigationRefreshRetryLimit;
        PendingNavigationRefresh.refreshRevealedActPoiDefinitions = false;
        PendingNavigationRefresh.hasPendingTarget = true;
    }
    ScheduleNavigationRefreshTimer(delayMilliseconds);
    return true;
}

auto SchedulePendingNavigationRefresh() noexcept -> bool {
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        if (!PendingNavigationRefresh.hasPendingTarget
            || PendingNavigationRefresh.callbackQueued) {
            return true;
        }
        PendingNavigationRefresh.callbackQueued = true;
    }

    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr
        || ThreadService->runOnUiThread(
            Context,
            RetryNavigationOnUi,
            nullptr) != D2RL::Threads::Result::Success) {
        std::scoped_lock lock(NavigationRefreshMutex);
        PendingNavigationRefresh.callbackQueued = false;
        return false;
    }
    return true;
}

auto RequestNavigationRefresh(
        std::uint64_t sessionGeneration,
        std::int32_t levelId,
        bool refreshRevealedActPoiDefinitions) noexcept -> bool {
    if (!FeaturesEnabled.load(std::memory_order_acquire)
            || !IsNavigationResolverActive()) {
        CancelPendingNavigationRefresh();
        return false;
    }
    const auto startedAt = GetTickCount64();
    const auto navigationResult = RefreshNavigationDestinations(
        sessionGeneration,
        levelId,
        Settings.navigation.customLevels.targets);
    auto result = navigationResult;
    auto labelResult = NavigationRefreshResult::Complete;
    if (refreshRevealedActPoiDefinitions) {
        labelResult = RefreshRevealedActPoiDefinitions(
            sessionGeneration);
        if (navigationResult == NavigationRefreshResult::Failed
                || labelResult == NavigationRefreshResult::Failed) {
            result = NavigationRefreshResult::Failed;
        } else if (navigationResult
                    == NavigationRefreshResult::PartialRetryable
                || labelResult
                    == NavigationRefreshResult::PartialRetryable) {
            result = NavigationRefreshResult::PartialRetryable;
        } else {
            result = NavigationRefreshResult::Complete;
        }
    }
    if (refreshRevealedActPoiDefinitions && Context != nullptr) {
        char timingMessage[256]{};
        std::snprintf(
            timingMessage,
            sizeof(timingMessage),
            "MapSense reveal timing: phase=navigation-initial session=%llu level=%d elapsed-ms=%llu navigation-result=%u labels-result=%u.",
            static_cast<unsigned long long>(sessionGeneration),
            levelId,
            static_cast<unsigned long long>(GetTickCount64() - startedAt),
            static_cast<unsigned>(navigationResult),
            static_cast<unsigned>(labelResult));
        Context->LogInfo(timingMessage);
    }
    if (result == NavigationRefreshResult::Complete) {
        CancelPendingNavigationRefresh();
        return true;
    }
    auto retryLevelId = levelId;
    if (retryLevelId == UnknownNavigationLevelId
        && result == NavigationRefreshResult::PartialRetryable) {
        retryLevelId = GetNavigationResolverCounters().lastLevelId;
    }
    ArmPendingNavigationRefresh(
        sessionGeneration,
        retryLevelId,
        NavigationRefreshRetryDelayMilliseconds,
        refreshRevealedActPoiDefinitions);
    return true;
}

void __cdecl RetryNavigationOnUi(
        const D2RL::PluginContext*,
        void*) noexcept {
    NavigationRefreshState attempt{};
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        PendingNavigationRefresh.callbackQueued = false;
        if (!PendingNavigationRefresh.hasPendingTarget) return;
        attempt = PendingNavigationRefresh;
    }

    if (!Operational.load(std::memory_order_acquire)
        || !FeaturesEnabled.load(std::memory_order_acquire)
        || !GameplayReady.load(std::memory_order_acquire)
        || !IsNavigationResolverActive()) {
        CancelPendingNavigationRefresh();
        return;
    }

    const auto startedAt = GetTickCount64();
    const auto navigationResult = RefreshNavigationDestinations(
        attempt.sessionGeneration,
        attempt.levelId,
        Settings.navigation.customLevels.targets);
    auto result = navigationResult;
    auto labelResult = NavigationRefreshResult::Complete;
    if (attempt.refreshRevealedActPoiDefinitions) {
        labelResult = RefreshRevealedActPoiDefinitions(
            attempt.sessionGeneration);
        if (navigationResult == NavigationRefreshResult::Failed
                || labelResult == NavigationRefreshResult::Failed) {
            result = NavigationRefreshResult::Failed;
        } else if (navigationResult
                    == NavigationRefreshResult::PartialRetryable
                || labelResult
                    == NavigationRefreshResult::PartialRetryable) {
            result = NavigationRefreshResult::PartialRetryable;
        } else {
            result = NavigationRefreshResult::Complete;
        }
    }
    if (attempt.refreshRevealedActPoiDefinitions && Context != nullptr) {
        char timingMessage[256]{};
        std::snprintf(
            timingMessage,
            sizeof(timingMessage),
            "MapSense reveal timing: phase=navigation-retry session=%llu level=%d elapsed-ms=%llu navigation-result=%u labels-result=%u retries-before=%u.",
            static_cast<unsigned long long>(attempt.sessionGeneration),
            attempt.levelId,
            static_cast<unsigned long long>(GetTickCount64() - startedAt),
            static_cast<unsigned>(navigationResult),
            static_cast<unsigned>(labelResult),
            attempt.retriesRemaining);
        Context->LogInfo(timingMessage);
    }
    if (result == NavigationRefreshResult::Complete) {
        std::scoped_lock lock(NavigationRefreshMutex);
        if (PendingNavigationRefresh.hasPendingTarget
            && PendingNavigationRefresh.sessionGeneration
                == attempt.sessionGeneration
            && PendingNavigationRefresh.levelId == attempt.levelId) {
            PendingNavigationRefresh.hasPendingTarget = false;
            PendingNavigationRefresh.retriesRemaining = 0U;
            PendingNavigationRefresh.refreshRevealedActPoiDefinitions = false;
        }
        return;
    }

    bool exhausted{};
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        if (!PendingNavigationRefresh.hasPendingTarget
            || PendingNavigationRefresh.sessionGeneration
                != attempt.sessionGeneration
            || PendingNavigationRefresh.levelId != attempt.levelId) {
            return;
        }
        if (PendingNavigationRefresh.retriesRemaining > 1U) {
            --PendingNavigationRefresh.retriesRemaining;
        } else {
            PendingNavigationRefresh.retriesRemaining = 0U;
            PendingNavigationRefresh.refreshRevealedActPoiDefinitions = false;
            PendingNavigationRefresh.hasPendingTarget = false;
            exhausted = true;
        }
    }

    if (!exhausted) {
        ScheduleNavigationRefreshTimer(
            NavigationRefreshRetryDelayMilliseconds);
    } else if (Context != nullptr && Settings.diagnostics) {
        Context->LogWarn(
            "MapSense navigation: an exact destination preset was not ready; no guessed line was published.");
    }
}

void CALLBACK NavigationRefreshTimerCallback(
        PTP_CALLBACK_INSTANCE,
        void*,
        PTP_TIMER) noexcept {
    if (!Operational.load(std::memory_order_acquire)) return;
    if (!SchedulePendingNavigationRefresh()
        && Context != nullptr && Settings.diagnostics) {
        Context->LogWarn(
            "MapSense navigation: a deferred refresh could not be queued.");
    }
}

void ScheduleNativeAtlasPump(
    std::uint32_t delayMilliseconds) noexcept;
void CompleteNativeAtlasLayerRevealIntent(
    const ClientLevelView& current) noexcept;

void OnNativeAutomapLevelObserved(
        std::int32_t currentLevelId,
        bool levelChanged,
        void*) noexcept {
    if (!Operational.load(std::memory_order_acquire)
        || !FeaturesEnabled.load(std::memory_order_acquire)
        || !GameplayReady.load(std::memory_order_acquire)
        || currentLevelId == UnknownNavigationLevelId) {
        return;
    }
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    if (sessionGeneration == 0U) return;
    if (IsRevealAllArmed()) {
        const auto observed = ObserveNativeAutomapAtlasPublication(
            sessionGeneration,
            currentLevelId);
        if (observed
            == NativeAutomapAtlasPublicationStatus::Complete) {
            ClientLevelView current{};
            if (ResolveCurrentClientLevelView(current)
                && current.levelId == currentLevelId) {
                CompleteNativeAtlasLayerRevealIntent(current);
            }
        }
        if (TryWakeNativeAutomapAtlasPublication(
                sessionGeneration, currentLevelId)) {
            ScheduleNativeAtlasPump(1U);
        }
    }
    if (HasPendingRevealWorkForLevel(currentLevelId)) {
        (void)RequestRememberedRevealForCurrentSession(
            currentLevelId,
            0U,
            true);
    }
    if (levelChanged) {
        LastDynamicNavigationRefreshTick.store(
            GetTickCount64(),
            std::memory_order_release);
        ArmPendingNavigationRefresh(
            sessionGeneration,
            currentLevelId,
            0U,
            false);
        return;
    }
    // Only genuinely dynamic progression contracts receive periodic refreshes.
    // Static outdoor boundaries use the bounded level-change retry budget;
    // rescanning their complete collision topology indefinitely can stall D2R.
    if (!HasDynamicMainProgressionTargetFor(currentLevelId)) return;
    const auto counters = GetNavigationResolverCounters();
    if (counters.lastLevelId == currentLevelId
        && (counters.lastProgressionX != 0
            || counters.lastProgressionY != 0)) {
        return;
    }

    const auto now = GetTickCount64();
    auto previous = LastDynamicNavigationRefreshTick.load(
        std::memory_order_acquire);
    while (now - previous
            >= DynamicNavigationRefreshIntervalMilliseconds) {
        if (LastDynamicNavigationRefreshTick.compare_exchange_weak(
                previous,
                now,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            TryArmPendingNavigationRefresh(
                sessionGeneration,
                currentLevelId,
                0U);
            return;
        }
    }
}

void OnRevealLevelInitialized(
        std::uint8_t dataContext,
        void* level,
        void*) noexcept {
    if (!Operational.load(std::memory_order_acquire)
            || !FeaturesEnabled.load(std::memory_order_acquire)
            || !GameplayReady.load(std::memory_order_acquire)
            || !PoiAvailable.load(std::memory_order_acquire)) {
        return;
    }
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    if (sessionGeneration == 0U) return;
    (void)ObserveInitializedClientLevelPoiDefinitions(
        sessionGeneration,
        dataContext,
        level);
}

[[nodiscard]] auto InitializeNavigationRefreshTimer() noexcept -> bool {
    std::scoped_lock lock(NavigationRefreshMutex);
    if (NavigationRefreshTimer != nullptr) return true;
    NavigationRefreshTimer = CreateThreadpoolTimer(
        NavigationRefreshTimerCallback,
        nullptr,
        nullptr);
    return NavigationRefreshTimer != nullptr;
}

void ShutdownNavigationRefreshTimer() noexcept {
    PTP_TIMER timer{};
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        timer = NavigationRefreshTimer;
        NavigationRefreshTimer = nullptr;
    }
    if (timer == nullptr) return;
    SetThreadpoolTimer(timer, nullptr, 0U, 0U);
    WaitForThreadpoolTimerCallbacks(timer, TRUE);
    CloseThreadpoolTimer(timer);
}

constexpr auto IsKnownRevealAct(std::int32_t act) noexcept -> bool {
    return act >= 0
        && act < static_cast<std::int32_t>(RevealPersistenceActCount);
}

void ResetAutomaticLevelRevealCounters() noexcept {
    AutomaticLevelRevealRequests.store(0U, std::memory_order_relaxed);
    AutomaticLevelRevealAccepted.store(0U, std::memory_order_relaxed);
    AutomaticLevelRevealRejected.store(0U, std::memory_order_relaxed);
    AutomaticLevelRevealDuplicateRefusals.store(
        0U,
        std::memory_order_relaxed);
}

void ScheduleRevealReplayRetryTimer(
        std::uint32_t delayMilliseconds) noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    if (RevealReplayTimer == nullptr) return;
    const auto boundedDelay = std::max(delayMilliseconds, 1U);
    ULARGE_INTEGER dueTime{};
    dueTime.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(boundedDelay) * 10'000LL);
    FILETIME due{
        .dwLowDateTime = dueTime.LowPart,
        .dwHighDateTime = dueTime.HighPart,
    };
    SetThreadpoolTimer(RevealReplayTimer, &due, 0U, 0U);
}

void ResetProgressiveRevealLocked() noexcept {
    ProgressiveReveal = {};
    if (ProgressiveRevealTimer != nullptr) {
        SetThreadpoolTimer(ProgressiveRevealTimer, nullptr, 0U, 0U);
    }
}

void CancelProgressiveReveal() noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    ResetProgressiveRevealLocked();
}

void ScheduleProgressiveRevealTimer(
        std::uint32_t delayMilliseconds) noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    if (!ProgressiveReveal.active || ProgressiveRevealTimer == nullptr) {
        return;
    }
    const auto boundedDelay = std::max(delayMilliseconds, 1U);
    ULARGE_INTEGER dueTime{};
    dueTime.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(boundedDelay) * 10'000LL);
    FILETIME due{
        .dwLowDateTime = dueTime.LowPart,
        .dwHighDateTime = dueTime.HighPart,
    };
    SetThreadpoolTimer(ProgressiveRevealTimer, &due, 0U, 0U);
}

[[nodiscard]] auto StartProgressiveReveal(
        std::uint64_t sessionGeneration,
        const ClientLevelView& current,
        std::int32_t act) noexcept -> bool {
    if (sessionGeneration == 0U || current.levelId <= 0
        || current.difficulty >= RevealDifficultyCount
        || act < 0
        || act >= static_cast<std::int32_t>(RevealPersistenceActCount)) {
        return false;
    }
    if (ResolveActiveLevelAct(current.levelId) != act) return false;
    CancelProgressiveReveal();
    if (RequestExternalLabelAtlas(
            sessionGeneration,
            current,
            act,
            DataCatalog.load(std::memory_order_acquire))) {
        return true;
    }
    if (Context != nullptr) {
        Context->LogWarn(
            "MapSense external labels: atlas request unavailable; legacy distant-room loading is deliberately disabled.");
    }
    return false;
}

void __cdecl RunNativeAtlasPumpOnUi(
    const D2RL::PluginContext*,
    void*) noexcept;

void ScheduleNativeAtlasPump(
        std::uint32_t delayMilliseconds) noexcept {
    std::scoped_lock lock(NativeAtlasPumpMutex);
    if (NativeAtlasPumpTimer == nullptr) return;
    NativeAtlasPumpRequested = true;
    const auto boundedDelay = std::max(delayMilliseconds, 1U);
    ULARGE_INTEGER dueTime{};
    dueTime.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(boundedDelay) * 10'000LL);
    FILETIME due{
        .dwLowDateTime = dueTime.LowPart,
        .dwHighDateTime = dueTime.HighPart,
    };
    SetThreadpoolTimer(NativeAtlasPumpTimer, &due, 0U, 0U);
}

void CancelNativeAtlasPublication(
        std::uint64_t sessionGeneration) noexcept {
    {
        std::scoped_lock lock(NativeAtlasPumpMutex);
        NativeAtlasPumpRequested = false;
        if (NativeAtlasPumpTimer != nullptr) {
            SetThreadpoolTimer(NativeAtlasPumpTimer, nullptr, 0U, 0U);
        }
    }
    // A UI callback that was already accepted by D2RLoader may still run, but
    // the cleared immutable catalog/snapshot identity makes it a no-op.
    ResetNativeAutomapAtlasPublication(sessionGeneration);
}

[[nodiscard]] auto QueueNativeAtlasPumpOnUi() noexcept -> bool {
    {
        std::scoped_lock lock(NativeAtlasPumpMutex);
        if (!NativeAtlasPumpRequested || NativeAtlasPumpCallbackQueued) {
            return true;
        }
        NativeAtlasPumpRequested = false;
        NativeAtlasPumpCallbackQueued = true;
    }
    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr
        || ThreadService->runOnUiThread(
            Context,
            RunNativeAtlasPumpOnUi,
            nullptr) != D2RL::Threads::Result::Success) {
        std::scoped_lock lock(NativeAtlasPumpMutex);
        NativeAtlasPumpCallbackQueued = false;
        NativeAtlasPumpRequested = true;
        return false;
    }
    return true;
}

void CompleteNativeAtlasLayerRevealIntent(
        const ClientLevelView& current) noexcept {
    const auto difficulty = static_cast<std::int32_t>(current.difficulty);
    const auto act = ResolveActiveLevelAct(current.levelId);
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    const auto catalog = AcquireNativeAutomapLayerCatalog();
    std::int32_t currentLayer{-1};
    const bool hasCurrentLayer = catalog != nullptr
        && catalog->sessionGeneration == sessionGeneration
        && catalog->seed == current.mapSeed
        && catalog->difficulty == current.difficulty
        && catalog->act == act
        && FindNativeAutomapLayer(*catalog, current.levelId, currentLayer)
        && NativeAutomapLevelIsReady(
            *catalog, current.levelId, currentLayer);
    if (!hasCurrentLayer) {
        if (Context != nullptr) {
            Context->LogWarn(
                "MapSense native atlas: completed layer lost its exact session catalog before acceptance; reveal credit was refused.");
        }
        return;
    }
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (RevealPersistence.Difficulty() == difficulty) {
            if (catalog->geometryScopeLevelId > 0) {
                (void)RevealPersistence.MarkLevelAccepted(
                    difficulty, catalog->geometryScopeLevelId);
            } else {
                for (const auto& level : catalog->levels) {
                    if (level.layer != currentLayer) continue;
                    (void)RevealPersistence.MarkLevelAccepted(
                        difficulty, level.levelId);
                }
            }
        }
    }
    if (Context != nullptr) {
        const auto counters = GetNativeAutomapAtlasCounters();
        char message[512]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense native atlas layer: COMPLETE session=%llu seed=%u difficulty=%u act=%d layer=%d scope-level=%d cells=attempted/inserted/duplicates:%llu/%llu/%llu trees=max-total-floor/wall:%llu/%llu synthetic-tag=1(nonserialized) catalogs=%llu.",
            static_cast<unsigned long long>(
                sessionGeneration),
            current.mapSeed,
            static_cast<unsigned>(current.difficulty),
            act,
            currentLayer,
            catalog->geometryScopeLevelId,
            static_cast<unsigned long long>(counters.cellsAttempted),
            static_cast<unsigned long long>(counters.cellsInserted),
            static_cast<unsigned long long>(counters.duplicateCells),
            static_cast<unsigned long long>(
                counters.maximumFloorTreeCells),
            static_cast<unsigned long long>(
                counters.maximumWallTreeCells),
            static_cast<unsigned long long>(
                counters.layerCatalogsPublished));
        Context->LogInfo(message);
    }
}

void __cdecl RunNativeAtlasPumpOnUi(
        const D2RL::PluginContext*,
        void*) noexcept {
    {
        std::scoped_lock lock(NativeAtlasPumpMutex);
        NativeAtlasPumpCallbackQueued = false;
    }
    if (!Operational.load(std::memory_order_acquire)
        || !FeaturesEnabled.load(std::memory_order_acquire)
        || !GameplayReady.load(std::memory_order_acquire)) {
        return;
    }
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    const auto snapshot = AcquireExternalAtlasGeometrySnapshot();
    ClientLevelView current{};
    if (sessionGeneration == 0U || snapshot == nullptr
        || snapshot->sessionGeneration != sessionGeneration
        || !ResolveCurrentClientLevelView(current)) {
        return;
    }
    const auto begin = BeginNativeAutomapAtlasPublication(
        sessionGeneration,
        snapshot,
        current,
        ResolveActiveLevelAct(current.levelId));
    if (begin == NativeAutomapAtlasPublicationStatus::Unavailable
        || begin == NativeAutomapAtlasPublicationStatus::Stale) {
        return;
    }
    if (begin == NativeAutomapAtlasPublicationStatus::Failed) {
        if (Context != nullptr) {
            Context->LogWarn(
                "MapSense native atlas: the generated geometry or native Levels.Layer catalog failed closed; no external terrain fallback will be drawn.");
        }
        return;
    }
    if (begin == NativeAutomapAtlasPublicationStatus::Complete) {
        CompleteNativeAtlasLayerRevealIntent(current);
        return;
    }
    if (begin
            == NativeAutomapAtlasPublicationStatus::WaitingForOwner
        || begin
            == NativeAutomapAtlasPublicationStatus::AwaitingWitness) {
        return;
    }
    const auto pumped = PumpNativeAutomapAtlasPublication(
        current,
        NativeAtlasCellsPerUiPump);
    if (pumped == NativeAutomapAtlasPublicationStatus::InProgress) {
        ScheduleNativeAtlasPump(NativeAtlasPumpDelayMilliseconds);
        return;
    }
    if (pumped
            == NativeAutomapAtlasPublicationStatus::WaitingForOwner
        || pumped
            == NativeAutomapAtlasPublicationStatus::AwaitingWitness) {
        return;
    }
    if (pumped == NativeAutomapAtlasPublicationStatus::Complete) {
        CompleteNativeAtlasLayerRevealIntent(current);
        return;
    }
    if (pumped == NativeAutomapAtlasPublicationStatus::Failed
        && Context != nullptr) {
        const auto counters = GetNativeAutomapAtlasCounters();
        char message[384]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense native atlas: active-layer terrain publication failed closed; labels remain independently eligible; trees=max-total-floor/wall:%llu/%llu synthetic-tag=1(nonserialized). This terrain layer will not be retried until D2R performs a real layer transition.",
            static_cast<unsigned long long>(
                counters.maximumFloorTreeCells),
            static_cast<unsigned long long>(
                counters.maximumWallTreeCells));
        Context->LogWarn(message);
    }
}

void CALLBACK NativeAtlasPumpTimerCallback(
        PTP_CALLBACK_INSTANCE,
        void*,
        PTP_TIMER) noexcept {
    if (!Operational.load(std::memory_order_acquire)) return;
    if (!QueueNativeAtlasPumpOnUi()) {
        ScheduleNativeAtlasPump(NativeAtlasPumpDelayMilliseconds);
    }
}

void OnExternalLabelAtlasResult(
        std::uint64_t sessionGeneration,
        std::uint8_t difficulty,
        std::int32_t act,
        std::int32_t currentLevelId,
        bool published,
        void*) noexcept {
    if (!Operational.load(std::memory_order_acquire)
        || !published || sessionGeneration == 0U
        || sessionGeneration
            != CurrentSessionGeneration.load(std::memory_order_acquire)) {
        return;
    }
    (void)difficulty;
    (void)act;
    // The helper atomically replaces its vanilla exit catalog. Re-run the
    // runtime-owned RoomTile pass afterwards so exact mod-added entrances are
    // merged back from the active client DRLG rather than guessed from TXT.
    (void)RequestNavigationRefresh(
        sessionGeneration,
        currentLevelId,
        true);
    ScheduleNativeAtlasPump(1U);
}

void __cdecl RunProgressiveRevealStepOnUi(
        const D2RL::PluginContext*,
        void*) noexcept {
    std::uint64_t sessionGeneration{};
    std::int32_t difficulty{UnknownRevealDifficulty};
    std::int32_t act{-1};
    std::int32_t levelId{UnknownRevealLevelId};
    ProgressiveRevealPhase phase{ProgressiveRevealPhase::Discover};
    {
        std::scoped_lock lock(RevealReplayMutex);
        ProgressiveReveal.callbackQueued = false;
        if (!ProgressiveReveal.active) return;
        sessionGeneration = ProgressiveReveal.sessionGeneration;
        difficulty = ProgressiveReveal.difficulty;
        act = ProgressiveReveal.act;
        phase = ProgressiveReveal.phase;
        levelId = ProgressiveRevealLevelIdLocked();
    }

    if (!Operational.load(std::memory_order_acquire)
        || !FeaturesEnabled.load(std::memory_order_acquire)
        || !GameplayReady.load(std::memory_order_acquire)
        || sessionGeneration == 0U
        || sessionGeneration
            != CurrentSessionGeneration.load(std::memory_order_acquire)) {
        CancelProgressiveReveal();
        return;
    }

    ClientLevelView current{};
    if (!ResolveCurrentClientLevelView(current)
        || current.difficulty != difficulty
        || ResolveActiveLevelAct(current.levelId) != act) {
        CancelProgressiveReveal();
        return;
    }

    const auto stepStartedAt = GetTickCount64();
    std::array<std::int32_t, ProgressiveRevealVisCapacity> targets{};
    std::size_t targetCount{};
    bool discoveryComplete{};
    bool captureComplete{};
    auto settlement = NavigationRefreshResult::PartialRetryable;
    if (phase == ProgressiveRevealPhase::Discover) {
        if (levelId > 0) {
            discoveryComplete = DiscoverProgressiveRevealLevelTargets(
                current,
                levelId,
                targets,
                targetCount);
        }
    } else if (phase == ProgressiveRevealPhase::Capture) {
        captureComplete = CaptureProgressiveRevealLevelPoiDefinitions(
            sessionGeneration,
            current,
            levelId);
    } else {
        settlement = RefreshRevealedActPoiDefinitions(sessionGeneration);
    }
    const auto stepElapsed = GetTickCount64() - stepStartedAt;

    bool finished{};
    bool failed{};
    std::uint64_t totalElapsed{};
    std::uint64_t maximumStep{};
    std::size_t discoveredLevels{};
    bool namesReady{};
    std::uint64_t namesReadyElapsed{};
    std::uint64_t maximumDiscoveryStep{};
    std::uint64_t maximumCaptureStep{};
    std::uint64_t capturedLevels{};
    std::uint32_t nextDelay{ProgressiveRevealStepDelayMilliseconds};
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (!ProgressiveReveal.active
            || ProgressiveReveal.sessionGeneration != sessionGeneration
            || ProgressiveReveal.difficulty != difficulty
            || ProgressiveReveal.act != act
            || ProgressiveReveal.phase != phase
            || ProgressiveRevealLevelIdLocked() != levelId) {
            return;
        }
        ProgressiveReveal.maximumStepMilliseconds = std::max(
            ProgressiveReveal.maximumStepMilliseconds,
            stepElapsed);
        if (phase == ProgressiveRevealPhase::Discover) {
            ProgressiveReveal.maximumDiscoveryStepMilliseconds = std::max(
                ProgressiveReveal.maximumDiscoveryStepMilliseconds,
                stepElapsed);
        } else if (phase == ProgressiveRevealPhase::Capture) {
            ProgressiveReveal.maximumCaptureStepMilliseconds = std::max(
                ProgressiveReveal.maximumCaptureStepMilliseconds,
                stepElapsed);
        }
        const auto finishDiscovery = [&]() noexcept {
            ProgressiveReveal.namesReadyElapsedMilliseconds =
                GetTickCount64() - ProgressiveReveal.startedAt;
            ProgressiveReveal.phase = ProgressiveRevealPhase::Settle;
            namesReady = true;
        };
        if (phase == ProgressiveRevealPhase::Discover) {
            if (!discoveryComplete) {
                ProgressiveReveal.failed = true;
                (void)ProgressiveReveal.graph.Advance();
                if (ProgressiveReveal.graph.HasCurrent()) {
                    ProgressiveReveal.phase = ProgressiveRevealPhase::Discover;
                } else {
                    finishDiscovery();
                }
            }
            if (ProgressiveReveal.graph.Current() == levelId
                && ProgressiveReveal.phase
                    == ProgressiveRevealPhase::Discover) {
                for (std::size_t index = 0U; index < targetCount; ++index) {
                    if (ResolveActiveLevelAct(targets[index]) != act) continue;
                    if (!ProgressiveReveal.graph.Add(targets[index])) {
                        ProgressiveReveal.failed = true;
                    }
                }
                ProgressiveReveal.phase = ProgressiveRevealPhase::Capture;
            }
        } else if (phase == ProgressiveRevealPhase::Capture) {
            if (captureComplete) {
                ++ProgressiveReveal.capturedLevels;
            }
            (void)ProgressiveReveal.graph.Advance();
            if (ProgressiveReveal.graph.HasCurrent()) {
                ProgressiveReveal.phase = ProgressiveRevealPhase::Discover;
            } else {
                finishDiscovery();
            }
        } else if (settlement == NavigationRefreshResult::Complete
            || ProgressiveReveal.settlementRetriesRemaining == 0U) {
            if (settlement != NavigationRefreshResult::Complete) {
                ProgressiveReveal.failed = true;
            }
            for (std::size_t index = 0U;
                    index < ProgressiveReveal.graph.LevelCount();
                    ++index) {
                (void)RevealPersistence.MarkLevelAccepted(
                    difficulty,
                    ProgressiveReveal.graph.LevelAt(index));
            }
            (void)RevealPersistence.MarkActAccepted(difficulty, act);
            ProgressiveReveal.active = false;
            finished = true;
        } else {
            --ProgressiveReveal.settlementRetriesRemaining;
            nextDelay = ProgressiveRevealSettlementDelayMilliseconds;
        }
        failed = ProgressiveReveal.failed;
        totalElapsed = GetTickCount64() - ProgressiveReveal.startedAt;
        maximumStep = ProgressiveReveal.maximumStepMilliseconds;
        discoveredLevels = ProgressiveReveal.graph.LevelCount();
        namesReadyElapsed =
            ProgressiveReveal.namesReadyElapsedMilliseconds;
        maximumDiscoveryStep =
            ProgressiveReveal.maximumDiscoveryStepMilliseconds;
        maximumCaptureStep =
            ProgressiveReveal.maximumCaptureStepMilliseconds;
        capturedLevels = ProgressiveReveal.capturedLevels;
    }

    if (namesReady && Context != nullptr) {
        char message[320]{};
        std::snprintf(
            message,
            sizeof(message),
            "MapSense passive distant names: names-ready session=%llu act=%d discovered-levels=%zu captured-levels=%llu elapsed-ms=%llu max-discover-step-ms=%llu max-capture-step-ms=%llu.",
            static_cast<unsigned long long>(sessionGeneration),
            act,
            discoveredLevels,
            static_cast<unsigned long long>(
                capturedLevels),
            static_cast<unsigned long long>(namesReadyElapsed),
            static_cast<unsigned long long>(maximumDiscoveryStep),
            static_cast<unsigned long long>(maximumCaptureStep));
        Context->LogInfo(message);
    }
    if (finished) {
        if (Context != nullptr) {
            char message[384]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense passive distant names: finished session=%llu act=%d discovered-levels=%zu captured-levels=%llu names-ready-ms=%llu elapsed-ms=%llu max-step-ms=%llu status=%s.",
                static_cast<unsigned long long>(sessionGeneration),
                act,
                discoveredLevels,
                static_cast<unsigned long long>(capturedLevels),
                static_cast<unsigned long long>(namesReadyElapsed),
                static_cast<unsigned long long>(totalElapsed),
                static_cast<unsigned long long>(maximumStep),
                failed ? "partial" : "complete");
            Context->LogInfo(message);
        }
        ArmPendingNavigationRefresh(
            sessionGeneration,
            current.levelId,
            0U,
            false);
        return;
    }
    ScheduleProgressiveRevealTimer(nextDelay);
}

[[nodiscard]] auto ScheduleProgressiveRevealStep() noexcept -> bool {
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (!ProgressiveReveal.active
            || ProgressiveReveal.callbackQueued) {
            return true;
        }
        ProgressiveReveal.callbackQueued = true;
    }
    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr
        || ThreadService->runOnUiThread(
            Context,
            RunProgressiveRevealStepOnUi,
            nullptr) != D2RL::Threads::Result::Success) {
        std::scoped_lock lock(RevealReplayMutex);
        ProgressiveReveal.callbackQueued = false;
        ProgressiveReveal.failed = true;
        if (ProgressiveReveal.uiQueueRetriesRemaining > 1U) {
            --ProgressiveReveal.uiQueueRetriesRemaining;
        } else {
            ProgressiveReveal.active = false;
        }
        return false;
    }
    return true;
}

void CALLBACK ProgressiveRevealTimerCallback(
        PTP_CALLBACK_INSTANCE,
        void*,
        PTP_TIMER) noexcept {
    if (!Operational.load(std::memory_order_acquire)) return;
    if (!ScheduleProgressiveRevealStep()) {
        ScheduleProgressiveRevealTimer(
            ProgressiveRevealSettlementDelayMilliseconds);
    }
}

void CancelPendingRevealReplay() noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    PendingRevealReplay.targetLevelId = UnknownRevealLevelId;
    PendingRevealReplay.retriesRemaining = 0U;
    PendingRevealReplay.automapObserved = false;
    PendingRevealReplay.reconcilePending = false;
    PendingRevealReplay.callbackQueued = false;
    if (RevealReplayTimer != nullptr) {
        SetThreadpoolTimer(RevealReplayTimer, nullptr, 0U, 0U);
    }
}

void ResetRevealReplayGameplaySession(
        std::uint64_t sessionGeneration) noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    RevealPersistence.BeginSession(sessionGeneration);
    PendingRevealReplay = {};
    PendingRevealReplay.sessionGeneration = sessionGeneration;
    ResetProgressiveRevealLocked();
    if (sessionGeneration != 0U) {
        ResetAutomaticLevelRevealCounters();
    }
    if (RevealReplayTimer != nullptr) {
        SetThreadpoolTimer(RevealReplayTimer, nullptr, 0U, 0U);
    }
}

void ResetRevealReplayProcessState() noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    RevealPersistence.ResetProcess();
    PendingRevealReplay = {};
    ResetProgressiveRevealLocked();
    ResetAutomaticLevelRevealCounters();
    if (RevealReplayTimer != nullptr) {
        SetThreadpoolTimer(RevealReplayTimer, nullptr, 0U, 0U);
    }
}

auto ObserveValidatedRevealDifficulty(
        std::int32_t difficulty) noexcept -> bool {
    RevealDifficultyObservation observation{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        observation = RevealPersistence.ObserveDifficulty(difficulty);
        if (observation == RevealDifficultyObservation::Changed) {
            PendingRevealReplay.targetLevelId = UnknownRevealLevelId;
            PendingRevealReplay.retriesRemaining = 0U;
            PendingRevealReplay.automapObserved = false;
            PendingRevealReplay.reconcilePending = false;
            PendingRevealReplay.callbackQueued = false;
            if (RevealReplayTimer != nullptr) {
                SetThreadpoolTimer(RevealReplayTimer, nullptr, 0U, 0U);
            }
            ResetProgressiveRevealLocked();
        }
    }
    if (observation == RevealDifficultyObservation::Invalid) return false;
    if (observation == RevealDifficultyObservation::Changed) {
        (void)DisableRevealAll();
        if (Context != nullptr) {
            Context->LogInfo(
                "MapSense: the difficulty changed; Reveal Map will remain off until enabled for the rerolled seed.");
        }
    }
    return true;
}

auto ArmRevealReplayRequest(
        std::uint64_t sessionGeneration,
        std::int32_t targetLevelId,
        std::uint32_t delayMilliseconds,
        bool automapObserved) noexcept -> bool {
    if (!Operational.load(std::memory_order_acquire)
        || sessionGeneration == 0U) {
        return false;
    }

    bool scheduleTimer{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (PendingRevealReplay.sessionGeneration != sessionGeneration) {
            return false;
        }
        if (ProgressiveReveal.active
            && ProgressiveReveal.sessionGeneration == sessionGeneration
            && (targetLevelId <= 0
                || ResolveActiveLevelAct(targetLevelId)
                    == ProgressiveReveal.act)) {
            return true;
        }
        if (!PendingRevealReplay.playerReady) {
            return true;
        }
        if (PendingRevealReplay.reconcilePending) {
            // ActChanged may arrive without a level id. It must not consume a
            // second budget or replace a more precise LevelChanged target.
            const bool wasAutomapObserved =
                PendingRevealReplay.automapObserved;
            if (MergePendingRevealAutomapObservation(
                    PendingRevealReplay,
                    targetLevelId,
                    automapObserved)) {
                if (!wasAutomapObserved
                    && PendingRevealReplay.automapObserved) {
                    // A real automap pass is stronger evidence than the
                    // readiness timer. Give that upgraded request a fresh
                    // bounded attempt immediately.
                    PendingRevealReplay.retriesRemaining =
                        RevealReplayRetryLimit;
                    scheduleTimer = true;
                } else {
                    return true;
                }
            }
        }
        if (!PendingRevealReplay.reconcilePending
            || !scheduleTimer) {
            PendingRevealReplay.targetLevelId = targetLevelId;
            PendingRevealReplay.retriesRemaining = RevealReplayRetryLimit;
            PendingRevealReplay.automapObserved = automapObserved;
            PendingRevealReplay.reconcilePending = true;
            scheduleTimer = true;
        }
    }
    if (scheduleTimer) {
        ScheduleRevealReplayRetryTimer(delayMilliseconds);
    }
    return true;
}

auto RequestRememberedRevealForCurrentSession(
        std::int32_t targetLevelId,
        std::uint32_t delayMilliseconds,
        bool automapObserved) noexcept -> bool {
    if (!FeaturesEnabled.load(std::memory_order_acquire)) return true;
    if (!GameplayReady.load(std::memory_order_acquire)) {
        // The process-lifetime ids remain remembered. LocalPlayerReady will
        // reconcile them against the new game's generated client DRLG.
        return true;
    }
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    if (sessionGeneration == 0U) return true;
    return ArmRevealReplayRequest(
        sessionGeneration,
        targetLevelId,
        delayMilliseconds,
        automapObserved);
}

auto HasPendingRevealWorkForLevel(
        std::int32_t levelId) noexcept -> bool {
    if (levelId <= 0) return false;
    std::scoped_lock lock(RevealReplayMutex);
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    if (ProgressiveReveal.active
        && ProgressiveReveal.sessionGeneration == sessionGeneration
        && ProgressiveReveal.act == ResolveActiveLevelAct(levelId)) {
        return false;
    }
    const auto difficulty = RevealPersistence.Difficulty();
    return RevealPersistence.ShouldReplayCurrentLevel(
        difficulty,
        ResolveActiveLevelAct(levelId),
        levelId);
}

void SetRevealReplayPlayerReady(
        std::uint64_t sessionGeneration) noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    if (PendingRevealReplay.sessionGeneration == sessionGeneration) {
        PendingRevealReplay.playerReady = true;
    }
}

void CompletePendingRevealReplay(
        std::uint64_t sessionGeneration) noexcept {
    std::scoped_lock lock(RevealReplayMutex);
    if (PendingRevealReplay.sessionGeneration != sessionGeneration) return;
    PendingRevealReplay.targetLevelId = UnknownRevealLevelId;
    PendingRevealReplay.retriesRemaining = 0U;
    PendingRevealReplay.automapObserved = false;
    PendingRevealReplay.reconcilePending = false;
}

auto RetryPendingRevealReplay(
        std::uint64_t sessionGeneration) noexcept -> bool {
    bool retry{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (PendingRevealReplay.sessionGeneration != sessionGeneration
            || !PendingRevealReplay.reconcilePending) {
            return true;
        }
        if (PendingRevealReplay.retriesRemaining > 1U) {
            --PendingRevealReplay.retriesRemaining;
            retry = true;
        } else {
            PendingRevealReplay.targetLevelId = UnknownRevealLevelId;
            PendingRevealReplay.retriesRemaining = 0U;
            PendingRevealReplay.automapObserved = false;
            PendingRevealReplay.reconcilePending = false;
        }
    }
    if (retry) {
        ScheduleRevealReplayRetryTimer(
            RevealReplayRetryDelayMilliseconds);
        return true;
    }
    return false;
}

void __cdecl RetryRememberedRevealOnUi(
        const D2RL::PluginContext*,
        void*) noexcept {
    RevealReplayRequestState attempt{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        PendingRevealReplay.callbackQueued = false;
        if (!PendingRevealReplay.reconcilePending) return;
        attempt = PendingRevealReplay;
    }

    if (!Operational.load(std::memory_order_acquire)
        || !FeaturesEnabled.load(std::memory_order_acquire)
        || !GameplayReady.load(std::memory_order_acquire)
        || attempt.sessionGeneration == 0U
        || attempt.sessionGeneration
            != CurrentSessionGeneration.load(std::memory_order_acquire)) {
        CancelPendingRevealReplay();
        return;
    }

    ClientLevelView current{};
    const bool clientReady = ResolveCurrentClientLevelView(current);
    if (!clientReady
        || !ObserveValidatedRevealDifficulty(current.difficulty)) {
        if (!RetryPendingRevealReplay(attempt.sessionGeneration)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: remembered reveal intent expired before the new client map became ready.");
        }
        return;
    }

    {
        std::scoped_lock lock(RevealReplayMutex);
        if (PendingRevealReplay.sessionGeneration
                != attempt.sessionGeneration
            || !PendingRevealReplay.reconcilePending
            || PendingRevealReplay.targetLevelId
                != attempt.targetLevelId) {
            return;
        }
    }

    // A LevelChanged target is authoritative. If D2R still exposes the prior
    // client DRLG, retain the complete new-level budget and retry instead of
    // consuming or crediting the wrong generated level.
    if (attempt.targetLevelId > 0
        && current.levelId != attempt.targetLevelId) {
        if (!RetryPendingRevealReplay(attempt.sessionGeneration)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: remembered reveal target did not become the active client level before its bounded retries expired.");
        }
        return;
    }

    const auto activeRevealMapSeed = ActiveRevealMapSeed.load(
        std::memory_order_acquire);
    if (activeRevealMapSeed != 0U
        && activeRevealMapSeed != current.mapSeed) {
        {
            std::scoped_lock lock(RevealReplayMutex);
            RevealPersistence.ClearRevealAll();
        }
        (void)DisableRevealAll();
        ActiveRevealMapSeed.store(0U, std::memory_order_release);
        ActiveRevealMapDifficulty.store(
            UnknownRevealDifficulty, std::memory_order_release);
    }

    const auto currentAct = ResolveActiveLevelAct(current.levelId);
    bool hasAnyIntent{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        hasAnyIntent = RevealPersistence.HasAnyIntent();
    }
    if (!hasAnyIntent
        && HasPersistedExternalRevealMapIntent(
            current.mapSeed, current.difficulty)
        && ArmRevealAll() == RevealOutcome::Armed) {
        std::scoped_lock lock(RevealReplayMutex);
        (void)RevealPersistence.SetRevealAll(current.difficulty, true);
        // The intent is restored immediately, but this act remains pending
        // until every generated cell has reached its authoritative native
        // layer. A helper or publisher failure therefore cannot be credited as
        // a successful reveal.
        ActiveRevealMapSeed.store(
            current.mapSeed, std::memory_order_release);
        ActiveRevealMapDifficulty.store(
            current.difficulty, std::memory_order_release);
        if (Context != nullptr) {
            Context->LogInfo(
                "MapSense: Reveal Map restored for the unchanged map seed.");
        }
    }

    bool hasIntent{};
    bool shouldReveal{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (PendingRevealReplay.sessionGeneration
                != attempt.sessionGeneration
            || !PendingRevealReplay.reconcilePending) {
            return;
        }
        if (PendingRevealReplay.targetLevelId <= 0) {
            PendingRevealReplay.targetLevelId = current.levelId;
        }
        hasIntent = RevealPersistence.HasReplayIntentForLevel(
            current.difficulty,
            currentAct,
            current.levelId);
        shouldReveal = RevealPersistence.ShouldReplayCurrentLevel(
            current.difficulty,
            currentAct,
            current.levelId);
    }

    if (!hasIntent) {
        CompletePendingRevealReplay(attempt.sessionGeneration);
        return;
    }

    // Product Reveal Map is driven exclusively by the generated atlas. The
    // legacy Reveal Level/Act replay below may materialize local rooms, so it
    // must never be reached while Reveal Map is armed. Reuse an already
    // generated act snapshot immediately, start only the player's active
    // native layer, and ask the helper again only when this level's label view
    // is not already current.
    if (IsRevealAllArmed() && IsKnownRevealAct(currentAct)) {
        AutomaticLevelRevealRequests.fetch_add(
            1U, std::memory_order_relaxed);

        auto snapshot = AcquireExternalAtlasGeometrySnapshot();
        const bool snapshotMatchesAct = snapshot != nullptr
            && snapshot->geometry != nullptr
            && snapshot->sessionGeneration == attempt.sessionGeneration
            && snapshot->seed == current.mapSeed
            && snapshot->difficulty == current.difficulty
            && snapshot->act == currentAct;
        const bool labelsMatchCurrent = snapshotMatchesAct
            && snapshot->currentLevelId == current.levelId;
        if (snapshotMatchesAct && !labelsMatchCurrent) {
            // Request de-duplication lives in the provider. This refreshes the
            // label topology/witness for a real level transition without
            // delaying publication from a reusable same-act geometry cache.
            (void)RequestExternalLabelAtlas(
                attempt.sessionGeneration,
                current,
                currentAct,
                DataCatalog.load(std::memory_order_acquire));
        }

        auto publication = QueryNativeAutomapAtlasPublication(
            attempt.sessionGeneration,
            current,
            currentAct);
        if ((publication == NativeAutomapAtlasPublicationStatus::Unavailable
                || publication
                    == NativeAutomapAtlasPublicationStatus::Stale)
            && snapshotMatchesAct) {
            publication = BeginNativeAutomapAtlasPublication(
                attempt.sessionGeneration,
                std::move(snapshot),
                current,
                currentAct);
        }

        if (publication == NativeAutomapAtlasPublicationStatus::Complete) {
            AutomaticLevelRevealDuplicateRefusals.fetch_add(
                1U, std::memory_order_relaxed);
            CompletePendingRevealReplay(attempt.sessionGeneration);
            return;
        }
        if (publication == NativeAutomapAtlasPublicationStatus::Accepted
            || publication
                == NativeAutomapAtlasPublicationStatus::InProgress
            || publication
                == NativeAutomapAtlasPublicationStatus::WaitingForOwner
            || publication
                == NativeAutomapAtlasPublicationStatus::AwaitingWitness) {
            AutomaticLevelRevealAccepted.fetch_add(
                1U, std::memory_order_relaxed);
            if (publication
                    == NativeAutomapAtlasPublicationStatus::Accepted
                || publication
                    == NativeAutomapAtlasPublicationStatus::InProgress) {
                ScheduleNativeAtlasPump(NativeAtlasPumpDelayMilliseconds);
            }
            CompletePendingRevealReplay(attempt.sessionGeneration);
            return;
        }
        if (publication == NativeAutomapAtlasPublicationStatus::Failed) {
            AutomaticLevelRevealRejected.fetch_add(
                1U, std::memory_order_relaxed);
            // Failure is latched for this exact native layer. A real D2R layer
            // transition may start another layer; repeated observations of the
            // failed layer cannot create a retry/log storm.
            CompletePendingRevealReplay(attempt.sessionGeneration);
            return;
        }

        const auto helperRequestsBefore =
            GetExternalLabelProviderCounters().requests;
        const auto helperStartedAt = GetTickCount64();
        const bool scheduled = StartProgressiveReveal(
            attempt.sessionGeneration,
            current,
            currentAct);
        const auto helperScheduleElapsed = GetTickCount64()
            - helperStartedAt;
        const auto helperRequestsAfter =
            GetExternalLabelProviderCounters().requests;
        const bool queuedNewHelperRequest =
            helperRequestsAfter > helperRequestsBefore;
        (scheduled
                ? AutomaticLevelRevealAccepted
                : AutomaticLevelRevealRejected)
            .fetch_add(1U, std::memory_order_relaxed);
        // RequestExternalLabelAtlas deliberately returns success for an exact
        // published/in-flight/pending duplicate. Do not turn the UI replay
        // cadence into a disk-log storm when no helper work was queued.
        if (Context != nullptr && (!scheduled || queuedNewHelperRequest)) {
            char message[352]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense reveal timing: phase=native-atlas-request session=%llu level=%d act=%d helper-schedule-ms=%llu accepted=%d queued=%d.",
                static_cast<unsigned long long>(attempt.sessionGeneration),
                current.levelId,
                currentAct,
                static_cast<unsigned long long>(helperScheduleElapsed),
                scheduled ? 1 : 0,
                queuedNewHelperRequest ? 1 : 0);
            Context->LogInfo(message);
        }
        if (scheduled) {
            CompletePendingRevealReplay(attempt.sessionGeneration);
            return;
        }
        if (!RetryPendingRevealReplay(attempt.sessionGeneration)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: the seed-scoped atlas request expired before the helper became available.");
        }
        return;
    }

    if (!shouldReveal) {
        AutomaticLevelRevealDuplicateRefusals.fetch_add(
            1U,
            std::memory_order_relaxed);
        // The native reveal intent is already satisfied for this act, but a
        // same-act level transition may enter a different automap coordinate
        // space (for example an outdoor entrance into a dungeon). Refresh the
        // immutable external snapshot without replaying native room loading.
        if (IsRevealAllArmed()) {
            (void)StartProgressiveReveal(
                attempt.sessionGeneration,
                current,
                currentAct);
        }
        CompletePendingRevealReplay(attempt.sessionGeneration);
        return;
    }

    AutomaticLevelRevealRequests.fetch_add(1U, std::memory_order_relaxed);
    bool automapObserved{};
    bool revealWholeAct{};
    bool actAlreadyAccepted{};
    bool levelAlreadyAccepted{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        revealWholeAct = RevealPersistence.ShouldRevealWholeAct(
            current.difficulty,
            currentAct);
        automapObserved = PendingRevealReplay.automapObserved;
        actAlreadyAccepted = RevealPersistence.IsActAccepted(
            current.difficulty,
            currentAct);
        levelAlreadyAccepted = RevealPersistence.IsLevelAccepted(
            current.difficulty,
            current.levelId);
    }
    const auto submission = MakeRevealReplaySubmissionPolicy(
        automapObserved,
        revealWholeAct,
        actAlreadyAccepted,
        levelAlreadyAccepted);
    if (submission.waitForAutomap) {
        if (!RetryPendingRevealReplay(attempt.sessionGeneration)
            && Context != nullptr) {
            Context->LogInfo(
                "MapSense: remembered reveal remains armed and will resume on the next real automap observation.");
        }
        return;
    }
    const auto submitWholeAct = submission.submitWholeAct;
    const auto revealCurrentLevel = submission.submitCurrentLevel;
    if (submitWholeAct) {
        const auto captureStartedAt = GetTickCount64();
        const bool scheduled = StartProgressiveReveal(
            attempt.sessionGeneration,
            current,
            currentAct);
        const auto captureElapsed = GetTickCount64() - captureStartedAt;
        (scheduled
                ? AutomaticLevelRevealAccepted
                : AutomaticLevelRevealRejected)
            .fetch_add(1U, std::memory_order_relaxed);
        if (Context != nullptr) {
            char message[352]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense reveal timing: phase=native-atlas-replay session=%llu level=%d act=%d automap-observed=%d helper-schedule-ms=%llu accepted=%d.",
                static_cast<unsigned long long>(attempt.sessionGeneration),
                current.levelId,
                currentAct,
                automapObserved ? 1 : 0,
                static_cast<unsigned long long>(captureElapsed),
                scheduled ? 1 : 0);
            Context->LogInfo(message);
        }
        if (scheduled) {
            CompletePendingRevealReplay(attempt.sessionGeneration);
            return;
        }
        if (!RetryPendingRevealReplay(attempt.sessionGeneration)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: the native atlas could not be scheduled before its bounded readiness retries expired.");
        }
        return;
    }
    const auto levelStartedAt = GetTickCount64();
    const auto levelOutcome = revealCurrentLevel
        ? RevealCurrentZone()
        : RevealOutcome::Complete;
    const auto levelElapsed = revealCurrentLevel
        ? GetTickCount64() - levelStartedAt
        : 0U;
    if (Context != nullptr && revealCurrentLevel) {
        char timingMessage[320]{};
        std::snprintf(
            timingMessage,
            sizeof(timingMessage),
            "MapSense reveal timing: phase=local-fallback session=%llu level=%d automap-observed=%d reveal-level=%d level-ms=%llu retries-before=%u.",
            static_cast<unsigned long long>(attempt.sessionGeneration),
            current.levelId,
            automapObserved ? 1 : 0,
            revealCurrentLevel ? 1 : 0,
            static_cast<unsigned long long>(levelElapsed),
            attempt.retriesRemaining);
        Context->LogInfo(timingMessage);
    }
    const bool levelAccepted = levelOutcome == RevealOutcome::Complete;
    const bool accepted = levelAccepted;
    (accepted
            ? AutomaticLevelRevealAccepted
            : AutomaticLevelRevealRejected)
        .fetch_add(1U, std::memory_order_relaxed);
    if (accepted) {
        {
            std::scoped_lock lock(RevealReplayMutex);
            if (PendingRevealReplay.sessionGeneration
                    != attempt.sessionGeneration
                || !PendingRevealReplay.reconcilePending) {
                return;
            }
            if (revealCurrentLevel && CanConfirmReplayedLevelReveal(
                    automapObserved,
                    levelOutcome)) {
                (void)RevealPersistence.MarkLevelAccepted(
                    current.difficulty,
                    current.levelId);
            }
            PendingRevealReplay.targetLevelId = UnknownRevealLevelId;
            PendingRevealReplay.retriesRemaining = 0U;
            PendingRevealReplay.automapObserved = false;
            PendingRevealReplay.reconcilePending = false;
        }
        if (Context != nullptr && Settings.diagnostics) {
            char message[224]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense native local fallback: current level %d submission=%s for session %llu.",
                current.levelId,
                automapObserved
                    ? "confirmed-at-automap"
                    : "accepted-awaiting-automap",
                static_cast<unsigned long long>(attempt.sessionGeneration));
            Context->LogInfo(message);
        }
        // The single-level fallback is local-only. Rebuild the static label
        // catalog so its exit names do not depend on the render proximity at
        // the instant this remembered level was replayed.
        if (GameplayReady.load(std::memory_order_acquire)
            && !RequestNavigationRefresh(
                attempt.sessionGeneration,
                current.levelId,
                false)
            && Context != nullptr && Settings.diagnostics) {
            Context->LogWarn(
                "MapSense navigation: the post-replay all-exit label refresh could not be queued.");
        }
        return;
    }

    if (!RetryPendingRevealReplay(attempt.sessionGeneration)
        && Context != nullptr) {
        Context->LogWarn(
            "MapSense: remembered reveal intent remained unavailable after its bounded native current-level retries; it stays remembered for the next matching level event.");
    }
}

auto SchedulePendingRevealReplay() noexcept -> bool {
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (!PendingRevealReplay.reconcilePending
            || PendingRevealReplay.callbackQueued) {
            return true;
        }
        PendingRevealReplay.callbackQueued = true;
    }

    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr
        || ThreadService->runOnUiThread(
            Context,
            RetryRememberedRevealOnUi,
            nullptr) != D2RL::Threads::Result::Success) {
        std::scoped_lock lock(RevealReplayMutex);
        PendingRevealReplay.callbackQueued = false;
        return false;
    }
    return true;
}

void CALLBACK RevealReplayTimerCallback(
        PTP_CALLBACK_INSTANCE,
        void*,
        PTP_TIMER) noexcept {
    if (!Operational.load(std::memory_order_acquire)) return;
    std::uint64_t sessionGeneration{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        sessionGeneration = PendingRevealReplay.sessionGeneration;
    }
    if (!SchedulePendingRevealReplay()
        && !RetryPendingRevealReplay(sessionGeneration)
        && Context != nullptr) {
        Context->LogWarn(
            "MapSense: remembered reveal intent could not be queued on D2R's UI thread before its retry budget expired.");
    } else if (Context != nullptr && Settings.diagnostics) {
        // Queue failures are intentionally counted against the same bounded
        // budget; successful scheduling produces no per-frame log traffic.
        std::scoped_lock lock(RevealReplayMutex);
        if (PendingRevealReplay.reconcilePending
            && !PendingRevealReplay.callbackQueued) {
            Context->LogWarn(
                "MapSense: deferred reveal reconciliation will retry on D2R's UI thread.");
        }
    }
}

[[nodiscard]] auto InitializeRevealReplayTimer() noexcept -> bool {
    std::scoped_lock lock(RevealReplayMutex, NativeAtlasPumpMutex);
    if (RevealReplayTimer != nullptr
        && ProgressiveRevealTimer != nullptr
        && NativeAtlasPumpTimer != nullptr) return true;
    if (RevealReplayTimer == nullptr) {
        RevealReplayTimer = CreateThreadpoolTimer(
            RevealReplayTimerCallback,
            nullptr,
            nullptr);
    }
    if (ProgressiveRevealTimer == nullptr) {
        ProgressiveRevealTimer = CreateThreadpoolTimer(
            ProgressiveRevealTimerCallback,
            nullptr,
            nullptr);
    }
    if (NativeAtlasPumpTimer == nullptr) {
        NativeAtlasPumpTimer = CreateThreadpoolTimer(
            NativeAtlasPumpTimerCallback,
            nullptr,
            nullptr);
    }
    if (RevealReplayTimer != nullptr
        && ProgressiveRevealTimer != nullptr
        && NativeAtlasPumpTimer != nullptr) return true;
    if (RevealReplayTimer != nullptr) {
        CloseThreadpoolTimer(RevealReplayTimer);
        RevealReplayTimer = nullptr;
    }
    if (ProgressiveRevealTimer != nullptr) {
        CloseThreadpoolTimer(ProgressiveRevealTimer);
        ProgressiveRevealTimer = nullptr;
    }
    if (NativeAtlasPumpTimer != nullptr) {
        CloseThreadpoolTimer(NativeAtlasPumpTimer);
        NativeAtlasPumpTimer = nullptr;
    }
    NativeAtlasPumpRequested = false;
    NativeAtlasPumpCallbackQueued = false;
    return false;
}

void ShutdownRevealReplayTimer() noexcept {
    PTP_TIMER timer{};
    PTP_TIMER progressiveTimer{};
    PTP_TIMER nativeAtlasTimer{};
    {
        std::scoped_lock lock(RevealReplayMutex, NativeAtlasPumpMutex);
        timer = RevealReplayTimer;
        progressiveTimer = ProgressiveRevealTimer;
        nativeAtlasTimer = NativeAtlasPumpTimer;
        RevealReplayTimer = nullptr;
        ProgressiveRevealTimer = nullptr;
        NativeAtlasPumpTimer = nullptr;
        NativeAtlasPumpRequested = false;
        NativeAtlasPumpCallbackQueued = false;
        RevealPersistence.ResetProcess();
        PendingRevealReplay = {};
        ProgressiveReveal = {};
    }
    if (timer != nullptr) {
        SetThreadpoolTimer(timer, nullptr, 0U, 0U);
        WaitForThreadpoolTimerCallbacks(timer, TRUE);
        CloseThreadpoolTimer(timer);
    }
    if (progressiveTimer != nullptr) {
        SetThreadpoolTimer(progressiveTimer, nullptr, 0U, 0U);
        WaitForThreadpoolTimerCallbacks(progressiveTimer, TRUE);
        CloseThreadpoolTimer(progressiveTimer);
    }
    if (nativeAtlasTimer != nullptr) {
        SetThreadpoolTimer(nativeAtlasTimer, nullptr, 0U, 0U);
        WaitForThreadpoolTimerCallbacks(nativeAtlasTimer, TRUE);
        CloseThreadpoolTimer(nativeAtlasTimer);
    }
}

auto ExecuteTrackedRevealAction(Action action) noexcept -> RevealOutcome {
    if (action == Action::DisableRevealAll) {
        const auto outcome = ExecuteAction(action);
        const auto seed = ActiveRevealMapSeed.exchange(
            0U, std::memory_order_acq_rel);
        const auto difficulty = ActiveRevealMapDifficulty.exchange(
            UnknownRevealDifficulty, std::memory_order_acq_rel);
        if (seed != 0U && difficulty >= 0
            && difficulty < static_cast<std::int32_t>(
                RevealDifficultyCount)) {
            (void)SetPersistedExternalRevealMapIntent(
                seed, static_cast<std::uint8_t>(difficulty), false);
        }
        {
            std::scoped_lock lock(RevealReplayMutex);
            RevealPersistence.ClearRevealAll();
        }
        CancelPendingRevealReplay();
        CancelProgressiveReveal();
        ResetExternalLabelProviderSession(
            CurrentSessionGeneration.load(std::memory_order_acquire));
        CancelNativeAtlasPublication(
            CurrentSessionGeneration.load(std::memory_order_acquire));
        (void)RequestRememberedRevealForCurrentSession();
        return outcome;
    }

    if (!FeaturesEnabled.load(std::memory_order_acquire)) {
        return RevealOutcome::Unavailable;
    }

    ClientLevelView current{};
    if (!ResolveCurrentClientLevelView(current)
        || !ObserveValidatedRevealDifficulty(current.difficulty)) {
        return RevealOutcome::Unavailable;
    }

    const auto totalStartedAt = GetTickCount64();
    const auto currentAct = ResolveActiveLevelAct(current.levelId);
    const auto primaryStartedAt = GetTickCount64();
    const auto outcome = ExecuteAction(action);
    const auto primaryElapsed = GetTickCount64() - primaryStartedAt;
    bool remembered{true};
    bool startPassiveNames{};
    {
        std::scoped_lock lock(RevealReplayMutex);
        if (action == Action::RevealZone
            && outcome == RevealOutcome::Complete) {
            remembered = RevealPersistence.RememberLevel(
                current.difficulty,
                current.levelId);
            (void)RevealPersistence.MarkLevelAccepted(
                current.difficulty,
                current.levelId);
        } else if (action == Action::RevealAct
            && outcome == RevealOutcome::Complete) {
            if (IsKnownRevealAct(currentAct)) {
                remembered = RevealPersistence.RememberAct(
                    current.difficulty,
                    currentAct);
                (void)RevealPersistence.MarkActAccepted(
                    current.difficulty, currentAct);
                (void)RevealPersistence.MarkLevelAccepted(
                    current.difficulty, current.levelId);
                startPassiveNames = true;
            } else {
                // The action succeeded, but an unknown Levels.txt id cannot be
                // persisted safely as an Act intent.
                remembered = false;
            }
        } else if (IsRevealAllArmAction(action)
            && (outcome == RevealOutcome::Armed
                || outcome == RevealOutcome::Disarmed)) {
            remembered = RevealPersistence.SetRevealAll(
                current.difficulty,
                outcome == RevealOutcome::Armed);
            if (outcome == RevealOutcome::Armed
                && IsKnownRevealAct(currentAct)) {
                // The toggle arms generation only. Acceptance is recorded by
                // CompleteNativeAtlasLayerRevealIntent after each native layer
                // finishes.
                startPassiveNames = true;
            }
        }
    }
    if (!remembered && Context != nullptr) {
        Context->LogWarn(
            "MapSense: a successful reveal could not be added to the bounded runtime intent set.");
    }

    if (IsRevealAllArmAction(action)
        && outcome == RevealOutcome::Armed) {
        ActiveRevealMapSeed.store(
            current.mapSeed, std::memory_order_release);
        ActiveRevealMapDifficulty.store(
            current.difficulty, std::memory_order_release);
        if (!SetPersistedExternalRevealMapIntent(
                current.mapSeed, current.difficulty, true)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: Reveal Map is active, but its seed-scoped cold-start intent could not be saved.");
        }
    } else if (IsRevealAllArmAction(action)
        && outcome == RevealOutcome::Disarmed) {
        (void)SetPersistedExternalRevealMapIntent(
            current.mapSeed, current.difficulty, false);
        ActiveRevealMapSeed.store(0U, std::memory_order_release);
        ActiveRevealMapDifficulty.store(
            UnknownRevealDifficulty, std::memory_order_release);
    }

    if (IsRevealAllArmAction(action)) {
        CancelPendingRevealReplay();
        if (outcome == RevealOutcome::Disarmed) {
            CancelProgressiveReveal();
            ResetExternalLabelProviderSession(
                CurrentSessionGeneration.load(
                    std::memory_order_acquire));
            CancelNativeAtlasPublication(
                CurrentSessionGeneration.load(
                    std::memory_order_acquire));
        }
        if (startPassiveNames
            && !StartProgressiveReveal(
                CurrentSessionGeneration.load(std::memory_order_acquire),
                current,
                currentAct)
            && !RequestRememberedRevealForCurrentSession(
                current.levelId,
                RevealReplayRetryDelayMilliseconds,
                false)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: passive distant-name capture could not be scheduled for the current session.");
        }
    } else if (action == Action::RevealAct
        && outcome == RevealOutcome::Complete
        && startPassiveNames
        && !StartProgressiveReveal(
            CurrentSessionGeneration.load(std::memory_order_acquire),
            current,
            currentAct)
        && !RequestRememberedRevealForCurrentSession(
            current.levelId,
            RevealReplayRetryDelayMilliseconds,
            false)
        && Context != nullptr) {
        Context->LogWarn(
            "MapSense: passive distant-name capture for Reveal Act could not be scheduled.");
    }
    if (Context != nullptr) {
        char timingMessage[320]{};
        std::snprintf(
            timingMessage,
            sizeof(timingMessage),
            "MapSense reveal timing: phase=action action=%s session=%llu level=%d primary-ms=%llu total-ms=%llu outcome=%u.",
            RevealActionName(action),
            static_cast<unsigned long long>(
                CurrentSessionGeneration.load(std::memory_order_acquire)),
            current.levelId,
            static_cast<unsigned long long>(primaryElapsed),
            static_cast<unsigned long long>(GetTickCount64() - totalStartedAt),
            static_cast<unsigned>(outcome));
        Context->LogInfo(timingMessage);
    }
    return outcome;
}

void __cdecl OnGameplayEvent(
        const D2RL::PluginContext*,
        const D2RL::Lifecycle::GameplayEvent* event,
        void*) noexcept {
    if (!D2RL::Lifecycle::HasGameplayEventField(
            event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
        return;
    }
    if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameJoined) {
        ResetNativeUiPanelVisibility();
        LastNativeUiPanelRefreshRequestTick.store(0U, std::memory_order_release);
        CurrentSessionGeneration.store(
            event->sessionGeneration,
            std::memory_order_release);
        LastDynamicNavigationRefreshTick.store(
            0U,
            std::memory_order_release);
        CancelPendingNavigationRefresh();
        ResetNavigationSession(event->sessionGeneration);
        ResetNativeAutomapMarker();
        ResetExternalLabelProviderSession(event->sessionGeneration);
        ResetNativeAutomapPoiSession(event->sessionGeneration);
        CancelNativeAtlasPublication(event->sessionGeneration);
        GameplayReady.store(false, std::memory_order_release);
        MenuExpanded.store(false, std::memory_order_release);
        SetD3D12ImGuiMenuOpen(false);
        SessionEpoch.fetch_add(1, std::memory_order_acq_rel);
        ResetRevealReplayGameplaySession(event->sessionGeneration);
        // InitLevel can run before GameJoined. Preserve the client DRLG it
        // captured while resetting per-game diagnostics. The process-lifetime
        // Reveal Level, Reveal Act, and Reveal All intents intentionally
        // survive Save & Exit within the same validated difficulty.
        BeginRevealSession();
    } else if (event->kind
        == D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady) {
        ResetNativeUiPanelVisibility();
        LastNativeUiPanelRefreshRequestTick.store(0U, std::memory_order_release);
        CurrentSessionGeneration.store(
            event->sessionGeneration,
            std::memory_order_release);
        (void)EnsureLocalizedDataCatalogReady(event->sessionGeneration);
        GameplayReady.store(true, std::memory_order_release);
        MenuExpanded.store(
            Settings.menu.startExpanded || Settings.overlay.startMenuOpen,
            std::memory_order_release);
        SetD3D12ImGuiMenuOpen(true);
        SetRevealReplayPlayerReady(event->sessionGeneration);
        if (!RequestRememberedRevealForCurrentSession(
                UnknownRevealLevelId,
                RevealReplayInitialDelayMilliseconds,
                false)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: remembered reveals for the new game could not be scheduled.");
        }
        if (IsNavigationResolverActive()
            && !RequestNavigationRefresh(
                event->sessionGeneration,
                UnknownNavigationLevelId)
            && Context != nullptr && Settings.diagnostics) {
            Context->LogWarn(
                "MapSense navigation: the initial level refresh could not be queued.");
        }
    } else if (event->kind == D2RL::Lifecycle::GameplayEventKind::GameLeft) {
        ResetNativeUiPanelVisibility();
        LastNativeUiPanelRefreshRequestTick.store(0U, std::memory_order_release);
        CancelPendingNavigationRefresh();
        ResetNavigationSession(event->sessionGeneration);
        ResetNativeAutomapMarker();
        ResetExternalLabelProviderSession(0U);
        ResetNativeAutomapPoiSession(0U);
        CancelNativeAtlasPublication(0U);
        GameplayReady.store(false, std::memory_order_release);
        CurrentSessionGeneration.store(0U, std::memory_order_release);
        LastDynamicNavigationRefreshTick.store(
            0U,
            std::memory_order_release);
        MenuExpanded.store(false, std::memory_order_release);
        SetD3D12ImGuiMenuOpen(false);
        SessionEpoch.fetch_add(1, std::memory_order_acq_rel);
        ResetRevealReplayGameplaySession(0U);
        ResetRevealSession();
    } else if (event->kind == D2RL::Lifecycle::GameplayEventKind::ActChanged) {
        CancelProgressiveReveal();
        ResetNativeAutomapMarker();
        ResetExternalLabelProviderSession(event->sessionGeneration);
        ResetNativeAutomapPoiSession(event->sessionGeneration);
        CancelNativeAtlasPublication(event->sessionGeneration);
        // ActChanged has no authoritative level id. LevelChanged will arm the
        // exact target; queuing here could spend readiness retries against the
        // previous client DRLG during an act transition.
        if (Settings.diagnostics && Context != nullptr) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense reveal persistence diagnostic: act-change session=%llu previous=%d current=%d armed=%d deferred-to-level-change=1.",
                static_cast<unsigned long long>(event->sessionGeneration),
                static_cast<std::int32_t>(event->previousValue),
                static_cast<std::int32_t>(event->currentValue),
                IsRevealAllArmed() ? 1 : 0);
            Context->LogInfo(message);
        }
    } else if (event->kind
        == D2RL::Lifecycle::GameplayEventKind::LevelChanged) {
        CurrentSessionGeneration.store(
            event->sessionGeneration,
            std::memory_order_release);
        if (DataCatalog.load(std::memory_order_acquire) == nullptr) {
            (void)EnsureLocalizedDataCatalogReady(event->sessionGeneration);
        }
        LastDynamicNavigationRefreshTick.store(
            0U,
            std::memory_order_release);
        CancelPendingNavigationRefresh();
        ResetNavigationLevel(
            event->sessionGeneration,
            event->currentValue);
        ResetNativeAutomapMarker();
        ResetNativeAutomapPoiLevel(
            event->sessionGeneration,
            event->currentValue);
        if (!RequestRememberedRevealForCurrentSession(event->currentValue)
            && Context != nullptr) {
            Context->LogWarn(
                "MapSense: remembered reveals for the changed level could not be scheduled.");
        }
        if (IsNavigationResolverActive()
            && !RequestNavigationRefresh(
                event->sessionGeneration,
                event->currentValue)
            && Context != nullptr && Settings.diagnostics) {
            Context->LogWarn(
                "MapSense navigation: the changed level refresh could not be queued.");
        }
    }
}

void WriteStatus(const D2RL::PluginContext* context) noexcept {
    if (context == nullptr) return;
    const auto counters = GetRevealCounters();
    const auto marker = GetNativeAutomapMarkerCounters();
    const auto renderer = GetD3D12ImGuiHostStatus();
    char message[2048]{};
    std::snprintf(
        message,
        sizeof(message),
            "RuffnecKk MapSense 1.0.2: active=%s; gameplay=%s; reveal-all=%s; markers=%s; immunity-scan=%s; renderer-hooks=%s; renderer=%s; chest-textures=%s; input=%s; menu=%s; presents=%llu; rendered=%llu; level traversals=%llu; rooms=%llu; failures=%llu; traversal limits=%llu; static-poi=candidates/materialized/released/failures:%llu/%llu/%llu/%llu; static-active-room-calls=0; automap-pulses=%llu; table-scans=%llu; buckets=%llu; table-limits=%llu; automap units=%llu; monsters=%llu; enemy-rejects=dead/unit/class/alignment:%llu/%llu/%llu/%llu; filter-faults=%llu; hostiles=%llu; hostile-bands=0-80/81-140/141-220/>220:%llu/%llu/%llu/%llu; projection-rejects=%llu; clip-rejects=%llu; max-hostile-subtiles=%u; max-accepted-subtiles=%u; max-published-subtiles=%u; accepted=%llu; inserted=%llu; refreshed=%llu; fresh=%llu; expired=%llu; marker waits=%llu; storage faults=%llu; marker faults=%llu.",
        IsRevealEngineActive() ? "true" : "false",
        GameplayReady.load(std::memory_order_acquire) ? "ready" : "inactive",
        IsRevealAllArmed() ? "armed" : "off",
        MarkerAvailable.load(std::memory_order_acquire)
            ? "ready"
            : "unavailable",
        FeaturesEnabled.load(std::memory_order_acquire)
                && Settings.monsters.enabled
                && Settings.immunities.enabled
                && MarkerAvailable.load(std::memory_order_acquire)
            ? "enabled"
            : "disabled",
        renderer.hooksInstalled ? "installed" : "waiting",
        renderer.rendererInitialized ? "ready" : "waiting",
        renderer.primeMhChestTexturesReady ? "PrimeMH-ready" : "fallback",
        renderer.inputSubclassInstalled ? "isolated" : "waiting",
        MenuExpanded.load(std::memory_order_acquire)
            ? "expanded"
            : "launcher",
        static_cast<unsigned long long>(renderer.presentCalls),
        static_cast<unsigned long long>(renderer.renderedFrames),
        static_cast<unsigned long long>(counters.levels),
        static_cast<unsigned long long>(counters.rooms),
        static_cast<unsigned long long>(counters.failures),
        static_cast<unsigned long long>(counters.traversalLimits),
        static_cast<unsigned long long>(counters.staticRoomCandidates),
        static_cast<unsigned long long>(counters.staticRoomsMaterialized),
        static_cast<unsigned long long>(counters.staticRoomsReleased),
        static_cast<unsigned long long>(counters.staticRoomFailures),
        static_cast<unsigned long long>(marker.automapPulses),
        static_cast<unsigned long long>(marker.monsterTableScans),
        static_cast<unsigned long long>(marker.monsterBucketsVisited),
        static_cast<unsigned long long>(marker.monsterTraversalLimits),
        static_cast<unsigned long long>(marker.unitsObserved),
        static_cast<unsigned long long>(marker.monstersObserved),
        static_cast<unsigned long long>(marker.modeRejected),
        static_cast<unsigned long long>(marker.unitFlagRejected),
        static_cast<unsigned long long>(marker.classRejected),
        static_cast<unsigned long long>(marker.alignmentRejected),
        static_cast<unsigned long long>(marker.metadataFaults),
        static_cast<unsigned long long>(marker.hostilesObserved),
        static_cast<unsigned long long>(marker.hostilesThrough80),
        static_cast<unsigned long long>(marker.hostilesFrom81Through140),
        static_cast<unsigned long long>(marker.hostilesFrom141Through220),
        static_cast<unsigned long long>(marker.hostilesBeyond220),
        static_cast<unsigned long long>(marker.projectionRejected),
        static_cast<unsigned long long>(marker.nativeClipRejected),
        marker.maximumHostileDistance,
        marker.maximumAcceptedDistance,
        marker.maximumPublishedDistance,
        static_cast<unsigned long long>(marker.candidatesAccepted),
        static_cast<unsigned long long>(marker.markersInserted),
        static_cast<unsigned long long>(marker.markersRefreshed),
        static_cast<unsigned long long>(marker.freshMarkers),
        static_cast<unsigned long long>(marker.markersExpired),
        static_cast<unsigned long long>(marker.contentionWaits),
        static_cast<unsigned long long>(marker.storageFailures),
        static_cast<unsigned long long>(marker.accessFaults));
    context->WriteConsoleMessage(message);
    const auto externalAtlas = GetExternalLabelProviderCounters();
    const auto nativeAtlas = GetNativeAutomapAtlasCounters();
    char externalAtlasMessage[1536]{};
    std::snprintf(
        externalAtlasMessage,
        sizeof(externalAtlasMessage),
        "MapSense native atlas: helper-requests=%llu; helper-processes=%llu; label-atlases=%llu; stale=%llu; helper-failures=%llu; timeouts=all/labels/geometry:%llu/%llu/%llu; cancellations=primary/prewarm:%llu/%llu; geometry-cache=hits/misses/invalid:%llu/%llu/%llu; geometry-generated=%llu; geometry-failures=%llu; snapshots=%llu; geometry-cells=%llu; active-helper=%s seed=%u act=%d; native-layers=accepted/completed/transitions:%llu/%llu/%llu; coordinate-catalogs=%llu; native-cells=attempted/inserted/duplicates:%llu/%llu/%llu; native-pending=%llu; owner-pending/mismatches=%llu/%llu; witness-passes/failures=%llu/%llu; native-failures=%llu; synthetic-tag=1(nonserialized); tree-max-total=floor/wall:%llu/%llu; foreign-owner-switches=0; external-terrain=disabled.",
        static_cast<unsigned long long>(externalAtlas.requests),
        static_cast<unsigned long long>(externalAtlas.processesStarted),
        static_cast<unsigned long long>(externalAtlas.atlasesPublished),
        static_cast<unsigned long long>(externalAtlas.staleResponses),
        static_cast<unsigned long long>(externalAtlas.failedResponses),
        static_cast<unsigned long long>(externalAtlas.timeouts),
        static_cast<unsigned long long>(externalAtlas.labelTimeouts),
        static_cast<unsigned long long>(externalAtlas.geometryTimeouts),
        static_cast<unsigned long long>(
            externalAtlas.primaryCancellations),
        static_cast<unsigned long long>(
            externalAtlas.prewarmCancellations),
        static_cast<unsigned long long>(externalAtlas.geometryCacheHits),
        static_cast<unsigned long long>(externalAtlas.geometryCacheMisses),
        static_cast<unsigned long long>(externalAtlas.geometryCacheInvalid),
        static_cast<unsigned long long>(
            externalAtlas.geometryAtlasesGenerated),
        static_cast<unsigned long long>(externalAtlas.geometryFailures),
        static_cast<unsigned long long>(
            externalAtlas.geometrySnapshotsPublished),
        static_cast<unsigned long long>(
            externalAtlas.publishedGeometryCells),
        ExternalLabelProviderOperationName(externalAtlas.activeOperation),
        externalAtlas.activeSeed,
        externalAtlas.activeAct,
        static_cast<unsigned long long>(nativeAtlas.atlasesAccepted),
        static_cast<unsigned long long>(nativeAtlas.atlasesCompleted),
        static_cast<unsigned long long>(nativeAtlas.layerTransitions),
        static_cast<unsigned long long>(
            nativeAtlas.layerCatalogsPublished),
        static_cast<unsigned long long>(nativeAtlas.cellsAttempted),
        static_cast<unsigned long long>(nativeAtlas.cellsInserted),
        static_cast<unsigned long long>(nativeAtlas.duplicateCells),
        static_cast<unsigned long long>(nativeAtlas.pendingCells),
        static_cast<unsigned long long>(nativeAtlas.ownerPending),
        static_cast<unsigned long long>(nativeAtlas.ownerMismatches),
        static_cast<unsigned long long>(nativeAtlas.witnessPasses),
        static_cast<unsigned long long>(nativeAtlas.witnessFailures),
        static_cast<unsigned long long>(nativeAtlas.failures),
        static_cast<unsigned long long>(
            nativeAtlas.maximumFloorTreeCells),
        static_cast<unsigned long long>(
            nativeAtlas.maximumWallTreeCells));
    context->WriteConsoleMessage(externalAtlasMessage);
    RevealReplayRequestState pendingReveal{};
    std::int32_t revealDifficulty{UnknownRevealDifficulty};
    {
        std::scoped_lock lock(RevealReplayMutex);
        pendingReveal = PendingRevealReplay;
        revealDifficulty = RevealPersistence.Difficulty();
    }
    char revealMessage[512]{};
    std::snprintf(
        revealMessage,
        sizeof(revealMessage),
        "MapSense native reveal replay: automatic calls=%llu; accepted=%llu; rejected=%llu; duplicate refusals=%llu; session=%llu; difficulty=%d; target-level=%d; automap-observed=%s; pending=%s; retries=%u.",
        static_cast<unsigned long long>(
            AutomaticLevelRevealRequests.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            AutomaticLevelRevealAccepted.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            AutomaticLevelRevealRejected.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(
            AutomaticLevelRevealDuplicateRefusals.load(
                std::memory_order_relaxed)),
        static_cast<unsigned long long>(pendingReveal.sessionGeneration),
        revealDifficulty,
        pendingReveal.targetLevelId,
        pendingReveal.automapObserved ? "yes" : "no",
        pendingReveal.reconcilePending ? "yes" : "no",
        pendingReveal.retriesRemaining);
    context->WriteConsoleMessage(revealMessage);
    NativeUiStateStatus nativeUi{};
    (void)AcquireNativeUiStateStatus(nativeUi);
    char nativeUiMessage[384]{};
    std::snprintf(
        nativeUiMessage,
        sizeof(nativeUiMessage),
        "MapSense native panel occlusion: active=%s; ui-mask=0x%08X; panel-mask=0x%08X; quest-known=%s; quest-visible=%s; retained-projection=%s; read-failures=%llu; quest-read-failures=%llu; render-policy=native-ui-state-plus-live-quest-widget-clip.",
        nativeUi.active ? "true" : "false",
        static_cast<unsigned>(nativeUi.activeMask),
        static_cast<unsigned>(nativeUi.blockingPanelMask),
        nativeUi.questVisibilityKnown ? "yes" : "no",
        nativeUi.questPanelVisible ? "yes" : "no",
        nativeUi.retainAutomapProjection ? "yes" : "no",
        static_cast<unsigned long long>(nativeUi.readFailures),
        static_cast<unsigned long long>(
            nativeUi.questVisibilityReadFailures));
    context->WriteConsoleMessage(nativeUiMessage);
    const auto averageDiscoveryMicroseconds =
        marker.discoveryTimingSamples != 0U
        ? marker.totalDiscoveryMicroseconds / marker.discoveryTimingSamples
        : 0U;
    const auto averageRefreshMicroseconds =
        marker.refreshTimingSamples != 0U
        ? marker.totalRefreshMicroseconds / marker.refreshTimingSamples
        : 0U;
    char markerMessage[1024]{};
    std::snprintf(
        markerMessage,
        sizeof(markerMessage),
        "MapSense monster pipeline: tracked=%llu; discovery scans=%llu timing avg/max=%llu/%llu us (%llu samples); position refreshes=%llu timing avg/max=%llu/%llu us (%llu samples); id lookups resolved/missing=%llu/%llu; accepted-bands=0-80/81-140/141-220/>220:%llu/%llu/%llu/%llu; clip-rejected-bands=0-80/81-140/141-220/>220:%llu/%llu/%llu/%llu.",
        static_cast<unsigned long long>(marker.trackedCurrent),
        static_cast<unsigned long long>(marker.monsterTableScans),
        static_cast<unsigned long long>(averageDiscoveryMicroseconds),
        static_cast<unsigned long long>(
            marker.maximumDiscoveryMicroseconds),
        static_cast<unsigned long long>(marker.discoveryTimingSamples),
        static_cast<unsigned long long>(marker.monsterPositionRefreshes),
        static_cast<unsigned long long>(averageRefreshMicroseconds),
        static_cast<unsigned long long>(marker.maximumRefreshMicroseconds),
        static_cast<unsigned long long>(marker.refreshTimingSamples),
        static_cast<unsigned long long>(marker.trackedIdsResolved),
        static_cast<unsigned long long>(marker.trackedIdsMissing),
        static_cast<unsigned long long>(marker.acceptedThrough80),
        static_cast<unsigned long long>(marker.acceptedFrom81Through140),
        static_cast<unsigned long long>(marker.acceptedFrom141Through220),
        static_cast<unsigned long long>(marker.acceptedBeyond220),
        static_cast<unsigned long long>(marker.clipRejectedThrough80),
        static_cast<unsigned long long>(
            marker.clipRejectedFrom81Through140),
        static_cast<unsigned long long>(
            marker.clipRejectedFrom141Through220),
        static_cast<unsigned long long>(marker.clipRejectedBeyond220));
    context->WriteConsoleMessage(markerMessage);
    const auto missile = GetNativeAutomapMissileCounters();
    const auto averageMissileScanMicroseconds =
        missile.scanTimingSamples != 0U
        ? missile.totalScanMicroseconds / missile.scanTimingSamples
        : 0U;
    char missileMessage[1024]{};
    std::snprintf(
        missileMessage,
        sizeof(missileMessage),
        "MapSense native missile source: client+server=true; automap-pulses=%llu; table-scans=client/server:%llu/%llu; buckets=client/server:%llu/%llu; observed=client/server:%llu/%llu; current=total(client/server):%llu(%llu/%llu); published-frames/missiles=%llu/%llu; published-client/server=%llu/%llu; dedup=suppressed/client-preferred/class-conflicts:%llu/%llu/%llu; traversal-limits=%llu; cycles=%llu; rejects=type/id/class/path/projection/clip:%llu/%llu/%llu/%llu/%llu/%llu; contention=writer/reader:%llu/%llu; access-faults=%llu; timing avg/max=%llu/%llu us (%llu samples); renderer-consumer=active-txt-prime-taxonomy.",
        static_cast<unsigned long long>(missile.automapPulses),
        static_cast<unsigned long long>(missile.clientTableScans),
        static_cast<unsigned long long>(missile.serverTableScans),
        static_cast<unsigned long long>(missile.clientBucketsVisited),
        static_cast<unsigned long long>(missile.serverBucketsVisited),
        static_cast<unsigned long long>(missile.clientUnitsObserved),
        static_cast<unsigned long long>(missile.serverUnitsObserved),
        static_cast<unsigned long long>(missile.currentPublished),
        static_cast<unsigned long long>(missile.currentClientPublished),
        static_cast<unsigned long long>(missile.currentServerPublished),
        static_cast<unsigned long long>(missile.framesPublished),
        static_cast<unsigned long long>(missile.missilesPublished),
        static_cast<unsigned long long>(missile.clientMissilesPublished),
        static_cast<unsigned long long>(missile.serverMissilesPublished),
        static_cast<unsigned long long>(missile.duplicatesSuppressed),
        static_cast<unsigned long long>(missile.clientPreferredDuplicates),
        static_cast<unsigned long long>(missile.classConflictsRejected),
        static_cast<unsigned long long>(missile.traversalLimits),
        static_cast<unsigned long long>(missile.cyclesRejected),
        static_cast<unsigned long long>(missile.unitTypeRejected),
        static_cast<unsigned long long>(missile.invalidUnitIds),
        static_cast<unsigned long long>(missile.invalidClassIds),
        static_cast<unsigned long long>(missile.pathRejected),
        static_cast<unsigned long long>(missile.projectionRejected),
        static_cast<unsigned long long>(missile.nativeClipRejected),
        static_cast<unsigned long long>(missile.writerContentionDrops),
        static_cast<unsigned long long>(missile.readerContentionDrops),
        static_cast<unsigned long long>(missile.accessFaults),
        static_cast<unsigned long long>(averageMissileScanMicroseconds),
        static_cast<unsigned long long>(missile.maximumScanMicroseconds),
        static_cast<unsigned long long>(missile.scanTimingSamples));
    context->WriteConsoleMessage(missileMessage);
    const auto poi = GetNativeAutomapPoiCounters();
    const auto dataCatalog = DataCatalog.load(std::memory_order_acquire);
    const auto* const poiPipeline = PoiAvailable.load(
            std::memory_order_acquire)
        ? "ready"
        : MarkerAvailable.load(std::memory_order_acquire)
                && dataCatalog == nullptr
            ? "pending-localization"
            : "unavailable";
    char poiMessage[768]{};
    std::snprintf(
        poiMessage,
        sizeof(poiMessage),
        "MapSense objects/labels: pipeline=%s; mask=0x%02X; automap-pulses=%llu; object-scans=%llu; buckets=%llu; observed=%llu; classified=%llu; traversal-limits=%llu; exit-definitions=%llu; projected=%llu; projection-rejects=%llu; waits=%llu; access-faults=%llu.",
        poiPipeline,
        BuildNativeAutomapPoiCollectionMask(),
        static_cast<unsigned long long>(poi.automapPulses),
        static_cast<unsigned long long>(poi.objectTableScans),
        static_cast<unsigned long long>(poi.objectBucketsVisited),
        static_cast<unsigned long long>(poi.objectUnitsObserved),
        static_cast<unsigned long long>(poi.objectUnitsClassified),
        static_cast<unsigned long long>(poi.objectTraversalLimits),
        static_cast<unsigned long long>(poi.exitDefinitionsPublished),
        static_cast<unsigned long long>(poi.projected),
        static_cast<unsigned long long>(poi.projectionRejected),
        static_cast<unsigned long long>(poi.contentionWaits),
        static_cast<unsigned long long>(poi.accessFaults));
    context->WriteConsoleMessage(poiMessage);
    if (dataCatalog != nullptr) {
        for (const auto& status : dataCatalog->FamilyStatuses()) {
            const auto family = DataCatalogFamilyName(status.family);
            char catalogMessage[512]{};
            std::snprintf(
                catalogMessage,
                sizeof(catalogMessage),
                "MapSense TXT/localization: family=%.*s state=%s rows=%zu localized=%zu unresolved=%zu.",
                static_cast<int>(family.size()),
                family.data(),
                DataCatalogFamilyStateName(status.state),
                status.rowCount,
                status.localizedNameCount,
                status.unresolvedNameCount);
            context->WriteConsoleMessage(catalogMessage);
        }
    }
    const auto navigation = GetNavigationResolverCounters();
    const auto navigationEngine = GetNavigationEngineStatus();
    NavigationRefreshState pendingNavigation{};
    {
        std::scoped_lock lock(NavigationRefreshMutex);
        pendingNavigation = PendingNavigationRefresh;
    }
    char navigationMessage[1024]{};
    std::snprintf(
        navigationMessage,
        sizeof(navigationMessage),
        "MapSense Direct navigation: resolver=%s; engine-level=%d; engine-destinations=%zu; projected=%zu; observed-level-changes=%llu; refreshes=%llu; rooms=%llu; presets=%llu; exits=%llu; waypoints=%llu; vis-slots=%llu; vis-pairs=%llu; vis-targets-pending=%llu; partial-refreshes=%llu; published=%llu; last-level=%d; last-destinations=%u; waypoint-subtile=(%d,%d); progression-subtile=(%d,%d); pending=%s/%d/%u; unresolved names=%llu; failures=%llu; traversal limits=%llu.",
        IsNavigationResolverActive() ? "ready" : "unavailable",
        navigationEngine.levelId,
        navigationEngine.destinationCount,
        navigationEngine.projectedLineCount,
        static_cast<unsigned long long>(
            navigationEngine.observedLevelChanges),
        static_cast<unsigned long long>(navigation.refreshes),
        static_cast<unsigned long long>(navigation.rooms),
        static_cast<unsigned long long>(navigation.presets),
        static_cast<unsigned long long>(navigation.exits),
        static_cast<unsigned long long>(navigation.waypoints),
        static_cast<unsigned long long>(navigation.visibilitySlots),
        static_cast<unsigned long long>(navigation.visibilityPairs),
        static_cast<unsigned long long>(
            navigation.pendingVisibilityTargets),
        static_cast<unsigned long long>(navigation.partialRefreshes),
        static_cast<unsigned long long>(navigation.published),
        navigation.lastLevelId,
        navigation.lastDestinationCount,
        navigation.lastWaypointX,
        navigation.lastWaypointY,
        navigation.lastProgressionX,
        navigation.lastProgressionY,
        pendingNavigation.hasPendingTarget ? "yes" : "no",
        pendingNavigation.levelId,
        pendingNavigation.retriesRemaining,
        static_cast<unsigned long long>(navigation.unresolvedNames),
        static_cast<unsigned long long>(navigation.failures),
        static_cast<unsigned long long>(navigation.traversalLimits));
    context->WriteConsoleMessage(navigationMessage);
}

auto __cdecl MapSenseCommand(
        D2R::Game::Client*,
        const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    try {
        const auto action = TrimAndLower(command);
        if (action.empty() || action == "status") {
            WriteStatus(command->plugin);
            return D2RL::ConsoleCommandResult::Handled;
        }
        if (action == "level" || action == "zone") {
            return QueueAction(Action::RevealZone)
                ? D2RL::ConsoleCommandResult::Handled
                : D2RL::ConsoleCommandResult::Failed;
        }
        if (action == "act") {
            return QueueAction(Action::RevealAct)
                ? D2RL::ConsoleCommandResult::Handled
                : D2RL::ConsoleCommandResult::Failed;
        }
        if (action == "all") {
            return QueueAction(Action::ToggleRevealAll)
                ? D2RL::ConsoleCommandResult::Handled
                : D2RL::ConsoleCommandResult::Failed;
        }
        if (action == "off") {
            return QueueAction(Action::DisableRevealAll)
                ? D2RL::ConsoleCommandResult::Handled
                : D2RL::ConsoleCommandResult::Failed;
        }
        if (action == "menu") {
            if (!GameplayReady.load(std::memory_order_acquire)) {
                command->plugin->WriteConsoleMessage(
                    "MapSense settings are available in game only.");
                return D2RL::ConsoleCommandResult::Handled;
            }
            return QueueAction(Action::ToggleMenu)
                ? D2RL::ConsoleCommandResult::Handled
                : D2RL::ConsoleCommandResult::Failed;
        }
        command->plugin->WriteConsoleMessage(
            "Usage: mapsense [status|level|act|all|off|menu].");
        return D2RL::ConsoleCommandResult::InvalidArguments;
    } catch (...) {
        command->plugin->WriteConsoleError(
            "MapSense: unexpected command failure.");
        return D2RL::ConsoleCommandResult::Failed;
    }
}

void UnregisterServices() noexcept {
    if (Context != nullptr && InputService != nullptr
        && InputService->unregisterAction != nullptr) {
        for (auto& handle : ActionHandles) {
            if (handle != D2RL::Input::InvalidHandle) {
                (void)InputService->unregisterAction(Context, handle);
                handle = D2RL::Input::InvalidHandle;
            }
        }
    }
    if (Context != nullptr && LifecycleService != nullptr
        && LifecycleService->unregisterGameplayEventListener != nullptr) {
        for (auto& handle : LifecycleHandles) {
            if (handle != D2RL::Lifecycle::InvalidHandle) {
                (void)LifecycleService->unregisterGameplayEventListener(
                    Context, handle);
                handle = D2RL::Lifecycle::InvalidHandle;
            }
        }
    }
}

auto RegisterInputActions() noexcept -> bool {
    struct Definition {
        const char* logicalId;
        const char* displayName;
    };
    constexpr std::array definitions{
        Definition{"toggle-reveal-all", "Toggle Reveal Map"},
        Definition{"toggle-settings", "Toggle MapSense Settings"},
    };

    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const D2RL::Input::ActionRegistration registration{
            .structSize = D2RL::Input::ActionRegistrationSize,
            .flags = 0,
            .logicalId = definitions[index].logicalId,
            .displayName = definitions[index].displayName,
            .category = "RuffnecKk Suite",
            .defaultPrimary = {
                .key = D2RL::Input::Key::None,
                .modifier = D2RL::Input::Modifier::None,
            },
            .defaultSecondary = {
                .key = D2RL::Input::Key::None,
                .modifier = D2RL::Input::Modifier::None,
            },
            .callback = OnAction,
            .userData = &ActionTokens[index],
        };
        if (InputService->registerAction(
                Context, &registration, &ActionHandles[index])
                != D2RL::Input::Result::Success
            || ActionHandles[index] == D2RL::Input::InvalidHandle) {
            if (index + 1U == definitions.size()) {
                Context->LogWarn(
                    "MapSense: the optional settings Controls action could not be registered.");
                continue;
            }
            Context->LogError(
                "MapSense: a required reveal Controls action could not be registered.");
            return false;
        }
    }
    return true;
}

auto RegisterLifecycleListeners() noexcept -> bool {
    constexpr std::array kinds{
        D2RL::Lifecycle::GameplayEventKind::GameJoined,
        D2RL::Lifecycle::GameplayEventKind::GameLeft,
        D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
        D2RL::Lifecycle::GameplayEventKind::ActChanged,
        D2RL::Lifecycle::GameplayEventKind::LevelChanged,
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .flags = 0,
            .kind = kinds[index],
            .reserved = 0,
            .callback = OnGameplayEvent,
            .userData = nullptr,
        };
        if (LifecycleService->registerGameplayEventListener(
                Context, &listener, &LifecycleHandles[index])
                != D2RL::Lifecycle::Result::Success
            || LifecycleHandles[index] == D2RL::Lifecycle::InvalidHandle) {
            Context->LogError(
                "MapSense: a required gameplay lifecycle listener could not be registered.");
            return false;
        }
    }
    return true;
}

void OnImGuiSettingsAction(ImGuiSettingsAction action) noexcept {
    auto requested = Action::ToggleRevealAll;
    switch (action) {
        case ImGuiSettingsAction::ToggleRevealMap:
            requested = Action::ToggleRevealAll;
            break;
    }
    if (!QueueAction(requested) && Context != nullptr) {
        Context->LogWarn(
            "MapSense: the ImGui reveal control could not queue its action.");
    }
}

void __cdecl DrainSettingsSaveOnUi(
        const D2RL::PluginContext* context,
        void*) noexcept {
    for (;;) {
        std::string serialized;
        {
            std::scoped_lock lock(ConfigSaveMutex);
            if (PendingConfigSave.empty()) {
                ConfigSaveDrainScheduled = false;
                return;
            }
            serialized.swap(PendingConfigSave);
        }
        if (!Operational.load(std::memory_order_acquire)
            || context == nullptr) {
            continue;
        }
        if (!context->WriteConfig(serialized.c_str())) {
            context->LogWarn(
                "MapSense: menu settings could not be written to the active configuration scope.");
        }
    }
}

void QueueSettingsSaveFromMenu() noexcept {
    if (!Operational.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr) {
        return;
    }

    std::string serialized;
    try {
        serialized = SerializeConfig(Settings);
    }
    catch (...) {
        Context->LogWarn(
            "MapSense: menu settings could not be serialized.");
        return;
    }

    bool schedule{};
    {
        std::scoped_lock lock(ConfigSaveMutex);
        PendingConfigSave = std::move(serialized);
        if (!ConfigSaveDrainScheduled) {
            ConfigSaveDrainScheduled = true;
            schedule = true;
        }
    }
    if (!schedule) return;
    if (ThreadService->runOnUiThread(
            Context, DrainSettingsSaveOnUi, nullptr)
        == D2RL::Threads::Result::Success) {
        return;
    }
    {
        std::scoped_lock lock(ConfigSaveMutex);
        ConfigSaveDrainScheduled = false;
    }
    Context->LogWarn(
        "MapSense: menu settings save could not be queued on the UI thread.");
}

void __cdecl RefreshNativeUiPanelsOnUi(
        const D2RL::PluginContext*,
        void*) noexcept {
    (void)RefreshNativeUiPanelVisibilityOnUiThread();
    NativeUiPanelRefreshQueued.store(false, std::memory_order_release);
}

void RequestNativeUiPanelVisibilityRefresh() noexcept {
    if (!GameplayReady.load(std::memory_order_acquire)
        || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr) {
        return;
    }
    const auto now = static_cast<std::uint64_t>(GetTickCount64());
    auto previous = LastNativeUiPanelRefreshRequestTick.load(
        std::memory_order_acquire);
    if (now >= previous
        && now - previous < NativeUiPanelRefreshIntervalMilliseconds) {
        return;
    }
    if (!LastNativeUiPanelRefreshRequestTick.compare_exchange_strong(
            previous,
            now,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }
    bool expected{};
    if (!NativeUiPanelRefreshQueued.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel,
            std::memory_order_acquire)) {
        return;
    }
    if (ThreadService->runOnUiThread(
            Context,
            RefreshNativeUiPanelsOnUi,
            nullptr) != D2RL::Threads::Result::Success) {
        NativeUiPanelRefreshQueued.store(false, std::memory_order_release);
    }
}

void ApplyMapSenseFeatureState(bool enabled) noexcept {
    FeaturesEnabled.store(enabled, std::memory_order_release);
    // The marker collector also supplies the living local-player automap
    // witness used by navigation and POIs, so the master controls collection
    // while the Monsters switch controls only monster publication/rendering.
    SetNativeAutomapMarkerEnabled(enabled);
    SetNativeAutomapMissileEnabled(
        enabled && Settings.missiles.enabled);
    SetNativeAutomapImmunityCollectionEnabled(
        enabled
            && Settings.monsters.enabled
            && Settings.immunities.enabled);
    ApplyNativeAutomapPoiCollectionSettings();

    if (!enabled) {
        CancelPendingNavigationRefresh();
        CancelPendingRevealReplay();
        CancelProgressiveReveal();
        InvalidateNavigationProjection();
        InvalidateNativeAutomapMarkerFrame();
        InvalidateNativeAutomapPoiFrame();
        return;
    }

    if (!GameplayReady.load(std::memory_order_acquire)) return;
    const auto sessionGeneration = CurrentSessionGeneration.load(
        std::memory_order_acquire);
    if (sessionGeneration == 0U) return;
    // The settings panel runs from Present, not D2R's UI/game thread. Queue
    // native DRLG work through the existing refresh coordinator instead of
    // resolving it synchronously from the renderer thread.
    ArmPendingNavigationRefresh(
        sessionGeneration,
        UnknownNavigationLevelId,
        0U,
        IsRevealAllArmed());
    (void)RequestRememberedRevealForCurrentSession(
        UnknownRevealLevelId,
        0U,
        false);
}

auto ResolveMapSensePanelScale(float automaticScale, void*) noexcept
        -> float {
    return ResolveMenuScale(Settings.menu.interfaceScale, automaticScale);
}

auto DrawMapSensePanel(bool* open, float menuScale, void*) noexcept
        -> D3D12ImGuiPanelBounds {
    if (open == nullptr || !*open) return {};
    const auto gameplayReady = GameplayReady.load(std::memory_order_acquire);
    if (!ShouldDrawMapSenseSettingsMenu(
            gameplayReady,
            false)) {
        *open = false;
        return {};
    }
    auto expanded = MenuExpanded.load(std::memory_order_acquire);
    const auto featuresBefore = FeaturesEnabled.load(
        std::memory_order_acquire);
    const auto bounds = DrawImGuiSettingsPanel(
        Settings,
        expanded,
        IsRevealAllArmed(),
        menuScale,
        OnImGuiSettingsAction);
    if (featuresBefore != Settings.enabled) {
        ApplyMapSenseFeatureState(Settings.enabled);
    } else {
        SetNativeAutomapMarkerEnabled(featuresBefore);
        SetNativeAutomapMissileEnabled(
            featuresBefore && Settings.missiles.enabled);
        SetNativeAutomapImmunityCollectionEnabled(
            featuresBefore
                && Settings.monsters.enabled
                && Settings.immunities.enabled);
        ApplyNativeAutomapPoiCollectionSettings();
    }
    MenuExpanded.store(expanded, std::memory_order_release);
    if (bounds.saveRequested) QueueSettingsSaveFromMenu();
    return {
        .visible = bounds.visible,
        .left = bounds.left,
        .top = bounds.top,
        .right = bounds.right,
        .bottom = bounds.bottom,
    };
}

auto WantsMapSenseOwnedOverlay(void*) noexcept -> bool {
    if (!FeaturesEnabled.load(std::memory_order_acquire)) return false;
    RequestNativeUiPanelVisibilityRefresh();
    NativeUiStateStatus nativeUi{};
    if (!AcquireNativeUiStateStatus(nativeUi)) {
        InvalidateNativeAutomapLocalPlayerFrame();
        return false;
    }
    if (!IsNativeGameplayAutomapFrame(nativeUi.activeMask)) {
        // A loading screen, closed automap or absent gameplay world revokes the
        // previous player witness without destroying any reveal/cache state.
        // A fresh living local-player automap pass must explicitly re-arm it.
        InvalidateNativeAutomapLocalPlayerFrame();
        return false;
    }
    const auto retainCurrentProjection = nativeUi.retainAutomapProjection;
    const auto navigationEnabled = Settings.navigation.waypoint.enabled
        || Settings.navigation.progression.enabled
        || Settings.navigation.customLevels.enabled
        || Settings.navigation.quests.enabled;
    const auto gameplayReady = GameplayReady.load(std::memory_order_acquire);
    return Operational.load(std::memory_order_acquire)
        && ShouldDrawMapSenseOwnedVisualFrame(
            gameplayReady,
            nativeUi.activeMask,
            IsNativeAutomapLocalPlayerFrameAlive())
        && ShouldDrawMapSenseOwnedMapOverlay(gameplayReady, false)
        && ((navigationEnabled
                && WantsNavigationLineFrame(retainCurrentProjection))
            || (Settings.monsters.enabled
                && WantsNativeAutomapMarkerFrame(retainCurrentProjection))
            || WantsNativeAutomapMissileFrame()
            || WantsNativeAutomapPoiFrame(retainCurrentProjection));
}

auto MarkerStyleFor(MonsterRank rank) noexcept
        -> const MonsterMarkerStyle& {
    switch (rank) {
        case MonsterRank::Normal: return Settings.monsters.normal;
        case MonsterRank::Minion: return Settings.monsters.minion;
        case MonsterRank::Champion: return Settings.monsters.champion;
        case MonsterRank::Unique: return Settings.monsters.unique;
        case MonsterRank::SuperUnique:
            return Settings.monsters.superUniqueBoss;
    }
    return Settings.monsters.normal;
}

auto ToImGuiColor(RgbaColor color, float globalOpacity) noexcept -> ImU32 {
    const auto channel = [](float value) noexcept {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F);
    };
    return IM_COL32(
        channel(color.red),
        channel(color.green),
        channel(color.blue),
        channel(color.alpha * globalOpacity));
}

[[nodiscard]] auto NavigationColorFor(
        NavigationLineKind kind,
        float opacity) noexcept -> ImU32 {
    switch (kind) {
        case NavigationLineKind::Waypoint:
            return ToImGuiColor(Settings.navigation.waypoint.color, opacity);
        case NavigationLineKind::Progression:
            return ToImGuiColor(
                Settings.navigation.progression.color,
                opacity);
        case NavigationLineKind::CustomLevel:
            return ToImGuiColor(
                Settings.navigation.customLevels.color,
                opacity);
        case NavigationLineKind::Quest:
            return ToImGuiColor(Settings.navigation.quests.color, opacity);
    }
    return ToImGuiColor(RgbaColor{1.0F, 1.0F, 1.0F, 1.0F}, opacity);
}

[[nodiscard]] auto NavigationLineEnabled(
        NavigationLineKind kind) noexcept -> bool {
    switch (kind) {
        case NavigationLineKind::Waypoint:
            return Settings.navigation.waypoint.enabled;
        case NavigationLineKind::Progression:
            return Settings.navigation.progression.enabled;
        case NavigationLineKind::CustomLevel:
            return Settings.navigation.customLevels.enabled;
        case NavigationLineKind::Quest:
            return Settings.navigation.quests.enabled;
    }
    return false;
}

void DrawNavigationLines(
        ImDrawList* drawList,
        const ImGuiIO& io,
        float opacity) noexcept {
    if (drawList == nullptr || NavigationLineSnapshots.empty()) return;
    const auto thickness = std::clamp(
        Settings.navigation.lineThickness * Settings.overlay.scale,
        MinimumNavigationLineThickness,
        MaximumNavigationLineThickness);
    for (const auto& line : NavigationLineSnapshots) {
        if (!NavigationLineEnabled(line.kind)) continue;
        if (line.nativeWidth <= 0 || line.nativeHeight <= 0) continue;
        const auto scaleX = io.DisplaySize.x
            / static_cast<float>(line.nativeWidth);
        const auto scaleY = io.DisplaySize.y
            / static_cast<float>(line.nativeHeight);
        if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
            || scaleX <= 0.0F || scaleY <= 0.0F) {
            continue;
        }
        const auto scaleTolerance = std::max(
            0.01F,
            std::max(scaleX, scaleY) * 0.01F);
        if (std::abs(scaleX - scaleY) > scaleTolerance) continue;

        const ImVec2 start{
            static_cast<float>(line.startX) * scaleX,
            static_cast<float>(line.startY) * scaleY,
        };
        const ImVec2 end{
            static_cast<float>(line.endX) * scaleX,
            static_cast<float>(line.endY) * scaleY,
        };
        if (!std::isfinite(start.x) || !std::isfinite(start.y)
            || !std::isfinite(end.x) || !std::isfinite(end.y)) {
            continue;
        }
        drawList->AddLine(
            start,
            end,
            NavigationColorFor(line.kind, opacity),
            thickness);
    }
}

void DrawClosedMarkerOutline(
        ImDrawList* drawList,
        const CrossOutline& outline,
        ImU32 color,
        float thickness) noexcept {
    std::array<ImVec2, CrossOutlinePointCount> points{};
    for (std::size_t index = 0; index < outline.size(); ++index) {
        points[index] = ImVec2{outline[index].x, outline[index].y};
    }
    drawList->AddPolyline(
        points.data(),
        static_cast<int>(points.size() - 1U),
        color,
        ImDrawFlags_Closed,
        thickness);
}

[[nodiscard]] auto ImmunityColorFor(
        MonsterImmunity immunity) noexcept -> RgbaColor {
    switch (immunity) {
        case MonsterImmunity::Physical:
            return Settings.immunities.physical;
        case MonsterImmunity::Fire:
            return Settings.immunities.fire;
        case MonsterImmunity::Cold:
            return Settings.immunities.cold;
        case MonsterImmunity::Lightning:
            return Settings.immunities.lightning;
        case MonsterImmunity::Poison:
            return Settings.immunities.poison;
        case MonsterImmunity::Magic:
            return Settings.immunities.magic;
    }
    return Settings.immunities.physical;
}

[[nodiscard]] auto CollectImmunityColors(
        std::uint8_t mask,
        float opacity,
        std::array<ImU32, 6>& colors) noexcept -> std::size_t {
    constexpr std::array Immunities{
        MonsterImmunity::Physical,
        MonsterImmunity::Fire,
        MonsterImmunity::Cold,
        MonsterImmunity::Lightning,
        MonsterImmunity::Poison,
        MonsterImmunity::Magic,
    };
    std::size_t count{};
    for (const auto immunity : Immunities) {
        if ((mask & ImmunityBit(immunity)) == 0U) continue;
        colors[count++] = ToImGuiColor(
            ImmunityColorFor(immunity),
            opacity);
    }
    return count;
}

void DrawColoredImmunityIndicators(
        ImDrawList* drawList,
        ImVec2 markerCenter,
        float markerSize,
        const std::array<ImU32, 6>& colors,
        std::size_t colorCount,
        float opacity) noexcept {
    if (drawList == nullptr || colorCount == 0U) return;

    auto* const font = ImGui::GetFont();
    if (font == nullptr) return;
    const auto fontSize = std::clamp(
        Settings.immunities.indicatorSize * Settings.overlay.scale,
        MinimumImmunityIndicatorSize,
        MaximumImmunityIndicatorSize);
    const auto sampleSize = font->CalcTextSizeA(
        fontSize,
        1000.0F,
        0.0F,
        "i");
    const auto lineHeight = std::max(sampleSize.y, fontSize);
    const auto advance = ComputeColoredImmunityIndicatorAdvance(fontSize);
    const auto rowCount = (colorCount + 2U) / 3U;
    const auto shadow = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.92F},
        opacity);
    const auto startY = std::max(
        2.0F,
        markerCenter.y - (markerSize * 0.5F)
            - std::max(2.0F, Settings.overlay.scale * 2.0F)
            - static_cast<float>(rowCount) * lineHeight);

    std::size_t index{};
    for (std::size_t row = 0U; row < rowCount; ++row) {
        const auto rowItems = std::min<std::size_t>(3U, colorCount - index);
        const auto rowSpan = static_cast<float>(rowItems - 1U) * advance;
        auto x = markerCenter.x - rowSpan * 0.5F - sampleSize.x * 0.5F;
        const auto y = startY + static_cast<float>(row) * lineHeight;
        for (std::size_t column = 0U; column < rowItems; ++column) {
            drawList->AddText(
                font,
                fontSize,
                ImVec2{x + 1.0F, y + 1.0F},
                shadow,
                "i");
            drawList->AddText(
                font,
                fontSize,
                ImVec2{x, y},
                colors[index++],
                "i");
            x += advance;
        }
    }
}

void DrawSplitImmunityHalo(
        ImDrawList* drawList,
        ImVec2 markerCenter,
        float markerSize,
        const std::array<ImU32, 6>& colors,
        std::size_t colorCount,
        float opacity) noexcept {
    if (drawList == nullptr || colorCount == 0U) return;

    constexpr float Tau = 6.28318530717958647692F;
    constexpr float Top = -1.57079632679489661923F;
    const auto thickness = std::clamp(
        Settings.immunities.haloThickness * Settings.overlay.scale,
        MinimumImmunityHaloThickness,
        MaximumImmunityHaloThickness);
    const auto radius = markerSize * 0.5F
        + thickness
        + std::max(1.5F, Settings.overlay.scale * 1.5F);
    const auto backing = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.90F},
        opacity);
    drawList->AddCircle(
        markerCenter,
        radius,
        backing,
        0,
        thickness + std::max(2.0F, Settings.overlay.scale * 2.0F));

    if (colorCount == 1U) {
        drawList->AddCircle(
            markerCenter,
            radius,
            colors[0],
            0,
            thickness);
        return;
    }

    const auto segmentSpan = Tau / static_cast<float>(colorCount);
    const auto gap = std::min(0.10F, segmentSpan * 0.06F);
    for (std::size_t index = 0U; index < colorCount; ++index) {
        const auto start = Top + static_cast<float>(index) * segmentSpan
            + gap;
        const auto finish = Top + static_cast<float>(index + 1U)
            * segmentSpan - gap;
        drawList->PathArcTo(markerCenter, radius, start, finish, 0);
        drawList->PathStroke(colors[index], ImDrawFlags_None, thickness);
    }
}

void DrawMonsterImmunities(
        ImDrawList* drawList,
        const NativeAutomapMarkerSnapshot& marker,
        ImVec2 markerCenter,
        float markerSize,
        float opacity) noexcept {
    if (!Settings.immunities.enabled || marker.immunityMask == 0U) return;

    std::array<ImU32, 6> colors{};
    const auto colorCount = CollectImmunityColors(
        marker.immunityMask,
        opacity,
        colors);
    if (Settings.immunities.style == ImmunityDisplayStyle::ColoredI) {
        DrawColoredImmunityIndicators(
            drawList,
            markerCenter,
            markerSize,
            colors,
            colorCount,
            opacity);
        return;
    }
    DrawSplitImmunityHalo(
        drawList,
        markerCenter,
        markerSize,
        colors,
        colorCount,
        opacity);
}

[[nodiscard]] auto TryScaleNativeAutomapPoint(
        const ImGuiIO& io,
        std::int32_t nativeWidth,
        std::int32_t nativeHeight,
        std::int32_t nativeX,
        std::int32_t nativeY,
        ImVec2& point) noexcept -> bool {
    if (nativeWidth <= 0 || nativeHeight <= 0) return false;
    const auto scaleX = io.DisplaySize.x
        / static_cast<float>(nativeWidth);
    const auto scaleY = io.DisplaySize.y
        / static_cast<float>(nativeHeight);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
        || scaleX <= 0.0F || scaleY <= 0.0F) {
        return false;
    }
    const auto scaleTolerance = std::max(
        0.01F,
        std::max(scaleX, scaleY) * 0.01F);
    if (std::abs(scaleX - scaleY) > scaleTolerance) return false;
    point = {
        static_cast<float>(nativeX) * scaleX,
        static_cast<float>(nativeY) * scaleY,
    };
    return std::isfinite(point.x) && std::isfinite(point.y)
        && point.x >= 0.0F && point.y >= 0.0F
        && point.x < io.DisplaySize.x && point.y < io.DisplaySize.y;
}

[[nodiscard]] auto DecodeAutomapUtf8Codepoint(
        const char*& cursor,
        std::uint32_t& codepoint) noexcept -> bool {
    const auto first = static_cast<unsigned char>(*cursor);
    if (first == 0U) return false;
    if (first <= 0x7FU) {
        codepoint = first;
        ++cursor;
        return true;
    }
    std::uint32_t value{};
    std::uint32_t minimum{};
    std::size_t continuationCount{};
    if ((first & 0xE0U) == 0xC0U) {
        value = first & 0x1FU;
        minimum = 0x80U;
        continuationCount = 1U;
    } else if ((first & 0xF0U) == 0xE0U) {
        value = first & 0x0FU;
        minimum = 0x800U;
        continuationCount = 2U;
    } else if ((first & 0xF8U) == 0xF0U) {
        value = first & 0x07U;
        minimum = 0x10000U;
        continuationCount = 3U;
    } else {
        return false;
    }
    for (std::size_t index = 1U; index <= continuationCount; ++index) {
        const auto next = static_cast<unsigned char>(cursor[index]);
        if (next == 0U || (next & 0xC0U) != 0x80U) return false;
        value = (value << 6U) | (next & 0x3FU);
    }
    if (value < minimum || value > 0xFFFFU
        || (value >= 0xD800U && value <= 0xDFFFU)) {
        return false;
    }
    cursor += continuationCount + 1U;
    codepoint = value;
    return true;
}

[[nodiscard]] auto SelectAutomapLabelFont(const char* text) noexcept
        -> ImFont* {
    auto* const automapFont = GetD3D12ImGuiAutomapFont();
    if (automapFont == nullptr || text == nullptr) return ImGui::GetFont();
    const char* cursor = text;
    while (*cursor != '\0') {
        std::uint32_t codepoint{};
        if (!DecodeAutomapUtf8Codepoint(cursor, codepoint)
            || automapFont->FindGlyphNoFallback(
                static_cast<ImWchar>(codepoint)) == nullptr) {
            return ImGui::GetFont();
        }
    }
    return automapFont;
}

enum class AutomapTextPlacement : std::uint8_t {
    Top,
    Centered,
    AboveIcon,
};

[[nodiscard]] auto DrawCenteredShadowedText(
        ImDrawList* drawList,
        const ImGuiIO& io,
        const char* text,
        ImVec2 anchor,
        float fontSize,
        ImU32 color,
        float opacity,
        AutomapTextPlacement placement = AutomapTextPlacement::Top,
        float iconTopExtent = 0.0F,
        float gap = 0.0F) noexcept -> float {
    if (drawList == nullptr || text == nullptr || text[0] == '\0') return 0.0F;
    auto* const font = SelectAutomapLabelFont(text);
    if (font == nullptr) return 0.0F;
    const auto bounds = font->CalcTextSizeA(
        fontSize,
        std::max(1.0F, io.DisplaySize.x),
        0.0F,
        text);
    const auto top = placement == AutomapTextPlacement::AboveIcon
        ? AutomapLabelTopAboveIcon(
            anchor.y,
            bounds.y,
            iconTopExtent,
            gap)
        : placement == AutomapTextPlacement::Centered
            ? anchor.y - bounds.y * 0.5F
            : anchor.y;
    const auto position = ImVec2{
        std::clamp(anchor.x - bounds.x * 0.5F,
            0.0F,
            std::max(0.0F, io.DisplaySize.x - bounds.x)),
        std::clamp(top,
            0.0F,
            std::max(0.0F, io.DisplaySize.y - bounds.y)),
    };
    const auto shadow = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.98F},
        opacity);
    const auto outlineRadius = std::clamp(
        fontSize * 0.035F,
        1.0F,
        2.0F);
    const std::array<ImVec2, 5> outlineOffsets{
        ImVec2{0.0F, -outlineRadius},
        ImVec2{-outlineRadius, 0.0F},
        ImVec2{outlineRadius, 0.0F},
        ImVec2{0.0F, outlineRadius},
        ImVec2{outlineRadius, outlineRadius},
    };
    for (const auto& offset : outlineOffsets) {
        drawList->AddText(
            font,
            fontSize,
            ImVec2{position.x + offset.x, position.y + offset.y},
            shadow,
            text);
    }
    drawList->AddText(font, fontSize, position, color, text);
    return bounds.y;
}

void DrawShrineIcon(
        ImDrawList* drawList,
        ImVec2 center,
        float size,
        ImU32 color,
        float opacity) noexcept {
    if (drawList == nullptr || size <= 0.0F) return;
    const auto radius = size * 0.42F;
    const auto line = std::clamp(size * 0.095F, 1.25F, 2.4F);
    const auto shadow = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.90F}, opacity);
    auto fillValue = ImGui::ColorConvertU32ToFloat4(color);
    fillValue.w = std::clamp(fillValue.w * 0.22F, 0.0F, 1.0F);
    const auto fill = ImGui::ColorConvertFloat4ToU32(fillValue);
    drawList->AddCircleFilled(center, radius + 2.0F, shadow, 20);
    drawList->AddCircleFilled(center, radius, fill, 20);
    drawList->AddCircle(center, radius, color, 20, line);
    const auto arm = radius * 0.64F;
    drawList->AddLine(
        ImVec2{center.x - arm, center.y},
        ImVec2{center.x + arm, center.y},
        shadow,
        line + 2.0F);
    drawList->AddLine(
        ImVec2{center.x, center.y - arm},
        ImVec2{center.x, center.y + arm},
        shadow,
        line + 2.0F);
    drawList->AddLine(
        ImVec2{center.x - arm, center.y},
        ImVec2{center.x + arm, center.y},
        color,
        line);
    drawList->AddLine(
        ImVec2{center.x, center.y - arm},
        ImVec2{center.x, center.y + arm},
        color,
        line);
}

void DrawFallbackChestIcon(
        ImDrawList* drawList,
        ImVec2 center,
        float size,
        ImU32 outlineColor,
        ImU32 interiorColor,
        ImU32 accentColor,
        float opacity) noexcept {
    if (drawList == nullptr || size <= 0.0F) return;
    const auto tint = [](ImU32 source, float brightness, float alphaScale) {
        auto value = ImGui::ColorConvertU32ToFloat4(source);
        value.x = std::clamp(value.x * brightness, 0.0F, 1.0F);
        value.y = std::clamp(value.y * brightness, 0.0F, 1.0F);
        value.z = std::clamp(value.z * brightness, 0.0F, 1.0F);
        value.w = std::clamp(value.w * alphaScale, 0.0F, 1.0F);
        return ImGui::ColorConvertFloat4ToU32(value);
    };
    const auto x = [center, size](float value) {
        return std::floor(center.x + value * size) + 0.5F;
    };
    const auto y = [center, size](float value) {
        return std::floor(center.y + value * size) + 0.5F;
    };
    const auto fillSide = tint(interiorColor, 0.60F, 0.52F);
    const auto fillFront = tint(interiorColor, 0.88F, 0.62F);
    const auto fillLidSide = tint(interiorColor, 0.72F, 0.56F);
    const auto fillLidFront = tint(interiorColor, 1.02F, 0.68F);
    const auto fillLock = tint(accentColor, 1.04F, 0.92F);
    const auto shadow = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.60F}, opacity);
    const auto lockOutline = tint(outlineColor, 0.58F, 1.0F);
    const auto keyhole = ToImGuiColor(
        RgbaColor{0.035F, 0.025F, 0.015F, 0.96F}, opacity);
    const auto lineThickness = std::clamp(size * 0.047F, 1.0F, 1.55F);
    const auto underStroke = lineThickness + 0.70F;

    // Compact clean-room three-quarter trunk. The lid is deliberately built
    // from a small, regular grid: the back rail, front seam and base are each
    // perfectly horizontal, while every depth edge uses the same diagonal.
    // At automap scale this reads as a chest instead of a basket or cage.
    const ImVec2 rearSeam{x(-0.48F), y(-0.11F)};
    const ImVec2 rearBottom{x(-0.48F), y(0.25F)};
    const ImVec2 frontSeamLeft{x(-0.28F), y(-0.01F)};
    const ImVec2 frontSeamRight{x(0.48F), y(-0.01F)};
    const ImVec2 frontBottomLeft{x(-0.28F), y(0.35F)};
    const ImVec2 frontBottomRight{x(0.48F), y(0.35F)};

    const ImVec2 rearShoulder{x(-0.44F), y(-0.25F)};
    const ImVec2 backRailLeft{x(-0.14F), y(-0.42F)};
    const ImVec2 backRailRight{x(0.22F), y(-0.42F)};
    const ImVec2 frontShoulderLeft{x(-0.28F), y(-0.19F)};
    const ImVec2 frontShoulderRight{x(0.43F), y(-0.24F)};

    const std::array<ImVec2, 4> sideFace{
        rearSeam,
        frontSeamLeft,
        frontBottomLeft,
        rearBottom,
    };
    const std::array<ImVec2, 4> frontFace{
        frontSeamLeft,
        frontSeamRight,
        frontBottomRight,
        frontBottomLeft,
    };
    const std::array<ImVec2, 4> sideLidFace{
        rearSeam,
        rearShoulder,
        backRailLeft,
        frontSeamLeft,
    };
    const std::array<ImVec2, 6> frontLidFace{
        frontSeamLeft,
        frontShoulderLeft,
        backRailLeft,
        backRailRight,
        frontShoulderRight,
        frontSeamRight,
    };
    drawList->AddConvexPolyFilled(
        sideFace.data(),
        static_cast<int>(sideFace.size()),
        fillSide);
    drawList->AddConvexPolyFilled(
        frontFace.data(),
        static_cast<int>(frontFace.size()),
        fillFront);
    drawList->AddConvexPolyFilled(
        sideLidFace.data(),
        static_cast<int>(sideLidFace.size()),
        fillLidSide);
    drawList->AddConvexPolyFilled(
        frontLidFace.data(),
        static_cast<int>(frontLidFace.size()),
        fillLidFront);

    const std::array<ImVec2, 10> silhouette{
        rearBottom,
        rearSeam,
        rearShoulder,
        backRailLeft,
        backRailRight,
        frontShoulderRight,
        frontSeamRight,
        frontBottomRight,
        frontBottomLeft,
        rearBottom,
    };
    drawList->AddPolyline(
        silhouette.data(),
        static_cast<int>(silhouette.size()),
        shadow,
        ImDrawFlags_None,
        underStroke);
    drawList->AddPolyline(
        silhouette.data(),
        static_cast<int>(silhouette.size()),
        outlineColor,
        ImDrawFlags_None,
        lineThickness);

    const auto drawStructure = [drawList, shadow, outlineColor, underStroke,
            lineThickness](const ImVec2* points, std::size_t count) noexcept {
        drawList->AddPolyline(
            points,
            static_cast<int>(count),
            shadow,
            ImDrawFlags_None,
            underStroke);
        drawList->AddPolyline(
            points,
            static_cast<int>(count),
            outlineColor,
            ImDrawFlags_None,
            lineThickness);
    };
    const std::array<ImVec2, 3> lidSeam{
        rearSeam,
        frontSeamLeft,
        frontSeamRight,
    };
    drawStructure(lidSeam.data(), lidSeam.size());
    const std::array<ImVec2, 2> cornerPost{
        frontSeamLeft,
        frontBottomLeft,
    };
    drawStructure(cornerPost.data(), cornerPost.size());
    const std::array<ImVec2, 3> lidCorner{
        backRailLeft,
        frontShoulderLeft,
        frontSeamLeft,
    };
    drawStructure(lidCorner.data(), lidCorner.size());

    const std::array<ImVec2, 4> lockPlate{
        ImVec2{x(0.035F), y(-0.045F)},
        ImVec2{x(0.195F), y(-0.045F)},
        ImVec2{x(0.195F), y(0.185F)},
        ImVec2{x(0.035F), y(0.185F)},
    };
    drawList->AddConvexPolyFilled(
        lockPlate.data(),
        static_cast<int>(lockPlate.size()),
        fillLock);
    drawList->AddPolyline(
        lockPlate.data(),
        static_cast<int>(lockPlate.size()),
        lockOutline,
        ImDrawFlags_Closed,
        std::max(1.0F, lineThickness * 0.85F));
    drawList->AddCircleFilled(
        ImVec2{x(0.115F), y(0.035F)},
        std::max(0.75F, size * 0.024F),
        keyhole);
    drawList->AddLine(
        ImVec2{x(0.115F), y(0.050F)},
        ImVec2{x(0.115F), y(0.125F)},
        keyhole,
        std::max(1.0F, size * 0.024F));
}

[[nodiscard]] auto DrawPrimeMhChestImage(
        ImDrawList* drawList,
        ImVec2 center,
        float chestSize,
        D3D12ImGuiTextureView texture,
        bool specialChest,
        float opacity) noexcept -> bool {
    if (drawList == nullptr || chestSize <= 0.0F || !texture) return false;
    const auto placement = ComputePrimeMhChestImagePlacement(
        center.x,
        center.y,
        chestSize,
        specialChest);
    const auto alpha = static_cast<int>(std::lround(
        std::clamp(opacity, 0.0F, 1.0F) * 255.0F));
    drawList->AddImage(
        static_cast<ImTextureID>(texture.textureId),
        ImVec2{placement.left, placement.top},
        ImVec2{placement.right, placement.bottom},
        ImVec2{0.0F, 0.0F},
        ImVec2{1.0F, 1.0F},
        IM_COL32(255, 255, 255, alpha));
    return true;
}

void DrawChestStateLock(
        ImDrawList* drawList,
        ImVec2 chestCenter,
        float chestSize,
        ImU32 stateColor,
        float opacity) noexcept {
    if (drawList == nullptr || chestSize <= 0.0F) return;
    const float width = std::clamp(chestSize * 0.24F, 5.5F, 15.0F);
    const float bodyHeight = width * 0.72F;
    const float lineThickness = std::clamp(
        chestSize * 0.045F, 1.1F, 2.2F);
    const ImVec2 lockCenter{
        chestCenter.x + chestSize * 0.14F,
        chestCenter.y + chestSize * 0.12F,
    };
    const auto shadow = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.92F}, opacity);
    const auto keyhole = ToImGuiColor(
        RgbaColor{0.015F, 0.015F, 0.02F, 0.98F}, opacity);
    const ImVec2 shackleMinimum{
        lockCenter.x - width * 0.28F,
        lockCenter.y - bodyHeight * 1.03F,
    };
    const ImVec2 shackleMaximum{
        lockCenter.x + width * 0.28F,
        lockCenter.y - bodyHeight * 0.06F,
    };
    drawList->AddRect(
        ImVec2{shackleMinimum.x + 1.0F, shackleMinimum.y + 1.0F},
        ImVec2{shackleMaximum.x + 1.0F, shackleMaximum.y + 1.0F},
        shadow,
        width * 0.30F,
        ImDrawFlags_RoundCornersTop,
        lineThickness + 2.0F);
    drawList->AddRect(
        shackleMinimum,
        shackleMaximum,
        stateColor,
        width * 0.30F,
        ImDrawFlags_RoundCornersTop,
        lineThickness);
    const ImVec2 bodyMinimum{
        lockCenter.x - width * 0.5F,
        lockCenter.y - bodyHeight * 0.25F,
    };
    const ImVec2 bodyMaximum{
        lockCenter.x + width * 0.5F,
        lockCenter.y + bodyHeight * 0.75F,
    };
    drawList->AddRectFilled(
        ImVec2{bodyMinimum.x + 1.0F, bodyMinimum.y + 1.0F},
        ImVec2{bodyMaximum.x + 1.0F, bodyMaximum.y + 1.0F},
        shadow,
        width * 0.10F);
    drawList->AddRectFilled(
        bodyMinimum,
        bodyMaximum,
        stateColor,
        width * 0.10F);
    drawList->AddRect(
        bodyMinimum,
        bodyMaximum,
        shadow,
        width * 0.10F,
        ImDrawFlags_RoundCornersAll,
        std::max(1.0F, lineThickness * 0.65F));
    const ImVec2 keyholeCenter{
        lockCenter.x,
        lockCenter.y + bodyHeight * 0.16F,
    };
    drawList->AddCircleFilled(
        keyholeCenter,
        std::max(0.85F, width * 0.10F),
        keyhole,
        12);
    drawList->AddLine(
        ImVec2{keyholeCenter.x, keyholeCenter.y + width * 0.06F},
        ImVec2{keyholeCenter.x, keyholeCenter.y + width * 0.22F},
        keyhole,
        std::max(1.0F, width * 0.10F));
}

void DrawArmorRackIcon(
        ImDrawList* drawList,
        ImVec2 center,
        float size,
        ImU32 color,
        float opacity) noexcept {
    if (drawList == nullptr) return;
    const auto half = size * 0.5F;
    const auto outline = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.90F},
        opacity);
    const std::array<ImVec2, 6> shield{
        ImVec2{center.x - half * 0.72F, center.y - half * 0.72F},
        ImVec2{center.x, center.y - half},
        ImVec2{center.x + half * 0.72F, center.y - half * 0.72F},
        ImVec2{center.x + half * 0.58F, center.y + half * 0.28F},
        ImVec2{center.x, center.y + half},
        ImVec2{center.x - half * 0.58F, center.y + half * 0.28F},
    };
    drawList->AddPolyline(
        shield.data(),
        static_cast<int>(shield.size()),
        outline,
        ImDrawFlags_Closed,
        std::max(3.0F, size * 0.18F));
    drawList->AddPolyline(
        shield.data(),
        static_cast<int>(shield.size()),
        color,
        ImDrawFlags_Closed,
        std::max(1.5F, size * 0.09F));
    drawList->AddLine(
        ImVec2{center.x, center.y - half * 0.62F},
        ImVec2{center.x, center.y + half * 0.50F},
        color,
        std::max(1.0F, size * 0.07F));
}

void DrawWeaponRackIcon(
        ImDrawList* drawList,
        ImVec2 center,
        float size,
        ImU32 color,
        float opacity) noexcept {
    if (drawList == nullptr) return;
    const auto half = size * 0.5F;
    const auto outline = ToImGuiColor(
        RgbaColor{0.0F, 0.0F, 0.0F, 0.90F},
        opacity);
    const auto drawSword = [drawList, center, half](
            float direction,
            ImU32 swordColor,
            float thickness) noexcept {
        const ImVec2 bladeStart{
            center.x - direction * half * 0.72F,
            center.y + half * 0.72F,
        };
        const ImVec2 bladeEnd{
            center.x + direction * half * 0.72F,
            center.y - half * 0.72F,
        };
        drawList->AddLine(bladeStart, bladeEnd, swordColor, thickness);
        const ImVec2 guardCenter{
            center.x - direction * half * 0.42F,
            center.y + half * 0.42F,
        };
        drawList->AddLine(
            ImVec2{guardCenter.x - half * 0.20F,
                guardCenter.y - direction * half * 0.20F},
            ImVec2{guardCenter.x + half * 0.20F,
                guardCenter.y + direction * half * 0.20F},
            swordColor,
            thickness);
    };
    drawSword(1.0F, outline, std::max(3.0F, size * 0.18F));
    drawSword(-1.0F, outline, std::max(3.0F, size * 0.18F));
    drawSword(1.0F, color, std::max(1.4F, size * 0.08F));
    drawSword(-1.0F, color, std::max(1.4F, size * 0.08F));
}

enum class AutomapPoiRenderPass : std::uint8_t {
    Objects,
    ProtectedLabels,
};

void DrawAutomapPoiSnapshots(
        ImDrawList* drawList,
        const ImGuiIO& io,
        const std::shared_ptr<const MapSenseDataCatalog>& dataCatalog,
        float opacity,
        AutomapPoiRenderPass renderPass) noexcept {
    if (drawList == nullptr || dataCatalog == nullptr
        || !Settings.objects.enabled) {
        return;
    }
    struct DrawnExitLabel final {
        bool isExitLabel{};
        std::int32_t targetLevelId{};
        AutomapLabelRectangle anchorRectangle{};
        AutomapLabelRectangle placedRectangle{};
    };
    std::array<
        DrawnExitLabel,
        MaximumAutomapExitLabels + 1U> drawnExitLabels{};
    std::size_t drawnExitLabelCount{};
    const bool labelPass = renderPass
        == AutomapPoiRenderPass::ProtectedLabels;
    const auto primeMhChestTexture = labelPass
        ? D3D12ImGuiTextureView{}
        : GetD3D12ImGuiPrimeMhChestTexture(false);
    const auto primeMhSuperChestTexture = labelPass
        ? D3D12ImGuiTextureView{}
        : GetD3D12ImGuiPrimeMhChestTexture(true);
    for (const auto& poi : PoiSnapshots) {
        const bool isLabel = poi.kind == AutomapPoiKind::ExitLabel
            || poi.kind == AutomapPoiKind::WaypointLabel
            || poi.kind == AutomapPoiKind::LevelLabel
            || poi.kind == AutomapPoiKind::ShrineLabel;
        if (isLabel != labelPass) continue;
            ImVec2 center{};
            if (!TryScaleNativeAutomapPoint(
                    io,
                    poi.nativeWidth,
                    poi.nativeHeight,
                    poi.x,
                    poi.y,
                    center)) {
                continue;
            }
            switch (poi.kind) {
            case AutomapPoiKind::ExitLabel: {
                if (!Settings.objects.exitLabels.enabled
                    || poi.sourceId < 0) {
                    break;
                }
                const auto* const level = dataCatalog->FindLevel(poi.sourceId);
                if (level == nullptr || !level->name.localized
                    || level->name.utf8.empty()) {
                    break;
                }
                const auto fontSize = std::clamp(
                    Settings.objects.exitLabels.size
                        * Settings.overlay.scale,
                    MinimumAutomapLabelSize,
                    MaximumAutomapLabelSize);
                auto* const font = SelectAutomapLabelFont(
                    level->name.utf8.c_str());
                if (font == nullptr) break;
                const auto textBounds = font->CalcTextSizeA(
                    fontSize,
                    std::max(1.0F, io.DisplaySize.x),
                    0.0F,
                    level->name.utf8.c_str());
                const auto textTop = AutomapLabelTopAboveIcon(
                    center.y,
                    textBounds.y,
                    NativeExitIconTopExtent * Settings.overlay.scale,
                    NativeAutomapLabelGap * Settings.overlay.scale);
                const auto textPosition = ImVec2{
                    std::clamp(
                        center.x - textBounds.x * 0.5F,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.x - textBounds.x)),
                    std::clamp(
                        textTop,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y)),
                };
                const AutomapLabelRectangle anchorRectangle{
                    .left = textPosition.x,
                    .top = textPosition.y,
                    .right = textPosition.x + textBounds.x,
                    .bottom = textPosition.y + textBounds.y,
                };
                bool duplicate{};
                for (std::size_t drawnIndex = 0U;
                        drawnIndex < drawnExitLabelCount;
                        ++drawnIndex) {
                    const auto& drawn = drawnExitLabels[drawnIndex];
                    if (drawn.isExitLabel
                            && drawn.targetLevelId == poi.sourceId
                            && AutomapLabelRectanglesOverlap(
                                drawn.anchorRectangle,
                                anchorRectangle,
                                std::max(1.0F, Settings.overlay.scale))) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) break;
                auto placedRectangle = anchorRectangle;
                bool placed{};
                const auto separation = textBounds.y
                    + std::max(4.0F, 5.0F * Settings.overlay.scale);
                constexpr std::size_t MaximumSeparationSlots = 16U;
                for (std::size_t slot = 0U;
                        slot < MaximumSeparationSlots;
                        ++slot) {
                    // Prefer stacking above the icon. Downward slots are a
                    // bounded fallback for labels already clipped at the top
                    // edge of the display.
                    const auto offset = slot == 0U
                        ? 0.0F
                        : slot <= MaximumSeparationSlots / 2U
                            ? -static_cast<float>(slot) * separation
                            : static_cast<float>(
                                slot - MaximumSeparationSlots / 2U)
                                * separation;
                    const auto candidateTop = std::clamp(
                        anchorRectangle.top + offset,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y));
                    const AutomapLabelRectangle candidate{
                        .left = anchorRectangle.left,
                        .top = candidateTop,
                        .right = anchorRectangle.right,
                        .bottom = candidateTop + textBounds.y,
                    };
                    bool collision{};
                    for (std::size_t drawnIndex = 0U;
                            drawnIndex < drawnExitLabelCount;
                            ++drawnIndex) {
                        if (AutomapLabelRectanglesOverlap(
                                drawnExitLabels[drawnIndex].placedRectangle,
                                candidate,
                                std::max(
                                    2.0F,
                                    3.0F * Settings.overlay.scale))) {
                            collision = true;
                            break;
                        }
                    }
                    if (collision) continue;
                    placedRectangle = candidate;
                    placed = true;
                    break;
                }
                if (!placed) break;
                if (drawnExitLabelCount < drawnExitLabels.size()) {
                    drawnExitLabels[drawnExitLabelCount++] = {
                        .isExitLabel = true,
                        .targetLevelId = poi.sourceId,
                        .anchorRectangle = anchorRectangle,
                        .placedRectangle = placedRectangle,
                    };
                }
                const ImVec2 placedCenter{
                    center.x,
                    center.y + placedRectangle.top - anchorRectangle.top,
                };
                (void)DrawCenteredShadowedText(
                    drawList,
                    io,
                    level->name.utf8.c_str(),
                    placedCenter,
                    fontSize,
                    ToImGuiColor(Settings.objects.exitLabels.color, opacity),
                    opacity,
                    AutomapTextPlacement::AboveIcon,
                    NativeExitIconTopExtent * Settings.overlay.scale,
                    NativeAutomapLabelGap * Settings.overlay.scale);
                break;
            }
            case AutomapPoiKind::WaypointLabel: {
                if (!Settings.objects.waypointLabels.enabled
                        || poi.sourceId <= 0) {
                    break;
                }
                const auto* const level = dataCatalog->FindLevel(poi.sourceId);
                if (level == nullptr || !level->name.localized
                        || level->waypointLabelUtf8.empty()) {
                    break;
                }
                const auto fontSize = std::clamp(
                    Settings.objects.waypointLabels.size
                        * Settings.overlay.scale,
                    MinimumAutomapLabelSize,
                    MaximumAutomapLabelSize);
                auto* const font = SelectAutomapLabelFont(
                    level->waypointLabelUtf8.c_str());
                if (font == nullptr) break;
                const auto textBounds = font->CalcTextSizeA(
                    fontSize,
                    std::max(1.0F, io.DisplaySize.x),
                    0.0F,
                    level->waypointLabelUtf8.c_str());
                const auto textTop = AutomapLabelTopAboveIcon(
                    center.y,
                    textBounds.y,
                    NativeWaypointIconTopExtent * Settings.overlay.scale,
                    NativeWaypointLabelGap * Settings.overlay.scale);
                const auto textPosition = ImVec2{
                    std::clamp(
                        center.x - textBounds.x * 0.5F,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.x - textBounds.x)),
                    std::clamp(
                        textTop,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y)),
                };
                const AutomapLabelRectangle anchorRectangle{
                    .left = textPosition.x,
                    .top = textPosition.y,
                    .right = textPosition.x + textBounds.x,
                    .bottom = textPosition.y + textBounds.y,
                };
                auto placedRectangle = anchorRectangle;
                bool placed{};
                const auto separation = textBounds.y
                    + std::max(4.0F, 5.0F * Settings.overlay.scale);
                constexpr std::size_t MaximumSeparationSlots = 16U;
                for (std::size_t slot = 0U;
                        slot < MaximumSeparationSlots;
                        ++slot) {
                    const auto offset = slot == 0U
                        ? 0.0F
                        : slot <= MaximumSeparationSlots / 2U
                            ? -static_cast<float>(slot) * separation
                            : static_cast<float>(
                                slot - MaximumSeparationSlots / 2U)
                                * separation;
                    const auto candidateTop = std::clamp(
                        anchorRectangle.top + offset,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y));
                    const AutomapLabelRectangle candidate{
                        .left = anchorRectangle.left,
                        .top = candidateTop,
                        .right = anchorRectangle.right,
                        .bottom = candidateTop + textBounds.y,
                    };
                    bool collision{};
                    for (std::size_t drawnIndex = 0U;
                            drawnIndex < drawnExitLabelCount;
                            ++drawnIndex) {
                        if (AutomapLabelRectanglesOverlap(
                                drawnExitLabels[drawnIndex].placedRectangle,
                                candidate,
                                std::max(
                                    2.0F,
                                    3.0F * Settings.overlay.scale))) {
                            collision = true;
                            break;
                        }
                    }
                    if (collision) continue;
                    placedRectangle = candidate;
                    placed = true;
                    break;
                }
                if (!placed) break;
                if (drawnExitLabelCount < drawnExitLabels.size()) {
                    drawnExitLabels[drawnExitLabelCount++] = {
                        .targetLevelId = poi.sourceId,
                        .anchorRectangle = anchorRectangle,
                        .placedRectangle = placedRectangle,
                    };
                }
                const ImVec2 placedCenter{
                    center.x,
                    center.y + placedRectangle.top - anchorRectangle.top,
                };
                (void)DrawCenteredShadowedText(
                    drawList,
                    io,
                    level->waypointLabelUtf8.c_str(),
                    placedCenter,
                    fontSize,
                    ToImGuiColor(
                        Settings.objects.waypointLabels.color,
                        opacity),
                    opacity,
                    AutomapTextPlacement::AboveIcon,
                    NativeWaypointIconTopExtent * Settings.overlay.scale,
                    NativeWaypointLabelGap * Settings.overlay.scale);
                break;
            }
            case AutomapPoiKind::LevelLabel: {
                if (poi.sourceId <= 0) break;
                const auto* const level = dataCatalog->FindLevel(poi.sourceId);
                if (level == nullptr || !level->name.localized
                        || level->name.utf8.empty()) {
                    break;
                }
                const bool waypointStyle = level->hasWaypoint
                    && Detail::AllowsWaypointLabelForLevel(level->id)
                    && Settings.objects.waypointLabels.enabled
                    && !level->waypointLabelUtf8.empty();
                if (!waypointStyle
                        && !Settings.objects.exitLabels.enabled) {
                    break;
                }
                const auto& text = waypointStyle
                    ? level->waypointLabelUtf8
                    : level->name.utf8;
                const auto configuredSize = waypointStyle
                    ? Settings.objects.waypointLabels.size
                    : Settings.objects.exitLabels.size;
                const auto configuredColor = waypointStyle
                    ? Settings.objects.waypointLabels.color
                    : Settings.objects.exitLabels.color;
                const auto fontSize = std::clamp(
                    configuredSize * Settings.overlay.scale,
                    MinimumAutomapLabelSize,
                    MaximumAutomapLabelSize);
                auto* const font = SelectAutomapLabelFont(text.c_str());
                if (font == nullptr) break;
                const auto textBounds = font->CalcTextSizeA(
                    fontSize,
                    std::max(1.0F, io.DisplaySize.x),
                    0.0F,
                    text.c_str());
                const auto centeredTop = center.y - textBounds.y * 0.5F;
                const auto textPosition = ImVec2{
                    std::clamp(
                        center.x - textBounds.x * 0.5F,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.x - textBounds.x)),
                    std::clamp(
                        centeredTop,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y)),
                };
                const AutomapLabelRectangle anchorRectangle{
                    .left = textPosition.x,
                    .top = textPosition.y,
                    .right = textPosition.x + textBounds.x,
                    .bottom = textPosition.y + textBounds.y,
                };
                bool duplicate{};
                for (std::size_t drawnIndex = 0U;
                        drawnIndex < drawnExitLabelCount;
                        ++drawnIndex) {
                    if (drawnExitLabels[drawnIndex].targetLevelId
                            == poi.sourceId) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) break;
                auto placedRectangle = anchorRectangle;
                bool placed{};
                const auto separation = textBounds.y
                    + std::max(4.0F, 5.0F * Settings.overlay.scale);
                constexpr std::size_t MaximumSeparationSlots = 16U;
                for (std::size_t slot = 0U;
                        slot < MaximumSeparationSlots;
                        ++slot) {
                    const auto offset = slot == 0U
                        ? 0.0F
                        : slot <= MaximumSeparationSlots / 2U
                            ? -static_cast<float>(slot) * separation
                            : static_cast<float>(
                                slot - MaximumSeparationSlots / 2U)
                                * separation;
                    const auto candidateTop = std::clamp(
                        anchorRectangle.top + offset,
                        0.0F,
                        std::max(0.0F, io.DisplaySize.y - textBounds.y));
                    const AutomapLabelRectangle candidate{
                        .left = anchorRectangle.left,
                        .top = candidateTop,
                        .right = anchorRectangle.right,
                        .bottom = candidateTop + textBounds.y,
                    };
                    bool collision{};
                    for (std::size_t drawnIndex = 0U;
                            drawnIndex < drawnExitLabelCount;
                            ++drawnIndex) {
                        if (AutomapLabelRectanglesOverlap(
                                drawnExitLabels[drawnIndex].placedRectangle,
                                candidate,
                                std::max(
                                    2.0F,
                                    3.0F * Settings.overlay.scale))) {
                            collision = true;
                            break;
                        }
                    }
                    if (collision) continue;
                    placedRectangle = candidate;
                    placed = true;
                    break;
                }
                if (!placed) break;
                if (drawnExitLabelCount < drawnExitLabels.size()) {
                    drawnExitLabels[drawnExitLabelCount++] = {
                        .targetLevelId = poi.sourceId,
                        .anchorRectangle = anchorRectangle,
                        .placedRectangle = placedRectangle,
                    };
                }
                const ImVec2 placedCenter{
                    center.x,
                    center.y + placedRectangle.top - anchorRectangle.top,
                };
                (void)DrawCenteredShadowedText(
                    drawList,
                    io,
                    text.c_str(),
                    placedCenter,
                    fontSize,
                    ToImGuiColor(configuredColor, opacity),
                    opacity,
                    AutomapTextPlacement::Centered);
                break;
            }
            case AutomapPoiKind::ShrineIcon: {
                // Keep D2R's native shrine marker. MapSense adds only the
                // proximity label; it no longer draws a second shrine icon.
                break;
            }
            case AutomapPoiKind::ShrineLabel: {
                if (!Settings.objects.shrineLabels.enabled
                    || poi.sourceId < 0) {
                    break;
                }
                const auto* const shrine = dataCatalog->FindShrineByInteractType(
                    static_cast<std::uint32_t>(poi.sourceId));
                if (shrine == nullptr || !shrine->name.localized
                    || shrine->name.utf8.empty()) {
                    break;
                }
                const auto fontSize = std::clamp(
                    Settings.objects.shrineLabels.size
                        * Settings.overlay.scale,
                    MinimumAutomapLabelSize,
                    MaximumAutomapLabelSize);
                // The localized buff name is proximity-gated by the native
                // collector and stays clearly above D2R's native marker.
                (void)DrawCenteredShadowedText(
                    drawList,
                    io,
                    shrine->name.utf8.c_str(),
                    center,
                    fontSize,
                    ToImGuiColor(Settings.objects.shrineLabels.color, opacity),
                    opacity,
                    AutomapTextPlacement::AboveIcon,
                    NativeShrineIconTopExtent * Settings.overlay.scale,
                    NativeShrineLabelGap * Settings.overlay.scale);
                break;
            }
            case AutomapPoiKind::Chest: {
                if (!Settings.objects.chests.enabled) break;
                const auto size = std::clamp(
                    Settings.objects.chests.size * Settings.overlay.scale,
                    MinimumAutomapObjectSize,
                    MaximumAutomapObjectSize);
                if (!DrawPrimeMhChestImage(
                        drawList,
                        center,
                        size,
                        primeMhChestTexture,
                        false,
                        opacity)) {
                    DrawFallbackChestIcon(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.outlineColor,
                            opacity),
                        ToImGuiColor(
                            Settings.objects.chests.interiorColor,
                            opacity),
                        ToImGuiColor(
                            Settings.objects.chests.interiorColor,
                            opacity),
                        opacity);
                }
                if ((poi.stateFlags & AutomapPoiStateTrapped) != 0U) {
                    DrawChestStateLock(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.trappedAccentColor,
                            opacity),
                        opacity);
                } else if ((poi.stateFlags & AutomapPoiStateLocked) != 0U) {
                    DrawChestStateLock(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.lockedAccentColor,
                            opacity),
                        opacity);
                }
                break;
            }
            case AutomapPoiKind::SuperChest: {
                if (!Settings.objects.superChests.enabled) break;
                const auto size = std::clamp(
                    Settings.objects.chests.size * Settings.overlay.scale,
                    MinimumAutomapObjectSize,
                    MaximumAutomapObjectSize);
                if (!DrawPrimeMhChestImage(
                        drawList,
                        center,
                        size,
                        primeMhSuperChestTexture,
                        true,
                        opacity)) {
                    DrawFallbackChestIcon(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.outlineColor,
                            opacity),
                        ToImGuiColor(
                            Settings.objects.chests.interiorColor,
                            opacity),
                        ToImGuiColor(
                            Settings.objects.chests.interiorColor,
                            opacity),
                        opacity);
                }
                if ((poi.stateFlags & AutomapPoiStateTrapped) != 0U) {
                    DrawChestStateLock(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.trappedAccentColor,
                            opacity),
                        opacity);
                } else if ((poi.stateFlags & AutomapPoiStateLocked) != 0U) {
                    DrawChestStateLock(
                        drawList,
                        center,
                        size,
                        ToImGuiColor(
                            Settings.objects.chests.lockedAccentColor,
                            opacity),
                        opacity);
                }
                break;
            }
            case AutomapPoiKind::ArmorRack: {
                if (!Settings.objects.armorRacks.enabled) break;
                const auto size = std::clamp(
                    Settings.objects.armorRacks.size * Settings.overlay.scale,
                    MinimumAutomapObjectSize,
                    MaximumAutomapObjectSize);
                DrawArmorRackIcon(
                    drawList,
                    center,
                    size,
                    ToImGuiColor(Settings.objects.armorRacks.color, opacity),
                    opacity);
                break;
            }
            case AutomapPoiKind::WeaponRack: {
                if (!Settings.objects.weaponRacks.enabled) break;
                const auto size = std::clamp(
                    Settings.objects.weaponRacks.size * Settings.overlay.scale,
                    MinimumAutomapObjectSize,
                    MaximumAutomapObjectSize);
                DrawWeaponRackIcon(
                    drawList,
                    center,
                    size,
                    ToImGuiColor(Settings.objects.weaponRacks.color, opacity),
                    opacity);
                break;
            }
            }
        }
}

[[nodiscard]] auto ResolveBossName(
        const MapSenseDataCatalog& dataCatalog,
        const NativeAutomapMarkerSnapshot& marker) noexcept
        -> const DataCatalogLocalizedText* {
    if (!Settings.monsters.superUniqueBoss.showNames) {
        return nullptr;
    }
    if (marker.superUniqueIndex >= 0) {
        const auto* const superUnique = dataCatalog.FindSuperUnique(
            static_cast<std::uint32_t>(marker.superUniqueIndex));
        if (superUnique != nullptr && superUnique->name.localized
            && !superUnique->name.utf8.empty()) {
            return &superUnique->name;
        }
    }
    if (marker.classId < 0) return nullptr;
    const auto* const monStats = dataCatalog.FindMonStats(
        static_cast<std::uint32_t>(marker.classId));
    if (monStats == nullptr
        || !HasNamedBossDisplayIdentity(
            marker.rank,
            marker.superUniqueIndex,
            monStats->primeEvil)
        || !monStats->name.localized || monStats->name.utf8.empty()) {
        return nullptr;
    }
    return &monStats->name;
}

[[nodiscard]] auto HasBossIdentity(
        const std::shared_ptr<const MapSenseDataCatalog>& dataCatalog,
        const NativeAutomapMarkerSnapshot& marker) noexcept -> bool {
    if (marker.rank == MonsterRank::SuperUnique
            || marker.superUniqueIndex >= 0) {
        return true;
    }
    if (dataCatalog == nullptr || marker.classId < 0) return false;
    const auto* const monStats = dataCatalog->FindMonStats(
        static_cast<std::uint32_t>(marker.classId));
    return monStats != nullptr
        && HasNamedBossDisplayIdentity(
            marker.rank,
            marker.superUniqueIndex,
            monStats->primeEvil);
}

[[nodiscard]] auto CountImmunities(std::uint8_t mask) noexcept
        -> std::size_t {
    std::size_t count{};
    while (mask != 0U) {
        count += mask & 1U;
        mask = static_cast<std::uint8_t>(mask >> 1U);
    }
    return count;
}

[[nodiscard]] auto MonsterNameTop(
        const NativeAutomapMarkerSnapshot& marker,
        ImVec2 markerCenter,
        float markerSize,
        float fontSize) noexcept -> float {
    const auto gap = std::max(2.0F, 2.0F * Settings.overlay.scale);
    auto occupiedTop = markerCenter.y - markerSize * 0.5F;
    if (Settings.immunities.enabled && marker.immunityMask != 0U) {
        if (Settings.immunities.style == ImmunityDisplayStyle::ColoredI) {
            auto* const font = ImGui::GetFont();
            const auto indicatorSize = std::clamp(
                Settings.immunities.indicatorSize * Settings.overlay.scale,
                MinimumImmunityIndicatorSize,
                MaximumImmunityIndicatorSize);
            const auto indicatorHeight = font != nullptr
                ? std::max(
                    font->CalcTextSizeA(
                        indicatorSize,
                        1'000.0F,
                        0.0F,
                        "i").y,
                    indicatorSize)
                : indicatorSize;
            const auto rows = (CountImmunities(marker.immunityMask) + 2U)
                / 3U;
            occupiedTop -= gap + static_cast<float>(rows) * indicatorHeight;
        } else {
            const auto thickness = std::clamp(
                Settings.immunities.haloThickness * Settings.overlay.scale,
                MinimumImmunityHaloThickness,
                MaximumImmunityHaloThickness);
            const auto radius = markerSize * 0.5F
                + thickness
                + std::max(1.5F, Settings.overlay.scale * 1.5F);
            occupiedTop = markerCenter.y - radius;
        }
    }
    return occupiedTop - gap - fontSize;
}

void DrawBossName(
        ImDrawList* drawList,
        const ImGuiIO& io,
        const std::shared_ptr<const MapSenseDataCatalog>& dataCatalog,
        const NativeAutomapMarkerSnapshot& marker,
        ImVec2 markerCenter,
        float markerSize,
        float opacity) noexcept {
    if (dataCatalog == nullptr) return;
    const auto* const name = ResolveBossName(*dataCatalog, marker);
    if (name == nullptr) return;
    const auto fontSize = std::clamp(
        Settings.monsters.superUniqueBoss.nameSize * Settings.overlay.scale,
        MinimumAutomapLabelSize,
        MaximumAutomapLabelSize);
    (void)DrawCenteredShadowedText(
        drawList,
        io,
        name->utf8.c_str(),
        ImVec2{markerCenter.x,
            MonsterNameTop(marker, markerCenter, markerSize, fontSize)},
        fontSize,
        ToImGuiColor(
            Settings.monsters.superUniqueBoss.nameColor,
            opacity),
        opacity);
}

struct MonsterMarkerScreenGeometry final {
    ImVec2 center{};
    float size{};
};

[[nodiscard]] auto TryResolveMonsterMarkerScreenGeometry(
        const NativeAutomapMarkerSnapshot& marker,
        const ImGuiIO& io,
        MonsterMarkerScreenGeometry& output) noexcept -> bool {
    if (marker.nativeWidth <= 0 || marker.nativeHeight <= 0) return false;
    const auto scaleX = io.DisplaySize.x
        / static_cast<float>(marker.nativeWidth);
    const auto scaleY = io.DisplaySize.y
        / static_cast<float>(marker.nativeHeight);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
            || scaleX <= 0.0F || scaleY <= 0.0F) {
        return false;
    }
    const auto scaleTolerance = std::max(
        0.01F,
        std::max(scaleX, scaleY) * 0.01F);
    if (std::abs(scaleX - scaleY) > scaleTolerance) return false;

    const auto x = static_cast<float>(marker.x) * scaleX;
    const auto y = static_cast<float>(marker.y) * scaleY;
    if (!std::isfinite(x) || !std::isfinite(y)
            || x < 0.0F || y < 0.0F
            || x >= io.DisplaySize.x || y >= io.DisplaySize.y) {
        return false;
    }
    const auto& style = MarkerStyleFor(marker.rank);
    output = {
        .center = ImVec2{x, y},
        .size = std::clamp(
            style.size * Settings.overlay.scale,
            MinimumMonsterMarkerSize,
            MaximumMonsterMarkerSize),
    };
    return true;
}

[[nodiscard]] auto TryResolveMissileScreenCenter(
        const NativeAutomapMissileSnapshot& missile,
        const ImGuiIO& io,
        ImVec2& center) noexcept -> bool {
    if (missile.nativeWidth <= 0 || missile.nativeHeight <= 0) return false;
    const auto scaleX = io.DisplaySize.x
        / static_cast<float>(missile.nativeWidth);
    const auto scaleY = io.DisplaySize.y
        / static_cast<float>(missile.nativeHeight);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY)
            || scaleX <= 0.0F || scaleY <= 0.0F) {
        return false;
    }
    const auto scaleTolerance = std::max(
        0.01F,
        std::max(scaleX, scaleY) * 0.01F);
    if (std::abs(scaleX - scaleY) > scaleTolerance) return false;

    center = ImVec2{
        static_cast<float>(missile.x) * scaleX,
        static_cast<float>(missile.y) * scaleY,
    };
    return std::isfinite(center.x) && std::isfinite(center.y)
        && center.x >= 0.0F && center.y >= 0.0F
        && center.x < io.DisplaySize.x && center.y < io.DisplaySize.y;
}

void DrawNativeMissileSnapshots(
        ImDrawList* drawList,
        const ImGuiIO& io,
        const std::shared_ptr<const MapSenseDataCatalog>& dataCatalog,
        float opacity) noexcept {
    if (drawList == nullptr || dataCatalog == nullptr
        || !Settings.missiles.enabled || MissileSnapshots.empty()) {
        return;
    }
    const auto styleFor = [](DataCatalogMissileElement element) noexcept
            -> const MissileMarkerStyle* {
        switch (element) {
            case DataCatalogMissileElement::Physical:
                return &Settings.missiles.physical;
            case DataCatalogMissileElement::Fire:
                return &Settings.missiles.fire;
            case DataCatalogMissileElement::Cold:
                return &Settings.missiles.cold;
            case DataCatalogMissileElement::Lightning:
                return &Settings.missiles.lightning;
            case DataCatalogMissileElement::Poison:
                return &Settings.missiles.poison;
            case DataCatalogMissileElement::Magic:
                return &Settings.missiles.magic;
            case DataCatalogMissileElement::Hidden:
                return nullptr;
        }
        return nullptr;
    };
    const auto missileFamilyState = dataCatalog->FamilyStatus(
        DataCatalogFamily::Missiles).state;
    const auto stockFallbackIsSafe =
        missileFamilyState == DataCatalogFamilyState::Unavailable
        || missileFamilyState == DataCatalogFamilyState::VanillaFallbackTxt;
    for (const auto& missile : MissileSnapshots) {
        if (missile.classId < 0) continue;
        const auto* const definition = dataCatalog->FindMissile(
            static_cast<std::uint32_t>(missile.classId));
        const auto element = definition != nullptr
            ? definition->element
            : stockFallbackIsSafe
                ? StockDataCatalogMissileElement(
                    static_cast<std::uint32_t>(missile.classId))
                : DataCatalogMissileElement::Hidden;
        const auto* const style = styleFor(element);
        if (style == nullptr) continue;
        ImVec2 center{};
        if (!TryResolveMissileScreenCenter(missile, io, center)) continue;
        const auto radiusX = std::clamp(
            style->size * Settings.overlay.scale,
            MinimumMissileMarkerSize,
            MaximumMissileMarkerSize);
        drawList->AddEllipseFilled(
            center,
            ImVec2{radiusX, radiusX * 0.5F},
            ToImGuiColor(style->color, opacity));
    }
}

void DrawMapSenseOwnedOverlay(void*) noexcept {
    if (!WantsMapSenseOwnedOverlay(nullptr)) return;

    RequestNativeUiPanelVisibilityRefresh();
    NativeUiStateStatus nativeUi{};
    if (!AcquireNativeUiStateStatus(nativeUi)) return;
    const auto retainCurrentProjection = nativeUi.retainAutomapProjection;
    const auto navigationLineCount = AcquireNavigationLineSnapshots(
        NavigationLineSnapshots,
        retainCurrentProjection);
    const auto markerCount = AcquireNativeAutomapMarkers(
        MarkerSnapshots,
        retainCurrentProjection);
    const auto missileCount = AcquireNativeAutomapMissiles(MissileSnapshots);
    const auto poiCount = AcquireNativeAutomapPoiSnapshots(
        PoiSnapshots,
        retainCurrentProjection);
    if (navigationLineCount == 0U && markerCount == 0U
        && missileCount == 0U && poiCount == 0U) {
        return;
    }

    const auto& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0.0F || io.DisplaySize.y <= 0.0F) {
        return;
    }
    NativeAutomapViewportSnapshot viewport{};
    NativeAutomapClipBounds nativeClip{};
    if (!AcquireNativeAutomapViewport(viewport, retainCurrentProjection)
        || !TryResolveNativeAutomapClipBounds(viewport, nativeClip)) {
        return;
    }
    NativeUiMapHorizontalClip panelClip{};
    if (!TryResolveNativeUiMapHorizontalClip(
            viewport.nativeWidth,
            viewport.nativeHeight,
            nativeUi.activeMask,
            panelClip)) {
        return;
    }
    nativeClip.left = std::max(nativeClip.left, panelClip.left);
    nativeClip.right = std::min(nativeClip.right, panelClip.right);
    if (nativeClip.right <= nativeClip.left) return;
    const auto viewportScaleX = io.DisplaySize.x
        / static_cast<float>(viewport.nativeWidth);
    const auto viewportScaleY = io.DisplaySize.y
        / static_cast<float>(viewport.nativeHeight);
    const auto viewportScaleTolerance = std::max(
        0.01F,
        std::max(viewportScaleX, viewportScaleY) * 0.01F);
    if (!std::isfinite(viewportScaleX)
        || !std::isfinite(viewportScaleY)
        || viewportScaleX <= 0.0F || viewportScaleY <= 0.0F
        || std::abs(viewportScaleX - viewportScaleY)
            > viewportScaleTolerance) {
        return;
    }
    const ImVec2 clipMinimum{
        static_cast<float>(nativeClip.left) * viewportScaleX,
        static_cast<float>(nativeClip.top) * viewportScaleY,
    };
    const ImVec2 clipMaximum{
        static_cast<float>(nativeClip.right) * viewportScaleX,
        static_cast<float>(nativeClip.bottom) * viewportScaleY,
    };
    if (!std::isfinite(clipMinimum.x) || !std::isfinite(clipMinimum.y)
        || !std::isfinite(clipMaximum.x) || !std::isfinite(clipMaximum.y)
        || clipMaximum.x <= clipMinimum.x
        || clipMaximum.y <= clipMinimum.y) {
        return;
    }
    const auto opacity = std::clamp(
        Settings.overlay.opacity,
        0.10F,
        1.0F);
    const auto dataCatalog = DataCatalog.load(std::memory_order_acquire);
    auto* const drawList = ImGui::GetForegroundDrawList();
    // D2R submits the native automap before its panels, so later panel artwork
    // hides it even though AutomapContext still spans the complete viewport.
    // Intersect that viewport with the current native UI-state side-panel clip
    // so every MapSense map primitive follows the same visible region. The
    // MapSense launcher/settings menu is drawn independently.
    drawList->PushClipRect(clipMinimum, clipMaximum, true);
    // Generated terrain now lives exclusively in D2R's native cell trees.
    // This overlay owns only navigation, POI, missile, monster, immunity,
    // boss-name and protected text primitives; it never submits a second map.
    // Navigation and translucent POIs remain below live monster identity.
    // Monster ranks are then submitted from
    // weakest to strongest so dense packs cannot bury minions, uniques,
    // superuniques, or native MonStats bosses.
    DrawNavigationLines(drawList, io, opacity);
    DrawAutomapPoiSnapshots(
        drawList,
        io,
        dataCatalog,
        opacity,
        AutomapPoiRenderPass::Objects);
    // Element-aware missiles remain below monster identity and protected text.
    DrawNativeMissileSnapshots(drawList, io, dataCatalog, opacity);
    if (Settings.monsters.enabled) {
        for (std::uint8_t layer = 0U;
                layer <= MaximumMonsterMarkerRenderLayer;
                ++layer) {
            for (const auto& marker : MarkerSnapshots) {
                const auto bossIdentity = HasBossIdentity(dataCatalog, marker);
                if (MonsterMarkerRenderLayer(marker.rank, bossIdentity) != layer) {
                    continue;
                }
                MonsterMarkerScreenGeometry geometry{};
                if (!TryResolveMonsterMarkerScreenGeometry(marker, io, geometry)) {
                    continue;
                }
                const auto& style = MarkerStyleFor(marker.rank);
                const auto color = ToImGuiColor(style.color, opacity);
                switch (style.shape) {
                case MonsterMarkerShape::X: {
                    const auto thickness = std::clamp(
                        style.thickness * Settings.overlay.scale,
                        MinimumMonsterMarkerThickness,
                        MaximumMonsterMarkerThickness);
                    DrawClosedMarkerOutline(
                        drawList,
                        BuildCrossOutline(
                            Vec2{geometry.center.x, geometry.center.y},
                            geometry.size * 0.5F),
                        color,
                        thickness);
                    break;
                }
                case MonsterMarkerShape::PlayerCross: {
                    const auto thickness = std::clamp(
                        style.thickness * Settings.overlay.scale,
                        MinimumMonsterMarkerThickness,
                        MaximumMonsterMarkerThickness);
                    const auto outline = BuildPlayerCrossOutline(
                        Vec2{geometry.center.x, geometry.center.y},
                        geometry.size);
                    const auto edgeThickness = thickness
                        + std::max(1.0F, Settings.overlay.scale);
                    DrawClosedMarkerOutline(
                        drawList,
                        outline,
                        ToImGuiColor(
                            RgbaColor{0.02F, 0.015F, 0.01F, 0.80F},
                            opacity),
                        edgeThickness);
                    DrawClosedMarkerOutline(
                        drawList,
                        outline,
                        color,
                        thickness);
                    break;
                }
                case MonsterMarkerShape::Dot: {
                    constexpr float OutlineThickness = 1.0F;
                    const auto radius = geometry.size * 0.5F;
                    const auto outlineColor = ToImGuiColor(
                        RgbaColor{0.02F, 0.015F, 0.01F, 0.72F},
                        opacity);
                    drawList->AddCircleFilled(
                        geometry.center,
                        radius + OutlineThickness,
                        outlineColor);
                    drawList->AddCircleFilled(
                        geometry.center,
                        radius,
                        color);
                    break;
                }
                }
                DrawMonsterImmunities(
                    drawList,
                    marker,
                    geometry.center,
                    geometry.size,
                    opacity);
            }
        }
        // Text always wins the final compositing pass. A normal/minion marker
        // that shares the same screen pixels cannot cover a boss name.
        for (const auto& marker : MarkerSnapshots) {
            MonsterMarkerScreenGeometry geometry{};
            if (!TryResolveMonsterMarkerScreenGeometry(marker, io, geometry)) {
                continue;
            }
            DrawBossName(
                drawList,
                io,
                dataCatalog,
                marker,
                geometry.center,
                geometry.size,
                opacity);
        }
    }
    // Exit, waypoint, and shrine labels own the final protected layer. No
    // monster icon, immunity badge, or boss name can paint over these
    // player-facing names.
    DrawAutomapPoiSnapshots(
        drawList,
        io,
        dataCatalog,
        opacity,
        AutomapPoiRenderPass::ProtectedLabels);
    drawList->PopClipRect();
}

void LogRendererInfo(const char* message) noexcept {
    if (Context != nullptr && message != nullptr) Context->LogInfo(message);
}

void LogRendererWarning(const char* message) noexcept {
    if (Context != nullptr && message != nullptr) Context->LogWarn(message);
}

void __cdecl DrainHostUiTask(
        const D2RL::PluginContext*,
        void*) noexcept {
    D3D12ImGuiUiTaskCallback task{};
    void* taskData{};
    {
        std::scoped_lock lock(HostUiTaskMutex);
        task = PendingHostUiTask;
        taskData = PendingHostUiTaskData;
        PendingHostUiTask = nullptr;
        PendingHostUiTaskData = nullptr;
        HostUiTaskScheduled = false;
    }
    if (task != nullptr) task(taskData);
}

auto QueueHostUiTask(
        D3D12ImGuiUiTaskCallback task,
        void* taskData,
        void*) noexcept -> bool {
    if (task == nullptr || Context == nullptr || ThreadService == nullptr
        || ThreadService->runOnUiThread == nullptr) {
        return false;
    }

    {
        std::scoped_lock lock(HostUiTaskMutex);
        if (HostUiTaskScheduled) return false;
        PendingHostUiTask = task;
        PendingHostUiTaskData = taskData;
        HostUiTaskScheduled = true;
    }

    if (ThreadService->runOnUiThread(Context, DrainHostUiTask, nullptr)
        == D2RL::Threads::Result::Success) {
        return true;
    }

    {
        std::scoped_lock lock(HostUiTaskMutex);
        PendingHostUiTask = nullptr;
        PendingHostUiTaskData = nullptr;
        HostUiTaskScheduled = false;
    }
    return false;
}

void OnOwnedOverlayDismissal(void*) noexcept {
    // The host serializes this callback with complete Present submissions.
    // Preserve resolver destinations and diagnostics while removing every
    // MapSense-owned automap pixel before D2R handles Tab or Escape itself.
    InvalidateNavigationProjection();
    InvalidateNativeAutomapMarkerFrame();
    InvalidateNativeAutomapPoiFrame();
}

[[nodiscard]] auto IsSafeActiveModFontToken(
        std::string_view value) noexcept -> bool {
    if (value.empty() || value == "." || value == "..") return false;
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x20U || character == '/' || character == '\\'
            || character == ':') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto EndsWithMpqFontSuffix(
        std::string_view value) noexcept -> bool {
    if (value.size() < 4U) return false;
    constexpr std::string_view suffix{".mpq"};
    const auto offset = value.size() - suffix.size();
    for (std::size_t index = 0U; index < suffix.size(); ++index) {
        auto character = value[offset + index];
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
        if (character != suffix[index]) return false;
    }
    return true;
}

void ConfigureD2RAutomapFont(
        const D2RL::PluginContext* context) noexcept {
    SetD3D12ImGuiAutomapFontPath(nullptr);
    if (context == nullptr) return;
    constexpr auto ActiveModEnd =
        offsetof(D2RL::PluginContext, activeMod) + sizeof(const char*);
    constexpr auto ModDirectoryEnd =
        offsetof(D2RL::PluginContext, modDirectory)
        + sizeof(const wchar_t*);
    if (static_cast<std::size_t>(context->contextSize) < ActiveModEnd
        || static_cast<std::size_t>(context->contextSize) < ModDirectoryEnd
        || context->modDirectory == nullptr
        || context->modDirectory[0] == L'\0') {
        return;
    }

    try {
        const std::filesystem::path root(context->modDirectory);
        std::vector<std::filesystem::path> candidates;
        candidates.reserve(2U);
        if (context->activeMod != nullptr) {
            std::size_t length{};
            constexpr std::size_t MaximumActiveModBytes = 256U;
            while (length < MaximumActiveModBytes
                && context->activeMod[length] != '\0') {
                ++length;
            }
            if (length < MaximumActiveModBytes) {
                std::string packageName(context->activeMod, length);
                if (IsSafeActiveModFontToken(packageName)) {
                    if (!EndsWithMpqFontSuffix(packageName))
                        packageName += ".mpq";
                    const auto* const begin =
                        reinterpret_cast<const char8_t*>(packageName.data());
                    const std::filesystem::path package(std::u8string(
                        begin,
                        begin + packageName.size()));
                    candidates.push_back(
                        root / package / L"data" / L"hd" / L"ui"
                            / L"fonts" / L"exocetblizzardot-medium.otf");
                }
            }
        }
        candidates.push_back(
            root / L"data" / L"hd" / L"ui" / L"fonts"
                / L"exocetblizzardot-medium.otf");
        for (const auto& candidate : candidates) {
            std::error_code error;
            if (!std::filesystem::is_regular_file(candidate, error)
                || error) {
                continue;
            }
            SetD3D12ImGuiAutomapFontPath(candidate.c_str());
            if (Context != nullptr)
                Context->LogInfo(
                    "MapSense: D2R Exocet automap font found in the active mod package.");
            return;
        }
    } catch (...) {
    }
    if (Context != nullptr)
        Context->LogWarn(
            "MapSense: D2R Exocet font was not exposed by the active mod; localized system font fallback remains active.");
}

void ConfigureAutomapSpriteAtlas() noexcept {
    // The former external terrain renderer is intentionally retired. Keeping
    // this path empty prevents its duplicate-map texture from being decoded
    // or uploaded while preserving every other ImGui texture.
    SetD3D12ImGuiAutomapSpritePath(nullptr);
}

DWORD WINAPI HostRetryWorkerMain(void*) noexcept {
    while (HostRetryStopEvent != nullptr
        && WaitForSingleObject(HostRetryStopEvent, 500U) == WAIT_TIMEOUT) {
        if (TryInstallD3D12ImGuiHooks()) return 0U;
    }
    return 0U;
}

auto StartImGuiHost() noexcept -> bool {
    const D3D12ImGuiHostCallbacks callbacks{
        .drawPanel = DrawMapSensePanel,
        .resolveMenuScale = ResolveMapSensePanelScale,
        .drawOwnedOverlay = DrawMapSenseOwnedOverlay,
        .wantsOwnedOverlay = WantsMapSenseOwnedOverlay,
        .ownedOverlayDismissal = OnOwnedOverlayDismissal,
        .userData = nullptr,
        .queueUiTask = QueueHostUiTask,
        .uiDispatcherUserData = nullptr,
        .info = LogRendererInfo,
        .warning = LogRendererWarning,
    };
    SetD3D12ImGuiMenuOpen(false);
    if (InitializeD3D12ImGuiHost(callbacks)) return true;

    HostRetryStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (HostRetryStopEvent == nullptr) return false;
    HostRetryWorker = CreateThread(
        nullptr, 0U, HostRetryWorkerMain, nullptr, 0U, nullptr);
    if (HostRetryWorker != nullptr) return true;
    CloseHandle(HostRetryStopEvent);
    HostRetryStopEvent = nullptr;
    return false;
}

void StopImGuiHost() noexcept {
    if (HostRetryStopEvent != nullptr) SetEvent(HostRetryStopEvent);
    if (HostRetryWorker != nullptr) {
        // The retry worker only performs bounded local D3D12 discovery. It
        // must be fully joined before this DLL can unload its hook code.
        WaitForSingleObject(HostRetryWorker, INFINITE);
        CloseHandle(HostRetryWorker);
        HostRetryWorker = nullptr;
    }
    if (HostRetryStopEvent != nullptr) {
        CloseHandle(HostRetryStopEvent);
        HostRetryStopEvent = nullptr;
    }
    ShutdownD3D12ImGuiHostAndNotifyClients();
}

enum class RendererHandoffResult {
    FloatingDamageAbsent,
    SafeForMapSense,
    Failed,
};

auto GiveMapSenseRendererPriority() noexcept -> RendererHandoffResult {
    const auto canonicalFloatingDamage = GetModuleHandleW(
        L"d2rl-ruffneckk-floating-damage.dll");
    const auto legacyFloatingDamage = GetModuleHandleW(
        L"RuffnecKkFloatingDamage.dll");
    if (canonicalFloatingDamage != nullptr && legacyFloatingDamage != nullptr
        && canonicalFloatingDamage != legacyFloatingDamage) {
        LogRendererWarning(
            "MapSense: canonical and legacy Floating Damage modules are both loaded; renderer handoff refused.");
        return RendererHandoffResult::Failed;
    }
    if (const auto floatingDamage = canonicalFloatingDamage != nullptr
            ? canonicalFloatingDamage
            : legacyFloatingDamage) {
        using YieldFn = bool(__cdecl*)() noexcept;
        const auto yield = reinterpret_cast<YieldFn>(GetProcAddress(
            floatingDamage,
            "RuffnecKkFloatingDamageUseMapSenseOverlayHost"));
        if (yield != nullptr && yield()) {
            LogRendererInfo(
                "MapSense: Floating Damage confirmed priority MapSense renderer ownership.");
            return RendererHandoffResult::SafeForMapSense;
        }
        LogRendererWarning(
            "MapSense: Floating Damage is present but renderer ownership could not be transferred safely.");
        return RendererHandoffResult::Failed;
    }
    return RendererHandoffResult::FloatingDamageAbsent;
}

} // namespace

extern "C" __declspec(dllexport)
const RuffnecKk::OverlayHost::HostApiV2* __cdecl
RuffnecKkMapSenseGetOverlayHostApi(
        std::uint32_t requestedVersion,
        std::uint32_t callerStructSize) noexcept {
    static const RuffnecKk::OverlayHost::HostApiV2 api{
        .structSize = RuffnecKk::OverlayHost::HostApiV2Size,
        .version = RuffnecKk::OverlayHost::ApiVersion2,
        .imguiAbiFingerprint =
            RuffnecKk::OverlayHost::ImGuiAbiFingerprint,
        .registerClient = RegisterD3D12ImGuiClientV2,
        .unregisterClient = UnregisterD3D12ImGuiClientV2,
    };
    if (!HostApiAvailable.load(std::memory_order_acquire)
        || requestedVersion != api.version
        || callerStructSize < api.structSize) {
        return nullptr;
    }
    return &api;
}

} // namespace RuffnecKk::MapSense

namespace {

constexpr D2RL::PluginInfo PluginInfo{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-mapsense",
    .name = "RuffnecKk MapSense",
    .version = "1.0.2",
    .author = "RuffnecKk",
    .description = "Reveals maps, marks monsters, and draws direct navigation lines.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept
        -> const D2RL::PluginInfo* {
    return &PluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(
        const D2RL::PluginContext* context) noexcept -> bool {
    using namespace RuffnecKk::MapSense;
    if (!D2RL::HasContext(context)
        || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    ResetUiLanguage();
    Operational.store(false, std::memory_order_release);
    FeaturesEnabled.store(false, std::memory_order_release);
    HostApiAvailable.store(false, std::memory_order_release);
    GameplayReady.store(false, std::memory_order_release);
    MenuExpanded.store(false, std::memory_order_release);
    MarkerAvailable.store(false, std::memory_order_release);
    PoiRuntimeAvailable.store(false, std::memory_order_release);
    PoiAvailable.store(false, std::memory_order_release);
    SessionEpoch.store(1, std::memory_order_release);
    CurrentSessionGeneration.store(0U, std::memory_order_release);
    LastDynamicNavigationRefreshTick.store(0U, std::memory_order_release);
    LastNativeUiPanelRefreshRequestTick.store(0U, std::memory_order_release);
    NativeUiPanelRefreshQueued.store(false, std::memory_order_release);
    DataCatalog.store({}, std::memory_order_release);
    {
        QueueLockGuard lock;
        ActionQueueHead = 0;
        ActionQueueSize = 0;
        ActionDrainScheduled = false;
    }
    {
        std::scoped_lock lock(ConfigSaveMutex);
        PendingConfigSave.clear();
        ConfigSaveDrainScheduled = false;
    }
    {
        std::scoped_lock lock(HostUiTaskMutex);
        PendingHostUiTask = nullptr;
        PendingHostUiTaskData = nullptr;
        HostUiTaskScheduled = false;
    }
    CancelPendingNavigationRefresh(true);
    ResetRevealReplayProcessState();
    if (!LoadConfig(Settings)) return false;
    FeaturesEnabled.store(
        Settings.enabled,
        std::memory_order_release);

    try {
        MarkerSnapshots.clear();
        MarkerSnapshots.reserve(MaximumRecentNativeAutomapMarkers);
        MissileSnapshots.clear();
        MissileSnapshots.reserve(MaximumNativeAutomapMissiles);
        PoiSnapshots.clear();
        PoiSnapshots.reserve(MaximumAutomapPoiSnapshots);
    } catch (...) {
        context->LogError(
            "MapSense: the bounded renderer marker/POI snapshot reserve could not be allocated.");
        return false;
    }

    const auto* const buildName = D2RL::GetBuildName(context);
    char buildDiagnostic[160]{};
    std::snprintf(
        buildDiagnostic,
        sizeof(buildDiagnostic),
        "MapSense: D2R build-name '%s' is diagnostic only; native compatibility is decided by the complete fail-closed fingerprint.",
        buildName != nullptr ? buildName : "unavailable");
    context->LogInfo(buildDiagnostic);

    if (!ValidateSFillLocationDiagnosticSuppression(context)) return false;

    if (context->QueryService(
            D2RL::ServiceId::Input,
            D2RL::InputServiceV1Version,
            &InputService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasInputServiceV1Field(
            InputService, D2RL::InputServiceV1RequiredSize)
        || InputService->registerAction == nullptr
        || InputService->unregisterAction == nullptr) {
        context->LogError(
            "MapSense: D2RLoader InputService v1 is unavailable.");
        return false;
    }
    if (context->QueryService(
            D2RL::ServiceId::Thread,
            D2RL::ThreadServiceV1Version,
            &ThreadService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceV1Field(
            ThreadService, D2RL::ThreadServiceV1RequiredSize)
        || ThreadService->runOnUiThread == nullptr) {
        context->LogError(
            "MapSense: D2RLoader ThreadService v1 is unavailable.");
        return false;
    }
    if (!InitializeNavigationRefreshTimer()) {
        context->LogError(
            "MapSense: the bounded navigation retry timer could not be created.");
        return false;
    }
    if (!InitializeRevealReplayTimer()) {
        context->LogError(
            "MapSense: the bounded reveal persistence retry timer could not be created.");
        ShutdownNavigationRefreshTimer();
        return false;
    }
    if (context->QueryService(
            D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version,
            &LifecycleService) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceV1Field(
            LifecycleService, D2RL::LifecycleServiceV1RequiredSize)
        || LifecycleService->registerGameplayEventListener == nullptr
        || LifecycleService->unregisterGameplayEventListener == nullptr) {
        context->LogError(
            "MapSense: D2RLoader LifecycleService v1 is unavailable.");
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        return false;
    }

    if (!RegisterInputActions() || !RegisterLifecycleListeners()) {
        UnregisterServices();
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        return false;
    }
    if (!InitializeRevealEngine(context, Settings.diagnostics)) {
        UnregisterServices();
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        return false;
    }
    InitializeNavigationEngine();
    if (!InitializeNavigationResolver(
            context,
            Settings.diagnostics)) {
        context->LogWarn(
            "MapSense: Direct navigation is unavailable because its D2R runtime proofs did not validate; Reveal and monster markers remain active.");
    }
    const auto markerAvailable = InitializeNativeAutomapMarker(
        context,
        Settings.diagnostics);
    MarkerAvailable.store(markerAvailable, std::memory_order_release);
    const auto poiRuntimeAvailable = markerAvailable
        && InitializeNativeAutomapPoi(context, Settings.diagnostics);
    PoiRuntimeAvailable.store(poiRuntimeAvailable, std::memory_order_release);
    PoiAvailable.store(false, std::memory_order_release);
    SetRevealLevelInitializedCallback(
        OnRevealLevelInitialized,
        nullptr);
    if (markerAvailable) {
        SetNativeAutomapLevelObservedCallback(
            OnNativeAutomapLevelObserved,
            nullptr);
        SetNativeAutomapMarkerEnabled(Settings.enabled);
        SetNativeAutomapMissileEnabled(
            Settings.enabled && Settings.missiles.enabled);
        SetNativeAutomapImmunityCollectionEnabled(
            Settings.enabled
                && Settings.monsters.enabled
                && Settings.immunities.enabled);
    } else {
        context->LogWarn(
            "MapSense: monster markers and native navigation projection are unavailable; Reveal and the settings panel remain active.");
    }
    if (poiRuntimeAvailable) {
        context->LogInfo(
            "MapSense: localized labels and data-driven objects are pending D2R language initialization.");
    } else {
        context->LogWarn(
            "MapSense: localized automap labels and data-driven object markers are unavailable; Reveal, navigation, and independently validated monster markers remain active.");
    }
    const auto nativeUiTelemetryAvailable = InitializeNativeUiState(context);
    if (!nativeUiTelemetryAvailable) {
        context->LogWarn(
            "MapSense: native panel-state fingerprint is unavailable; MapSense map pixels will fail closed while the launcher remains available.");
    }
    MenuExpanded.store(
        Settings.menu.startExpanded || Settings.overlay.startMenuOpen,
        std::memory_order_release);
    HostApiAvailable.store(true, std::memory_order_release);
    if (GiveMapSenseRendererPriority()
        == RendererHandoffResult::Failed) {
        HostApiAvailable.store(false, std::memory_order_release);
        SetNativeAutomapLevelObservedCallback(nullptr, nullptr);
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        ShutdownNativeAutomapMarker();
        ShutdownNativeAutomapPoi();
        ShutdownNavigationResolver();
        ShutdownNavigationEngine();
        StopImGuiHost();
        ShutdownNativeUiState();
        ShutdownRevealEngine();
        UnregisterServices();
        PoiAvailable.store(false, std::memory_order_release);
        PoiRuntimeAvailable.store(false, std::memory_order_release);
        DataCatalog.store({}, std::memory_order_release);
        context->LogError(
            "MapSense: renderer startup was refused to prevent competing DirectX 12 owners.");
        return false;
    }
    ConfigureD2RAutomapFont(context);
    ConfigureAutomapSpriteAtlas();
    if (!StartImGuiHost()) {
        HostApiAvailable.store(false, std::memory_order_release);
        SetNativeAutomapLevelObservedCallback(nullptr, nullptr);
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        ShutdownNativeAutomapMarker();
        ShutdownNativeAutomapPoi();
        ShutdownNavigationResolver();
        ShutdownNavigationEngine();
        StopImGuiHost();
        ShutdownNativeUiState();
        ShutdownRevealEngine();
        UnregisterServices();
        PoiAvailable.store(false, std::memory_order_release);
        PoiRuntimeAvailable.store(false, std::memory_order_release);
        DataCatalog.store({}, std::memory_order_release);
        context->LogError(
            "MapSense: the in-frame DirectX 12/ImGui host could not start or schedule a retry.");
        return false;
    }

    if (!InstallSFillLocationDiagnosticSuppression(context)) {
        HostApiAvailable.store(false, std::memory_order_release);
        SetNativeAutomapLevelObservedCallback(nullptr, nullptr);
        ShutdownRevealReplayTimer();
        ShutdownNavigationRefreshTimer();
        ShutdownNativeAutomapMarker();
        ShutdownNativeAutomapPoi();
        ShutdownNavigationResolver();
        ShutdownNavigationEngine();
        StopImGuiHost();
        ShutdownNativeUiState();
        ShutdownRevealEngine();
        UnregisterServices();
        PoiAvailable.store(false, std::memory_order_release);
        PoiRuntimeAvailable.store(false, std::memory_order_release);
        DataCatalog.store({}, std::memory_order_release);
        return false;
    }

    const auto externalLabelsAvailable = poiRuntimeAvailable
        && InitializeExternalLabelProvider(
            context,
            Settings.diagnostics,
            OnExternalLabelAtlasResult,
            nullptr);
    if (!externalLabelsAvailable) {
        context->LogWarn(
            "MapSense: seed-scoped distant labels are unavailable; native Reveal remains active without loading distant rooms.");
    }

    auto registration = D2RL::MakeConsoleCommand(
        "mapsense",
        MapSenseCommand,
        "Control MapSense reveal modes and its in-game settings panel.");
    registration.usage = "mapsense [status|level|act|all|off|menu]";
    if (!context->RegisterConsoleCommand(registration)) {
        context->LogWarn(
            "MapSense: console command registration failed; Controls actions remain available.");
    }

    Operational.store(true, std::memory_order_release);
    char loadedMessage[384]{};
    std::snprintf(
        loadedMessage,
        sizeof(loadedMessage),
        "RuffnecKk MapSense 1.0.2 loaded; labels/objects=%s; native-seed-atlas=%s; monster-markers=%s; Direct-navigation=%s; Reveal/settings=active; native-panel-occlusion=%s.",
        poiRuntimeAvailable ? "pending-localization" : "unavailable",
        externalLabelsAvailable ? "active" : "unavailable",
        markerAvailable ? "active" : "unavailable",
        IsNavigationResolverActive() ? "active" : "unavailable",
        nativeUiTelemetryAvailable ? "active" : "fail-closed");
    context->LogInfo(loadedMessage);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace RuffnecKk::MapSense;
    Operational.store(false, std::memory_order_release);
    FeaturesEnabled.store(false, std::memory_order_release);
    HostApiAvailable.store(false, std::memory_order_release);
    GameplayReady.store(false, std::memory_order_release);
    MarkerAvailable.store(false, std::memory_order_release);
    PoiRuntimeAvailable.store(false, std::memory_order_release);
    PoiAvailable.store(false, std::memory_order_release);
    CurrentSessionGeneration.store(0U, std::memory_order_release);
    LastDynamicNavigationRefreshTick.store(0U, std::memory_order_release);
    LastNativeUiPanelRefreshRequestTick.store(0U, std::memory_order_release);
    NativeUiPanelRefreshQueued.store(false, std::memory_order_release);
    SessionEpoch.fetch_add(1, std::memory_order_acq_rel);
    SetRevealLevelInitializedCallback(nullptr, nullptr);
    SetNativeAutomapLevelObservedCallback(nullptr, nullptr);
    CancelPendingNavigationRefresh(true);
    CancelPendingRevealReplay();
    ShutdownExternalLabelProvider();
    ShutdownRevealReplayTimer();
    ShutdownNavigationRefreshTimer();
    ShutdownNativeAutomapMarker();
    ShutdownNativeAutomapPoi();
    ShutdownNavigationResolver();
    ShutdownNavigationEngine();
    StopImGuiHost();
    ShutdownNativeUiState();
    {
        std::scoped_lock lock(ConfigSaveMutex);
        PendingConfigSave.clear();
        ConfigSaveDrainScheduled = false;
    }
    {
        std::scoped_lock lock(HostUiTaskMutex);
        PendingHostUiTask = nullptr;
        PendingHostUiTaskData = nullptr;
        HostUiTaskScheduled = false;
    }
    ShutdownRevealEngine();
    UnregisterServices();
    ShutdownSFillLocationDiagnosticSuppression();
    PoiSnapshots.clear();
    DataCatalog.store({}, std::memory_order_release);
    ResetUiLanguage();
    InputService = nullptr;
    LifecycleService = nullptr;
    ThreadService = nullptr;
    Context = nullptr;
}
