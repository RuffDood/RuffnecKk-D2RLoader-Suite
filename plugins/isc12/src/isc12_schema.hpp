#pragma once

#include "isc12_envelope.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ruffneckk::isc12 {

inline constexpr char SchemaDomainTag[] =
    "ISC12.ItemStatCost.Descriptor.v1";
inline constexpr std::uint16_t CanonicalSchemaDescriptorVersion = 1;
inline constexpr std::uint16_t InvalidStatReference = 0xFFFF;
inline constexpr std::uint8_t DefaultEffectiveStuff = 6;
inline constexpr std::size_t CompiledItemStatRecordStride = 0x144;
// Player-stat values are produced/consumed as 32-bit integers and their
// parameters as 16-bit words. These semantic bounds also keep the fixed G2
// 0x4000-byte buffer far below capacity at its unchanged 512-row cap.
inline constexpr std::uint8_t MaximumSerializedCsvBits = 32;
inline constexpr std::uint8_t MaximumSerializedCsvParamBits = 16;

struct ItemStatSemanticRow {
    std::string statName;
    bool sendOther{};
    bool isSigned{};
    std::uint8_t sendBits{};
    std::uint8_t sendParamBits{};
    bool updateAnimRate{};
    bool saved{};
    bool csvSigned{};
    std::uint8_t csvBits{};
    std::uint8_t csvParamBits{};
    bool callback{};
    bool hasMinimum{};
    std::int32_t minimumAccumulator{};
    std::uint8_t encode{};
    std::int32_t add{};
    std::int32_t multiply{};
    std::uint8_t valueShift{};
    std::uint8_t saveBits{};
    std::int32_t saveAdd{};
    std::uint32_t saveParamBits{};
    bool keepZero{};
    std::uint8_t operation{};
    std::uint8_t operationParam{};
    std::uint16_t operationBase{InvalidStatReference};
    std::uint16_t operationStat1{InvalidStatReference};
    std::uint16_t operationStat2{InvalidStatReference};
    std::uint16_t operationStat3{InvalidStatReference};
    bool direct{};
    std::uint16_t maximumStat{InvalidStatReference};
    bool damageRelated{};
    std::uint16_t itemEvent1{InvalidStatReference};
    std::uint16_t itemEventFunction1{};
    std::uint16_t itemEvent2{InvalidStatReference};
    std::uint16_t itemEventFunction2{};
};

enum class SchemaError : std::uint8_t {
    None,
    InvalidArgument,
    TooManyRows,
    SizeMismatch,
    InvalidUtf8,
    RecordIdMismatch,
    UnsafeCodecWidth,
    Allocation,
    HashFailure,
};

auto NormalizeEffectiveStuff(std::uint32_t value) noexcept -> std::uint8_t;

// The descriptor is binary and little-endian. Row identity is the physical
// ordinal plus the exact UTF-8 Stat token; comment, localization and display
// fields are never admitted. The fixed domain tag includes its terminal NUL.
// On failure, output is left byte-exactly unchanged.
auto BuildSchemaDescriptor(
    std::span<const ItemStatSemanticRow> rows,
    std::uint32_t effectiveStuff,
    std::vector<std::uint8_t>& output) noexcept -> SchemaError;

auto CalculateSchemaHash(
    std::span<const ItemStatSemanticRow> rows,
    std::uint32_t effectiveStuff,
    Sha256Digest& output) noexcept -> SchemaError;

// Converts the governed D2R 3.2/3.3 compiled 0x144-byte records into the
// semantic model. Derived op caches and every display-only field are skipped.
// On failure, rows and effectiveStuff are left unchanged.
auto DecodeCompiledItemStatRecords(
    std::span<const std::uint8_t> recordBytes,
    std::size_t rowCount,
    std::span<const std::string_view> statNames,
    std::vector<ItemStatSemanticRow>& rows,
    std::uint8_t& effectiveStuff) noexcept -> SchemaError;

} // namespace ruffneckk::isc12
