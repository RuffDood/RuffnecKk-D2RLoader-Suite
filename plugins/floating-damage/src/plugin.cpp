#include <D2RLPlugin/api.h>

// The player-facing feature design originates from Fr4nsson's D2R Damage
// Numbers project (MIT). This SDK-v3 lifecycle, governed 92777 hook, and native
// projection cache are RuffnecKk adaptations around D2RHUD-derived rendering
// and presentation code. locbones authorized that code's use, modification,
// and redistribution on 2026-08-16.

#include "config_parser.hpp"
#include "d3d12_renderer.hpp"
#include "default_config.hpp"
#include "floating_damage.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr std::uintptr_t HitpointsCommitContextRva = 0x44D083;
constexpr std::uintptr_t HitpointsCommitCallRva = 0x44D093;
constexpr std::uintptr_t GetUnitStatRva = 0x2F5020;
constexpr std::uintptr_t SetUnitStatRva = 0x2F7D10;
constexpr std::uintptr_t GetClientUnitRva = 0x09A5D0;
constexpr std::uintptr_t UpdateCameraRva = 0x0B9B90;
constexpr std::uintptr_t GetRenderThreadContextRootRva = 0x685750;
constexpr std::uintptr_t ProjectUnitToScreenRva = 0x76A7D0;
constexpr std::uintptr_t GetNativeHeightRva = 0x07F4A0;
constexpr std::uintptr_t GetNativeWidthRva = 0x07F510;
constexpr std::uint16_t CriticalStrikeResultFlag = 0x2000;
constexpr std::uint32_t MonsterUnitType = 1;
constexpr std::int32_t HitPointsStatId = 6;

constexpr std::size_t DamagePhysicalOffset = 0x018;
constexpr std::size_t DamageFireOffset = 0x020;
constexpr std::size_t DamageLightningOffset = 0x02C;
constexpr std::size_t DamageMagicOffset = 0x030;
constexpr std::size_t DamageColdOffset = 0x034;
constexpr std::size_t DamagePoisonOffset = 0x038;

const D2RL::PluginContext* Context{};
const D2RL::LifecycleServiceV1* LifecycleService{};
const D2RL::InputServiceV1* InputService{};
D2RL::Input::ActionHandle ToggleAction{D2RL::Input::InvalidHandle};
D2RL::Lifecycle::ListenerHandle GameJoinedListener{
    D2RL::Lifecycle::InvalidHandle};
D2RL::Lifecycle::ListenerHandle GameLeftListener{
    D2RL::Lifecycle::InvalidHandle};
D2RL::Lifecycle::ListenerHandle LocalPlayerReadyListener{
    D2RL::Lifecycle::InvalidHandle};
std::uint8_t* Base{};
HMODULE Module{};
std::atomic<std::uint64_t> CapturedEvents{};
std::atomic<std::uint64_t> DisplayedEvents{};
std::atomic<std::uint64_t> ProjectionSuccesses{};
std::atomic<std::uint64_t> ProjectionFailures{};
std::atomic_bool ProjectionReadyLogged{};
std::atomic<bool> OverlayReady{};
std::atomic_bool RuntimeActive{};
HANDLE OverlayStopEvent{};
HANDLE OverlayWorker{};
void* HitpointsCommitRelay{};

#pragma pack(push, 1)
struct UnitView {
    std::uint32_t unitType;
    std::uint32_t classId;
    std::uint32_t unitId;
    std::uint32_t mode;
};
#pragma pack(pop)

struct NativeScreenPoint {
    float x;
    float y;
};

using GetUnitStatFn = std::int32_t(__fastcall*)(
    UnitView*, std::int32_t, std::uint16_t) noexcept;
using SetUnitStatFn = void(__fastcall*)(
    UnitView*, std::int32_t, std::int32_t, std::uint16_t) noexcept;
using GetClientUnitFn = UnitView*(__fastcall*)(
    std::uint32_t unitId, std::uint32_t unitType) noexcept;
using ProjectUnitToScreenFn = bool(__fastcall*)(
    void* renderContext,
    UnitView* unit,
    NativeScreenPoint* point,
    bool useUnitHeight) noexcept;
using UpdateCameraFn = void(__fastcall*)() noexcept;
using GetRenderThreadContextRootFn = void*(__fastcall*)() noexcept;
using GetNativeDimensionFn = std::int32_t(__fastcall*)() noexcept;
GetUnitStatFn GetUnitStat{};
SetUnitStatFn SetUnitStat{};
GetClientUnitFn GetClientUnit{};
ProjectUnitToScreenFn ProjectUnitToScreen{};
UpdateCameraFn OriginalUpdateCamera{};
GetRenderThreadContextRootFn GetRenderThreadContextRoot{};
GetNativeDimensionFn GetNativeHeight{};
GetNativeDimensionFn GetNativeWidth{};

constexpr std::size_t ProjectionCacheSize = 8192;
constexpr std::uint64_t ProjectionFreshnessMs = 250;
constexpr std::size_t ProjectionRequestCapacity = 1024;
constexpr std::uint64_t ProjectionRequestLeaseMs = 300;
constexpr std::uint64_t ProjectionSweepIntervalMs = 4;

struct ProjectionCacheEntry {
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> key{};
    std::atomic<std::uint64_t> elevatedPoint{};
    std::atomic<std::uint64_t> elevatedTick{};
    std::atomic<std::uint64_t> basePoint{};
    std::atomic<std::uint64_t> baseTick{};
    std::atomic<std::uint64_t> attemptTick{};
    std::atomic_bool visible{};
};

struct ProjectionRequestSlot {
    std::atomic_flag writing = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> key{};
    std::atomic<std::uint64_t> requestedUntil{};
};

std::array<ProjectionCacheEntry, ProjectionCacheSize> ProjectionCache{};
std::array<ProjectionRequestSlot, ProjectionRequestCapacity>
    ProjectionRequests{};
std::atomic<std::int32_t> CachedNativeWidth{};
std::atomic<std::int32_t> CachedNativeHeight{};
std::atomic<std::uint64_t> NativeDimensionsRefreshTick{};
std::atomic<std::uint64_t> LastProjectionSweepTick{};
std::atomic<std::uint64_t> ActiveProjectionAttempts{};
std::atomic<std::uint64_t> ActiveProjectionMisses{};
std::atomic<std::uint64_t> ProjectionRequestDrops{};
std::atomic<std::uint64_t> CameraFrameTicks{};
std::atomic<std::uint64_t> RenderContextMisses{};
std::atomic_bool CameraFrameReadyLogged{};
thread_local bool ProjectionSweepActive{};

void __cdecl LogOverlayDiagnostic(const char* message) noexcept {
    if (Context && message
            && FloatingDamage::GetConfig().diagnosticsEnabled)
        Context->LogInfo(message);
}

