#pragma once
#include <D2RLPlugin/api.h>
#include "../interact_settings.hpp"
namespace AutoInteract {
auto Start(const D2RL::PluginContext*, bool validationOnly) noexcept -> bool;
void Stop() noexcept;
auto SettingsApi() noexcept -> const Reimagined::InteractSettings::Api*;
}
