#pragma once
#include "pickup_coexist.hpp"
namespace RuffnecKk::AutoPickup {
// Older Teleport cannot yield; retain its safe fallback until it is updated.
inline auto StandaloneTeleportPresent(const D2RL::PluginContext* context) noexcept -> bool {
    return Reimagined::PickupCoexist::LegacyTeleportPresent(context);
}
}
