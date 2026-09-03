#include "isc12_envelope.hpp"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace ruffneckk::isc12 {
namespace {

constexpr std::size_t D2SHeaderSize = 16;
constexpr std::size_t D2ISectorHeaderSize = 64;
constexpr std::uint32_t InnerMagic = 0xAA55AA55;

auto ReadU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        -> std::uint16_t {
    return static_cast<std::uint16_t>(bytes[offset])
        | static_cast<std::uint16_t>(bytes[offset + 1]) << 8U;
}

auto ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[offset])
        | static_cast<std::uint32_t>(bytes[offset + 1]) << 8U
        | static_cast<std::uint32_t>(bytes[offset + 2]) << 16U
        | static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
}

auto ReadU64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
        -> std::uint64_t {
    std::uint64_t value{};
    for (std::size_t index{}; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index])
            << (index * 8U);
    }
    return value;
}

auto WriteU16(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint16_t value) noexcept -> void {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8U);
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

auto WriteU64(
        std::span<std::uint8_t> bytes,
        std::size_t offset,
        std::uint64_t value) noexcept -> void {
    for (std::size_t index{}; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            value >> (index * 8U));
    }
}

auto ShaMatches(
        const Sha256Digest& expected,
        std::span<const std::uint8_t> actual) noexcept -> bool {
    std::uint8_t difference{};
    for (std::size_t index{}; index < expected.size(); ++index) {
        difference |= static_cast<std::uint8_t>(
            expected[index] ^ actual[index]);
    }
    return difference == 0;
}

auto IsSupportedKind(StoreKind kind) noexcept -> bool {
    return kind == StoreKind::D2S || kind == StoreKind::D2I;
}

} // namespace

auto CalculateSha256(
        std::span<const std::uint8_t> bytes,
        Sha256Digest& output) noexcept -> bool {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    try {
        if (BCryptOpenAlgorithmProvider(
                &algorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                0) < 0) {
            return false;
        }
        DWORD objectSize{};
        DWORD hashSize{};
        DWORD copied{};
        if (BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectSize),
                sizeof(objectSize),
                &copied,
                0) < 0
                || copied != sizeof(objectSize)
                || BCryptGetProperty(
                    algorithm,
                    BCRYPT_HASH_LENGTH,
                    reinterpret_cast<PUCHAR>(&hashSize),
                    sizeof(hashSize),
                    &copied,
                    0) < 0
                || copied != sizeof(hashSize)
                || hashSize != output.size()) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }
        std::vector<std::uint8_t> object(objectSize);
        if (BCryptCreateHash(
                algorithm,
                &hash,
                object.empty() ? nullptr : object.data(),
                objectSize,
                nullptr,
                0,
                0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }
        std::size_t offset{};
        while (offset < bytes.size()) {
            const auto count = static_cast<ULONG>(std::min<std::size_t>(
                bytes.size() - offset,
                static_cast<std::size_t>(
                    (std::numeric_limits<ULONG>::max)())));
            if (BCryptHashData(
                    hash,
                    const_cast<PUCHAR>(bytes.data() + offset),
                    count,
                    0) < 0) {
                BCryptDestroyHash(hash);
                BCryptCloseAlgorithmProvider(algorithm, 0);
                return false;
            }
            offset += count;
        }
        const bool finished = BCryptFinishHash(
            hash,
            output.data(),
            static_cast<ULONG>(output.size()),
            0) >= 0;
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return finished;
    } catch (...) {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }
}

auto CalculateD2SChecksum(
        std::span<const std::uint8_t> payload) noexcept -> std::uint32_t {
    std::uint32_t checksum{};
    for (std::size_t index{}; index < payload.size(); ++index) {
        const auto value = index >= 12 && index < 16 ? 0U : payload[index];
        checksum = checksum * 2U + (checksum >> 31U) + value;
    }
    return checksum;
}

auto ValidateInnerStore(
        StoreKind kind,
        std::span<const std::uint8_t> payload) noexcept -> bool {
    if (kind == StoreKind::D2S) {
        return payload.size() >= D2SHeaderSize
            && payload.size() <= (std::numeric_limits<std::uint32_t>::max)()
            && ReadU32(payload, 0) == InnerMagic
            && ReadU32(payload, 4) == InnerFormatVersion
            && ReadU32(payload, 8) == payload.size()
            && ReadU32(payload, 12) == CalculateD2SChecksum(payload);
    }
    if (kind != StoreKind::D2I || payload.empty()) return false;

    std::size_t offset{};
    while (offset < payload.size()) {
        const auto remaining = payload.size() - offset;
        if (remaining < D2ISectorHeaderSize) return false;
        const auto sector = payload.subspan(offset, remaining);
        const auto sectorSize = ReadU16(sector, 0x10);
        if (ReadU32(sector, 0) != InnerMagic
                || ReadU32(sector, 4) != 2
                || ReadU32(sector, 8) != InnerFormatVersion
                || sectorSize < D2ISectorHeaderSize
                || sectorSize > remaining
                || sector[0x14] > 2) {
            return false;
        }
        offset += sectorSize;
    }
    return offset == payload.size();
}

