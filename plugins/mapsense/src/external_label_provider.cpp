#include "external_label_provider.hpp"

#include "external_atlas_cache.hpp"
#include "mapsense_data_catalog.hpp"
#include "native_automap_poi.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iterator>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace RuffnecKk::MapSense {
namespace {

constexpr wchar_t HelperFileName[] = L"RuffnecKkMapSenseMapgen.exe";
constexpr std::uint32_t ProtocolVersion = 3U;
constexpr std::size_t MaximumResponseBytes = 2U * 1'024U * 1'024U;
constexpr std::size_t MaximumAtlasLevels = ExternalAtlasLevelCapacity;
constexpr std::size_t MaximumAtlasExits = ExternalAtlasEdgeCapacity;
constexpr std::size_t MaximumAtlasWaypoints = 512U;
constexpr std::size_t MaximumAtlasPortals = 64U;
constexpr std::size_t MaximumWitnessRooms = 4'096U;

constexpr std::size_t LevelFirstRoomOffset = 0x10;
constexpr std::size_t LevelPositionXOffset = 0x24;
constexpr std::size_t LevelPositionYOffset = 0x28;
constexpr std::size_t DrlgRoomNextOffset = 0x48;
constexpr std::size_t DrlgRoomTileXOffset = 0x60;
constexpr std::size_t DrlgRoomTileYOffset = 0x64;
constexpr std::size_t DrlgRoomWidthOffset = 0x68;
constexpr std::size_t DrlgRoomHeightOffset = 0x6C;
constexpr std::int32_t SubtilesPerGameTile = 5;
constexpr std::uint32_t ExternalExitStableType = 5U;
constexpr std::uint32_t ExternalWaypointStableType = 2U;
constexpr std::uint32_t ExternalLevelStableType = 6U;
constexpr std::uint32_t ExternalPortalStableType = 7U;

struct RoomRectangle final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] auto operator<=>(const RoomRectangle&) const = default;
};

struct AtlasLevel final {
    std::int32_t levelId{};
    std::int32_t originSubtileX{};
    std::int32_t originSubtileY{};
    std::int32_t anchorSubtileX{};
    std::int32_t anchorSubtileY{};
    std::size_t roomCount{};
};

struct AtlasExit final {
    std::int32_t sourceLevelId{};
    std::int32_t targetLevelId{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t kind{};
};

struct AtlasWaypoint final {
    std::int32_t levelId{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t classId{};
};

struct AtlasPortal final {
    std::int32_t sourceLevelId{};
    std::int32_t targetLevelId{};
    std::int32_t subtileX{};
    std::int32_t subtileY{};
    std::int32_t classId{};
};

struct ParsedAtlas final {
    std::vector<AtlasLevel> levels;
    std::vector<AtlasExit> exits;
    std::vector<AtlasWaypoint> waypoints;
    std::vector<AtlasPortal> portals;
    std::vector<RoomRectangle> witnessRooms;
    double helperElapsedMilliseconds{};
};

struct AtlasPublication final {
    std::vector<std::int32_t> visibleLevels;
    std::vector<AutomapExitLabelDefinition> exits;
    std::vector<AutomapWaypointLabelDefinition> waypoints;
    std::vector<AutomapLevelLabelDefinition> levels;
};

struct Request final {
    std::uint64_t serial{};
    std::uint64_t sessionGeneration{};
    std::uint32_t seed{};
    std::uint8_t difficulty{};
    std::int32_t act{};
    std::int32_t currentLevelId{};
    std::int32_t currentOriginSubtileX{};
    std::int32_t currentOriginSubtileY{};
    std::uint64_t dataFingerprint{};
    std::vector<RoomRectangle> currentRooms;
    std::shared_ptr<const MapSenseDataCatalog> dataCatalog{};
};

using RequestKey = ExternalLabelProviderRequestIdentity;

enum class GeometryPreparationResult : std::uint8_t {
    Ready,
    Failed,
    Cancelled,
};

struct HelperCommandResult final {
    bool succeeded{};
    bool timedOut{};
    bool cancelled{};
};

struct NativeLevelWitness final {
    std::int32_t originSubtileX{};
    std::int32_t originSubtileY{};
    std::array<RoomRectangle, MaximumWitnessRooms> rooms{};
    std::size_t roomCount{};
};

class UniqueHandle final {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(HANDLE value) noexcept : value_(value) {}
    ~UniqueHandle() {
        Reset();
    }

    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;

