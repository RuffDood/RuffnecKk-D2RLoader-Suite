#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <span>
#include <string_view>

namespace ruffneckk::isc12 {

enum class AtomicWriteStage : std::uint8_t {
    None,
    InvalidArgument,
    Allocation,
    TempName,
    TempCreate,
    Write,
    Flush,
    Close,
    TargetInspect,
    Replace,
    Rollback,
    Committed,
};

struct AtomicWriteResult {
    bool committed{};
    AtomicWriteStage stage{AtomicWriteStage::None};
    DWORD windowsError{ERROR_SUCCESS};
    bool rollbackAttempted{};
    bool rollbackSucceeded{};
    bool cleanupWarning{};
};

// A false native persistence callback is allowed only when the canonical
// destination is proven unchanged (or restored). A failed rollback is an
// uncertain post-transition state and must therefore fail closed.
inline auto AtomicFailurePreservedDestination(
        const AtomicWriteResult& result) noexcept -> bool {
    return !result.committed
        && (!result.rollbackAttempted || result.rollbackSucceeded);
}

struct AtomicFileApi {
    decltype(&::CreateFileW) createFileW{};
    decltype(&::WriteFile) writeFile{};
    decltype(&::FlushFileBuffers) flushFileBuffers{};
    decltype(&::CloseHandle) closeHandle{};
    decltype(&::GetFileAttributesW) getFileAttributesW{};
    decltype(&::ReplaceFileW) replaceFileW{};
    decltype(&::MoveFileExW) moveFileExW{};
    decltype(&::DeleteFileW) deleteFileW{};
    decltype(&::GetLastError) getLastError{};
    decltype(&::GetCurrentProcessId) getCurrentProcessId{};
    decltype(&::GetCurrentThreadId) getCurrentThreadId{};
};

auto DefaultAtomicFileApi() noexcept -> AtomicFileApi;

// Writes to a unique sibling file, flushes and closes it, then performs a
// same-directory replacement. The original final path is never opened with
// CREATE_ALWAYS. A committed result remains success even if deleting the
// post-commit backup produces a cleanup warning.
auto WriteFileAtomically(
    std::wstring_view finalPath,
    std::span<const std::uint8_t> bytes,
    const AtomicFileApi& api) noexcept -> AtomicWriteResult;

inline auto WriteFileAtomically(
        std::wstring_view finalPath,
        std::span<const std::uint8_t> bytes) noexcept -> AtomicWriteResult {
    return WriteFileAtomically(finalPath, bytes, DefaultAtomicFileApi());
}

} // namespace ruffneckk::isc12