auto BuildEnvelope(
        StoreKind kind,
        std::span<const std::uint8_t> payload,
        const Sha256Digest& schemaHash,
        std::vector<std::uint8_t>& output) noexcept -> EnvelopeError {
    if (!IsSupportedKind(kind)) return EnvelopeError::StoreKind;
    if (!ValidateInnerStore(kind, payload)) return EnvelopeError::InnerPayload;
    if (payload.size() > (std::numeric_limits<std::size_t>::max)()
            - EnvelopeHeaderSize) {
        return EnvelopeError::PayloadLength;
    }

    Sha256Digest payloadHash{};
    if (!CalculateSha256(payload, payloadHash)) return EnvelopeError::HashFailure;
    try {
        std::vector<std::uint8_t> staged(
            EnvelopeHeaderSize + payload.size());
        std::span header{staged.data(), static_cast<std::size_t>(
            EnvelopeHeaderSize)};
        std::copy(EnvelopeMagic.begin(), EnvelopeMagic.end(), header.begin());
        WriteU16(header, 8, EnvelopeVersion);
        WriteU16(header, 10, EnvelopeHeaderSize);
        header[12] = static_cast<std::uint8_t>(kind);
        header[13] = static_cast<std::uint8_t>(SerializedBitWidth);
        WriteU16(header, 14, SerializedSentinel);
        WriteU32(header, 16, EnvelopeFlags);
        WriteU64(header, 20, payload.size());
        WriteU32(header, 28, SchemaDescriptorVersion);
        std::copy(
            schemaHash.begin(), schemaHash.end(),
            header.begin() + EnvelopeSchemaHashOffset);
        std::copy(
            payloadHash.begin(), payloadHash.end(),
            header.begin() + EnvelopePayloadHashOffset);
        std::copy(payload.begin(), payload.end(),
            staged.begin() + EnvelopeHeaderSize);
        output.swap(staged);
        return EnvelopeError::None;
    } catch (...) {
        return EnvelopeError::Allocation;
    }
}

auto ValidateEnvelope(
        StoreKind expectedKind,
        std::span<const std::uint8_t> bytes,
        const Sha256Digest& expectedSchemaHash) noexcept -> EnvelopeValidation {
    if (!IsSupportedKind(expectedKind)) {
        return {.error = EnvelopeError::InvalidArgument};
    }
    if (bytes.size() < EnvelopeHeaderSize) {
        return {.error = EnvelopeError::TooShort};
    }
    if (!std::equal(EnvelopeMagic.begin(), EnvelopeMagic.end(), bytes.begin())) {
        return {.error = EnvelopeError::Magic};
    }
    if (ReadU16(bytes, 8) != EnvelopeVersion) {
        return {.error = EnvelopeError::Version};
    }
    if (ReadU16(bytes, 10) != EnvelopeHeaderSize) {
        return {.error = EnvelopeError::HeaderSize};
    }
    const auto actualKind = static_cast<StoreKind>(bytes[12]);
    if (!IsSupportedKind(actualKind) || actualKind != expectedKind) {
        return {.error = EnvelopeError::StoreKind};
    }
    if (bytes[13] != SerializedBitWidth) {
        return {.error = EnvelopeError::CodecBits};
    }
    if (ReadU16(bytes, 14) != SerializedSentinel) {
        return {.error = EnvelopeError::Sentinel};
    }
    if (ReadU32(bytes, 16) != EnvelopeFlags) {
        return {.error = EnvelopeError::Flags};
    }
    const auto payloadLength = ReadU64(bytes, 20);
    if (payloadLength != bytes.size() - EnvelopeHeaderSize) {
        return {.error = EnvelopeError::PayloadLength};
    }
    if (ReadU32(bytes, 28) != SchemaDescriptorVersion) {
        return {.error = EnvelopeError::SchemaDescriptorVersion};
    }
    if (!ShaMatches(
            expectedSchemaHash,
            bytes.subspan(EnvelopeSchemaHashOffset, expectedSchemaHash.size()))) {
        return {.error = EnvelopeError::SchemaHash};
    }
    const auto payload = bytes.subspan(EnvelopeHeaderSize);
    Sha256Digest payloadHash{};
    if (!CalculateSha256(payload, payloadHash)) {
        return {.error = EnvelopeError::HashFailure};
    }
    if (!ShaMatches(
            payloadHash,
            bytes.subspan(EnvelopePayloadHashOffset, payloadHash.size()))) {
        return {.error = EnvelopeError::PayloadHash};
    }
    if (!ValidateInnerStore(actualKind, payload)) {
        return {.error = EnvelopeError::InnerPayload};
    }
    return {
        .error = EnvelopeError::None,
        .storeKind = actualKind,
        .payload = payload,
    };
}

} // namespace ruffneckk::isc12