    UniqueHandle(UniqueHandle&& other) noexcept
        : value_(std::exchange(other.value_, nullptr)) {}
    auto operator=(UniqueHandle&& other) noexcept -> UniqueHandle& {
        if (this != &other) {
            Reset(std::exchange(other.value_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] auto Get() const noexcept -> HANDLE { return value_; }
    [[nodiscard]] explicit operator bool() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] auto Release() noexcept -> HANDLE {
        return std::exchange(value_, nullptr);
    }
    void Reset(HANDLE value = nullptr) noexcept {
        if (*this) CloseHandle(value_);
        value_ = value;
    }

private:
    HANDLE value_{};
};

const D2RL::PluginContext* Context{};
std::atomic_bool Active{};
std::atomic_bool Diagnostics{};
std::mutex StateMutex;
std::condition_variable StateCondition;
std::thread Worker;
bool StopRequested{};
std::optional<Request> PendingRequest;
std::optional<RequestKey> InFlightRequest;
std::optional<RequestKey> PublishedRequest;
std::array<RequestKey, ExternalAtlasLevelCapacity> FailedRequests{};
std::size_t FailedRequestCount{};
std::uint64_t LatestSerial{};
std::uint64_t ActiveSessionGeneration{};
HANDLE ActiveChildProcess{};
ExternalLabelProviderOperation ActiveChildOperation{
    ExternalLabelProviderOperation::None};
bool ActiveChildCancellationRequested{};
std::filesystem::path HelperPath;
std::filesystem::path GeometryCacheRoot;
ExternalLabelAtlasResultCallback ResultCallback{};
void* ResultUserData{};
std::atomic<std::shared_ptr<const ExternalAtlasGeometrySnapshot>>
    PublishedGeometrySnapshot{};

std::atomic_uint64_t Requests{};
std::atomic_uint64_t ProcessesStarted{};
std::atomic_uint64_t AtlasesPublished{};
std::atomic_uint64_t StaleResponses{};
std::atomic_uint64_t FailedResponses{};
std::atomic_uint64_t Timeouts{};
std::atomic_uint64_t LabelTimeouts{};
std::atomic_uint64_t GeometryTimeouts{};
std::atomic_uint64_t PrimaryCancellations{};
std::atomic_uint64_t PrewarmCancellations{};
std::atomic_uint64_t GeometryCacheHits{};
std::atomic_uint64_t GeometryCacheMisses{};
std::atomic_uint64_t GeometryCacheInvalid{};
std::atomic_uint64_t GeometryAtlasesGenerated{};
std::atomic_uint64_t GeometryFailures{};
std::atomic_uint64_t GeometrySnapshotsPublished{};
std::atomic_uint64_t PublishedGeometryCells{};
std::atomic_uint32_t ActiveOperationCode{};
std::atomic_uint32_t ActiveOperationSeed{};
std::atomic_int32_t ActiveOperationAct{-1};

[[nodiscard]] auto RequestIsCurrentLocked(
        const Request& request) noexcept -> bool {
    return !StopRequested && request.serial == LatestSerial
        && request.sessionGeneration == ActiveSessionGeneration;
}

void CancelActiveChildLocked() noexcept {
    if (ActiveChildProcess == nullptr) return;
    ActiveChildCancellationRequested = true;
    (void)TerminateProcess(ActiveChildProcess, ERROR_CANCELLED);
}

void RecordHelperCommandResult(
        ExternalLabelProviderOperation operation,
        const HelperCommandResult& result) noexcept {
    if (result.timedOut) {
        Timeouts.fetch_add(1U, std::memory_order_relaxed);
        if (operation == ExternalLabelProviderOperation::Labels) {
            LabelTimeouts.fetch_add(1U, std::memory_order_relaxed);
        } else {
            GeometryTimeouts.fetch_add(1U, std::memory_order_relaxed);
        }
    }
    if (!result.cancelled) return;
    if (operation == ExternalLabelProviderOperation::PrewarmGeometry) {
        PrewarmCancellations.fetch_add(1U, std::memory_order_relaxed);
    } else {
        PrimaryCancellations.fetch_add(1U, std::memory_order_relaxed);
    }
}

[[nodiscard]] auto ContainsFailedRequestLocked(
        const RequestKey& key) noexcept -> bool {
    return std::find(
        FailedRequests.begin(),
        FailedRequests.begin()
            + static_cast<std::ptrdiff_t>(FailedRequestCount),
        key)
        != FailedRequests.begin()
            + static_cast<std::ptrdiff_t>(FailedRequestCount);
}

void RememberFailedRequestLocked(const RequestKey& key) noexcept {
    if (ContainsFailedRequestLocked(key)
        || FailedRequestCount >= FailedRequests.size()) {
        return;
    }
    FailedRequests[FailedRequestCount++] = key;
}

[[nodiscard]] auto KeyFor(const Request& request) noexcept -> RequestKey {
    return {
        .sessionGeneration = request.sessionGeneration,
        .seed = request.seed,
        .difficulty = request.difficulty,
        .act = request.act,
        .currentLevelId = request.currentLevelId,
        .dataFingerprint = request.dataFingerprint,
    };
}

[[nodiscard]] auto StableId(
        std::uint32_t type,
        std::int32_t id,
        std::int32_t x,
        std::int32_t y) noexcept -> std::uint64_t {
    auto hash = UINT64_C(1469598103934665603);
    const std::array values{
        type,
        static_cast<std::uint32_t>(id),
        static_cast<std::uint32_t>(x),
        static_cast<std::uint32_t>(y),
    };
    for (const auto value : values) {
        for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
            hash ^= static_cast<std::uint8_t>(value >> shift);
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

[[nodiscard]] auto ResolveHelperPath(
        std::filesystem::path& output) noexcept -> bool {
    output.clear();
    std::array<wchar_t, 32'768U> modulePath{};
    const auto length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase),
        modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length == 0U
        || length >= static_cast<DWORD>(modulePath.size())) {
        return false;
    }
    try {
        output = std::filesystem::path(
            std::wstring_view(modulePath.data(), length)).parent_path()
            / HelperFileName;
    } catch (...) {
        output.clear();
        return false;
    }
    return GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES;
}

[[nodiscard]] auto CaptureNativeLevelWitness(
        const void* levelPointer,
        NativeLevelWitness& output) noexcept -> bool {
    output = {};
    __try {
        const auto* const level = static_cast<const std::uint8_t*>(
            levelPointer);
        output.originSubtileX =
            *reinterpret_cast<const std::int32_t*>(
                level + LevelPositionXOffset) * SubtilesPerGameTile;
        output.originSubtileY =
            *reinterpret_cast<const std::int32_t*>(
                level + LevelPositionYOffset) * SubtilesPerGameTile;

        auto* room = *reinterpret_cast<std::uint8_t* const*>(
            level + LevelFirstRoomOffset);
        while (room != nullptr && output.roomCount < output.rooms.size()) {
            const RoomRectangle rectangle{
                .x = *reinterpret_cast<const std::int32_t*>(
                    room + DrlgRoomTileXOffset),
                .y = *reinterpret_cast<const std::int32_t*>(
                    room + DrlgRoomTileYOffset),
                .width = *reinterpret_cast<const std::int32_t*>(
                    room + DrlgRoomWidthOffset),
                .height = *reinterpret_cast<const std::int32_t*>(
                    room + DrlgRoomHeightOffset),
            };
            if (rectangle.x < 0 || rectangle.y < 0
                || rectangle.width <= 0 || rectangle.height <= 0) {
                return false;
            }
            output.rooms[output.roomCount++] = rectangle;
            room = *reinterpret_cast<std::uint8_t* const*>(
                room + DrlgRoomNextOffset);
        }
        return room == nullptr && output.roomCount != 0U;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        output = {};
        return false;
    }
}

[[nodiscard]] auto CaptureRequest(
        std::uint64_t sessionGeneration,
        const ClientLevelView& current,
        std::int32_t resolvedAct,
        std::shared_ptr<const MapSenseDataCatalog> dataCatalog,
        Request& output) noexcept -> bool {
    output = {};
    if (sessionGeneration == 0U || current.level == nullptr
        || current.drlg == nullptr || current.levelId <= 0
        || current.difficulty > 2U || resolvedAct < 0 || resolvedAct >= 5
        || DeriveDrlgStartSeed(current.mapSeed) != current.drlgStartSeed) {
        return false;
    }
    NativeLevelWitness witness{};
    if (!CaptureNativeLevelWitness(current.level, witness)) return false;
    output.sessionGeneration = sessionGeneration;
    output.seed = current.mapSeed;
    output.difficulty = current.difficulty;
    output.act = resolvedAct;
    output.currentLevelId = current.levelId;
    output.currentOriginSubtileX = witness.originSubtileX;
    output.currentOriginSubtileY = witness.originSubtileY;
    output.dataFingerprint = dataCatalog != nullptr
        ? dataCatalog->AtlasDataFingerprint() : 0U;
    try {
        output.currentRooms.assign(
            witness.rooms.begin(),
            witness.rooms.begin()
                + static_cast<std::ptrdiff_t>(witness.roomCount));
        output.dataCatalog = std::move(dataCatalog);
    } catch (...) {
        output = {};
        return false;
    }
    std::sort(output.currentRooms.begin(), output.currentRooms.end());
    return true;
}

[[nodiscard]] auto ReadAvailablePipe(
        HANDLE pipe,
        std::string& output) noexcept -> bool {
    for (;;) {
        DWORD available{};
        if (!PeekNamedPipe(pipe, nullptr, 0U, nullptr, &available, nullptr)) {
            const auto error = GetLastError();
            return error == ERROR_BROKEN_PIPE;
        }
        if (available == 0U) return true;
        std::array<char, 8'192U> buffer{};
        const auto requested = static_cast<DWORD>(std::min<std::size_t>(
            buffer.size(), available));
        DWORD read{};
        if (!ReadFile(pipe, buffer.data(), requested, &read, nullptr)) {
            return GetLastError() == ERROR_BROKEN_PIPE;
        }
        if (read == 0U) return true;
        if (output.size() > MaximumResponseBytes - read) return false;
        output.append(buffer.data(), read);
    }
}

[[nodiscard]] auto RunHelperCommand(
        std::wstring_view arguments,
        const Request& request,
        std::int32_t operationAct,
        ExternalLabelProviderOperation operation,
        std::string& output,
        const std::filesystem::path* workingDirectoryOverride = nullptr) noexcept
        -> HelperCommandResult {
    output.clear();
    HelperCommandResult result{};
    SECURITY_ATTRIBUTES security{
        .nLength = sizeof(SECURITY_ATTRIBUTES),
        .lpSecurityDescriptor = nullptr,
        .bInheritHandle = TRUE,
    };
    HANDLE readRaw{};
    HANDLE writeRaw{};
    if (!CreatePipe(&readRaw, &writeRaw, &security, 0U)) return result;
    UniqueHandle readPipe(readRaw);
    UniqueHandle writePipe(writeRaw);
    if (!SetHandleInformation(
            readPipe.Get(), HANDLE_FLAG_INHERIT, 0U)) {
        return result;
    }

    std::wstring command;
    std::wstring workingDirectory;
    try {
        command = L"\"" + HelperPath.wstring() + L"\" "
            + std::wstring(arguments);
        workingDirectory = workingDirectoryOverride != nullptr
            ? workingDirectoryOverride->wstring()
            : HelperPath.parent_path().wstring();
    } catch (...) {
        return result;
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writePipe.Get();
    startup.hStdError = writePipe.Get();
    PROCESS_INFORMATION processInfo{};
    const auto priorityClass =
        operation == ExternalLabelProviderOperation::PrewarmGeometry
        ? IDLE_PRIORITY_CLASS
        : BELOW_NORMAL_PRIORITY_CLASS;
    if (!CreateProcessW(
            HelperPath.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW | priorityClass,
            nullptr,
            workingDirectory.c_str(),
            &startup,
            &processInfo)) {
        return result;
    }
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle processThread(processInfo.hThread);
    writePipe.Reset();
    ProcessesStarted.fetch_add(1U, std::memory_order_relaxed);
    {
        std::scoped_lock lock(StateMutex);
        ActiveChildProcess = process.Get();
        ActiveChildOperation = operation;
        ActiveChildCancellationRequested = false;
        ActiveOperationCode.store(
            static_cast<std::uint32_t>(operation),
            std::memory_order_release);
        ActiveOperationSeed.store(request.seed, std::memory_order_release);
        ActiveOperationAct.store(operationAct, std::memory_order_release);
        if (!RequestIsCurrentLocked(request)) CancelActiveChildLocked();
    }

    const auto started = GetTickCount64();
    bool success = true;
    for (;;) {
        if (!ReadAvailablePipe(readPipe.Get(), output)) {
            success = false;
            break;
        }
        const auto wait = WaitForSingleObject(process.Get(), 5U);
        if (wait == WAIT_OBJECT_0) {
            success = ReadAvailablePipe(readPipe.Get(), output);
            break;
        }
        if (wait == WAIT_FAILED) {
            success = false;
            break;
        }
        if (GetTickCount64() - started
            > ExternalLabelProviderOperationTimeoutMilliseconds(operation)) {
            result.timedOut = true;
            TerminateProcess(process.Get(), ERROR_TIMEOUT);
            WaitForSingleObject(process.Get(), 1'000U);
            success = false;
            break;
        }
    }
    bool cancellationRequested{};
    {
        std::scoped_lock lock(StateMutex);
        if (ActiveChildProcess == process.Get()) {
            cancellationRequested = ActiveChildCancellationRequested;
            ActiveChildProcess = nullptr;
            ActiveChildOperation = ExternalLabelProviderOperation::None;
            ActiveChildCancellationRequested = false;
            ActiveOperationCode.store(
                static_cast<std::uint32_t>(
                    ExternalLabelProviderOperation::None),
                std::memory_order_release);
            ActiveOperationSeed.store(0U, std::memory_order_release);
            ActiveOperationAct.store(-1, std::memory_order_release);
        }
    }
    DWORD exitCode{};
    const bool hasExitCode = GetExitCodeProcess(process.Get(), &exitCode)
        != FALSE;
    result.cancelled = cancellationRequested
        || (hasExitCode && exitCode == ERROR_CANCELLED);
    if (!hasExitCode || exitCode != 0U || result.cancelled) {
        success = false;
    }
    result.succeeded = success;
    return result;
}

[[nodiscard]] auto AppendHelperDataSourceArguments(
        const Request& request,
        std::wstring& arguments) noexcept -> bool {
    if (request.dataCatalog == nullptr) return true;
    try {
        const auto appendRoots = [&arguments](
                std::wstring_view option,
                std::span<const std::filesystem::path> roots) {
            for (const auto& root : roots) {
                if (root.empty() || !root.is_absolute()) return false;
                const auto normalized = root.lexically_normal().wstring();
                if (normalized.empty()
                    || normalized.find(L'"') != std::wstring::npos) {
                    return false;
                }
                arguments += L" ";
                arguments += option;
                arguments += L" \"";
                arguments += normalized;
                arguments += L"\"";
            }
            return true;
        };
        return appendRoots(
                L"--excel-root",
                request.dataCatalog->ActiveExcelDirectories())
            && appendRoots(
                L"--tiles-root",
                request.dataCatalog->ActiveTileDirectories());
    } catch (...) {
        return false;
    }
}

[[nodiscard]] auto RunLabelHelper(
        const Request& request,
        std::string& output) noexcept -> HelperCommandResult {
    try {
        auto arguments = L"labels "
            + std::to_wstring(request.seed) + L" "
            + std::to_wstring(request.difficulty) + L" "
            + std::to_wstring(request.act) + L" "
            + std::to_wstring(request.currentLevelId);
        if (!AppendHelperDataSourceArguments(request, arguments)) {
            return {};
        }
        return RunHelperCommand(
            arguments,
            request,
            request.act,
            ExternalLabelProviderOperation::Labels,
            output);
    } catch (...) {
        output.clear();
        return {};
    }
}

[[nodiscard]] auto ReadGeometryArtifact(
        const std::filesystem::path& path,
        std::vector<std::uint8_t>& output) -> bool {
    output.clear();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size < ExternalAtlasGeometryHeaderBytes
        || size > ExternalAtlasGeometryMaximumBytes
        || size > static_cast<std::uintmax_t>(
            (std::numeric_limits<std::size_t>::max)())) {
        return false;
    }
    output.resize(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.read(
        reinterpret_cast<char*>(output.data()),
        static_cast<std::streamsize>(output.size()));
    return input && input.peek() == std::char_traits<char>::eof();
}

[[nodiscard]] auto PrepareGeometryCacheAct(
        const Request& request,
        std::uint8_t act,
        ExternalLabelProviderOperation operation,
        ExternalAtlasGeometry* output = nullptr,
        std::string* combinedLabelOutput = nullptr,
        bool* combinedHelperAttempted = nullptr,
        HelperCommandResult* combinedHelperResult = nullptr) noexcept
        -> GeometryPreparationResult {
    if (combinedLabelOutput != nullptr) combinedLabelOutput->clear();
    if (combinedHelperAttempted != nullptr) {
        *combinedHelperAttempted = false;
    }
    if (combinedHelperResult != nullptr) *combinedHelperResult = {};
    if (GeometryCacheRoot.empty() || act >= 5U
        || (operation != ExternalLabelProviderOperation::PrimaryGeometry
            && operation
                != ExternalLabelProviderOperation::PrewarmGeometry)) {
        return GeometryPreparationResult::Failed;
    }
    const ExternalAtlasCacheKey key{
        .seed = request.seed,
        .difficulty = request.difficulty,
        .act = act,
        .dataFingerprint = request.dataFingerprint,
    };
    ExternalAtlasGeometry cached;
    const auto cacheResult = LoadExternalAtlasGeometryCache(
        GeometryCacheRoot,
        key,
        cached);
    if (cacheResult == ExternalAtlasCacheResult::Hit) {
        GeometryCacheHits.fetch_add(1U, std::memory_order_relaxed);
        if (output != nullptr) *output = std::move(cached);
        return GeometryPreparationResult::Ready;
    }
    if (cacheResult == ExternalAtlasCacheResult::Miss) {
        GeometryCacheMisses.fetch_add(1U, std::memory_order_relaxed);
    } else if (cacheResult == ExternalAtlasCacheResult::Invalid) {
        GeometryCacheInvalid.fetch_add(1U, std::memory_order_relaxed);
    }

    std::filesystem::path target;
    if (!BuildExternalAtlasCachePath(GeometryCacheRoot, key, target)) {
        GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
        return GeometryPreparationResult::Failed;
    }
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (error) {
        GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
        return GeometryPreparationResult::Failed;
    }
    auto temporary = target;
    try {
        temporary += L".generated-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(GetTickCount64());
        const bool combinePrimaryAtlas = operation
                == ExternalLabelProviderOperation::PrimaryGeometry
            && combinedLabelOutput != nullptr
            && combinedHelperAttempted != nullptr
            && combinedHelperResult != nullptr;
        auto arguments = combinePrimaryAtlas
            ? L"atlas-binary "
                + std::to_wstring(request.seed) + L" "
                + std::to_wstring(request.difficulty) + L" "
                + std::to_wstring(act) + L" "
                + std::to_wstring(request.currentLevelId) + L" \""
                + temporary.filename().wstring() + L"\""
            : L"geometry-binary "
                + std::to_wstring(request.seed) + L" "
                + std::to_wstring(request.difficulty) + L" "
                + std::to_wstring(act) + L" \""
                + temporary.filename().wstring() + L"\"";
        if (!AppendHelperDataSourceArguments(request, arguments)) {
            GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
            return GeometryPreparationResult::Failed;
        }
        std::string diagnostics;
        auto& helperOutput = combinePrimaryAtlas
            ? *combinedLabelOutput
            : diagnostics;
        const auto generationDirectory = temporary.parent_path();
        const auto helperResult = RunHelperCommand(
            arguments,
            request,
            static_cast<std::int32_t>(act),
            operation,
            helperOutput,
            &generationDirectory);
        if (combinePrimaryAtlas) {
            *combinedHelperAttempted = true;
            *combinedHelperResult = helperResult;
        }
        RecordHelperCommandResult(operation, helperResult);
        if (helperResult.cancelled) {
            std::filesystem::remove(temporary, error);
            return GeometryPreparationResult::Cancelled;
        }
        std::vector<std::uint8_t> bytes;
        ExternalAtlasGeometry generated;
        const auto valid = helperResult.succeeded
            && ReadGeometryArtifact(temporary, bytes)
            && ParseExternalAtlasGeometry(
                bytes,
                key.seed,
                key.difficulty,
                key.act,
                generated)
            && StoreExternalAtlasGeometryCache(
                GeometryCacheRoot,
                key,
                bytes);
        std::filesystem::remove(temporary, error);
        if (!valid) {
            GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
            if (Diagnostics.load(std::memory_order_acquire)
                && Context != nullptr) {
                char message[384]{};
                std::snprintf(
                    message,
                    sizeof(message),
                    "MapSense external atlas geometry: FAIL seed=%u difficulty=%u act=%u launched=%u timed-out=%u artifact-bytes=%zu helper='%.*s'.",
                    request.seed,
                    static_cast<unsigned>(request.difficulty),
                    static_cast<unsigned>(act),
                    static_cast<unsigned>(helperResult.succeeded),
                    static_cast<unsigned>(helperResult.timedOut),
                    bytes.size(),
                    static_cast<int>(std::min<std::size_t>(
                    helperOutput.size(), 160U)),
                    helperOutput.c_str());
                Context->LogWarn(message);
            }
            return GeometryPreparationResult::Failed;
        }
        GeometryAtlasesGenerated.fetch_add(1U, std::memory_order_relaxed);
        if (output != nullptr) *output = std::move(generated);
        if (Diagnostics.load(std::memory_order_acquire)
            && Context != nullptr) {
            char message[256]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense external atlas geometry: PASS seed=%u difficulty=%u act=%u bytes=%zu source=generated-cache.",
                request.seed,
                static_cast<unsigned>(request.difficulty),
                static_cast<unsigned>(act),
                bytes.size());
            Context->LogInfo(message);
        }
        return GeometryPreparationResult::Ready;
    } catch (...) {
        std::filesystem::remove(temporary, error);
        GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
        return GeometryPreparationResult::Failed;
    }
}

void PrewarmGeometryCache(const Request& request) noexcept {
    const auto prepare = [&request](std::uint8_t act) noexcept {
        {
            std::scoped_lock lock(StateMutex);
            if (!ShouldContinueExternalAtlasPrewarm(
                    StopRequested,
                    PendingRequest.has_value(),
                    request.serial,
                    LatestSerial,
                    request.sessionGeneration,
                    ActiveSessionGeneration)) {
                return false;
            }
        }
        ExternalAtlasCacheKey key{
            .seed = request.seed,
            .difficulty = request.difficulty,
            .act = act,
            .dataFingerprint = request.dataFingerprint,
        };
        std::filesystem::path path;
        if (BuildExternalAtlasCachePath(GeometryCacheRoot, key, path)) {
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error) && !error) {
                return true;
            }
        }
        return PrepareGeometryCacheAct(
            request,
            act,
            ExternalLabelProviderOperation::PrewarmGeometry)
            != GeometryPreparationResult::Cancelled;
    };
    for (std::uint8_t act = 0U; act < 5U; ++act) {
        if (act == static_cast<std::uint8_t>(request.act)) continue;
        if (!prepare(act)) return;
    }
}

template <typename... Values>
[[nodiscard]] auto ReadExactRecord(
        std::istringstream& stream,
        Values&... values) -> bool {
    if (!((stream >> values) && ...)) return false;
    std::string extra;
    return !(stream >> extra);
}

[[nodiscard]] auto ParseAtlas(
        const Request& request,
        const std::string& response,
        ParsedAtlas& output) -> bool {
    output = {};
    std::istringstream responseStream(response);
    std::string line;
    bool sawHeader{};
    bool sawEnd{};
    std::size_t declaredLevels{};
    std::size_t declaredExits{};
    std::size_t declaredWaypoints{};
    std::size_t declaredPortals{};
    std::size_t declaredRooms{};
    while (std::getline(responseStream, line)) {
        std::istringstream stream(line);
        std::string prefix;
        char type{};
        if (!(stream >> prefix >> type) || prefix != "MS1") continue;
        if (sawEnd) return false;
        if (type == 'H') {
            std::uint32_t version{};
            std::uint32_t seed{};
            std::uint32_t difficulty{};
            std::int32_t act{};
            std::int32_t currentLevel{};
            if (sawHeader || !ReadExactRecord(
                    stream,
                    version,
                    seed,
                    difficulty,
                    act,
                    currentLevel)
                || version != ProtocolVersion || seed != request.seed
                || difficulty != request.difficulty || act != request.act
                || currentLevel != request.currentLevelId) {
                return false;
            }
            sawHeader = true;
        } else if (type == 'L') {
            AtlasLevel level{};
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    level.levelId,
                    level.originSubtileX,
                    level.originSubtileY,
                    level.anchorSubtileX,
                    level.anchorSubtileY,
                    level.roomCount)
                || output.levels.size() >= MaximumAtlasLevels
                || level.levelId <= 0
                || level.originSubtileX < 0 || level.originSubtileY < 0
                || level.anchorSubtileX < 0 || level.anchorSubtileY < 0
                || level.roomCount > MaximumWitnessRooms) {
                return false;
            }
            output.levels.push_back(level);
        } else if (type == 'R') {
            std::int32_t levelId{};
            RoomRectangle room{};
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    levelId,
                    room.x,
                    room.y,
                    room.width,
                    room.height)
                || levelId != request.currentLevelId
                || output.witnessRooms.size() >= MaximumWitnessRooms
                || room.x < 0 || room.y < 0
                || room.width <= 0 || room.height <= 0) {
                return false;
            }
            output.witnessRooms.push_back(room);
        } else if (type == 'E') {
            AtlasExit exit{};
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    exit.sourceLevelId,
                    exit.targetLevelId,
                    exit.subtileX,
                    exit.subtileY,
                    exit.kind)
                || output.exits.size() >= MaximumAtlasExits
                || exit.sourceLevelId <= 0 || exit.targetLevelId <= 0
                || exit.subtileX < 0 || exit.subtileY < 0
                || (exit.kind != 0 && exit.kind != 1)) {
                return false;
            }
            output.exits.push_back(exit);
        } else if (type == 'W') {
            AtlasWaypoint waypoint{};
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    waypoint.levelId,
                    waypoint.subtileX,
                    waypoint.subtileY,
                    waypoint.classId)
                || output.waypoints.size() >= MaximumAtlasWaypoints
                || waypoint.levelId <= 0
                || waypoint.subtileX < 0 || waypoint.subtileY < 0
                || waypoint.classId < 0) {
                return false;
            }
            output.waypoints.push_back(waypoint);
        } else if (type == 'P') {
            AtlasPortal portal{};
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    portal.sourceLevelId,
                    portal.targetLevelId,
                    portal.subtileX,
                    portal.subtileY,
                    portal.classId)
                || output.portals.size() >= MaximumAtlasPortals
                || portal.sourceLevelId <= 0 || portal.targetLevelId <= 0
                || portal.subtileX < 0 || portal.subtileY < 0
                || portal.classId < 0) {
                return false;
            }
            output.portals.push_back(portal);
        } else if (type == 'Z') {
            if (!sawHeader || !ReadExactRecord(
                    stream,
                    declaredLevels,
                    declaredExits,
                    declaredWaypoints,
                    declaredPortals,
                    declaredRooms,
                    output.helperElapsedMilliseconds)) {
                return false;
            }
            sawEnd = true;
        } else {
            return false;
        }
    }
    if (!sawHeader || !sawEnd
        || declaredLevels != output.levels.size()
        || declaredExits != output.exits.size()
        || declaredWaypoints != output.waypoints.size()
        || declaredPortals != output.portals.size()
        || declaredRooms != output.witnessRooms.size()) {
        return false;
    }
    std::vector<std::int32_t> actLevelIds;
    actLevelIds.reserve(output.levels.size());
    for (const auto& level : output.levels) {
        actLevelIds.push_back(level.levelId);
    }
    std::sort(actLevelIds.begin(), actLevelIds.end());
    if (std::adjacent_find(actLevelIds.begin(), actLevelIds.end())
        != actLevelIds.end()) {
        return false;
    }
    for (const auto& exit : output.exits) {
        if (!IsExternalAtlasTopologyEdgeValid(
                actLevelIds,
                ExternalAtlasTopologyEdge{
                    .sourceLevelId = exit.sourceLevelId,
                    .targetLevelId = exit.targetLevelId,
                    .kind = exit.kind,
                })) {
            return false;
        }
    }
    for (const auto& waypoint : output.waypoints) {
        if (!std::binary_search(
                actLevelIds.begin(),
                actLevelIds.end(),
                waypoint.levelId)) {
            return false;
        }
    }
    for (const auto& portal : output.portals) {
        if (!std::binary_search(
                actLevelIds.begin(),
                actLevelIds.end(),
                portal.sourceLevelId)) {
            return false;
        }
    }
    std::sort(output.witnessRooms.begin(), output.witnessRooms.end());
    return true;
}

