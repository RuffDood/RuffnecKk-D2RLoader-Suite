#pragma once

#include <D2RLPlugin/context.h>

namespace MapSeedSelector {
auto StartPicker(const D2RL::PluginContext* context) noexcept -> bool;
void StopPicker() noexcept;
}
