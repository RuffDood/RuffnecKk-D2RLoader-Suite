#include "process_lifetime_worker.hpp"
#include "gps_route_provider.hpp"
#include "navigation_engine.hpp"

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace RuffnecKk::MapSense {
namespace {

static_assert(MaximumGpsRouteGoalSnapDistance
    == MaximumNavigationGpsGoalSnapDistance);

constexpr wchar_t HelperFileName[] = L"RuffnecKkMapSenseMapgen.exe";
constexpr DWORD RouteTimeoutMilliseconds = 15'000U;
constexpr std::size_t MaximumRouteArtifactBytes = MaximumGpsRouteArtifactBytes;
constexpr std::size_t MaximumInputTableBytes = 64U * 1'024U * 1'024U;
constexpr std::array<std::string_view, 7U> InputTableNames{
    "levels.txt", "lvlprest.txt", "lvltypes.txt", "lvlmaze.txt",
    "lvlsub.txt", "lvlwarp.txt", "objects.txt",
};

const D2RL::PluginContext* Context{};
std::atomic_bool Active{};
std::mutex StateMutex;
std::condition_variable StateCondition;
Detail::ProcessLifetimeWorker Worker;
bool StopRequested{};
GpsRouteSessionGate SessionGate{};
std::uint64_t LatestRequestEpoch{};
// Reset may originate in Present. It must never wait for the worker's state
// mutex, so the consumer applies this request at its next locked boundary.
GpsRouteResetMailbox PendingReset{};
std::uint64_t ProviderInstanceToken{};
std::uint64_t NextOutputSerial{};
std::vector<GpsRouteProviderRequest> Desired{};
struct FailedRequest final {
    std::uint64_t requestEpoch{};
    GpsRoutePublicationIdentity identity{};
};
std::vector<FailedRequest> Failed{};
std::vector<GpsRouteProviderPath> Published{};
std::filesystem::path HelperPath;
std::filesystem::path OutputRoot;
HANDLE ActiveChildProcess{};

class UniqueHandle final {
public:
    explicit UniqueHandle(HANDLE value = nullptr) noexcept : value_(value) {}
    ~UniqueHandle() { Reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    auto operator=(const UniqueHandle&) -> UniqueHandle& = delete;
    [[nodiscard]] auto Get() const noexcept -> HANDLE { return value_; }
    void Reset(HANDLE value = nullptr) noexcept {
        if (value_ != nullptr) CloseHandle(value_);
        value_ = value;
    }
private:
    HANDLE value_{};
};

[[nodiscard]] auto ResolveHelperPath(std::filesystem::path& output) noexcept
        -> bool {
    output.clear();
    std::array<wchar_t, 32'768U> modulePath{};
    const auto length = GetModuleFileNameW(
        reinterpret_cast<HMODULE>(&__ImageBase), modulePath.data(),
        static_cast<DWORD>(modulePath.size()));
    if (length == 0U || length >= modulePath.size()) return false;
    try {
        output = std::filesystem::path(std::wstring_view(modulePath.data(), length))
            .parent_path() / HelperFileName;
    } catch (...) {
        output.clear();
        return false;
    }
    return GetFileAttributesW(output.c_str()) != INVALID_FILE_ATTRIBUTES;
}

[[nodiscard]] auto ResolveOutputRoot(std::filesystem::path& output) noexcept
        -> bool {
    output.clear();
    std::array<wchar_t, 32'768U> temporary{};
    const auto length = GetTempPathW(
        static_cast<DWORD>(temporary.size()), temporary.data());
    if (length == 0U || length >= temporary.size()) return false;
    try {
        output = std::filesystem::path(std::wstring_view(temporary.data(), length))
            / L"RuffnecKkMapSense" / L"gps-route-diagnostics";
        std::error_code error;
        std::filesystem::create_directories(output, error);
        return !error;
    } catch (...) {
        output.clear();
        return false;
    }
}

[[nodiscard]] auto ContainsIdentity(
        const std::vector<GpsRouteProviderRequest>& values,
        const GpsRoutePublicationIdentity& identity) noexcept -> bool {
    return std::any_of(values.begin(), values.end(),
        [&identity](const GpsRouteProviderRequest& value) noexcept {
            return value.identity == identity;
        });
}

[[nodiscard]] auto SameRequestScope(
        GpsRoutePublicationIdentity left,
        GpsRoutePublicationIdentity right) noexcept -> bool {
    return SameGpsRoutePublicationScope(left, right);
}

[[nodiscard]] auto ContainsRequestScope(
        const std::vector<GpsRouteProviderRequest>& values,
        const GpsRoutePublicationIdentity& identity) noexcept -> bool {
    return std::any_of(values.begin(), values.end(),
        [&identity](const GpsRouteProviderRequest& value) noexcept {
            return SameRequestScope(value.identity, identity);
        });
}

[[nodiscard]] auto SameDesiredIdentities(
        std::span<const GpsRouteProviderRequest> left,
        std::span<const GpsRouteProviderRequest> right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    return std::all_of(left.begin(), left.end(),
        [&right](const GpsRouteProviderRequest& request) noexcept {
            return std::any_of(right.begin(), right.end(),
                [&request](const GpsRouteProviderRequest& candidate) noexcept {
                    return SameRequestScope(candidate.identity, request.identity);
                });
        });
}

[[nodiscard]] auto ContainsIdentity(
        const std::vector<GpsRoutePublicationIdentity>& values,
        const GpsRoutePublicationIdentity& identity) noexcept -> bool {
    return std::find(values.begin(), values.end(), identity) != values.end();
}

[[nodiscard]] auto ContainsFailureScope(
        const GpsRouteProviderRequest& request) noexcept -> bool {
    return std::any_of(Failed.begin(), Failed.end(),
        [&request](const FailedRequest& failed) noexcept {
            return failed.requestEpoch == request.requestEpoch
                && SameRequestScope(failed.identity, request.identity);
        });
}

[[nodiscard]] auto HasPublishedScope(
        const GpsRoutePublicationIdentity& identity) noexcept -> bool {
    return std::any_of(Published.begin(), Published.end(),
        [&identity](const GpsRouteProviderPath& path) noexcept {
            return SameRequestScope(path.identity, identity);
        });
}

[[nodiscard]] auto CanPublishMovesLocked(
        const GpsRouteProviderRequest& request,
        std::size_t moveCount) noexcept -> bool {
    if (moveCount < 2U || moveCount > MaximumNavigationGpsRoutePoints) {
        return false;
    }
    std::size_t totalMoves{};
    for (const auto& path : Published) {
        if (path.identity.destinationId == request.identity.destinationId
            && path.identity.destinationKind == request.identity.destinationKind) {
            continue;
        }
        if (path.moves.size() < 2U
            || path.moves.size() > MaximumNavigationGpsRoutePoints
            || totalMoves > MaximumNavigationGpsRoutePoints - path.moves.size()) {
            return false;
        }
        totalMoves += path.moves.size();
    }
    return CanAccumulateGpsRoutePoints(
        totalMoves, moveCount, MaximumNavigationGpsRoutePoints);
}

[[nodiscard]] auto IsDesiredLocked(
        const GpsRouteProviderRequest& request) noexcept -> bool {
    return !StopRequested
        && SessionGate.Accepts(request.identity.sessionGeneration)
        && request.requestEpoch == LatestRequestEpoch
        && std::any_of(Desired.begin(), Desired.end(),
            [&request](const GpsRouteProviderRequest& desired) noexcept {
                return desired.requestEpoch == request.requestEpoch
                    && SameRequestScope(desired.identity, request.identity);
            });
}

void CancelActiveChildLocked() noexcept {
    if (ActiveChildProcess != nullptr) {
        (void)TerminateProcess(ActiveChildProcess, ERROR_CANCELLED);
    }
}

void ApplyPendingResetLocked() noexcept {
    if (!SessionGate.ConsumeReset(PendingReset)) return;
    ++LatestRequestEpoch;
    Desired.clear();
    Failed.clear();
    Published.clear();
    CancelActiveChildLocked();
}

[[nodiscard]] auto ValidRequest(const GpsRouteProviderRequest& request)
        noexcept -> bool {
    if (!IsGpsRoutePublicationCurrent(request.identity, request.identity)
        || request.identity.artifact.difficulty > 2U) {
        return false;
    }
    return std::all_of(request.excelRoots.begin(), request.excelRoots.end(),
        [](const std::filesystem::path& path) noexcept { return !path.empty(); })
        && std::all_of(request.tileRoots.begin(), request.tileRoots.end(),
            [](const std::filesystem::path& path) noexcept { return !path.empty(); });
}

void FingerprintBytes(
        std::uint64_t& fingerprint,
        const std::uint8_t* bytes,
        std::size_t count) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        fingerprint ^= bytes[index];
        fingerprint *= UINT64_C(1099511628211);
    }
}

[[nodiscard]] auto Quote(std::wstring_view value) -> std::wstring {
    std::wstring output{L"\""};
    output.append(value);
    output.push_back(L'\"');
    return output;
}

[[nodiscard]] auto RunRouteHelper(
        const GpsRouteProviderRequest& request,
        std::filesystem::path& artifactPath,
        DWORD& exitCode) noexcept -> bool {
    artifactPath.clear();
    exitCode = STILL_ACTIVE;
    std::wstring command;
    try {
        artifactPath = OutputRoot / (L"route-" + std::to_wstring(GetCurrentProcessId())
            + L"-" + std::to_wstring(ProviderInstanceToken)
            + L"-" + std::to_wstring(request.requestEpoch)
            + L"-" + std::to_wstring(++NextOutputSerial) + L".msr");
        const auto& identity = request.identity.artifact;
        command = Quote(HelperPath.wstring()) + L" route-follow-binary "
            + std::to_wstring(identity.seed) + L" "
            + std::to_wstring(identity.difficulty) + L" "
            + std::to_wstring(identity.levelId) + L" "
            + std::to_wstring(identity.fromSubtileX) + L" "
            + std::to_wstring(identity.fromSubtileY) + L" "
            + std::to_wstring(identity.toSubtileX) + L" "
            + std::to_wstring(identity.toSubtileY) + L" "
            + (identity.mode == GpsRouteMode::Walk ? L"walk " : L"teleport ")
            + Quote(artifactPath.wstring());
        for (const auto& root : request.excelRoots) {
            command += L" --excel-root " + Quote(root.wstring());
        }
        for (const auto& root : request.tileRoots) {
            command += L" --tiles-root " + Quote(root.wstring());
        }
    } catch (...) {
        artifactPath.clear();
        return false;
    }
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(
            HelperPath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr,
            HelperPath.parent_path().c_str(), &startup, &processInfo)) {
        artifactPath.clear();
        return false;
    }
    UniqueHandle process(processInfo.hProcess);
    UniqueHandle thread(processInfo.hThread);
    {
        std::scoped_lock lock(StateMutex);
        ActiveChildProcess = process.Get();
        if (!IsDesiredLocked(request)) CancelActiveChildLocked();
    }
    const auto wait = WaitForSingleObject(process.Get(), RouteTimeoutMilliseconds);
    if (wait == WAIT_TIMEOUT) {
        (void)TerminateProcess(process.Get(), ERROR_TIMEOUT);
        (void)WaitForSingleObject(process.Get(), 1'000U);
    }
    {
        std::scoped_lock lock(StateMutex);
        if (ActiveChildProcess == process.Get()) ActiveChildProcess = nullptr;
    }
    if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.Get(), &exitCode)
        || exitCode != 0U) {
        std::error_code error;
        std::filesystem::remove(artifactPath, error);
        artifactPath.clear();
        return false;
    }
    return true;
}

