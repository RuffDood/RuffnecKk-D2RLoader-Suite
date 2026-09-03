#include "isc12_persistence_policy.hpp"

#include "isc12_store_kind.hpp"

namespace ruffneckk::isc12 {

auto PrepareStoreRead(
        std::string_view storeName,
        std::uint32_t readStatus,
        std::uint64_t announcedLength,
        std::uint64_t actualLength,
        std::span<const std::uint8_t> physicalBytes) noexcept
        -> StorePreparationResult {
    const auto kind = ClassifyStoreName(storeName);
    if (kind == StoreKind::Other) {
        return {
            .disposition = StorePreparation::PassThrough,
            .storeKind = kind,
            .error = PersistenceError::None,
        };
    }
    if (readStatus != 0) {
        return {
            .disposition = StorePreparation::Rejected,
            .storeKind = kind,
            .error = PersistenceError::ReadFailure,
        };
    }
    if (announcedLength != actualLength
            || announcedLength != physicalBytes.size()) {
        return {
            .disposition = StorePreparation::Rejected,
            .storeKind = kind,
            .error = PersistenceError::ReadLength,
        };
    }
    if (!ValidateInnerStore(kind, physicalBytes)) {
        return {
            .disposition = StorePreparation::Rejected,
            .storeKind = kind,
            .error = PersistenceError::InnerStore,
        };
    }
    return {
        .disposition = StorePreparation::Prepared,
        .storeKind = kind,
        .error = PersistenceError::None,
    };
}

auto PrepareStoreWrite(
        std::string_view storeName,
        std::span<const std::uint8_t> physicalBytes) noexcept
        -> StorePreparationResult {
    const auto kind = ClassifyStoreName(storeName);
    if (kind == StoreKind::Other) {
        return {
            .disposition = StorePreparation::PassThrough,
            .storeKind = kind,
            .error = PersistenceError::None,
        };
    }
    if (!ValidateInnerStore(kind, physicalBytes)) {
        return {
            .disposition = StorePreparation::Rejected,
            .storeKind = kind,
            .error = PersistenceError::InnerStore,
        };
    }
    return {
        .disposition = StorePreparation::Prepared,
        .storeKind = kind,
        .error = PersistenceError::None,
    };
}

} // namespace ruffneckk::isc12