constexpr D2RL::PluginInfo Info{
    .infoSize = D2RL::PluginInfoSize,
    .apiVersion = D2RL_PLUGIN_API_VERSION,
    .id = "ruffneckk-floating-damage",
    .name = "Floating Damage",
    .version = "1.4.0",
    .author = "RuffnecKk",
    .description = "Shows floating combat numbers and rolling damage per second.",
    .flags = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

std::string Trim(std::string_view value) {
    std::size_t first{};
    while (first < value.size()
            && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return std::string(value.substr(first, last - first));
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool LoadConfig() {
    constexpr std::size_t MaximumConfigBytes = 65'536;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t required{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &required)) {
        Context->LogError(required >= buffer.size()
            ? "FloatingDamage: configuration exceeds 65535 bytes."
            : "FloatingDamage: configuration could not be read.");
        return false;
    }

    FloatingDamage::Config parsed{};
    std::string error;
    if (!FloatingDamage::ParseConfigToml(
            std::string_view(buffer.data()),
            parsed,
            error)) {
        const std::string message =
            "FloatingDamage: invalid TOML (" + error
            + "); no hook or renderer was changed.";
        Context->LogError(message.c_str());
        return false;
    }

    FloatingDamage::GetConfig() = parsed;
    FloatingDamage::SetEnabled(parsed.enabled);
    D3D12::SetDiagnosticLogCallback(
        parsed.diagnosticsEnabled ? LogOverlayDiagnostic : nullptr);
    return true;
}

auto __cdecl OnToggleInputAction(
    const D2RL::PluginContext*,
    const D2RL::Input::ActionEvent* event,
    void*
) noexcept -> D2RL::Input::ActionResult {
    if (D2RL::Input::HasActionEventField(
            event, D2RL::Input::ActionEventRequiredSize)
            && event->kind == D2RL::Input::ActionEventKind::Pressed) {
        FloatingDamage::RequestToggle();
    }
    return D2RL::Input::ActionResult::Ignored;
}

void UnregisterInputAction() noexcept {
    if (InputService && Context
            && ToggleAction != D2RL::Input::InvalidHandle) {
        (void)InputService->unregisterAction(Context, ToggleAction);
    }
    ToggleAction = D2RL::Input::InvalidHandle;
    InputService = nullptr;
}

bool RegisterInputAction() noexcept {
    if (Context->QueryService(
            D2RL::ServiceId::Input,
            D2RL::InputServiceV1Version,
            &InputService) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasInputServiceV1Field(
                InputService, D2RL::InputServiceV1RequiredSize)
            || InputService->registerAction == nullptr
            || InputService->unregisterAction == nullptr) {
        Context->LogError(
            "FloatingDamage: D2RLoader Input service v1 is unavailable.");
        InputService = nullptr;
        return false;
    }

    const D2RL::Input::ActionRegistration registration{
        .structSize = D2RL::Input::ActionRegistrationSize,
        .flags = 0,
        .logicalId = "toggle-floating-damage",
        .displayName = "Toggle Floating Damage",
        .category = "RuffnecKk Suite",
        .defaultPrimary = {
            D2RL::Input::Key::Z,
            D2RL::Input::Modifier::Shift,
        },
        .defaultSecondary = {
            D2RL::Input::Key::None,
            D2RL::Input::Modifier::None,
        },
        .callback = OnToggleInputAction,
        .userData = nullptr,
    };
    const auto result = InputService->registerAction(
        Context, &registration, &ToggleAction);
    if (result == D2RL::Input::Result::Success
            && ToggleAction != D2RL::Input::InvalidHandle) {
        return true;
    }

    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "FloatingDamage: Input v1 action registration failed with result %u.",
        static_cast<unsigned>(result));
    Context->LogError(message);
    ToggleAction = D2RL::Input::InvalidHandle;
    InputService = nullptr;
    return false;
}

void __cdecl OnGameplayLifecycle(
    const D2RL::PluginContext*,
    const D2RL::Lifecycle::GameplayEvent* event,
    void*) noexcept {
    if (!D2RL::Lifecycle::HasGameplayEventField(
            event, D2RL::Lifecycle::GameplayEventRequiredSize)) {
        return;
    }

    switch (event->kind) {
    case D2RL::Lifecycle::GameplayEventKind::GameJoined:
    case D2RL::Lifecycle::GameplayEventKind::GameLeft:
        FloatingDamage::SetGameplayActive(false);
        break;
    case D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady:
        FloatingDamage::SetGameplayActive(true);
        break;
    default:
        break;
    }
}

void UnregisterLifecycleListeners() noexcept {
    const auto unregister = [](D2RL::Lifecycle::ListenerHandle& handle) {
        if (LifecycleService && Context
                && handle != D2RL::Lifecycle::InvalidHandle) {
            (void)LifecycleService->unregisterGameplayEventListener(
                Context, handle);
        }
        handle = D2RL::Lifecycle::InvalidHandle;
    };
    unregister(LocalPlayerReadyListener);
    unregister(GameLeftListener);
    unregister(GameJoinedListener);
}

bool RegisterLifecycleListener(
    D2RL::Lifecycle::GameplayEventKind kind,
    D2RL::Lifecycle::ListenerHandle& handle,
    const char* failureMessage) noexcept {
    const D2RL::Lifecycle::GameplayEventListener listener{
        .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
        .flags = 0,
        .kind = kind,
        .reserved = 0,
        .callback = OnGameplayLifecycle,
        .userData = nullptr,
    };
    const auto result = LifecycleService->registerGameplayEventListener(
        Context, &listener, &handle);
    if (result == D2RL::Lifecycle::Result::Success
            && handle != D2RL::Lifecycle::InvalidHandle) {
        return true;
    }
    if (handle != D2RL::Lifecycle::InvalidHandle) {
        (void)LifecycleService->unregisterGameplayEventListener(
            Context, handle);
    }
    if (Context) Context->LogError(failureMessage);
    handle = D2RL::Lifecycle::InvalidHandle;
    return false;
}

bool RegisterLifecycleListeners() noexcept {
    if (Context->QueryService(
            D2RL::ServiceId::Lifecycle,
            D2RL::LifecycleServiceV1Version,
            &LifecycleService) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceV1Field(
                LifecycleService,
                D2RL::LifecycleServiceV1RequiredSize)
            || LifecycleService->registerGameplayEventListener == nullptr
            || LifecycleService->unregisterGameplayEventListener == nullptr) {
        Context->LogError(
            "FloatingDamage: D2RLoader Lifecycle service v1 is unavailable.");
        LifecycleService = nullptr;
        return false;
    }

    if (!RegisterLifecycleListener(
            D2RL::Lifecycle::GameplayEventKind::GameJoined,
            GameJoinedListener,
            "FloatingDamage: GameJoined listener registration failed.")) {
        LifecycleService = nullptr;
        return false;
    }
    if (!RegisterLifecycleListener(
            D2RL::Lifecycle::GameplayEventKind::GameLeft,
            GameLeftListener,
            "FloatingDamage: GameLeft listener registration failed.")) {
        UnregisterLifecycleListeners();
        LifecycleService = nullptr;
        return false;
    }
    if (!RegisterLifecycleListener(
            D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady,
            LocalPlayerReadyListener,
            "FloatingDamage: LocalPlayerReady listener registration failed.")) {
        UnregisterLifecycleListeners();
        LifecycleService = nullptr;
        return false;
    }
    return true;
}

bool SaveEnabled(bool enabled) {
    constexpr std::size_t MaximumConfigBytes = 65'536;
    std::array<char, MaximumConfigBytes> buffer{};
    std::uint32_t required{};
    if (!Context->ReadConfig(
            buffer.data(),
            static_cast<std::uint32_t>(buffer.size()),
            &required)) {
        return false;
    }

    std::string text(buffer.data());
    FloatingDamage::Config current{};
    std::string error;
    if (!FloatingDamage::ParseConfigToml(text, current, error)) return false;

    std::string table;
    bool replaced{};
    std::size_t cursor{};
    while (cursor < text.size()) {
        const std::size_t newline = text.find('\n', cursor);
        const std::size_t lineEnd = newline == std::string::npos
            ? text.size()
            : newline;
        std::string_view rawLine(text.data() + cursor, lineEnd - cursor);
        if (!rawLine.empty() && rawLine.back() == '\r')
            rawLine.remove_suffix(1);
        const std::size_t comment = rawLine.find('#');
        const std::string content = Trim(rawLine.substr(
            0,
            comment == std::string_view::npos
                ? rawLine.size()
                : comment));
        if (!content.empty() && content.front() == '[') {
            table = content.substr(1, content.size() - 2);
        } else if (table == "general") {
            const std::size_t equals = rawLine.find('=');
            if (equals != std::string_view::npos
                    && Trim(rawLine.substr(0, equals)) == "enabled") {
                std::size_t valueStart = equals + 1;
                while (valueStart < rawLine.size()
                        && (rawLine[valueStart] == ' '
                            || rawLine[valueStart] == '\t')) {
                    ++valueStart;
                }
                std::size_t valueEnd = comment == std::string_view::npos
                    ? rawLine.size()
                    : comment;
                while (valueEnd > valueStart
                        && (rawLine[valueEnd - 1] == ' '
                            || rawLine[valueEnd - 1] == '\t')) {
                    --valueEnd;
                }
                text.replace(
                    cursor + valueStart,
                    valueEnd - valueStart,
                    enabled ? "true" : "false");
                replaced = true;
                break;
            }
        }
        cursor = newline == std::string::npos ? text.size() : newline + 1;
    }
    if (!replaced) return false;

    FloatingDamage::Config updated{};
    if (!FloatingDamage::ParseConfigToml(text, updated, error)
            || updated.enabled != enabled) {
        return false;
    }
    return Context->WriteConfig(text.c_str());
}
constexpr std::uint64_t MakeProjectionKey(
    std::uint32_t unitType,
    std::uint32_t unitId) noexcept {
    return (static_cast<std::uint64_t>(unitType) << 32) | unitId;
}

constexpr std::size_t ProjectionCacheIndex(std::uint64_t key) noexcept {
    key ^= key >> 33;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33;
    return static_cast<std::size_t>(key) & (ProjectionCacheSize - 1);
}

constexpr std::size_t ProjectionRequestIndex(std::uint64_t key) noexcept {
    key ^= key >> 33;
    key *= UINT64_C(0xc4ceb9fe1a85ec53);
    key ^= key >> 33;
    return static_cast<std::size_t>(key)
        & (ProjectionRequestCapacity - 1);
}

static_assert((ProjectionCacheSize & (ProjectionCacheSize - 1)) == 0);
static_assert(
    (ProjectionRequestCapacity & (ProjectionRequestCapacity - 1)) == 0);
static_assert(ProjectionRequestCapacity >= 320);

void RequestTargetProjection(
    std::uint32_t unitType,
    std::uint32_t unitId) noexcept {
    if (!FloatingDamage::IsGameplayActive()
            || unitType != MonsterUnitType)
        return;

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t requestedUntil = now + ProjectionRequestLeaseMs;
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    const std::size_t start = ProjectionRequestIndex(key);
    for (std::size_t probe = 0;
            probe < ProjectionRequestCapacity;
            ++probe) {
        ProjectionRequestSlot& slot = ProjectionRequests[
            (start + probe) & (ProjectionRequestCapacity - 1)];
        if (slot.writing.test(std::memory_order_acquire))
            continue;
        const std::uint64_t currentKey = slot.key.load(
            std::memory_order_acquire);
        if (currentKey == key) {
            if (slot.writing.test_and_set(std::memory_order_acquire))
                continue;
            if (slot.key.load(std::memory_order_relaxed) == key) {
                const std::uint64_t lockedUntil = slot.requestedUntil.load(
                    std::memory_order_relaxed);
                slot.requestedUntil.store(
                    std::max(lockedUntil, requestedUntil),
                    std::memory_order_relaxed);
                slot.writing.clear(std::memory_order_release);
                return;
            }
            slot.writing.clear(std::memory_order_release);
            continue;
        }

        const std::uint64_t currentUntil = slot.requestedUntil.load(
            std::memory_order_acquire);
        if (currentKey != 0 && currentUntil >= now)
            continue;
        if (slot.writing.test_and_set(std::memory_order_acquire))
            continue;

        const std::uint64_t lockedKey = slot.key.load(
            std::memory_order_relaxed);
        const std::uint64_t lockedUntil = slot.requestedUntil.load(
            std::memory_order_relaxed);
        if (lockedKey == key || lockedKey == 0 || lockedUntil < now) {
            slot.requestedUntil.store(
                requestedUntil, std::memory_order_relaxed);
            slot.key.store(key, std::memory_order_release);
            slot.writing.clear(std::memory_order_release);
            return;
        }
        slot.writing.clear(std::memory_order_release);
    }

    ProjectionRequestDrops.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t PackNativePoint(const NativeScreenPoint& point) noexcept {
    const std::uint64_t x = std::bit_cast<std::uint32_t>(point.x);
    const std::uint64_t y = std::bit_cast<std::uint32_t>(point.y);
    return x | (y << 32);
}

NativeScreenPoint UnpackNativePoint(std::uint64_t packed) noexcept {
    return NativeScreenPoint{
        std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
        std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32)),
    };
}

