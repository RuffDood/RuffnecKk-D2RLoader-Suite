#include <D2RLPlugin/api.h>

#include <cstring>

extern "C" auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo*;

namespace {

#define REQUIRE(expression) do { if (!(expression)) return __LINE__; } while (false)

} // namespace

int main() {
    const auto* info = D2RLoaderGetPluginInfo();
    REQUIRE(info != nullptr);
    REQUIRE(info->infoSize == D2RL::PluginInfoSize);
    REQUIRE(info->apiVersion == D2RL_PLUGIN_API_VERSION);
    REQUIRE(std::strcmp(info->id, "ruffneckk-vendor-stock-refresh") == 0);
    REQUIRE(std::strcmp(info->name, "Vendor Stock Refresh") == 0);
    REQUIRE(std::strcmp(info->version, "2.1.6") == 0);
    REQUIRE(std::strcmp(info->author, "RuffnecKk") == 0);
    REQUIRE(D2RL::PluginRoleValue(info->flags)
        == D2RL::FlagsValue(D2RL::PluginFlags::Shared));
    REQUIRE(D2RL::HasFlag(info->flags, D2RL::PluginFlags::NativeHooks));
    REQUIRE(!D2RL::HasFlag(info->flags, D2RL::PluginFlags::ModScopedOnly));
    REQUIRE(D2RL::HasOnlyKnownPluginFlags(info->flags));
    REQUIRE(D2RL::HasValidPluginRole(info->flags));
    return 0;
}
