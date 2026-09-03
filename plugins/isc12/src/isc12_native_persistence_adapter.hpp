#pragma once

#include "isc12_persistence_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ruffneckk::isc12 {

inline constexpr std::size_t NativePersistencePathCapacity = 0x30C;
inline constexpr std::uint64_t MaximumNativePhysicalStoreLength =
    (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::uint64_t MaximumNativeInnerStoreLength =
    MaximumNativePhysicalStoreLength;

enum class NativePersistenceDisposition : std::uint8_t {
    ProceedNative,
    Success,
    Reject,
    Fatal,
};

enum class NativePersistenceError : std::uint8_t {
    None,
    CodecNotReady,
    SchemaUnavailable,
    StorePreparation,
    PathBuffer,
    PathTraversal,
    PathMismatch,
    PathConversion,
    ClearRejectedRead,
    PhysicalLength,
};

struct NativePersistenceResult {
    NativePersistenceDisposition disposition{
        NativePersistenceDisposition::Reject};
    StoreKind storeKind{StoreKind::Other};
    NativePersistenceError error{NativePersistenceError::None};
    PersistenceError persistenceError{PersistenceError::None};
};

using ClearRejectedNativeReadCallback = bool (*)(void* context) noexcept;

struct NativeReadCallbacks {
    void* context{};
    ClearRejectedNativeReadCallback clearRejectedRead{};
};

struct NativeReadRequest {
    std::string_view storeName;
    bool codecReady{};
    std::uint32_t readStatus{};
    std::uint64_t announcedLength{};
    std::uint64_t actualLength{};
    std::span<const std::uint8_t> physicalBytes;
};

inline auto NativeReadMaySnapshot(
        std::uint32_t nativeStatus,
        std::uint32_t nativeSuccessStatus,
        std::uint64_t announcedLength,
        std::uint32_t actualLength) noexcept -> bool {
    return nativeStatus == nativeSuccessStatus
        && announcedLength == actualLength;
}

inline auto NativeWriteLengthSupported(
        std::uint64_t innerLength) noexcept -> bool {
    return innerLength <= MaximumNativeInnerStoreLength;
}

struct NativeWriteRequest {
    std::string_view storeName;
    // The span models the complete bounded native char buffer. A terminal NUL
    // must occur within NativePersistencePathCapacity bytes. Bytes after that
    // first NUL are ignored exactly as MultiByteToWideChar(..., -1) ignores
    // them.
    std::span<const char> nativePathUtf8;
    bool codecReady{};
    bool schemaReady{};
    std::span<const std::uint8_t> physicalBytes;
};

// Non-target manager objects proceed natively before any callback is inspected.
// Target reads are deliberately schema-independent because D2RLoader enumerates
// character D2S files before DataTablesLoaded publishes the authoritative
// ItemStatCost snapshot. Target rejection always clears the native read state.
// Successful reads retain their byte-exact standard container in the buffer.
auto AdaptNativeStoreRead(
    const NativeReadRequest& request,
    const NativeReadCallbacks& callbacks) noexcept -> NativePersistenceResult;

// Successful target writes return ProceedNative so D2RCore owns the actual
// file write, close, backup integration and .d2rl environment sidecar. Every
// validation failure returns Reject before CREATE_ALWAYS runs.
auto AdaptNativeStoreWrite(const NativeWriteRequest& request) noexcept
    -> NativePersistenceResult;

} // namespace ruffneckk::isc12
