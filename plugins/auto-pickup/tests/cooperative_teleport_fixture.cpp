#include <D2RLPlugin/api.h>
#include "pickup_coexist.hpp"
// A yielding peer, packaged only with test sources. Real standalone binaries
// can also be passed to activation_tests for cross-project qualification.
D2RL_PLUGIN_EXPORT bool D2RLoaderLoadPlugin(const D2RL::PluginContext* ctx) noexcept {
    return Reimagined::PickupCoexist::YieldToPickit(ctx);
}
D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {}