[[nodiscard]] auto ValidateCurrentLevelWitness(
        const Request& request,
        const ParsedAtlas& atlas) noexcept -> bool {
    const auto level = std::find_if(
        atlas.levels.begin(),
        atlas.levels.end(),
        [&request](const AtlasLevel& candidate) noexcept {
            return candidate.levelId == request.currentLevelId;
        });
    return level != atlas.levels.end()
        && level->originSubtileX == request.currentOriginSubtileX
        && level->originSubtileY == request.currentOriginSubtileY
        && level->roomCount == request.currentRooms.size()
        && atlas.witnessRooms == request.currentRooms;
}

[[nodiscard]] auto VisibleLevels(
        const ParsedAtlas& atlas) -> std::vector<std::int32_t> {
    // Publication is no longer guessed from continuous-seam topology. Every
    // exact anchor in the generated act is retained; the UI-thread native
    // publisher resolves authoritative Levels.Layer values and the renderer
    // admits only anchors owned by the current ready layer.
    std::vector<std::int32_t> visible;
    visible.reserve(atlas.levels.size());
    for (const auto& level : atlas.levels) {
        visible.push_back(level.levelId);
    }
    std::sort(visible.begin(), visible.end());
    visible.erase(std::unique(visible.begin(), visible.end()), visible.end());
    return visible;
}

