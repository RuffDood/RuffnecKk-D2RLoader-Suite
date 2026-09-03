#include "isc12_native_persistence_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

int Failures{};

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::cerr << "CHECK failed at line " << __LINE__ \
                      << ": " #expression "\n"; \
            ++Failures; \
        } \
    } while (false)

struct CallbackState {
    std::size_t clearCalls{};
    bool clearSucceeds{true};
};

auto ClearRejectedRead(void* context) noexcept -> bool {
    auto& state = *static_cast<CallbackState*>(context);
    ++state.clearCalls;
    return state.clearSucceeds;
}

auto ReadCallbacks(CallbackState& state)
        -> ruffneckk::isc12::NativeReadCallbacks {
    return {
        .context = &state,
        .clearRejectedRead = &ClearRejectedRead,
    };
}

auto WriteU32(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint32_t value) noexcept -> void {
    for (std::size_t index{}; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> (index * 8U));
    }
}

auto NativePath(const std::string& path) noexcept -> std::span<const char> {
    return {path.c_str(), path.size() + 1};
}

} // namespace

int main() {
    using namespace ruffneckk::isc12;

    CHECK(NativeReadMaySnapshot(0, 0, 42, 42));
    CHECK(NativeReadMaySnapshot(0x1234, 0x1234, 42, 42));
    CHECK(!NativeReadMaySnapshot(5, 0, 42, 42));
    CHECK(!NativeReadMaySnapshot(0, 0, 42, 41));
    CHECK(!NativeReadMaySnapshot(
        0, 0, UINT64_C(0x100000000), UINT32_MAX));
    CHECK(NativeWriteLengthSupported(MaximumNativeInnerStoreLength));
    CHECK(!NativeWriteLengthSupported(
        MaximumNativeInnerStoreLength + 1));

    static_assert(NativePersistencePathCapacity == 0x30C);
    static_assert(MaximumNativeInnerStoreLength
        == MaximumNativePhysicalStoreLength);
    static_assert(noexcept(AdaptNativeStoreRead(
        std::declval<const NativeReadRequest&>(),
        std::declval<const NativeReadCallbacks&>())));
    static_assert(noexcept(AdaptNativeStoreWrite(
        std::declval<const NativeWriteRequest&>())));

    std::array<std::uint8_t, 16> standardD2S{};
    WriteU32(standardD2S, 0, 0xAA55AA55);
    WriteU32(standardD2S, 4, InnerFormatVersion);
    WriteU32(
        standardD2S, 8, static_cast<std::uint32_t>(standardD2S.size()));
    WriteU32(standardD2S, 12, CalculateD2SChecksum(standardD2S));
    CHECK(ValidateInnerStore(StoreKind::D2S, standardD2S));

    Sha256Digest schemaHash{};
    schemaHash.fill(0x5A);
    std::vector<std::uint8_t> retiredEnvelope;
    CHECK(BuildEnvelope(
        StoreKind::D2S, standardD2S, schemaHash, retiredEnvelope)
        == EnvelopeError::None);

    {
        const NativeReadRequest request{
            .storeName = "opaque.manager.object",
            .codecReady = false,
        };
        const auto result = AdaptNativeStoreRead(request, {});
        CHECK(result.disposition
            == NativePersistenceDisposition::ProceedNative);
        CHECK(result.storeKind == StoreKind::Other);

        const NativeWriteRequest writeRequest{
            .storeName = "opaque.manager.object",
            .codecReady = false,
            .schemaReady = false,
        };
        const auto writeResult = AdaptNativeStoreWrite(writeRequest);
        CHECK(writeResult.disposition
            == NativePersistenceDisposition::ProceedNative);
        CHECK(writeResult.storeKind == StoreKind::Other);
    }

    {
        CallbackState state;
        NativeReadRequest request{
            .storeName = "Hero.d2s",
            .codecReady = false,
        };
        auto result = AdaptNativeStoreRead(request, ReadCallbacks(state));
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::CodecNotReady);
        CHECK(state.clearCalls == 1);

        CallbackState failedCleanupState;
        failedCleanupState.clearSucceeds = false;
        request.codecReady = false;
        result = AdaptNativeStoreRead(
            request, ReadCallbacks(failedCleanupState));
        CHECK(result.disposition == NativePersistenceDisposition::Fatal);
        CHECK(result.error == NativePersistenceError::ClearRejectedRead);
        CHECK(failedCleanupState.clearCalls == 1);

        result = AdaptNativeStoreRead(request, {});
        CHECK(result.disposition == NativePersistenceDisposition::Fatal);
        CHECK(result.error == NativePersistenceError::ClearRejectedRead);
    }

    {
        CallbackState state;
        NativeReadRequest request{
            .storeName = "Hero.d2s",
            .codecReady = true,
            .readStatus = 0,
            .announcedLength = standardD2S.size(),
            .actualLength = standardD2S.size() - 1,
            .physicalBytes = standardD2S,
        };
        auto result = AdaptNativeStoreRead(request, ReadCallbacks(state));
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::StorePreparation);
        CHECK(result.persistenceError == PersistenceError::ReadLength);
        CHECK(state.clearCalls == 1);

        auto invalidD2S = standardD2S;
        invalidD2S[0] ^= 0xFF;
        CallbackState invalidState;
        request.actualLength = invalidD2S.size();
        request.announcedLength = invalidD2S.size();
        request.physicalBytes = invalidD2S;
        result = AdaptNativeStoreRead(
            request, ReadCallbacks(invalidState));
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.persistenceError == PersistenceError::InnerStore);
        CHECK(invalidState.clearCalls == 1);

        CallbackState envelopeState;
        request.actualLength = retiredEnvelope.size();
        request.announcedLength = retiredEnvelope.size();
        request.physicalBytes = retiredEnvelope;
        result = AdaptNativeStoreRead(
            request, ReadCallbacks(envelopeState));
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.persistenceError == PersistenceError::InnerStore);
        CHECK(envelopeState.clearCalls == 1);

        CallbackState validState;
        request.actualLength = standardD2S.size();
        request.announcedLength = standardD2S.size();
        request.physicalBytes = standardD2S;
        result = AdaptNativeStoreRead(
            request, ReadCallbacks(validState));
        CHECK(result.disposition == NativePersistenceDisposition::Success);
        CHECK(result.storeKind == StoreKind::D2S);
        CHECK(validState.clearCalls == 0);
    }

    {
        // Character enumeration happens before DataTablesLoaded. A valid
        // standard D2S must therefore pass without a published schema.
        CallbackState earlyFrontendState;
        const NativeReadRequest request{
            .storeName = "EarlyFrontendHero.d2s",
            .codecReady = true,
            .readStatus = 0,
            .announcedLength = standardD2S.size(),
            .actualLength = standardD2S.size(),
            .physicalBytes = standardD2S,
        };
        const auto result = AdaptNativeStoreRead(
            request, ReadCallbacks(earlyFrontendState));
        CHECK(result.disposition == NativePersistenceDisposition::Success);
        CHECK(result.storeKind == StoreKind::D2S);
        CHECK(earlyFrontendState.clearCalls == 0);
    }

    const std::string asciiPath{"C:\\save\\Hero.d2s"};
    {
        NativeWriteRequest request{
            .storeName = "Hero.d2s",
            .nativePathUtf8 = NativePath(asciiPath),
            .codecReady = false,
            .schemaReady = true,
            .physicalBytes = standardD2S,
        };
        auto result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::CodecNotReady);

        request.codecReady = true;
        request.schemaReady = false;
        result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::SchemaUnavailable);
    }

    {
        const std::string traversalPath{"C:\\save\\..\\Hero.d2s"};
        NativeWriteRequest request{
            .storeName = "Hero.d2s",
            .nativePathUtf8 = NativePath(traversalPath),
            .codecReady = true,
            .schemaReady = true,
            .physicalBytes = standardD2S,
        };
        auto result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::PathTraversal);

        const std::string mismatchPath{"C:\\save\\Other.d2s"};
        request.nativePathUtf8 = NativePath(mismatchPath);
        result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::PathMismatch);

        const std::array<char, 8> unterminatedPath{
            'H','e','r','o','.','d','2','s'};
        request.nativePathUtf8 = unterminatedPath;
        result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::PathBuffer);
    }

    {
        const std::string utf8StoreName{"H\xC3\xA9ros.d2s"};
        const std::string utf8Path{
            "C:\\sauvegardes\\Qu\xC3\xA9" "bec\\H\xC3\xA9ros.d2s"};
        NativeWriteRequest request{
            .storeName = utf8StoreName,
            .nativePathUtf8 = NativePath(utf8Path),
            .codecReady = true,
            .schemaReady = true,
            .physicalBytes = standardD2S,
        };
        auto result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition
            == NativePersistenceDisposition::ProceedNative);
        CHECK(result.storeKind == StoreKind::D2S);

        request.physicalBytes = retiredEnvelope;
        result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.error == NativePersistenceError::StorePreparation);
        CHECK(result.persistenceError == PersistenceError::InnerStore);

        const std::array<std::uint8_t, 2> invalidStore{1, 2};
        request.physicalBytes = invalidStore;
        result = AdaptNativeStoreWrite(request);
        CHECK(result.disposition == NativePersistenceDisposition::Reject);
        CHECK(result.persistenceError == PersistenceError::InnerStore);
    }

    if (Failures != 0) {
        std::cerr << Failures
                  << " native persistence adapter test(s) failed\n";
        return 1;
    }
    return 0;
}
