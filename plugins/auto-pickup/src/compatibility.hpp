#pragma once
#include <cstdint>
#include <string_view>

namespace RuffnecKk::AutoPickup {
constexpr auto AcceptPickupEntry(bool unchanged, bool trackedInlineHook,
    std::uint32_t ownerCount, std::string_view owner) noexcept -> bool {
    return unchanged || (trackedInlineHook && ownerCount == 1 && owner == "d2rl-auto-deposit");
}
}