[[nodiscard]] auto ContainsLevel(
        const std::vector<std::int32_t>& levels,
        std::int32_t levelId) noexcept -> bool {
    return std::binary_search(levels.begin(), levels.end(), levelId);
}

[[nodiscard]] auto BuildExitDefinitions(
        const ParsedAtlas& atlas,
        const std::vector<std::int32_t>& connected,
        std::vector<AutomapExitLabelDefinition>& output) -> bool {
    output.clear();
    std::vector<AtlasExit> uniqueWarps;
    struct SeamGroup final {
        std::int32_t sourceLevelId{};
        std::int32_t targetLevelId{};
        std::vector<std::pair<std::int32_t, std::int32_t>> anchors;
    };
    std::vector<SeamGroup> seamGroups;
    for (const auto& exit : atlas.exits) {
        // A label is anchored in the source map space. Continuous seams stay
        // inside the visible component. Warp targets intentionally may live in
        // a disconnected dungeon/interior coordinate island.
        if (!ContainsLevel(connected, exit.sourceLevelId)
            || (exit.kind == 1
                && !ContainsLevel(connected, exit.targetLevelId))) {
            continue;
        }
        if (exit.kind == 1) {
            auto group = std::find_if(
                seamGroups.begin(),
                seamGroups.end(),
                [&exit](const SeamGroup& candidate) noexcept {
                    return candidate.sourceLevelId == exit.sourceLevelId
                        && candidate.targetLevelId == exit.targetLevelId;
                });
            if (group == seamGroups.end()) {
                seamGroups.push_back({
                    .sourceLevelId = exit.sourceLevelId,
                    .targetLevelId = exit.targetLevelId,
                });
                group = std::prev(seamGroups.end());
            }
            group->anchors.emplace_back(exit.subtileX, exit.subtileY);
            continue;
        }
        const auto duplicate = std::find_if(
            uniqueWarps.begin(),
            uniqueWarps.end(),
            [&exit](const AtlasExit& candidate) noexcept {
                return candidate.sourceLevelId == exit.sourceLevelId
                    && candidate.targetLevelId == exit.targetLevelId
                    && candidate.subtileX == exit.subtileX
                    && candidate.subtileY == exit.subtileY;
            });
        if (duplicate == uniqueWarps.end()) uniqueWarps.push_back(exit);
    }

    for (const auto& exit : uniqueWarps) {
        output.push_back({
            .stableId = StableId(
                ExternalExitStableType,
                exit.targetLevelId,
                exit.subtileX,
                exit.subtileY),
            .sourceLevelId = exit.sourceLevelId,
            .targetLevelId = exit.targetLevelId,
            .subtileX = exit.subtileX,
            .subtileY = exit.subtileY,
        });
    }
    for (const auto& portal : atlas.portals) {
        if (!ContainsLevel(connected, portal.sourceLevelId)) continue;
        const auto duplicate = std::find_if(
            output.begin(),
            output.end(),
            [&portal](
                    const AutomapExitLabelDefinition& existing) noexcept {
                return existing.sourceLevelId == portal.sourceLevelId
                    && existing.targetLevelId == portal.targetLevelId
                    && existing.subtileX == portal.subtileX
                    && existing.subtileY == portal.subtileY;
            });
        if (duplicate != output.end()) continue;
        output.push_back({
            .stableId = StableId(
                ExternalPortalStableType,
                portal.targetLevelId,
                portal.subtileX,
                portal.subtileY),
            .sourceLevelId = portal.sourceLevelId,
            .targetLevelId = portal.targetLevelId,
            .subtileX = portal.subtileX,
            .subtileY = portal.subtileY,
        });
    }
    for (auto& group : seamGroups) {
        std::sort(group.anchors.begin(), group.anchors.end());
        group.anchors.erase(
            std::unique(group.anchors.begin(), group.anchors.end()),
            group.anchors.end());
        if (group.anchors.empty()) continue;
        // MS1 v3 retains one collision-proven, player-width opening per
        // directed outdoor level pair. Averaging room-centre samples produced
        // the misplaced Flayer Jungle label and is deliberately rejected.
        ExternalPhysicalSeamAnchor anchor{};
        if (!SelectUniqueExternalPhysicalSeamAnchor(
                group.anchors,
                anchor)) {
            return false;
        }
        const auto x = anchor.subtileX;
        const auto y = anchor.subtileY;
        output.push_back({
            .stableId = StableId(
                ExternalExitStableType,
                group.targetLevelId,
                x,
                y),
            .sourceLevelId = group.sourceLevelId,
            .targetLevelId = group.targetLevelId,
            .subtileX = x,
            .subtileY = y,
            .canonicalLevelPairAnchor = true,
        });
    }
    return output.size() <= MaximumAutomapExitLabels;
}

