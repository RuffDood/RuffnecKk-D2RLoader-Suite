#pragma once
#include <cstdint>
namespace Reimagined::EmbeddedTeleportPickup {
inline constexpr std::uint32_t StandardGoldPickupDistance = 4;
struct Config {
    bool enabled{true}, validateOnly{}, gold{true}, includeItemPlugin{true};
    bool diagnosticsEnabled{true}, followUpEnabled{true};
    std::uint32_t pickupRange{4}, followUpIntervalMs{100}, followUpWindowMs{2000};
};
}
