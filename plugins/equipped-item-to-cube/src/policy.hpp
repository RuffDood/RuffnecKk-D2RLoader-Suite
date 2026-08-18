#pragma once

#include "config.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace RuffnecKk::EquippedItemToCube {

inline constexpr std::size_t ItemTransferPacketSize = 21;
inline constexpr std::uint8_t InventoryTransferOpcode = 0x54;
inline constexpr std::uint8_t EquippedTransferOpcode = 0x58;
inline constexpr std::uint32_t SelfTargetGuid = 0xFFFFFFFFu;
inline constexpr std::uint32_t CubeInventoryPage = 3;
inline constexpr std::uint32_t BodyLocationCount = 11;

using ItemTransferPacket = std::array<std::uint8_t, ItemTransferPacketSize>;

constexpr auto ReadU32(
    const ItemTransferPacket& packet,
    std::size_t offset
) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(packet[offset])
        | (static_cast<std::uint32_t>(packet[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(packet[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(packet[offset + 3]) << 24);
}

constexpr void WriteU32(
    ItemTransferPacket& packet,
    std::size_t offset,
    std::uint32_t value
) noexcept {
    packet[offset] = static_cast<std::uint8_t>(value);
    packet[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    packet[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    packet[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

constexpr auto IsEquippedBodyLocation(std::uint32_t bodyLocation) noexcept -> bool {
    return bodyLocation > 0 && bodyLocation < BodyLocationCount;
}

constexpr auto ShouldRewriteCubeTransfer(
    bool rewriteArmed,
    const ItemTransferPacket& packet,
    std::uint32_t bodyLocation
) noexcept -> bool {
    return rewriteArmed
        && IsEquippedBodyLocation(bodyLocation)
        && packet[0] == InventoryTransferOpcode
        && ReadU32(packet, 13) == CubeInventoryPage;
}

constexpr auto RewriteAsEquippedTransfer(
    const ItemTransferPacket& inventoryPacket,
    std::uint32_t bodyLocation
) noexcept -> ItemTransferPacket {
    ItemTransferPacket equippedPacket{};
    equippedPacket[0] = EquippedTransferOpcode;
    WriteU32(equippedPacket, 1, ReadU32(inventoryPacket, 1));
    WriteU32(equippedPacket, 5, SelfTargetGuid);
    WriteU32(equippedPacket, 9, bodyLocation);
    WriteU32(equippedPacket, 13, ReadU32(inventoryPacket, 13));
    WriteU32(equippedPacket, 17, ReadU32(inventoryPacket, 17));
    return equippedPacket;
}

} // namespace RuffnecKk::EquippedItemToCube