[[nodiscard]] auto ReadArtifact(
        const std::filesystem::path& path,
        std::vector<std::uint8_t>& bytes) noexcept -> bool {
    bytes.clear();
    try {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0U || size > MaximumRouteArtifactBytes) return false;
        bytes.resize(static_cast<std::size_t>(size));
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        input.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return input && input.gcount() == static_cast<std::streamsize>(bytes.size());
    } catch (...) {
        bytes.clear();
        return false;
    }
}

void WorkerMain() noexcept {
    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        GpsRouteProviderRequest request{};
        {
            std::unique_lock lock(StateMutex);
            ApplyPendingResetLocked();
            StateCondition.wait(lock, [] {
                return StopRequested
                    || PendingReset.Pending()
                    || std::any_of(Desired.begin(), Desired.end(),
                    [](const GpsRouteProviderRequest& candidate) noexcept {
                        return !ContainsFailureScope(candidate)
                            && !HasPublishedScope(candidate.identity);
                    });
            });
            ApplyPendingResetLocked();
            if (StopRequested) return;
            const auto next = std::find_if(Desired.begin(), Desired.end(),
                [](const GpsRouteProviderRequest& candidate) noexcept {
                    return !ContainsFailureScope(candidate)
                        && !HasPublishedScope(candidate.identity);
                });
            if (next == Desired.end()) continue;
            request = *next;
        }

        std::filesystem::path artifactPath;
        std::vector<std::uint8_t> bytes;
        GpsRouteArtifact artifact{};
        std::uint64_t inputFingerprint{};
        const bool fingerprintReady = ComputeGpsRouteInputFingerprint(
            request.excelRoots, inputFingerprint);
        if (fingerprintReady) {
            request.identity.artifact.dataFingerprint = inputFingerprint;
        }
        DWORD helperExitCode{STILL_ACTIVE};
        const bool invoked = fingerprintReady
            && RunRouteHelper(request, artifactPath, helperExitCode);
        const bool artifactRead = invoked && ReadArtifact(artifactPath, bytes);
        const bool artifactParsed = artifactRead
            && ParseGpsRouteArtifact(bytes, request.identity.artifact, artifact);
        const bool valid = artifactParsed;
        std::error_code error;
        if (!artifactPath.empty()) std::filesystem::remove(artifactPath, error);

        {
            std::scoped_lock lock(StateMutex);
            if (!IsDesiredLocked(request)) continue;
            if (!valid || !CanPublishMovesLocked(request, artifact.moves.size())) {
                if (!ContainsFailureScope(request)) {
                    Failed.push_back({request.requestEpoch, request.identity});
                }
                if (Context != nullptr) Context->LogWarn(
                    valid
                        ? "MapSense GPS route: cumulative route budget was exceeded; no route was published."
                        : "MapSense GPS route: helper artifact was rejected; no route was published.");
                if (!valid && Context != nullptr) {
                    char detail[512]{};
                    const auto& identity = request.identity.artifact;
                    const char* stage = !fingerprintReady ? "fingerprint"
                        : !invoked ? "helper-process"
                        : !artifactRead ? "artifact-read" : "artifact-parse";
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "MapSense GPS route: stage=%s exit=%lu seed=%u difficulty=%u level=%d from=%d,%d to=%d,%d mode=%s artifact-bytes=%llu fingerprint=%016llX.",
                        stage,
                        static_cast<unsigned long>(helperExitCode),
                        identity.seed,
                        identity.difficulty,
                        identity.levelId,
                        identity.fromSubtileX,
                        identity.fromSubtileY,
                        identity.toSubtileX,
                        identity.toSubtileY,
                        identity.mode == GpsRouteMode::Walk ? "walk" : "teleport",
                        static_cast<unsigned long long>(bytes.size()),
                        static_cast<unsigned long long>(identity.dataFingerprint));
                    Context->LogWarn(detail);
                }
            } else {
                auto published = std::find_if(Published.begin(), Published.end(),
                    [&request](const GpsRouteProviderPath& path) noexcept {
                        return path.identity.destinationId == request.identity.destinationId
                            && path.identity.destinationKind == request.identity.destinationKind;
                    });
                GpsRouteProviderPath path{
                    .identity = request.identity,
                    .moves = std::move(artifact.moves),
                    .walkGrid = std::move(artifact.walkGrid),
                };
                if (Context != nullptr && !path.moves.empty()) {
                    const auto& first = path.moves.front();
                    const auto& last = path.moves.back();
                    char detail[512]{};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "MapSense GPS pipeline: helper=accepted kind=%u destination=%llu mode=%s moves=%llu from=%d,%d endpoint=%d,%d requested=%d,%d.",
                        static_cast<unsigned>(request.identity.destinationKind),
                        static_cast<unsigned long long>(request.identity.destinationId),
                        request.identity.artifact.mode == GpsRouteMode::Walk
                            ? "walk" : "teleport",
                        static_cast<unsigned long long>(path.moves.size()),
                        first.subtileX,
                        first.subtileY,
                        last.subtileX,
                        last.subtileY,
                        request.identity.artifact.toSubtileX,
                        request.identity.artifact.toSubtileY);
                    Context->LogInfo(detail);
                }
                if (published == Published.end()) Published.push_back(std::move(path));
                else *published = std::move(path);
                if (Context != nullptr) {
                    char detail[256]{};
                    std::snprintf(
                        detail,
                        sizeof(detail),
                        "MapSense GPS pipeline: provider-stored=%llu desired=%llu epoch=%llu.",
                        static_cast<unsigned long long>(Published.size()),
                        static_cast<unsigned long long>(Desired.size()),
                        static_cast<unsigned long long>(LatestRequestEpoch));
                    Context->LogInfo(detail);
                }
            }
        }
    }
}

} // namespace

