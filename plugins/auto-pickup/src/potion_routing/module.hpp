#pragma once
#include "policy.hpp"
#include <D2RLPlugin/api.h>
namespace RuffnecKk::PotionRouting {
auto Prepare(const D2RL::PluginContext *) noexcept -> bool;
auto Activate() noexcept -> bool;
void Stop() noexcept;
// Synchronous authoritative-thread calls. No unit/provider pointer is retained.
auto CanRoute(void *player, void *item, const Route &) noexcept -> bool;
auto Pickup(void *player, void *item, std::uint32_t guid,
            std::uint32_t distance, const Route &) noexcept -> bool;
} // namespace RuffnecKk::PotionRouting
