#pragma once

#include "isc12_contract.hpp"
#include "isc12_store_kind.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ruffneckk::isc12 {

using Sha256Digest = std::array<std::uint8_t, 32>;

inline constexpr std::array<std::uint8_t, 8> EnvelopeMagic{
    'I', 'S', 'C', '1', '2', '\r', '\n', 0x1A,
};
inline constexpr std::uint16_t EnvelopeVersion = 1;
inline constexpr std::uint16_t EnvelopeHeaderSize = 96;
inline constexpr std::uint32_t EnvelopeFlags = 0;
inline constexpr std::uint32_t SchemaDescriptorVersion = 1;
inline constexpr std::uint32_t InnerFormatVersion = 105;
inline constexpr std::size_t EnvelopeSchemaHashOffset = 32;
inline constexpr std::size_t EnvelopePayloadHashOffset = 64;

static_assert(EnvelopePayloadHashOffset + Sha256Digest{}.size()
    == EnvelopeHeaderSize);

enum class EnvelopeError : std::uint8_t {
    None,
    InvalidArgument,
    Allocation,
    HashFailure,
    TooShort,
    Magic,
    Version,
    HeaderSize,
    StoreKind,
    CodecBits,
    Sentinel,
    Flags,
    PayloadLength,
    SchemaDescriptorVersion,
    SchemaHash,
    PayloadHash,
    InnerPayload,
};

struct EnvelopeValidation {
    EnvelopeError error{EnvelopeError::InvalidArgument};
    StoreKind storeKind{StoreKind::Other};
    std::span<const std::uint8_t> payload{};

    explicit operator bool() const noexcept {
        return error == EnvelopeError::None;
    }
};

auto CalculateSha256(
    std::span<const std::uint8_t> bytes,
    Sha256Digest& output) noexcept -> bool;

auto CalculateD2SChecksum(
    std::span<const std::uint8_t> payload) noexcept -> std::uint32_t;

auto ValidateInnerStore(
    StoreKind kind,
    std::span<const std::uint8_t> payload) noexcept -> bool;

// On failure, output is left byte-exactly unchanged.
auto BuildEnvelope(
    StoreKind kind,
    std::span<const std::uint8_t> payload,
    const Sha256Digest& schemaHash,
    std::vector<std::uint8_t>& output) noexcept -> EnvelopeError;

auto ValidateEnvelope(
    StoreKind expectedKind,
    std::span<const std::uint8_t> bytes,
    const Sha256Digest& expectedSchemaHash) noexcept -> EnvelopeValidation;

} // namespace ruffneckk::isc12
