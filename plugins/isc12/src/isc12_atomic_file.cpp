#include "isc12_atomic_file.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <string>

namespace ruffneckk::isc12 {
namespace {

constexpr std::size_t MaximumNameAttempts = 32;
std::atomic_uint64_t SiblingSequence{};

auto ApiIsComplete(const AtomicFileApi& api) noexcept -> bool {
    return api.createFileW && api.writeFile && api.flushFileBuffers
        && api.closeHandle && api.getFileAttributesW && api.replaceFileW
        && api.moveFileExW && api.deleteFileW && api.getLastError
        && api.getCurrentProcessId && api.getCurrentThreadId;
}

auto LastErrorOr(
        const AtomicFileApi& api,
        DWORD fallback) noexcept -> DWORD {
    const auto error = api.getLastError();
    return error == ERROR_SUCCESS ? fallback : error;
}

auto IsMissing(DWORD error) noexcept -> bool {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

auto MakeSiblingPath(
        std::wstring_view finalPath,
        std::wstring_view marker,
        const AtomicFileApi& api) -> std::wstring {
    const auto sequence = SiblingSequence.fetch_add(
        1, std::memory_order_relaxed);
    std::wstring result{finalPath};
    result.append(marker);
    result.append(std::to_wstring(api.getCurrentProcessId()));
    result.push_back(L'.');
    result.append(std::to_wstring(api.getCurrentThreadId()));
    result.push_back(L'.');
    result.append(std::to_wstring(sequence));
    return result;
}

auto CleanupTemp(
        const AtomicFileApi& api,
        const std::wstring& tempPath) noexcept -> void {
    if (!tempPath.empty()) (void)api.deleteFileW(tempPath.c_str());
}

auto Failure(
        AtomicWriteStage stage,
        DWORD error,
        bool rollbackAttempted = false,
        bool rollbackSucceeded = false) noexcept -> AtomicWriteResult {
    return {
        .committed = false,
        .stage = stage,
        .windowsError = error,
        .rollbackAttempted = rollbackAttempted,
        .rollbackSucceeded = rollbackSucceeded,
    };
}

} // namespace

auto DefaultAtomicFileApi() noexcept -> AtomicFileApi {
    return {
        .createFileW = &::CreateFileW,
        .writeFile = &::WriteFile,
        .flushFileBuffers = &::FlushFileBuffers,
        .closeHandle = &::CloseHandle,
        .getFileAttributesW = &::GetFileAttributesW,
        .replaceFileW = &::ReplaceFileW,
        .moveFileExW = &::MoveFileExW,
        .deleteFileW = &::DeleteFileW,
        .getLastError = &::GetLastError,
        .getCurrentProcessId = &::GetCurrentProcessId,
        .getCurrentThreadId = &::GetCurrentThreadId,
    };
}

auto WriteFileAtomically(
        std::wstring_view finalPath,
        std::span<const std::uint8_t> bytes,
        const AtomicFileApi& api) noexcept -> AtomicWriteResult {
    if (!ApiIsComplete(api) || finalPath.empty()
            || finalPath.find(L'\0') != std::wstring_view::npos
            || (bytes.data() == nullptr && !bytes.empty())) {
        return Failure(AtomicWriteStage::InvalidArgument, ERROR_INVALID_PARAMETER);
    }

    std::wstring tempPath;
    HANDLE tempHandle = INVALID_HANDLE_VALUE;
    try {
        const std::wstring canonicalPath{finalPath};
        DWORD createError = ERROR_ALREADY_EXISTS;
        for (std::size_t attempt{}; attempt < MaximumNameAttempts; ++attempt) {
            tempPath = MakeSiblingPath(
                canonicalPath, L".isc12.tmp.", api);
            tempHandle = api.createFileW(
                tempPath.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (tempHandle != INVALID_HANDLE_VALUE) break;
            createError = LastErrorOr(api, ERROR_OPEN_FAILED);
            if (createError != ERROR_FILE_EXISTS
                    && createError != ERROR_ALREADY_EXISTS) {
                return Failure(AtomicWriteStage::TempCreate, createError);
            }
        }
        if (tempHandle == INVALID_HANDLE_VALUE) {
            return Failure(AtomicWriteStage::TempName, createError);
        }

        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto remaining = bytes.size() - offset;
            const auto chunk = static_cast<DWORD>(std::min<std::size_t>(
                remaining,
                static_cast<std::size_t>(
                    (std::numeric_limits<DWORD>::max)())));
            DWORD written{};
            if (!api.writeFile(
                    tempHandle,
                    bytes.data() + offset,
                    chunk,
                    &written,
                    nullptr)
                    || written == 0 || written > chunk) {
                const auto error = LastErrorOr(api, ERROR_WRITE_FAULT);
                (void)api.closeHandle(tempHandle);
                tempHandle = INVALID_HANDLE_VALUE;
                CleanupTemp(api, tempPath);
                return Failure(AtomicWriteStage::Write, error);
            }
            offset += written;
        }

        if (!api.flushFileBuffers(tempHandle)) {
            const auto error = LastErrorOr(api, ERROR_WRITE_FAULT);
            (void)api.closeHandle(tempHandle);
            tempHandle = INVALID_HANDLE_VALUE;
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::Flush, error);
        }
        if (!api.closeHandle(tempHandle)) {
            const auto error = LastErrorOr(api, ERROR_INVALID_HANDLE);
            tempHandle = INVALID_HANDLE_VALUE;
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::Close, error);
        }
        tempHandle = INVALID_HANDLE_VALUE;