[[nodiscard]] auto BuildAtlasPublication(
        const Request& request,
        const ParsedAtlas& atlas,
        AtlasPublication& output) -> bool {
    output = {};
    output.visibleLevels = VisibleLevels(atlas);
    if (!ContainsLevel(output.visibleLevels, request.currentLevelId)
        || output.visibleLevels.empty()) {
        return false;
    }

    if (!BuildExitDefinitions(
            atlas, output.visibleLevels, output.exits)) {
        return false;
    }
    for (const auto& waypoint : atlas.waypoints) {
        if (!ContainsLevel(output.visibleLevels, waypoint.levelId)) continue;
        if (std::find_if(
                output.waypoints.begin(),
                output.waypoints.end(),
                [&waypoint](const AutomapWaypointLabelDefinition& existing) {
                    return existing.levelId == waypoint.levelId;
                }) != output.waypoints.end()) {
            return false;
        }
        output.waypoints.push_back({
            .stableId = StableId(
                ExternalWaypointStableType,
                waypoint.levelId,
                waypoint.subtileX,
                waypoint.subtileY),
            .levelId = waypoint.levelId,
            .subtileX = waypoint.subtileX,
            .subtileY = waypoint.subtileY,
        });
    }
    if (output.waypoints.size() > MaximumAutomapWaypointLabels) return false;

    for (const auto& level : atlas.levels) {
        if (!ContainsLevel(output.visibleLevels, level.levelId)) continue;
        output.levels.push_back({
            .stableId = StableId(
                ExternalLevelStableType,
                level.levelId,
                level.anchorSubtileX,
                level.anchorSubtileY),
            .levelId = level.levelId,
            .subtileX = level.anchorSubtileX,
            .subtileY = level.anchorSubtileY,
        });
    }
    return output.levels.size() <= MaximumAutomapLevelLabels;
}