void RefreshNativeDimensions(std::uint64_t now) noexcept {
    const std::uint64_t previous = NativeDimensionsRefreshTick.load(
        std::memory_order_relaxed);
    if (CachedNativeWidth.load(std::memory_order_relaxed) > 0
            && CachedNativeHeight.load(std::memory_order_relaxed) > 0
            && now - previous < 1000) {
        return;
    }

    std::uint64_t expected = previous;
    if (!NativeDimensionsRefreshTick.compare_exchange_strong(
            expected, now, std::memory_order_acq_rel)) {
        return;
    }

    __try {
        const std::int32_t width = GetNativeWidth ? GetNativeWidth() : 0;
        const std::int32_t height = GetNativeHeight ? GetNativeHeight() : 0;
        if (width > 0 && height > 0) {
            CachedNativeWidth.store(width, std::memory_order_release);
            CachedNativeHeight.store(height, std::memory_order_release);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CacheNativeProjection(
    UnitView* unit,
    const NativeScreenPoint& point,
    bool elevated) noexcept {
    __try {
        if (!unit
                || unit->unitType != MonsterUnitType
                || !std::isfinite(point.x)
                || !std::isfinite(point.y)) {
            return;
        }

        const std::uint64_t now = GetTickCount64();
        const std::uint64_t key = MakeProjectionKey(unit->unitType, unit->unitId);
        ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
        if (entry.writing.test_and_set(std::memory_order_acquire))
            return;

        if (entry.key.load(std::memory_order_relaxed) != key) {
            entry.elevatedTick.store(0, std::memory_order_relaxed);
            entry.baseTick.store(0, std::memory_order_relaxed);
        }
        const std::uint64_t packed = PackNativePoint(point);
        if (elevated) {
            entry.elevatedPoint.store(packed, std::memory_order_relaxed);
            entry.elevatedTick.store(now, std::memory_order_relaxed);
        }
        else {
            entry.basePoint.store(packed, std::memory_order_relaxed);
            entry.baseTick.store(now, std::memory_order_relaxed);
        }
        entry.visible.store(true, std::memory_order_relaxed);
        entry.attemptTick.store(now, std::memory_order_relaxed);
        entry.key.store(key, std::memory_order_release);
        entry.writing.clear(std::memory_order_release);
        RefreshNativeDimensions(now);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void CacheNativeProjectionFailure(
    std::uint32_t unitType,
    std::uint32_t unitId,
    std::uint64_t now) noexcept {
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
    if (entry.writing.test_and_set(std::memory_order_acquire))
        return;
    if (entry.key.load(std::memory_order_relaxed) != key) {
        entry.elevatedTick.store(0, std::memory_order_relaxed);
        entry.baseTick.store(0, std::memory_order_relaxed);
    }
    entry.visible.store(false, std::memory_order_relaxed);
    entry.attemptTick.store(now, std::memory_order_relaxed);
    entry.key.store(key, std::memory_order_release);
    entry.writing.clear(std::memory_order_release);
}

void ProjectRequestedTargets(
    void* renderContext,
    std::uint64_t now) noexcept {
    if (!renderContext || !ProjectUnitToScreen || !GetClientUnit)
        return;
    if (ProjectionSweepActive)
        return;

    std::uint64_t previous = LastProjectionSweepTick.load(
        std::memory_order_relaxed);
    if (now - previous < ProjectionSweepIntervalMs
            || !LastProjectionSweepTick.compare_exchange_strong(
                previous, now, std::memory_order_acq_rel)) {
        return;
    }

    ProjectionSweepActive = true;
    for (ProjectionRequestSlot& slot : ProjectionRequests) {
        if (slot.writing.test(std::memory_order_acquire))
            continue;
        const std::uint64_t key = slot.key.load(std::memory_order_acquire);
        const std::uint64_t requestedUntil = slot.requestedUntil.load(
            std::memory_order_acquire);
        if (key == 0 || requestedUntil < now
                || slot.writing.test(std::memory_order_acquire)
                || slot.key.load(std::memory_order_acquire) != key) {
            continue;
        }

        const std::uint32_t unitType = static_cast<std::uint32_t>(key >> 32);
        const std::uint32_t unitId = static_cast<std::uint32_t>(key);
        bool projected{};
        __try {
            UnitView* target = GetClientUnit(unitId, unitType);
            if (target) {
                NativeScreenPoint point{};
                projected = ProjectUnitToScreen(
                    renderContext, target, &point, true);
                if (projected) {
                    CacheNativeProjection(target, point, true);
                }
                else {
                    projected = ProjectUnitToScreen(
                        renderContext, target, &point, false);
                    if (projected)
                        CacheNativeProjection(target, point, false);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            projected = false;
        }

        ActiveProjectionAttempts.fetch_add(1, std::memory_order_relaxed);
        if (!projected) {
            ActiveProjectionMisses.fetch_add(1, std::memory_order_relaxed);
            CacheNativeProjectionFailure(unitType, unitId, now);
        }
    }
    ProjectionSweepActive = false;
}

void __fastcall HookUpdateCamera() noexcept {
    if (OriginalUpdateCamera)
        OriginalUpdateCamera();

    if (!FloatingDamage::IsGameplayActive())
        return;

    CameraFrameTicks.fetch_add(1, std::memory_order_relaxed);
    void* renderContext{};
    __try {
        auto* root = static_cast<std::uint8_t*>(
            GetRenderThreadContextRoot
                ? GetRenderThreadContextRoot()
                : nullptr);
        if (root)
            renderContext = *reinterpret_cast<void**>(root + 0x20);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        renderContext = nullptr;
    }

    if (!renderContext) {
        RenderContextMisses.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (FloatingDamage::GetConfig().diagnosticsEnabled
            && !CameraFrameReadyLogged.exchange(
                true, std::memory_order_acq_rel)
            && Context) {
        Context->LogInfo(
            "FloatingDamage camera-frame projection rendezvous ready on D2R's native render thread.");
    }
    ProjectRequestedTargets(renderContext, GetTickCount64());
}

bool TryReadCachedNativeProjection(
    std::uint32_t unitType,
    std::uint32_t unitId,
    NativeScreenPoint& point) noexcept {
    const std::uint64_t key = MakeProjectionKey(unitType, unitId);
    ProjectionCacheEntry& entry = ProjectionCache[ProjectionCacheIndex(key)];
    if (entry.writing.test(std::memory_order_acquire)
            || entry.key.load(std::memory_order_acquire) != key) {
        return false;
    }

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t attemptTick = entry.attemptTick.load(
        std::memory_order_relaxed);
    if (attemptTick == 0
            || now - attemptTick > ProjectionFreshnessMs
            || !entry.visible.load(std::memory_order_relaxed)) {
        return false;
    }
    const std::uint64_t elevatedTick = entry.elevatedTick.load(
        std::memory_order_relaxed);
    const std::uint64_t baseTick = entry.baseTick.load(
        std::memory_order_relaxed);
    std::uint64_t packed{};
    if (elevatedTick != 0 && now - elevatedTick <= ProjectionFreshnessMs) {
        packed = entry.elevatedPoint.load(std::memory_order_relaxed);
    }
    else if (baseTick != 0 && now - baseTick <= ProjectionFreshnessMs) {
        packed = entry.basePoint.load(std::memory_order_relaxed);
    }
    else {
        return false;
    }

    if (entry.writing.test(std::memory_order_acquire)
            || entry.key.load(std::memory_order_acquire) != key) {
        return false;
    }
    point = UnpackNativePoint(packed);
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool TryProjectTargetToScreen(
    std::uint32_t unitType,
    std::uint32_t unitId,
    float displayWidth,
    float displayHeight,
    float* screenX,
    float* screenY) noexcept {
    if (!FloatingDamage::IsGameplayActive()
            || unitType != MonsterUnitType
            || !screenX
            || !screenY
            || displayWidth <= 0.0f
            || displayHeight <= 0.0f) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    RequestTargetProjection(unitType, unitId);

    NativeScreenPoint native{};
    const std::int32_t nativeWidth = CachedNativeWidth.load(
        std::memory_order_acquire);
    const std::int32_t nativeHeight = CachedNativeHeight.load(
        std::memory_order_acquire);
    if (nativeWidth <= 0
            || nativeHeight <= 0
            || !TryReadCachedNativeProjection(unitType, unitId, native)) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    const float projectedX = native.x * displayWidth / static_cast<float>(nativeWidth);
    const float projectedY = native.y * displayHeight / static_cast<float>(nativeHeight);
    if (!std::isfinite(projectedX)
            || !std::isfinite(projectedY)
            || projectedX < 0.0f
            || projectedX > displayWidth
            || projectedY < 0.0f
            || projectedY > displayHeight) {
        ProjectionFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    *screenX = projectedX;
    *screenY = projectedY;
    const std::uint64_t success = ProjectionSuccesses.fetch_add(
        1, std::memory_order_relaxed) + 1;
    if (success == 1
            && FloatingDamage::GetConfig().diagnosticsEnabled
            && !ProjectionReadyLogged.exchange(true, std::memory_order_acq_rel)
            && Context) {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage render-thread projection cache ready: target=%u; native=%.1f,%.1f/%dx%d; overlay=%.1f,%.1f/%.0fx%.0f.",
            unitId,
            native.x,
            native.y,
            nativeWidth,
            nativeHeight,
            projectedX,
            projectedY,
            displayWidth,
            displayHeight);
        Context->LogInfo(message);
    }
    return true;
}

bool IsCritical(void* damage) noexcept {
    __try {
        if (!damage) return false;
        const auto flags = *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(damage) + 4);
        return (flags & CriticalStrikeResultFlag) != 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

FloatingDamage::Element ElementFromDamage(const void* damage) noexcept {
    struct Component {
        std::size_t offset;
        FloatingDamage::Element element;
    };
    constexpr std::array components{
        Component{DamagePhysicalOffset, FloatingDamage::Element::Physical},
        Component{DamageFireOffset, FloatingDamage::Element::Fire},
        Component{DamageLightningOffset, FloatingDamage::Element::Lightning},
        Component{DamageMagicOffset, FloatingDamage::Element::Magic},
        Component{DamageColdOffset, FloatingDamage::Element::Cold},
        Component{DamagePoisonOffset, FloatingDamage::Element::Poison},
    };

    __try {
        if (!damage) return FloatingDamage::Element::Physical;
        std::int32_t largest{};
        FloatingDamage::Element result = FloatingDamage::Element::Physical;
        for (const auto& component : components) {
            const auto value = *reinterpret_cast<const std::int32_t*>(
                static_cast<const std::uint8_t*>(damage) + component.offset);
            if (value > largest) {
                largest = value;
                result = component.element;
            }
        }
        return result;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return FloatingDamage::Element::Physical;
    }
}

bool TryGetMonsterId(UnitView* target, std::uint32_t& unitId) noexcept {
    __try {
        if (!target || target->unitType != MonsterUnitType) return false;
        unitId = target->unitId;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool TryGetFixedHitpoints(UnitView* target, std::int32_t& hitpoints) noexcept {
    __try {
        if (!target || !GetUnitStat) return false;
        hitpoints = GetUnitStat(target, HitPointsStatId, 0);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

constexpr std::int32_t VisibleHitpoints(std::int32_t fixedHitpoints) noexcept {
    return fixedHitpoints > 0 ? fixedHitpoints >> 8 : 0;
}

constexpr std::int32_t VisibleHitpointLoss(
    std::int32_t beforeFixed,
    std::int32_t afterFixed) noexcept {
    const std::int32_t before = VisibleHitpoints(beforeFixed);
    const std::int32_t after = VisibleHitpoints(afterFixed);
    return before > after ? before - after : 0;
}

static_assert(VisibleHitpointLoss(20 * 256, 20 * 256 - 1023) == 4);
static_assert(VisibleHitpointLoss(20 * 256, 20 * 256 - 794) == 4);
static_assert(VisibleHitpointLoss(20 * 256 - 794, 20 * 256 - 1588) == 3);

__declspec(noinline) void __fastcall HookHitpointsCommit(
    UnitView* target,
    std::int32_t statId,
    std::int32_t newFixed,
    std::uint16_t layer,
    UnitView*,
    void* damage
) noexcept {
    std::uint32_t targetId{};
    std::int32_t beforeFixed{};
    const bool observe = FloatingDamage::IsGameplayActive()
        && statId == HitPointsStatId
        && layer == 0
        && TryGetMonsterId(target, targetId)
        && TryGetFixedHitpoints(target, beforeFixed);
    const bool critical = observe && IsCritical(damage);
    const FloatingDamage::Element element = observe
        ? ElementFromDamage(damage)
        : FloatingDamage::Element::Physical;

    SetUnitStat(target, statId, newFixed, layer);
    if (!observe) return;

    std::int32_t afterFixed{};
    if (!TryGetFixedHitpoints(target, afterFixed)) return;
    const std::int32_t amount = VisibleHitpointLoss(beforeFixed, afterFixed);
    if (amount <= 0) return;

    const std::uint64_t captured = CapturedEvents.fetch_add(1, std::memory_order_relaxed) + 1;
    if (captured == 1 && Context
            && FloatingDamage::GetConfig().diagnosticsEnabled) {
        char message[192]{};
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage captured its first committed visible HP loss: fixed=%d->%d; popup=%d.",
            beforeFixed,
            afterFixed,
            amount);
        Context->LogInfo(message);
    }
    if (!FloatingDamage::IsEnabled()) return;

    RequestTargetProjection(MonsterUnitType, targetId);
    FloatingDamage::QueueGameDamage(
        amount,
        MonsterUnitType,
        targetId,
        critical ? FloatingDamage::Kind::Critical : FloatingDamage::Kind::Normal,
        element);
    const std::uint64_t displayed = DisplayedEvents.fetch_add(1, std::memory_order_relaxed) + 1;
    if (displayed == 1 && Context
            && FloatingDamage::GetConfig().diagnosticsEnabled) {
        Context->LogInfo("FloatingDamage queued its first committed target-monster HP loss.");
    }
}

template <std::size_t Size>
bool MatchesSignature(
    std::uintptr_t rva,
    const std::array<std::uint8_t, Size>& expected) noexcept {
    return Context && Context->CheckExpectedBytes(
        rva,
        expected.data(),
        static_cast<std::uint32_t>(expected.size()));
}

template <std::size_t Size>
bool ValidateComposableSetUnitStatEntry(
    const std::array<std::uint8_t, Size>& expected) noexcept {
    const D2RL::DiagnosticsServiceV1* diagnostics{};
    if (!Context
            || Context->QueryService(
                D2RL::ServiceId::Diagnostics,
                D2RL::DiagnosticsServiceV1Version,
                &diagnostics) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDiagnosticsServiceV1Field(
                diagnostics,
                D2RL::DiagnosticsServiceV1RequiredSize)
            || !diagnostics->queryHookStatus) {
        return MatchesSignature(SetUnitStatRva, expected);
    }

    D2RL::Diagnostics::HookQuery query{
        .structSize = D2RL::Diagnostics::HookQuerySize,
        .rva = SetUnitStatRva,
        .expected = expected.data(),
        .expectedSize = static_cast<std::uint32_t>(expected.size()),
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    if (diagnostics->queryHookStatus(Context, &query, &status)
            != D2RL::Diagnostics::Result::Success) {
        Context->LogError(
            "FloatingDamage: Diagnostics v1 could not inspect the shared STATLIST_SetUnitStat entry.");
        return false;
    }
    if (status.state == D2RL::Diagnostics::ModificationState::Unchanged)
        return true;
    if (status.state != D2RL::Diagnostics::ModificationState::Tracked
            || status.kind != D2RL::Diagnostics::ModificationKind::InlineHook
            || status.ownerCount != 1
            || status.ownerPluginId[0] == '\0') {
        char message[256]{};
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage: shared STATLIST_SetUnitStat entry is not composable (state=%u; kind=%u; owners=%u).",
            static_cast<unsigned>(status.state),
            static_cast<unsigned>(status.kind),
            status.ownerCount);
        Context->LogError(message);
        return false;
    }

    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "FloatingDamage: composing through loader-owned STATLIST_SetUnitStat inline hook (%.*s).",
        63,
        status.ownerPluginId);
    Context->LogInfo(message);
    return true;
}

void* AllocateRelayPageNear(void* hint) noexcept {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(
        systemInfo.dwAllocationGranularity);
    const auto base = reinterpret_cast<std::uintptr_t>(hint)
        & ~(granularity - 1);
    for (std::uintptr_t delta = granularity;
            delta < UINT64_C(0x70000000); delta += granularity) {
        if (base > std::numeric_limits<std::uintptr_t>::max() - delta) break;
        if (auto* memory = VirtualAlloc(
                reinterpret_cast<void*>(base + delta),
                systemInfo.dwPageSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return memory;
        }
    }
    return nullptr;
}

bool CreateHitpointsCommitRelay() noexcept {
    HitpointsCommitRelay = AllocateRelayPageNear(Base + HitpointsCommitCallRva);
    if (!HitpointsCommitRelay) return false;

    std::array<std::uint8_t, 31> relay{
        0x48,0x83,0xEC,0x38,
        0x4C,0x89,0x74,0x24,0x20,
        0x48,0x89,0x7C,0x24,0x28,
        0x48,0xB8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0xFF,0xD0,
        0x48,0x83,0xC4,0x38,
        0xC3,
    };
    const auto hookAddress = reinterpret_cast<std::uintptr_t>(
        &HookHitpointsCommit);
    std::memcpy(relay.data() + 16, &hookAddress, sizeof(hookAddress));
    std::memcpy(HitpointsCommitRelay, relay.data(), relay.size());

    DWORD previousProtection{};
    if (!VirtualProtect(
            HitpointsCommitRelay,
            relay.size(),
            PAGE_EXECUTE_READ,
            &previousProtection)) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    FlushInstructionCache(
        GetCurrentProcess(), HitpointsCommitRelay, relay.size());

    const auto relayAddress = reinterpret_cast<std::uintptr_t>(
        HitpointsCommitRelay);
    const auto baseAddress = reinterpret_cast<std::uintptr_t>(Base);
    if (relayAddress < baseAddress
            || relayAddress - baseAddress
                > std::numeric_limits<std::uint32_t>::max()) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    return true;
}

bool InstallDamageHook() noexcept {
    constexpr std::array<std::uint8_t, 29> getUnitStatExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,
        0x41,0x0F,0xB7,0xE8,0x8B,0xFA,0x48,0x8B,0xD9,
    };
    constexpr std::array<std::uint8_t, 29> setUnitStatExpected{
        0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,
        0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,
        0xEC,0x40,0x45,0x0F,0xB7,0xE1,0x45,0x8B,0xF0,
    };
    constexpr std::array<std::uint8_t, 32> getClientUnitExpected{
        0x4C,0x63,0xCA,0x48,0x8D,0x05,0x36,0x93,
        0x98,0x02,0x8B,0xD1,0x44,0x8B,0xC1,0x49,
        0x8B,0xC9,0x83,0xE2,0x7F,0x48,0xC1,0xE1,
        0x0A,0x48,0x03,0xC8,0xE9,0x7F,0x4C,0x00,
    };
    constexpr std::array<std::uint8_t, 33> updateCameraExpected{
        0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,
        0xEC,0x20,0xE8,0x31,0x17,0xFD,0xFF,0x8B,
        0xC8,0xE8,0xDA,0x08,0xFE,0xFF,0x48,0x8B,
        0xC8,0x48,0x8B,0xD8,0xE8,0xAF,0x13,0x29,
        0x00,
    };
    constexpr std::array<std::uint8_t, 37>
        getRenderThreadContextRootExpected{
        0x48,0x83,0xEC,0x28,0x65,0x48,0x8B,0x04,
        0x25,0x58,0x00,0x00,0x00,0x8B,0x0D,0x65,
        0xED,0xF2,0x02,0xBA,0x6C,0x12,0x00,0x00,
        0x48,0x8B,0x0C,0xC8,0x8B,0x04,0x0A,0x39,
        0x05,0x3F,0xF4,0xD8,0x02,
    };
    constexpr std::array<std::uint8_t, 61> projectUnitToScreenExpected{
        0x4C,0x8B,0xDC,0x55,0x56,0x57,0x41,0x57,
        0x49,0x8D,0x6B,0xA8,0x48,0x81,0xEC,0x38,
        0x01,0x00,0x00,0x48,0x8B,0x05,0xDE,0x0A,
        0x26,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,
        0xA0,0x40,0x32,0xFF,0x4C,0x89,0x44,0x24,
        0x40,0x45,0x0F,0xB6,0xF9,0x48,0x8B,0xF2,
        0x48,0x85,0xD2,0x0F,0x84,0x6E,0x07,0x00,
        0x00,0x4C,0x8B,0x82,0xD8,
    };
    constexpr std::array<std::uint8_t, 27> getNativeHeightExpected{
        0x48,0x83,0xEC,0x28,0xE8,0x67,0x6D,0x7C,
        0x00,0x84,0xC0,0x74,0x0E,0xE8,0x1E,0x51,
        0x5D,0x00,0x48,0xC1,0xE8,0x20,0x48,0x83,
        0xC4,0x28,0xC3,
    };
    constexpr std::array<std::uint8_t, 33> getNativeWidthExpected{
        0x48,0x83,0xEC,0x28,0xE8,0xF7,0x6C,0x7C,
        0x00,0x84,0xC0,0x74,0x09,0x48,0x83,0xC4,
        0x28,0xE9,0xAA,0x50,0x5D,0x00,0x8B,0x05,
        0x3C,0x18,0x22,0x02,0x48,0x83,0xC4,0x28,
        0xC3,
    };
    constexpr std::array<std::uint8_t, 21> hitpointsCommitContextExpected{
        0x48,0x8B,0xCE,0x3D,0x00,0x01,0x00,0x00,0x44,0x0F,
        0x4D,0xC0,0x41,0x8D,0x51,0x06,0xE8,0x78,0xAC,0xEA,0xFF,
    };
    constexpr std::array<std::uint8_t, 5> hitpointsCommitCallExpected{
        0xE8,0x78,0xAC,0xEA,0xFF,
    };
    if (!MatchesSignature(GetUnitStatRva, getUnitStatExpected)
            || !MatchesSignature(
                GetClientUnitRva,
                getClientUnitExpected)
            || !MatchesSignature(
                UpdateCameraRva,
                updateCameraExpected)
            || !MatchesSignature(
                GetRenderThreadContextRootRva,
                getRenderThreadContextRootExpected)
            || !MatchesSignature(
                ProjectUnitToScreenRva,
                projectUnitToScreenExpected)
            || !MatchesSignature(
                GetNativeHeightRva,
                getNativeHeightExpected)
            || !MatchesSignature(
                GetNativeWidthRva,
                getNativeWidthExpected)
            || !MatchesSignature(
                HitpointsCommitContextRva,
                hitpointsCommitContextExpected)) {
        return false;
    }
    if (!ValidateComposableSetUnitStatEntry(setUnitStatExpected)) return false;
    GetUnitStat = reinterpret_cast<GetUnitStatFn>(Base + GetUnitStatRva);
    SetUnitStat = reinterpret_cast<SetUnitStatFn>(Base + SetUnitStatRva);
    GetClientUnit = reinterpret_cast<GetClientUnitFn>(
        Base + GetClientUnitRva);
    ProjectUnitToScreen = reinterpret_cast<ProjectUnitToScreenFn>(
        Base + ProjectUnitToScreenRva);
    GetRenderThreadContextRoot =
        reinterpret_cast<GetRenderThreadContextRootFn>(
            Base + GetRenderThreadContextRootRva);
    GetNativeHeight = reinterpret_cast<GetNativeDimensionFn>(
        Base + GetNativeHeightRva);
    GetNativeWidth = reinterpret_cast<GetNativeDimensionFn>(
        Base + GetNativeWidthRva);
    if (!CreateHitpointsCommitRelay()) return false;
    if (!Context->InstallInlineHook(
            UpdateCameraRva,
            updateCameraExpected.data(),
            static_cast<std::uint32_t>(updateCameraExpected.size()),
            HookUpdateCamera,
            &OriginalUpdateCamera)) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    const auto relayRva = reinterpret_cast<std::uintptr_t>(
        HitpointsCommitRelay) - reinterpret_cast<std::uintptr_t>(Base);
    if (!Context->PatchCallRel32(
            HitpointsCommitCallRva,
            hitpointsCommitCallExpected.data(),
            static_cast<std::uint32_t>(hitpointsCommitCallExpected.size()),
            relayRva,
            5)) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
        return false;
    }
    return true;
}

std::filesystem::path FindOptionalKodiaFont(
    const D2RL::PluginContext* context) noexcept {
    if (!context) return {};
    try {
        std::vector<std::filesystem::path> candidates;
        const auto appendCandidates = [&](const std::filesystem::path& root) {
            if (root.empty()) return;
            if (context->activeMod && context->activeMod[0] != '\0') {
                candidates.push_back(
                    root / (std::string(context->activeMod) + ".mpq")
                        / "data/hd/ui/fonts/kodia.ttf");
            }
            candidates.push_back(root / "data/hd/ui/fonts/kodia.ttf");
        };
        if (context->modDirectory)
            appendCandidates(std::filesystem::path(context->modDirectory));
        if (context->scopeRootDirectory)
            appendCandidates(
                std::filesystem::path(context->scopeRootDirectory));

        for (const auto& candidate : candidates) {
            std::error_code error;
            if (std::filesystem::is_regular_file(candidate, error) && !error)
                return candidate;
        }
    }
    catch (...) {
    }
    return {};
}

DWORD WINAPI OverlayWorkerMain(void*) noexcept {
    while (WaitForSingleObject(OverlayStopEvent, 500) == WAIT_TIMEOUT) {
        if (!D3D12::InstallHooks()) continue;
        OverlayReady.store(true, std::memory_order_release);
        if (Context && FloatingDamage::GetConfig().diagnosticsEnabled) {
            Context->LogInfo(
                "FloatingDamage: autonomous DirectX 12 overlay hooks installed after graphics startup.");
        }
        return 0;
    }
    return 0;
}

bool StartOverlayWorker() noexcept {
    OverlayStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!OverlayStopEvent) return false;
    OverlayWorker = CreateThread(nullptr, 0, OverlayWorkerMain, nullptr, 0, nullptr);
    if (OverlayWorker) {
        D3D12::SetExternalOverlayAvailability(true);
        return true;
    }
    CloseHandle(OverlayStopEvent);
    OverlayStopEvent = nullptr;
    return false;
}

void StopOverlayWorker() noexcept {
    D3D12::SetExternalOverlayAvailability(false);
    if (OverlayStopEvent) SetEvent(OverlayStopEvent);
    if (OverlayWorker) {
        WaitForSingleObject(OverlayWorker, 3000);
        CloseHandle(OverlayWorker);
        OverlayWorker = nullptr;
    }
    if (OverlayStopEvent) {
        CloseHandle(OverlayStopEvent);
        OverlayStopEvent = nullptr;
    }
    D3D12::RemoveHooks();
    OverlayReady.store(false, std::memory_order_release);
}

auto ConsoleCommand(
    D2R::Game::Client*,
    const D2RL::ConsoleCommandContext* command,
    void*
) noexcept -> D2RL::ConsoleCommandResult {
    if (!command || !command->plugin) return D2RL::ConsoleCommandResult::Failed;
    const std::string action = Lower(Trim(command->args ? std::string_view(command->args, command->argsLength) : std::string_view{}));
    auto& config = FloatingDamage::GetConfig();
    const bool enabled = FloatingDamage::IsEnabled();

    if (action.empty() || action == "status") {
        char message[896]{};
        float displayWidth{};
        float displayHeight{};
        D3D12::GetDisplaySize(displayWidth, displayHeight);
        const D3D12::OverlayDiagnostics overlay =
            D3D12::GetOverlayDiagnostics();
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage 1.4.0: enabled=%s; runtime=%s; diagnostics=%s; in_game=%s; input_action=%s; overlay_hooks=%s; presents=%llu; queues=%llu; imgui_attempts=%llu; imgui_failures=%llu; init_stage=%u; overlay_frames=%llu; camera_frames=%llu; context_misses=%llu; captured=%llu; queued=%llu; projected=%llu; rejected=%llu; forced=%llu; missed=%llu; request_drops=%llu; active=%zu; pending=%zu; font=%d; display=%.0fx%.0f; scale=%.3f.",
            enabled ? "true" : "false",
            RuntimeActive.load(std::memory_order_acquire) ? "active" : "not installed",
            config.diagnosticsEnabled ? "true" : "false",
            FloatingDamage::IsGameplayActive() ? "true" : "false",
            ToggleAction != D2RL::Input::InvalidHandle
                ? "registered"
                : "not registered",
            OverlayReady.load(std::memory_order_acquire) ? "ready" : "waiting",
            static_cast<unsigned long long>(overlay.presentCalls),
            static_cast<unsigned long long>(overlay.directQueueCaptures),
            static_cast<unsigned long long>(overlay.rendererInitAttempts),
            static_cast<unsigned long long>(overlay.rendererInitFailures),
            overlay.lastInitFailureStage,
            static_cast<unsigned long long>(overlay.renderedFrames),
            static_cast<unsigned long long>(CameraFrameTicks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(RenderContextMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(CapturedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(DisplayedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionSuccesses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionFailures.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionRequestDrops.load(std::memory_order_relaxed)),
            FloatingDamage::ActiveCount(),
            FloatingDamage::PendingCount(),
            config.fontIndex,
            displayWidth,
            displayHeight,
            FloatingDamage::GetResolutionScale(displayHeight));
        command->plugin->WriteConsoleMessage(message);
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "on" || action == "off" || action == "toggle") {
        const bool requestedEnabled = action == "toggle"
            ? !enabled
            : action == "on";
        if (!SaveEnabled(requestedEnabled))
            return D2RL::ConsoleCommandResult::Failed;
        FloatingDamage::SetEnabled(requestedEnabled);
        if (requestedEnabled
                && !RuntimeActive.load(std::memory_order_acquire)) {
            command->plugin->WriteConsoleMessage(
                "Floating Damage enabled in the configuration; restart D2R to install it.");
        } else {
            command->plugin->WriteConsoleMessage(requestedEnabled
                ? "Floating Damage enabled."
                : "Floating Damage disabled; restart D2R to remove its hooks.");
        }
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "preview") {
        if (!FloatingDamage::IsGameplayActive()) {
            command->plugin->WriteConsoleMessage(
                "Floating Damage preview is available only in game.");
            return D2RL::ConsoleCommandResult::Handled;
        }
        float width{}, height{};
        D3D12::GetDisplaySize(width, height);
        FloatingDamage::QueuePreviewBurstAt(width * 0.5f, height * 0.5f);
        command->plugin->WriteConsoleMessage("Floating Damage preview queued.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "reload") {
        if (!LoadConfig()) return D2RL::ConsoleCommandResult::Failed;
        command->plugin->WriteConsoleMessage(
            FloatingDamage::IsEnabled()
                    == RuntimeActive.load(std::memory_order_acquire)
                ? "Floating Damage configuration reloaded."
                : "Floating Damage configuration reloaded; restart D2R to apply the master switch.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    if (action == "reset") {
        if (!Context->WriteConfig(
                RuffnecKk::FloatingDamage::DefaultConfigToml)) {
            return D2RL::ConsoleCommandResult::Failed;
        }
        const FloatingDamage::Config defaults{};
        FloatingDamage::GetConfig() = defaults;
        FloatingDamage::SetEnabled(defaults.enabled);
        D3D12::SetDiagnosticLogCallback(nullptr);
        command->plugin->WriteConsoleMessage(
            RuntimeActive.load(std::memory_order_acquire)
                ? "Floating Damage defaults restored and saved."
                : "Floating Damage defaults restored and saved; restart D2R to install it.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    command->plugin->WriteConsoleMessage("Usage: floating-damage [status|on|off|toggle|preview|reload|reset].");
    return D2RL::ConsoleCommandResult::InvalidArguments;
}
} // namespace

extern "C" __declspec(dllexport)
const RuffnecKk::FloatingDamageOverlay::ExternalOverlayApiV1* __cdecl
RuffnecKkFloatingDamageGetOverlayApi(
    std::uint32_t requestedVersion,
    std::uint32_t callerStructSize) noexcept {
    static const RuffnecKk::FloatingDamageOverlay::ExternalOverlayApiV1 api{
        .structSize = RuffnecKk::FloatingDamageOverlay::ExternalOverlayApiV1Size,
        .version = RuffnecKk::FloatingDamageOverlay::ApiVersion1,
        .registerNamedOverlay = D3D12::RegisterNamedExternalOverlay,
        .addRect = D3D12::OverlayAddRect,
        .addRectFilled = D3D12::OverlayAddRectFilled,
    };
    if (requestedVersion != api.version
        || callerStructSize < api.structSize) {
        return nullptr;
    }
    return &api;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)
            || context->apiVersion != D2RL_PLUGIN_API_VERSION) {
        return false;
    }
    Context = context;
    LifecycleService = nullptr;
    InputService = nullptr;
    ToggleAction = D2RL::Input::InvalidHandle;
    GameJoinedListener = D2RL::Lifecycle::InvalidHandle;
    GameLeftListener = D2RL::Lifecycle::InvalidHandle;
    LocalPlayerReadyListener = D2RL::Lifecycle::InvalidHandle;
    RuntimeActive.store(false, std::memory_order_release);
    FloatingDamage::SetGameplayActive(false);
    Base = reinterpret_cast<std::uint8_t*>(context->exeBase);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(static_cast<const void*>(&Info)),
            &Module)) {
        Module = nullptr;
    }
    if (!Base || !Module) return false;
    const auto* runtimeBuild = D2RL::GetBuildName(context);
    if (runtimeBuild == nullptr
        || (std::strcmp(runtimeBuild, "92777") != 0
            && std::strcmp(runtimeBuild, "93847") != 0)) {
        context->LogError(
            "FloatingDamage: only D2R builds 92777 and 93847 are supported.");
        return false;
    }
    if (!context->EnsureConfig(
            RuffnecKk::FloatingDamage::DefaultConfigToml)
            || !LoadConfig()) {
        context->LogError("FloatingDamage: configuration could not be created or read.");
        return false;
    }

    D2RL::ConsoleCommandRegistration registration = D2RL::MakeConsoleCommand(
        "floating-damage", ConsoleCommand, "Control Floating Damage and show its status.");
    registration.usage = "floating-damage [status|on|off|toggle|preview|reload|reset]";
    if (!context->RegisterConsoleCommand(registration)) {
        context->LogWarn("FloatingDamage: console command could not be registered.");
    }
    if (!FloatingDamage::GetConfig().enabled) {
        D3D12::SetDiagnosticLogCallback(nullptr);
        context->LogInfo(
            "Floating Damage 1.4.0 by RuffnecKk disabled; no input action, renderer or combat hook was installed.");
        return true;
    }
    if (!RegisterInputAction())
        return false;
    if (!RegisterLifecycleListeners()) {
        UnregisterInputAction();
        return false;
    }
    D3D12::SetDllModule(Module);
    const auto kodiaFont = FindOptionalKodiaFont(context);
    D3D12::SetOptionalKodiaFontPath(
        kodiaFont.empty() ? nullptr : kodiaFont.c_str());
    context->LogInfo(
        kodiaFont.empty()
            ? "FloatingDamage: Kodia was not found in the active mod; font index 12 will fall back to index 0."
            : "FloatingDamage: active-mod Kodia detected for font index 12.");
    if (!StartOverlayWorker()) {
        D3D12::SetOptionalKodiaFontPath(nullptr);
        D3D12::SetDiagnosticLogCallback(nullptr);
        UnregisterLifecycleListeners();
        LifecycleService = nullptr;
        UnregisterInputAction();
        context->LogError("FloatingDamage: DirectX 12 overlay worker could not be started.");
        return false;
    }
    if (!InstallDamageHook()) {
        StopOverlayWorker();
        D3D12::SetOptionalKodiaFontPath(nullptr);
        D3D12::SetDiagnosticLogCallback(nullptr);
        UnregisterLifecycleListeners();
        LifecycleService = nullptr;
        UnregisterInputAction();
        context->LogError("FloatingDamage: D2R builds 92777/93847 HP commit, composable stat setter, client-unit lookup, camera-frame or native projection guards could not be installed; plugin refused.");
        return false;
    }
    FloatingDamage::SetTargetScreenPositionProvider(TryProjectTargetToScreen);
    RuntimeActive.store(true, std::memory_order_release);
    context->LogInfo("FloatingDamage 1.4.0 active for D2R builds 92777/93847 with native Input v1 rebinding, a composable stat setter, Lifecycle v1 gameplay gating, persistent Kodia font index 12, shared overlay API v1 and per-frame camera-thread multi-target projection.");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    FloatingDamage::SetGameplayActive(false);
    UnregisterInputAction();
    UnregisterLifecycleListeners();
    FloatingDamage::SetTargetScreenPositionProvider(nullptr);
    if (Context && FloatingDamage::GetConfig().diagnosticsEnabled
            && RuntimeActive.load(std::memory_order_acquire)) {
        char message[320]{};
        const D3D12::OverlayDiagnostics overlay =
            D3D12::GetOverlayDiagnostics();
        std::snprintf(
            message,
            sizeof(message),
            "FloatingDamage stopped: presents=%llu; queues=%llu; imgui_attempts=%llu; imgui_failures=%llu; init_stage=%u; overlay_frames=%llu; camera_frames=%llu; context_misses=%llu; captured=%llu; queued=%llu; forced=%llu; missed=%llu; request_drops=%llu.",
            static_cast<unsigned long long>(overlay.presentCalls),
            static_cast<unsigned long long>(overlay.directQueueCaptures),
            static_cast<unsigned long long>(overlay.rendererInitAttempts),
            static_cast<unsigned long long>(overlay.rendererInitFailures),
            overlay.lastInitFailureStage,
            static_cast<unsigned long long>(overlay.renderedFrames),
            static_cast<unsigned long long>(CameraFrameTicks.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(RenderContextMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(CapturedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(DisplayedEvents.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionAttempts.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ActiveProjectionMisses.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(ProjectionRequestDrops.load(std::memory_order_relaxed)));
        Context->LogInfo(message);
    }
    StopOverlayWorker();
    RuntimeActive.store(false, std::memory_order_release);
    D3D12::SetOptionalKodiaFontPath(nullptr);
    D3D12::SetDiagnosticLogCallback(nullptr);
    if (HitpointsCommitRelay) {
        VirtualFree(HitpointsCommitRelay, 0, MEM_RELEASE);
        HitpointsCommitRelay = nullptr;
    }
    OriginalUpdateCamera = nullptr;
    GetNativeWidth = nullptr;
    GetNativeHeight = nullptr;
    GetRenderThreadContextRoot = nullptr;
    ProjectUnitToScreen = nullptr;
    GetClientUnit = nullptr;
    SetUnitStat = nullptr;
    GetUnitStat = nullptr;
    Module = nullptr;
    Base = nullptr;
    LifecycleService = nullptr;
    InputService = nullptr;
    Context = nullptr;
}
