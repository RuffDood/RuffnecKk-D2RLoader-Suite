#include "isc12_native_persistence_adapter.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ruffneckk::isc12 {
namespace {

auto Result(
        NativePersistenceDisposition disposition,
        StoreKind storeKind,
        NativePersistenceError error = NativePersistenceError::None,
        PersistenceError persistenceError = PersistenceError::None) noexcept
        -> NativePersistenceResult {
    return {
        .disposition = disposition,
        .storeKind = storeKind,
        .error = error,
        .persistenceError = persistenceError,
    };
}

auto RejectRead(
        StoreKind storeKind,
        NativePersistenceError error,
        PersistenceError persistenceError,
        const NativeReadCallbacks& callbacks) noexcept
        -> NativePersistenceResult {
    if (!callbacks.clearRejectedRead
            || !callbacks.clearRejectedRead(callbacks.context)) {
        return Result(
            NativePersistenceDisposition::Fatal,
            storeKind,
            NativePersistenceError::ClearRejectedRead,
            persistenceError);
    }
    return Result(
        NativePersistenceDisposition::Reject,
        storeKind,
        error,
        persistenceError);
}

auto HasTraversalComponent(std::string_view path) noexcept -> bool {
    std::size_t componentStart{};
    bool firstComponent = true;
    while (componentStart <= path.size()) {
        const auto separator = path.find_first_of("\\/", componentStart);
        const auto componentEnd = separator == std::string_view::npos
            ? path.size()
            : separator;
        auto component = path.substr(
            componentStart, componentEnd - componentStart);
        if (firstComponent && component.size() >= 2 && component[1] == ':') {
            component.remove_prefix(2);
        }
        if (component == "." || component == "..") return true;
        if (separator == std::string_view::npos) break;
        componentStart = separator + 1;
        firstComponent = false;
    }
    return false;
}

enum class PathConversion : std::uint8_t {
    Success,
    Invalid,
    Allocation,
};

auto ConvertNativePath(
        std::span<const char> nativeBuffer,
        std::string_view storeName,
        std::wstring& widePath,
        NativePersistenceError& error) noexcept -> PathConversion {
    if (nativeBuffer.empty()
            || nativeBuffer.size() > NativePersistencePathCapacity) {
        error = NativePersistenceError::PathBuffer;
        return PathConversion::Invalid;
    }
    const auto terminator = std::find(
        nativeBuffer.begin(), nativeBuffer.end(), '\0');
    if (terminator == nativeBuffer.end() || terminator == nativeBuffer.begin()) {
        error = NativePersistenceError::PathBuffer;
        return PathConversion::Invalid;
    }
    const auto pathLength = static_cast<std::size_t>(
        terminator - nativeBuffer.begin());
    const std::string_view utf8Path{nativeBuffer.data(), pathLength};
    if (HasTraversalComponent(utf8Path)) {
        error = NativePersistenceError::PathTraversal;
        return PathConversion::Invalid;
    }
    const auto separator = utf8Path.find_last_of("\\/");
    const auto terminalName = separator == std::string_view::npos
        ? utf8Path
        : utf8Path.substr(separator + 1);
    if (terminalName != storeName) {
        error = NativePersistenceError::PathMismatch;
        return PathConversion::Invalid;
    }

    const auto required = ::MultiByteToWideChar(
        CP_UTF8, 0, nativeBuffer.data(), -1, nullptr, 0);
    if (required <= 1) {
        error = NativePersistenceError::PathConversion;
        return PathConversion::Invalid;
    }
    try {
        std::wstring staged(static_cast<std::size_t>(required), L'\0');
        const auto converted = ::MultiByteToWideChar(
            CP_UTF8,
            0,
            nativeBuffer.data(),
            -1,
            staged.data(),
            required);
        if (converted != required || staged.back() != L'\0') {
            error = NativePersistenceError::PathConversion;
            return PathConversion::Invalid;
        }
        staged.pop_back();
        widePath.swap(staged);
        return PathConversion::Success;
    } catch (...) {
        error = NativePersistenceError::PathConversion;
        return PathConversion::Allocation;
    }
}

} // namespace

auto AdaptNativeStoreRead(
        const NativeReadRequest& request,
        const NativeReadCallbacks& callbacks) noexcept
        -> NativePersistenceResult {
    const auto storeKind = ClassifyStoreName(request.storeName);
    if (storeKind == StoreKind::Other) {
        return Result(NativePersistenceDisposition::ProceedNative, storeKind);
    }
    if (!request.codecReady) {
        return RejectRead(
            storeKind,
            NativePersistenceError::CodecNotReady,
            PersistenceError::None,
            callbacks);
    }
    const auto preparation = PrepareStoreRead(
        request.storeName,
        request.readStatus,
        request.announcedLength,
        request.actualLength,
        request.physicalBytes);
    if (preparation.disposition != StorePreparation::Prepared) {
        return RejectRead(
            storeKind,
            NativePersistenceError::StorePreparation,
            preparation.error,
            callbacks);
    }
    return Result(NativePersistenceDisposition::Success, storeKind);
}

auto AdaptNativeStoreWrite(const NativeWriteRequest& request) noexcept
        -> NativePersistenceResult {
    const auto storeKind = ClassifyStoreName(request.storeName);
    if (storeKind == StoreKind::Other) {
        return Result(NativePersistenceDisposition::ProceedNative, storeKind);
    }
    if (!request.codecReady) {
        return Result(
            NativePersistenceDisposition::Reject,
            storeKind,
            NativePersistenceError::CodecNotReady,
            PersistenceError::None);
    }
    if (!request.schemaReady) {
        return Result(
            NativePersistenceDisposition::Reject,
            storeKind,
            NativePersistenceError::SchemaUnavailable,
            PersistenceError::None);
    }
    if (!NativeWriteLengthSupported(request.physicalBytes.size())) {
        return Result(
            NativePersistenceDisposition::Reject,
            storeKind,
            NativePersistenceError::PhysicalLength,
            PersistenceError::None);
    }

    std::wstring widePath;
    NativePersistenceError pathError{NativePersistenceError::None};
    if (ConvertNativePath(
            request.nativePathUtf8,
            request.storeName,
            widePath,
            pathError) != PathConversion::Success) {
        return Result(
            NativePersistenceDisposition::Reject,
            storeKind,
            pathError,
            PersistenceError::None);
    }

    const auto preparation = PrepareStoreWrite(
        request.storeName,
        request.physicalBytes);
    if (preparation.disposition != StorePreparation::Prepared) {
        return Result(
            NativePersistenceDisposition::Reject,
            storeKind,
            NativePersistenceError::StorePreparation,
            preparation.error);
    }
    return Result(NativePersistenceDisposition::ProceedNative, storeKind);
}

} // namespace ruffneckk::isc12