auto ComputeGpsRouteInputFingerprint(
        std::span<const std::filesystem::path> excelRoots,
        std::uint64_t& fingerprint) noexcept -> bool {
    fingerprint = UINT64_C(14695981039346656037);
    try {
        for (const auto name : InputTableNames) {
            FingerprintBytes(
                fingerprint,
                reinterpret_cast<const std::uint8_t*>(name.data()),
                name.size());
            bool found{};
            for (const auto& root : excelRoots) {
                const auto path = root / std::string{name};
                std::error_code error;
                if (!std::filesystem::exists(path, error)) {
                    if (error) return false;
                    continue;
                }
                const auto size = std::filesystem::file_size(path, error);
                if (error || size == 0U || size > MaximumInputTableBytes
                    || size > static_cast<std::uintmax_t>(
                        (std::numeric_limits<std::size_t>::max)())) {
                    return false;
                }
                std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
                std::ifstream input(path, std::ios::binary);
                if (!input) return false;
                input.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
                if (!input || input.gcount()
                    != static_cast<std::streamsize>(bytes.size())) return false;
                FingerprintBytes(fingerprint, bytes.data(), bytes.size());
                found = true;
                break;
            }
            if (!found) {
                const std::uint8_t missing{};
                FingerprintBytes(fingerprint, &missing, 1U);
            }
        }
        return true;
    } catch (...) {
        fingerprint = 0U;
        return false;
    }
}

