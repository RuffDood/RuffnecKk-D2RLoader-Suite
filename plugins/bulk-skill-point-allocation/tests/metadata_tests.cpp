#include <D2RLPlugin/api.h>

#include <cstring>
#include <iostream>

extern "C" auto D2RLoaderGetPluginInfo() noexcept
    -> const D2RL::PluginInfo*;

namespace {
auto Require(bool value, const char* expression, int line) -> bool {
    if (value) return true;
    std::cerr << "line " << line << ": failed: " << expression << '\n';
    return false;
}
}

#define REQUIRE(value) \
    do { if (!Require((value), #value, __LINE__)) return 1; } while (false)

int main() {
    const auto* info = D2RLoaderGetPluginInfo();
    REQUIRE(info != nullptr);
    REQUIRE(info->infoSize == D2RL::PluginInfoSize);
    REQUIRE(info->apiVersion == D2RL_PLUGIN_API_VERSION);
    REQUIRE(std::strcmp(
        info->id,
        "ruffneckk-bulk-skill-point-allocation") == 0);
    REQUIRE(std::strcmp(info->name, "Bulk Skill Point Allocation") == 0);
    REQUIRE(std::strcmp(info->version, "1.3.3") == 0);
    REQUIRE(std::strcmp(info->author, "RuffnecKk") == 0);
    REQUIRE(D2RL::HasFlag(info->flags, D2RL::PluginFlags::Client));
    REQUIRE(D2RL::HasFlag(info->flags, D2RL::PluginFlags::NativeHooks));
    REQUIRE(!D2RL::HasFlag(info->flags, D2RL::PluginFlags::Server));
    REQUIRE(!D2RL::HasFlag(info->flags, D2RL::PluginFlags::Shared));
    REQUIRE((D2RL::FlagsValue(info->flags) & 0x00000001U) == 0);
    REQUIRE(D2RL::HasOnlyKnownPluginFlags(info->flags));
    REQUIRE(D2RL::HasValidPluginRole(info->flags));
    return 0;
}
