#pragma once
#include <D2RLPlugin/api.h>
#include <cstddef>
namespace RuffnecKk::CastTriggers::ProcPresentation {
struct Options { bool serverAim{true}; bool clientAim{true}; bool missileRewind{true}; };
// Prepare checks every selected native witness and allocates relays before any patch.
bool Prepare(const D2RL::PluginContext*, Options) noexcept;
bool Install() noexcept;
void Enable() noexcept;
void Stop() noexcept;
void Describe(char*, std::size_t) noexcept;
}