auto InitializeGpsRouteProvider(
        const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    std::filesystem::path helper;
    std::filesystem::path outputRoot;
    if (!ResolveHelperPath(helper) || !ResolveOutputRoot(outputRoot)) {
        context->LogWarn("MapSense GPS route: helper is unavailable; GPS lines cannot start.");
        return false;
    }
    try {
        std::scoped_lock lock(StateMutex);
        if (Active.load(std::memory_order_acquire)) return true;
        Context = context;
        HelperPath = std::move(helper);
        OutputRoot = std::move(outputRoot);
        StopRequested = false;
        SessionGate.Reset();
        LatestRequestEpoch = 0U;
        PendingReset.Reset();
        ProviderInstanceToken = GetTickCount64();
        NextOutputSerial = 0U;
        Desired.clear();
        Failed.clear();
        Published.clear();
        ActiveChildProcess = nullptr;
        Worker.Start(WorkerMain);
        Active.store(true, std::memory_order_release);
    } catch (...) {
        Context = nullptr;
        HelperPath.clear();
        OutputRoot.clear();
        return false;
    }
    context->LogInfo("MapSense GPS route: helper is ready.");
    return true;
}

void ShutdownGpsRouteProvider() noexcept {
    Active.store(false, std::memory_order_release);
    {
        std::scoped_lock lock(StateMutex);
        StopRequested = true;
        Desired.clear();
        Published.clear();
        CancelActiveChildLocked();
    }
    StateCondition.notify_all();
    if (Worker.joinable()) Worker.join();
    {
        std::scoped_lock lock(StateMutex);
        StopRequested = false;
        Failed.clear();
        SessionGate.Reset();
        LatestRequestEpoch = 0U;
        PendingReset.Reset();
        ProviderInstanceToken = 0U;
        ActiveChildProcess = nullptr;
        HelperPath.clear();
        OutputRoot.clear();
    }
    Context = nullptr;
}