[[nodiscard]] auto CommitAtlasPublication(
        const Request& request,
        const AtlasPublication& publication) noexcept -> bool {
    if (!ReplaceExternalAutomapExitLabels(
            request.sessionGeneration,
            publication.exits.data(),
            publication.exits.size())
        || !ReplaceExternalAutomapWaypointLabels(
            request.sessionGeneration,
            publication.waypoints.data(),
            publication.waypoints.size())
        || !ReplaceExternalAutomapLevelLabels(
            request.sessionGeneration,
            publication.levels.data(),
            publication.levels.size())) {
        return false;
    }
    return true;
}

void LogFailure(const Request& request, const char* reason) noexcept {
    if (Context == nullptr) return;
    char message[320]{};
    std::snprintf(
        message,
        sizeof(message),
        "MapSense external labels: rejected session=%llu seed=%u difficulty=%u act=%d level=%d reason=%s.",
        static_cast<unsigned long long>(request.sessionGeneration),
        request.seed,
        static_cast<unsigned>(request.difficulty),
        request.act,
        request.currentLevelId,
        reason);
    Context->LogWarn(message);
}

void NotifyResult(const Request& request, bool published) noexcept {
    const auto callback = ResultCallback;
    if (callback == nullptr) return;
    callback(
        request.sessionGeneration,
        request.difficulty,
        request.act,
        request.currentLevelId,
        published,
        ResultUserData);
}

