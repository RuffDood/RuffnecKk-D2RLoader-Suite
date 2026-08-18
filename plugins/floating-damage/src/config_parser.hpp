#pragma once

#include "floating_damage.hpp"

#include <string>
#include <string_view>

namespace FloatingDamage {

// Parses into a temporary Config and assigns output only after the entire file
// has passed syntax, type, range, table, key, and duplicate validation.
bool ParseConfigToml(
    std::string_view text,
    Config& output,
    std::string& error);

} // namespace FloatingDamage