void ResetGpsRouteProviderSession(std::uint64_t sessionGeneration) noexcept {
    PendingReset.Request(sessionGeneration);
    StateCondition.notify_all();
}

auto SubmitGpsRouteProviderRequests(
        std::span<const GpsRouteProviderRequest> requests) noexcept -> bool {
    if (!Active.load(std::memory_order_acquire)) return false;
    if (!IsValidGpsRouteRequestCount(
            requests.size(), MaximumNavigationGpsRoutePaths)) {
        return false;
    }
    for (const auto& request : requests) {
        if (!ValidRequest(request)) return false;
    }
    try {
        std::unique_lock lock(StateMutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        ApplyPendingResetLocked();
        if (StopRequested
            || !SessionGate.Accepts(requests.front().identity.sessionGeneration)) {
            return false;
        }
        const auto generation = requests.front().identity.sessionGeneration;
        for (const auto& request : requests) {
            if (request.identity.sessionGeneration != generation) return false;
        }
        for (std::size_t index = 0U; index < requests.size(); ++index) {
            for (std::size_t previous = 0U; previous < index; ++previous) {
                if (SameRequestScope(
                        requests[previous].identity, requests[index].identity)) {
                    return false;
                }
            }
        }
        if (SameDesiredIdentities(Desired, requests)) return true;
        std::vector<GpsRouteProviderRequest> replacement(
            requests.begin(), requests.end());
        ++LatestRequestEpoch;
        for (auto& request : replacement) request.requestEpoch = LatestRequestEpoch;
        Desired = std::move(replacement);
        Failed.clear();
        Published.erase(std::remove_if(Published.begin(), Published.end(),
            [](const GpsRouteProviderPath& path) noexcept {
                return !ContainsRequestScope(Desired, path.identity);
            }), Published.end());
        CancelActiveChildLocked();
    } catch (...) {
        return false;
    }
    StateCondition.notify_all();
    return true;
}

auto AcquireGpsRouteProviderPaths(
        std::vector<GpsRouteProviderPath>& paths) noexcept -> bool {
    paths.clear();
    if (!Active.load(std::memory_order_acquire)) return false;
    try {
        std::unique_lock lock(StateMutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        ApplyPendingResetLocked();
        std::size_t totalMoves{};
        for (const auto& path : Published) {
            if (path.moves.size() < 2U
                || path.moves.size() > MaximumNavigationGpsRoutePoints
            || !CanAccumulateGpsRoutePoints(
                totalMoves, path.moves.size(), MaximumNavigationGpsRoutePoints)) {
                paths.clear();
                return false;
            }
            totalMoves += path.moves.size();
        }
        paths = Published;
        return true;
    } catch (...) {
        paths.clear();
        return false;
    }
}

auto GetGpsRouteProviderStatus() noexcept -> GpsRouteProviderStatus {
    if (!Active.load(std::memory_order_acquire)) {
        return GpsRouteProviderStatus::Unavailable;
    }
    try {
        std::unique_lock lock(StateMutex, std::try_to_lock);
        if (!lock.owns_lock()) return GpsRouteProviderStatus::Calculating;
        ApplyPendingResetLocked();
        return EvaluateGpsRouteProviderAggregateStatus(Desired,
            [](const GpsRouteProviderRequest& request) noexcept {
                return HasPublishedScope(request.identity);
            },
            [](const GpsRouteProviderRequest& request) noexcept {
                return ContainsFailureScope(request);
            });
    } catch (...) {
        return GpsRouteProviderStatus::Unavailable;
    }
}

auto AcquireGpsRouteProviderStatuses(
        std::span<GpsRouteProviderStatus> statuses) noexcept -> bool {
    std::fill(statuses.begin(), statuses.end(), GpsRouteProviderStatus::Unavailable);
    if (!Active.load(std::memory_order_acquire)) return false;
    try {
        std::unique_lock lock(StateMutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            std::fill(statuses.begin(), statuses.end(),
                GpsRouteProviderStatus::Calculating);
            return false;
        }
        ApplyPendingResetLocked();
        for (std::size_t kind = 0U; kind < statuses.size(); ++kind) {
            bool desired{};
            bool pending{};
            bool published{};
            for (const auto& request : Desired) {
                if (request.identity.destinationKind != kind) continue;
                desired = true;
                if (HasPublishedScope(request.identity)) published = true;
                else if (!ContainsFailureScope(request)) pending = true;
            }
            if (!desired) continue;
            statuses[kind] = pending
                ? GpsRouteProviderStatus::Calculating
                : published ? GpsRouteProviderStatus::RouteReady
                            : GpsRouteProviderStatus::NoRoute;
        }
        return true;
    } catch (...) {
        std::fill(statuses.begin(), statuses.end(),
            GpsRouteProviderStatus::Unavailable);
        return false;
    }
}

} // namespace RuffnecKk::MapSense
