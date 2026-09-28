#pragma once

#include "player_scaling_policy.hpp"

namespace ruffneckk::player_scaling {

// The native setting applies its own range after the chat parser's clamp.
// Update that same object before committing the recovered command value.
// Do this for every command: the setting may have been reinitialized since
// the last command. Keep its native notification/persistence setter intact.
template<class SetRange, class SetCount>
auto ApplyPlayersCommand(
        void* setting,
        std::int32_t requested,
        const Config& config,
        SetRange setRange,
        SetCount setCount) noexcept -> std::int32_t {
    const auto accepted = config.battleNetSimulationEnabled
        ? 1 : ClampPlayersCommand(requested, config);
    const auto minimum = config.battleNetSimulationEnabled
        ? 1 : config.minimumScalingPlayers;
    const auto maximum = config.battleNetSimulationEnabled
        ? 1 : config.maximumCommandPlayers;
    setRange(setting, minimum, maximum);
    setCount(setting, accepted);
    return accepted;
}

} // namespace ruffneckk::player_scaling
