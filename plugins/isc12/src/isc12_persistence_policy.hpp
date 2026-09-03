#pragma once

#include "isc12_envelope.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ruffneckk::isc12 {

enum class StorePreparation : std::uint8_t {
    PassThrough,
    Prepared,
    Rejected,
};

enum class PersistenceError : std::uint8_t {
    None,
    ReadFailure,
    ReadLength,
    InnerStore,
};

struct StorePreparationResult {
    StorePreparation disposition{StorePreparation::Rejected};
    StoreKind storeKind{StoreKind::Other};
    PersistenceError error{PersistenceError::InnerStore};
};

// Non-target manager objects remain byte-exact vanilla pass-through. A target
// is released only after a complete read and standard D2S/D2I container
// validation. The physical bytes remain untouched so D2RLoader can inspect the
// same standard container and maintain its environment sidecar.
auto PrepareStoreRead(
    std::string_view storeName,
    std::uint32_t readStatus,
    std::uint64_t announcedLength,
    std::uint64_t actualLength,
    std::span<const std::uint8_t> physicalBytes) noexcept
    -> StorePreparationResult;

// Valid target stores keep their standard physical representation and are
// delegated to the native D2RCore writer after validation.
auto PrepareStoreWrite(
    std::string_view storeName,
    std::span<const std::uint8_t> physicalBytes) noexcept
    -> StorePreparationResult;

} // namespace ruffneckk::isc12
