#include "isc12_schema.hpp"

#include "isc12_contract.hpp"

#include <algorithm>
#include <limits>

namespace ruffneckk::isc12 {
namespace {

static_assert(SchemaDescriptorVersion
    == CanonicalSchemaDescriptorVersion);

constexpr std::uint32_t FlagSendOther = 1U << 0U;
constexpr std::uint32_t FlagSigned = 1U << 1U;
constexpr std::uint32_t FlagDamageRelated = 1U << 2U;
constexpr std::uint32_t FlagDirect = 1U << 3U;
constexpr std::uint32_t FlagUpdateAnimRate = 1U << 8U;
constexpr std::uint32_t FlagMinimum = 1U << 9U;
constexpr std::uint32_t FlagCallback = 1U << 10U;
constexpr std::uint32_t FlagSaved = 1U << 11U;
constexpr std::uint32_t FlagCsvSigned = 1U << 12U;

constexpr std::uint16_t SemanticSendOther = 1U << 0U;
constexpr std::uint16_t SemanticSigned = 1U << 1U;
constexpr std::uint16_t SemanticUpdateAnimRate = 1U << 2U;
constexpr std::uint16_t SemanticSaved = 1U << 3U;
constexpr std::uint16_t SemanticCsvSigned = 1U << 4U;
constexpr std::uint16_t SemanticCallback = 1U << 5U;
constexpr std::uint16_t SemanticMinimum = 1U << 6U;
constexpr std::uint16_t SemanticDirect = 1U << 7U;
constexpr std::uint16_t SemanticDamageRelated = 1U << 8U;

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

auto AppendU8(std::vector<std::uint8_t>& bytes, std::uint8_t value) -> void {
    bytes.push_back(value);
}

auto AppendBool(std::vector<std::uint8_t>& bytes, bool value) -> void {
    AppendU8(bytes, value ? 1U : 0U);
}

auto AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) -> void {
    AppendU8(bytes, static_cast<std::uint8_t>(value));
    AppendU8(bytes, static_cast<std::uint8_t>(value >> 8U));
}

auto AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) -> void {
    for (std::size_t index{}; index < sizeof(value); ++index) {
        AppendU8(bytes, static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

auto AppendI32(std::vector<std::uint8_t>& bytes, std::int32_t value) -> void {
    AppendU32(bytes, static_cast<std::uint32_t>(value));
}

auto NormalizeStatReference(
        std::uint16_t value,
        std::size_t rowCount) noexcept -> std::uint16_t {
    return value < rowCount ? value : InvalidStatReference;
}

auto IsValidUtf8(std::string_view value) noexcept -> bool {
    if (value.empty() || value.size() > 0xFFFF
            || value.find('\0') != std::string_view::npos) {
        return false;
    }
    std::size_t index{};
    while (index < value.size()) {
        const auto first = static_cast<std::uint8_t>(value[index]);
        if (first <= 0x7F) {
            ++index;
            continue;
        }
        std::size_t continuationCount{};
        std::uint32_t codePoint{};
        std::uint32_t minimum{};
        if (first >= 0xC2 && first <= 0xDF) {
            continuationCount = 1;
            codePoint = first & 0x1FU;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            continuationCount = 2;
            codePoint = first & 0x0FU;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            continuationCount = 3;
            codePoint = first & 0x07U;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (continuationCount > value.size() - index - 1) return false;
        for (std::size_t continuation{};
                continuation < continuationCount;
                ++continuation) {
            const auto next = static_cast<std::uint8_t>(
                value[index + continuation + 1]);
            if ((next & 0xC0U) != 0x80U) return false;
            codePoint = (codePoint << 6U) | (next & 0x3FU);
        }
        if (codePoint < minimum || codePoint > 0x10FFFF
                || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
            return false;
        }
        index += continuationCount + 1;
    }
    return true;
}

auto SemanticFlags(const ItemStatSemanticRow& row) noexcept -> std::uint16_t {
    return (row.sendOther ? SemanticSendOther : 0U)
        | (row.isSigned ? SemanticSigned : 0U)
        | (row.updateAnimRate ? SemanticUpdateAnimRate : 0U)
        | (row.saved ? SemanticSaved : 0U)
        | (row.csvSigned ? SemanticCsvSigned : 0U)
        | (row.callback ? SemanticCallback : 0U)
        | (row.hasMinimum ? SemanticMinimum : 0U)
        | (row.direct ? SemanticDirect : 0U)
        | (row.damageRelated ? SemanticDamageRelated : 0U);
}

auto DecodeRow(
        std::span<const std::uint8_t> record,
        std::string_view statName) -> ItemStatSemanticRow {
    const auto flags = ReadU32(record, 0x04);
    return {
        .statName = std::string{statName},
        .sendOther = (flags & FlagSendOther) != 0,
        .isSigned = (flags & FlagSigned) != 0,
        .sendBits = record[0x08],
        .sendParamBits = record[0x09],
        .updateAnimRate = (flags & FlagUpdateAnimRate) != 0,
        .saved = (flags & FlagSaved) != 0,
        .csvSigned = (flags & FlagCsvSigned) != 0,
        .csvBits = record[0x0A],
        .csvParamBits = record[0x0B],
        .callback = (flags & FlagCallback) != 0,
        .hasMinimum = (flags & FlagMinimum) != 0,
        .minimumAccumulator = static_cast<std::int32_t>(ReadU32(record, 0x28)),
        .encode = record[0x2C],
        .add = static_cast<std::int32_t>(ReadU32(record, 0x10)),
        .multiply = static_cast<std::int32_t>(ReadU32(record, 0x0C)),
        .valueShift = record[0x14],
        .saveBits = record[0x15],
        .saveAdd = static_cast<std::int32_t>(ReadU32(record, 0x18)),
        .saveParamBits = ReadU32(record, 0x20),
        .keepZero = record[0x4C] != 0,
        .operation = record[0x50],
        .operationParam = record[0x51],
        .operationBase = ReadU16(record, 0x52),
        .operationStat1 = ReadU16(record, 0x54),
        .operationStat2 = ReadU16(record, 0x56),
        .operationStat3 = ReadU16(record, 0x58),
        .direct = (flags & FlagDirect) != 0,
        .maximumStat = ReadU16(record, 0x2E),
        .damageRelated = (flags & FlagDamageRelated) != 0,
        .itemEvent1 = ReadU16(record, 0x44),
        .itemEventFunction1 = ReadU16(record, 0x48),
        .itemEvent2 = ReadU16(record, 0x46),
        .itemEventFunction2 = ReadU16(record, 0x4A),
    };
}

} // namespace

auto NormalizeEffectiveStuff(std::uint32_t value) noexcept -> std::uint8_t {
    return value >= 1 && value <= 8
        ? static_cast<std::uint8_t>(value)
        : DefaultEffectiveStuff;
}

auto BuildSchemaDescriptor(
        std::span<const ItemStatSemanticRow> rows,
        std::uint32_t effectiveStuff,
        std::vector<std::uint8_t>& output) noexcept -> SchemaError {
    if (rows.size() > MaximumRecordCount) return SchemaError::TooManyRows;
    try {
        std::vector<std::uint8_t> staged;
        constexpr std::size_t DomainSize = sizeof(SchemaDomainTag);
        constexpr std::size_t HeaderSize = DomainSize + 2 + 1 + 2 + 4 + 1;
        constexpr std::size_t FixedRowSize = 54;
        if (rows.size() > ((std::numeric_limits<std::size_t>::max)()
                - HeaderSize) / FixedRowSize) {
            return SchemaError::TooManyRows;
        }
        auto descriptorSize = HeaderSize + rows.size() * FixedRowSize;
        for (const auto& row : rows) {
            if (!IsValidUtf8(row.statName)) return SchemaError::InvalidUtf8;
            if (row.csvBits > MaximumSerializedCsvBits
                    || row.csvParamBits > MaximumSerializedCsvParamBits) {
                return SchemaError::UnsafeCodecWidth;
            }
            if (row.statName.size()
                    > (std::numeric_limits<std::size_t>::max)()
                        - descriptorSize) {
                return SchemaError::TooManyRows;
            }
            descriptorSize += row.statName.size();
        }
        staged.reserve(descriptorSize);
        staged.insert(
            staged.end(), SchemaDomainTag, SchemaDomainTag + DomainSize);
        AppendU16(staged, CanonicalSchemaDescriptorVersion);
        AppendU8(staged, static_cast<std::uint8_t>(SerializedBitWidth));
        AppendU16(staged, SerializedSentinel);
        AppendU32(staged, static_cast<std::uint32_t>(rows.size()));
        AppendU8(staged, NormalizeEffectiveStuff(effectiveStuff));

        for (std::size_t ordinal{}; ordinal < rows.size(); ++ordinal) {
            const auto& row = rows[ordinal];
            AppendU16(staged, static_cast<std::uint16_t>(ordinal));
            AppendU16(staged, static_cast<std::uint16_t>(row.statName.size()));
            staged.insert(
                staged.end(), row.statName.begin(), row.statName.end());
            AppendU16(staged, SemanticFlags(row));
            AppendU8(staged, row.sendBits);
            AppendU8(staged, row.sendParamBits);
            AppendU8(staged, row.csvBits);
            AppendU8(staged, row.csvParamBits);
            AppendI32(staged, row.multiply);
            AppendI32(staged, row.add);
            AppendU8(staged, row.valueShift);
            AppendI32(staged, row.minimumAccumulator);
            AppendU8(staged, row.saveBits);
            AppendI32(staged, row.saveAdd);
            AppendU32(staged, row.saveParamBits);
            AppendU8(staged, row.encode);
            AppendU16(staged, NormalizeStatReference(
                row.maximumStat, rows.size()));
            AppendU16(staged, row.itemEvent1);
            AppendU16(staged, row.itemEventFunction1);
            AppendU16(staged, row.itemEvent2);
            AppendU16(staged, row.itemEventFunction2);
            AppendBool(staged, row.keepZero);
            AppendU8(staged, row.operation);
            AppendU8(staged, row.operationParam);
            AppendU16(staged, NormalizeStatReference(
                row.operationBase, rows.size()));
            AppendU16(staged, NormalizeStatReference(
                row.operationStat1, rows.size()));
            AppendU16(staged, NormalizeStatReference(
                row.operationStat2, rows.size()));
            AppendU16(staged, NormalizeStatReference(
                row.operationStat3, rows.size()));
        }
        if (staged.size() != descriptorSize) return SchemaError::SizeMismatch;
        output.swap(staged);
        return SchemaError::None;
    } catch (...) {
        return SchemaError::Allocation;
    }
}

auto CalculateSchemaHash(
        std::span<const ItemStatSemanticRow> rows,
        std::uint32_t effectiveStuff,
        Sha256Digest& output) noexcept -> SchemaError {
    std::vector<std::uint8_t> descriptor;
    const auto build = BuildSchemaDescriptor(rows, effectiveStuff, descriptor);
    if (build != SchemaError::None) return build;
    Sha256Digest staged{};
    if (!CalculateSha256(descriptor, staged)) return SchemaError::HashFailure;
    output = staged;
    return SchemaError::None;
}

auto DecodeCompiledItemStatRecords(
        std::span<const std::uint8_t> recordBytes,
        std::size_t rowCount,
        std::span<const std::string_view> statNames,
        std::vector<ItemStatSemanticRow>& rows,
        std::uint8_t& effectiveStuff) noexcept -> SchemaError {
    if (rowCount > MaximumRecordCount) return SchemaError::TooManyRows;
    if (statNames.size() != rowCount
            || rowCount > (std::numeric_limits<std::size_t>::max)()
            / CompiledItemStatRecordStride
            || recordBytes.size() != rowCount * CompiledItemStatRecordStride) {
        return SchemaError::SizeMismatch;
    }
    try {
        std::vector<ItemStatSemanticRow> staged;
        staged.reserve(rowCount);
        for (std::size_t index{}; index < rowCount; ++index) {
            if (!IsValidUtf8(statNames[index])) return SchemaError::InvalidUtf8;
            const auto record = recordBytes.subspan(
                index * CompiledItemStatRecordStride,
                CompiledItemStatRecordStride);
            if (ReadU16(record, 0) != index) {
                return SchemaError::RecordIdMismatch;
            }
            if (record[0x0A] > MaximumSerializedCsvBits
                    || record[0x0B] > MaximumSerializedCsvParamBits) {
                return SchemaError::UnsafeCodecWidth;
            }
            staged.push_back(DecodeRow(record, statNames[index]));
        }
        const auto stagedStuff = rowCount == 0
            ? DefaultEffectiveStuff
            : NormalizeEffectiveStuff(ReadU32(recordBytes, 0x13C));
        rows.swap(staged);
        effectiveStuff = stagedStuff;
        return SchemaError::None;
    } catch (...) {
        return SchemaError::Allocation;
    }
}

} // namespace ruffneckk::isc12