        const auto targetAttributes = api.getFileAttributesW(
            canonicalPath.c_str());
        if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
            const auto inspectError = LastErrorOr(api, ERROR_FILE_NOT_FOUND);
            if (!IsMissing(inspectError)) {
                CleanupTemp(api, tempPath);
                return Failure(AtomicWriteStage::TargetInspect, inspectError);
            }
            if (api.moveFileExW(
                    tempPath.c_str(),
                    canonicalPath.c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
                return {
                    .committed = true,
                    .stage = AtomicWriteStage::Committed,
                };
            }
            const auto moveError = LastErrorOr(api, ERROR_WRITE_FAULT);
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::Replace, moveError);
        }
        if ((targetAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::TargetInspect, ERROR_ACCESS_DENIED);
        }

        std::wstring backupPath;
        DWORD backupError = ERROR_ALREADY_EXISTS;
        bool backupReady{};
        for (std::size_t attempt{}; attempt < MaximumNameAttempts; ++attempt) {
            backupPath = MakeSiblingPath(
                canonicalPath, L".isc12.bak.", api);
            const auto attributes = api.getFileAttributesW(backupPath.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES) {
                backupError = LastErrorOr(api, ERROR_FILE_NOT_FOUND);
                if (IsMissing(backupError)) {
                    backupReady = true;
                    break;
                }
                CleanupTemp(api, tempPath);
                return Failure(AtomicWriteStage::TargetInspect, backupError);
            }
        }
        if (!backupReady) {
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::TempName, backupError);
        }

        // REPLACEFILE_WRITE_THROUGH is documented as unsupported. Durability
        // comes from flushing the sibling temp before ReplaceFileW.
        if (api.replaceFileW(
                canonicalPath.c_str(),
                tempPath.c_str(),
                backupPath.c_str(),
                0,
                nullptr,
                nullptr)) {
            AtomicWriteResult result{
                .committed = true,
                .stage = AtomicWriteStage::Committed,
            };
            if (!api.deleteFileW(backupPath.c_str())) {
                const auto cleanupError = LastErrorOr(api, ERROR_ACCESS_DENIED);
                if (!IsMissing(cleanupError)) {
                    result.windowsError = cleanupError;
                    result.cleanupWarning = true;
                }
            }
            return result;
        }

        const auto replaceError = LastErrorOr(api, ERROR_WRITE_FAULT);
        if (IsMissing(replaceError)) {
            // The destination may have disappeared after inspection. A move
            // without REPLACE_EXISTING cannot overwrite a newly created file.
            if (api.moveFileExW(
                    tempPath.c_str(),
                    canonicalPath.c_str(),
                    MOVEFILE_WRITE_THROUGH)) {
                return {
                    .committed = true,
                    .stage = AtomicWriteStage::Committed,
                };
            }
            const auto moveError = LastErrorOr(api, replaceError);
            CleanupTemp(api, tempPath);
            return Failure(AtomicWriteStage::Replace, moveError);
        }

        if (replaceError == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
            const bool restored = api.moveFileExW(
                backupPath.c_str(),
                canonicalPath.c_str(),
                MOVEFILE_WRITE_THROUGH) != FALSE;
            if (restored) CleanupTemp(api, tempPath);
            // If rollback fails, preserve both sibling files for recovery.
            return Failure(
                AtomicWriteStage::Rollback,
                replaceError,
                true,
                restored);
        }

        CleanupTemp(api, tempPath);
        // On documented non-1177 failures the original final path is retained.
        // Do not delete an unexpected backup on failure: it may be the only
        // surviving copy after an undocumented filesystem edge case.
        return Failure(AtomicWriteStage::Replace, replaceError);
    } catch (...) {
        if (tempHandle != INVALID_HANDLE_VALUE) {
            (void)api.closeHandle(tempHandle);
        }
        CleanupTemp(api, tempPath);
        return Failure(AtomicWriteStage::Allocation, ERROR_NOT_ENOUGH_MEMORY);
    }
}

} // namespace ruffneckk::isc12