void WorkerMain() noexcept {
    // The current act is one transaction. Keep its coordinator below the
    // game's normal-priority threads; speculative other-act children run at
    // idle priority and are preempted by real gameplay requests.
    (void)SetThreadPriority(
        GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        Request request{};
        {
            std::unique_lock lock(StateMutex);
            StateCondition.wait(lock, [] {
                return StopRequested || PendingRequest.has_value();
            });
            if (StopRequested) return;
            request = std::move(*PendingRequest);
            PendingRequest.reset();
            InFlightRequest = KeyFor(request);
        }

        std::string response;
        ExternalAtlasGeometry geometry;
        bool combinedHelperAttempted{};
        HelperCommandResult combinedHelperResult{};
        const auto geometryResult = PrepareGeometryCacheAct(
            request,
            static_cast<std::uint8_t>(request.act),
            ExternalLabelProviderOperation::PrimaryGeometry,
            &geometry,
            &response,
            &combinedHelperAttempted,
            &combinedHelperResult);
        const auto labelResult = combinedHelperAttempted
            ? combinedHelperResult
            : RunLabelHelper(request, response);
        if (!combinedHelperAttempted) {
            RecordHelperCommandResult(
                ExternalLabelProviderOperation::Labels,
                labelResult);
        }
        ParsedAtlas atlas;
        bool valid = labelResult.succeeded;
        try {
            valid = valid && ParseAtlas(request, response, atlas);
        } catch (...) {
            valid = false;
        }
        if (valid && !ValidateCurrentLevelWitness(request, atlas)) {
            valid = false;
            LogFailure(request, "current-level-coordinate-witness-mismatch");
        }

        {
            std::scoped_lock lock(StateMutex);
            if (!RequestIsCurrentLocked(request)) {
                InFlightRequest.reset();
                StaleResponses.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
            if (!valid) {
                RememberFailedRequestLocked(KeyFor(request));
                InFlightRequest.reset();
            }
        }
        if (!valid) {
            FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            if (labelResult.succeeded) {
                LogFailure(request, "malformed-or-incomplete-response");
            } else if (!labelResult.cancelled) {
                LogFailure(
                    request,
                    labelResult.timedOut
                        ? "label-timeout"
                        : "label-process-failure");
            }
            NotifyResult(request, false);
            continue;
        }

        AtlasPublication publication;
        bool labelsReady{};
        try {
            labelsReady = BuildAtlasPublication(
                request, atlas, publication);
        } catch (...) {
            labelsReady = false;
        }
        if (!labelsReady) {
            {
                std::scoped_lock lock(StateMutex);
                if (!RequestIsCurrentLocked(request)) {
                    InFlightRequest.reset();
                    StaleResponses.fetch_add(1U, std::memory_order_relaxed);
                    continue;
                }
                RememberFailedRequestLocked(KeyFor(request));
                InFlightRequest.reset();
            }
            FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            LogFailure(request, "label-publication-build-failure");
            NotifyResult(request, false);
            continue;
        }

        {
            std::scoped_lock lock(StateMutex);
            if (!RequestIsCurrentLocked(request)) {
                InFlightRequest.reset();
                StaleResponses.fetch_add(1U, std::memory_order_relaxed);
                continue;
            }
        }

        if (geometryResult == GeometryPreparationResult::Cancelled) {
            std::scoped_lock lock(StateMutex);
            InFlightRequest.reset();
            if (!RequestIsCurrentLocked(request)) {
                StaleResponses.fetch_add(1U, std::memory_order_relaxed);
            } else {
                RememberFailedRequestLocked(KeyFor(request));
                FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            }
            continue;
        }
        if (geometryResult != GeometryPreparationResult::Ready) {
            {
                std::scoped_lock lock(StateMutex);
                if (!RequestIsCurrentLocked(request)) {
                    InFlightRequest.reset();
                    StaleResponses.fetch_add(1U, std::memory_order_relaxed);
                    continue;
                }
                RememberFailedRequestLocked(KeyFor(request));
                InFlightRequest.reset();
            }
            FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            LogFailure(request, "current-act-geometry-failure");
            NotifyResult(request, false);
            continue;
        }

        const auto containsGeometryLevel = [&geometry](
                std::int32_t levelId) noexcept {
            return std::find_if(
                geometry.levels.begin(),
                geometry.levels.end(),
                [levelId](const ExternalAtlasGeometryLevel& level) {
                    return level.levelId == levelId;
                }) != geometry.levels.end();
        };
        const auto missingRequiredLevel = std::find_if(
            publication.visibleLevels.begin(),
            publication.visibleLevels.end(),
            [&geometry, &containsGeometryLevel](
                    std::int32_t levelId) noexcept {
                return IsExternalAtlasStandardCampaignLevel(
                        geometry.act, levelId)
                    && !containsGeometryLevel(levelId);
            });
        if (missingRequiredLevel != publication.visibleLevels.end()) {
            GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
            {
                std::scoped_lock lock(StateMutex);
                if (!RequestIsCurrentLocked(request)) {
                    InFlightRequest.reset();
                    StaleResponses.fetch_add(1U, std::memory_order_relaxed);
                    continue;
                }
                RememberFailedRequestLocked(KeyFor(request));
                InFlightRequest.reset();
            }
            FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            LogFailure(
                request,
                "required-visible-level-missing-from-geometry");
            NotifyResult(request, false);
            continue;
        }

        std::shared_ptr<const ExternalAtlasGeometrySnapshot> snapshot;
        try {
            auto immutableGeometry =
                std::make_shared<const ExternalAtlasGeometry>(
                    std::move(geometry));
            snapshot = std::make_shared<const ExternalAtlasGeometrySnapshot>(
                ExternalAtlasGeometrySnapshot{
                    .sessionGeneration = request.sessionGeneration,
                    .seed = request.seed,
                    .difficulty = request.difficulty,
                    .act = static_cast<std::uint8_t>(request.act),
                    .currentLevelId = request.currentLevelId,
                    .visibleLevelIds = publication.visibleLevels,
                    .geometry = std::move(immutableGeometry),
                });
        } catch (...) {
            snapshot.reset();
        }

        bool committed{};
        bool stale{};
        {
            std::scoped_lock lock(StateMutex);
            const auto completion = DecideExternalLabelProviderCompletion(
                RequestIsCurrentLocked(request),
                labelsReady,
                geometryResult == GeometryPreparationResult::Ready,
                snapshot != nullptr);
            if (completion == ExternalLabelProviderCompletion::Stale) {
                stale = true;
                InFlightRequest.reset();
            } else if (completion == ExternalLabelProviderCompletion::Failed
                || !CommitAtlasPublication(request, publication)) {
                RememberFailedRequestLocked(KeyFor(request));
                InFlightRequest.reset();
            } else {
                PublishedGeometrySnapshot.store(
                    snapshot,
                    std::memory_order_release);
                PublishedGeometryCells.store(
                    snapshot->geometry->cells.size(),
                    std::memory_order_relaxed);
                PublishedRequest = KeyFor(request);
                InFlightRequest.reset();
                committed = true;
            }
        }
        if (stale) {
            StaleResponses.fetch_add(1U, std::memory_order_relaxed);
            continue;
        }
        if (!committed) {
            GeometryFailures.fetch_add(1U, std::memory_order_relaxed);
            FailedResponses.fetch_add(1U, std::memory_order_relaxed);
            LogFailure(request, "transaction-commit-failure");
            NotifyResult(request, false);
            continue;
        }

        GeometrySnapshotsPublished.fetch_add(1U, std::memory_order_relaxed);
        AtlasesPublished.fetch_add(1U, std::memory_order_relaxed);
        NotifyResult(request, true);
        if (Context != nullptr) {
            char message[512]{};
            std::snprintf(
                message,
                sizeof(message),
                "MapSense external labels: PASS session=%llu seed=%u difficulty=%u act=%d current-level=%d visible-levels=%zu exits=%zu waypoints=%zu room-witness=%zu data=%016llX helper-ms=%.3f.",
                static_cast<unsigned long long>(request.sessionGeneration),
                request.seed,
                static_cast<unsigned>(request.difficulty),
                request.act,
                request.currentLevelId,
                publication.visibleLevels.size(),
                publication.exits.size(),
                publication.waypoints.size(),
                request.currentRooms.size(),
                static_cast<unsigned long long>(request.dataFingerprint),
                atlas.helperElapsedMilliseconds);
            Context->LogInfo(message);
        }
        PrewarmGeometryCache(request);
    }
}

} // namespace

auto InitializeExternalLabelProvider(
        const D2RL::PluginContext* context,
        bool diagnostics,
        ExternalLabelAtlasResultCallback resultCallback,
        void* resultUserData) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    std::filesystem::path helper;
    if (!ResolveHelperPath(helper)) {
        context->LogWarn(
            "MapSense external labels: helper executable is unavailable; distant labels remain fail-closed.");
        return false;
    }
    std::filesystem::path geometryCacheRoot;
    if (!ResolveExternalAtlasCacheRoot(geometryCacheRoot)) {
        context->LogWarn(
            "MapSense external atlas: persistent cache root is unavailable; labels remain active but geometry generation is disabled.");
    }
    try {
        std::scoped_lock lock(StateMutex);
        if (Active.load(std::memory_order_acquire)) return true;
        Context = context;
        Diagnostics.store(diagnostics, std::memory_order_release);
        ResultCallback = resultCallback;
        ResultUserData = resultUserData;
        HelperPath = std::move(helper);
        GeometryCacheRoot = std::move(geometryCacheRoot);
        PublishedGeometrySnapshot.store(
            std::shared_ptr<const ExternalAtlasGeometrySnapshot>{},
            std::memory_order_release);
        PublishedGeometryCells.store(0U, std::memory_order_relaxed);
        StopRequested = false;
        PendingRequest.reset();
        InFlightRequest.reset();
        PublishedRequest.reset();
        FailedRequestCount = 0U;
        LatestSerial = 0U;
        ActiveSessionGeneration = 0U;
        ActiveChildProcess = nullptr;
        ActiveChildOperation = ExternalLabelProviderOperation::None;
        ActiveChildCancellationRequested = false;
        ActiveOperationCode.store(
            static_cast<std::uint32_t>(
                ExternalLabelProviderOperation::None),
            std::memory_order_release);
        ActiveOperationSeed.store(0U, std::memory_order_release);
        ActiveOperationAct.store(-1, std::memory_order_release);
        Worker = std::thread(WorkerMain);
        Active.store(true, std::memory_order_release);
    } catch (...) {
        Context = nullptr;
        HelperPath.clear();
        GeometryCacheRoot.clear();
        return false;
    }
    context->LogInfo(
        "MapSense external labels: automatic seed-scoped helper is ready.");
    return true;
}

void ShutdownExternalLabelProvider() noexcept {
    Active.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(StateMutex);
        StopRequested = true;
        ++LatestSerial;
        PendingRequest.reset();
        CancelActiveChildLocked();
        PublishedGeometrySnapshot.store(
            std::shared_ptr<const ExternalAtlasGeometrySnapshot>{},
            std::memory_order_release);
        PublishedGeometryCells.store(0U, std::memory_order_relaxed);
    }
    StateCondition.notify_all();
    if (Worker.joinable()) Worker.join();
    {
        std::scoped_lock lock(StateMutex);
        StopRequested = false;
        InFlightRequest.reset();
        PublishedRequest.reset();
        FailedRequestCount = 0U;
        ActiveSessionGeneration = 0U;
        ActiveChildProcess = nullptr;
        ActiveChildOperation = ExternalLabelProviderOperation::None;
        ActiveChildCancellationRequested = false;
        ActiveOperationCode.store(
            static_cast<std::uint32_t>(
                ExternalLabelProviderOperation::None),
            std::memory_order_release);
        ActiveOperationSeed.store(0U, std::memory_order_release);
        ActiveOperationAct.store(-1, std::memory_order_release);
        HelperPath.clear();
        GeometryCacheRoot.clear();
    }
    Context = nullptr;
    ResultCallback = nullptr;
    ResultUserData = nullptr;
}

void ResetExternalLabelProviderSession(
        std::uint64_t sessionGeneration) noexcept {
    std::scoped_lock lock(StateMutex);
    ActiveSessionGeneration = sessionGeneration;
    ++LatestSerial;
    PendingRequest.reset();
    InFlightRequest.reset();
    PublishedRequest.reset();
    FailedRequestCount = 0U;
    CancelActiveChildLocked();
    PublishedGeometrySnapshot.store(
        std::shared_ptr<const ExternalAtlasGeometrySnapshot>{},
        std::memory_order_release);
    PublishedGeometryCells.store(0U, std::memory_order_relaxed);
}

auto RequestExternalLabelAtlas(
        std::uint64_t sessionGeneration,
        const ClientLevelView& current,
        std::int32_t resolvedAct,
        std::shared_ptr<const MapSenseDataCatalog> dataCatalog) noexcept
        -> bool {
    if (!Active.load(std::memory_order_acquire)) return false;
    Request request;
    try {
        if (!CaptureRequest(
                sessionGeneration,
                current,
                resolvedAct,
                std::move(dataCatalog),
                request)) {
            return false;
        }
    } catch (...) {
        return false;
    }
    {
        std::scoped_lock lock(StateMutex);
        if (StopRequested
            || sessionGeneration != ActiveSessionGeneration) {
            return false;
        }
        const auto key = KeyFor(request);
        const auto pendingKey = PendingRequest
            ? std::optional<RequestKey>{KeyFor(*PendingRequest)}
            : std::nullopt;
        const auto decision = DecideExternalLabelProviderSubmission(
            key,
            PublishedRequest,
            InFlightRequest,
            pendingKey,
            ContainsFailedRequestLocked(key),
            ActiveChildOperation);
        if (decision == ExternalLabelProviderSubmission::Duplicate) {
            return true;
        }
        request.serial = ++LatestSerial;
        PendingRequest = std::move(request);
        if (decision
            == ExternalLabelProviderSubmission::QueueAndCancel) {
            CancelActiveChildLocked();
        }
    }
    Requests.fetch_add(1U, std::memory_order_relaxed);
    StateCondition.notify_one();
    return true;
}

auto IsExternalLabelProviderActive() noexcept -> bool {
    return Active.load(std::memory_order_acquire);
}

auto AcquireExternalAtlasGeometrySnapshot() noexcept
        -> std::shared_ptr<const ExternalAtlasGeometrySnapshot> {
    return PublishedGeometrySnapshot.load(std::memory_order_acquire);
}

auto WantsExternalAtlasGeometryFrame() noexcept -> bool {
    const auto snapshot = AcquireExternalAtlasGeometrySnapshot();
    return snapshot != nullptr && snapshot->geometry != nullptr
        && !snapshot->visibleLevelIds.empty()
        && !snapshot->geometry->cells.empty();
}

auto HasPersistedExternalRevealMapIntent(
        std::uint32_t seed,
        std::uint8_t difficulty) noexcept -> bool {
    std::filesystem::path root;
    try {
        std::scoped_lock lock(StateMutex);
        root = GeometryCacheRoot;
    } catch (...) {
        return false;
    }
    return HasExternalAtlasRevealMapIntent(root, seed, difficulty);
}

auto SetPersistedExternalRevealMapIntent(
        std::uint32_t seed,
        std::uint8_t difficulty,
        bool enabled) noexcept -> bool {
    std::filesystem::path root;
    try {
        std::scoped_lock lock(StateMutex);
        root = GeometryCacheRoot;
    } catch (...) {
        return false;
    }
    return StoreExternalAtlasRevealMapIntent(
        root, seed, difficulty, enabled);
}

auto GetExternalLabelProviderCounters() noexcept
        -> ExternalLabelProviderCounters {
    return {
        .requests = Requests.load(std::memory_order_relaxed),
        .processesStarted = ProcessesStarted.load(std::memory_order_relaxed),
        .atlasesPublished = AtlasesPublished.load(std::memory_order_relaxed),
        .staleResponses = StaleResponses.load(std::memory_order_relaxed),
        .failedResponses = FailedResponses.load(std::memory_order_relaxed),
        .timeouts = Timeouts.load(std::memory_order_relaxed),
        .labelTimeouts = LabelTimeouts.load(std::memory_order_relaxed),
        .geometryTimeouts = GeometryTimeouts.load(
            std::memory_order_relaxed),
        .primaryCancellations = PrimaryCancellations.load(
            std::memory_order_relaxed),
        .prewarmCancellations = PrewarmCancellations.load(
            std::memory_order_relaxed),
        .geometryCacheHits = GeometryCacheHits.load(
            std::memory_order_relaxed),
        .geometryCacheMisses = GeometryCacheMisses.load(
            std::memory_order_relaxed),
        .geometryCacheInvalid = GeometryCacheInvalid.load(
            std::memory_order_relaxed),
        .geometryAtlasesGenerated = GeometryAtlasesGenerated.load(
            std::memory_order_relaxed),
        .geometryFailures = GeometryFailures.load(
            std::memory_order_relaxed),
        .geometrySnapshotsPublished = GeometrySnapshotsPublished.load(
            std::memory_order_relaxed),
        .publishedGeometryCells = PublishedGeometryCells.load(
            std::memory_order_relaxed),
        .activeOperation = static_cast<ExternalLabelProviderOperation>(
            ActiveOperationCode.load(std::memory_order_acquire)),
        .activeSeed = ActiveOperationSeed.load(std::memory_order_acquire),
        .activeAct = ActiveOperationAct.load(std::memory_order_acquire),
    };
}

} // namespace RuffnecKk::MapSense
