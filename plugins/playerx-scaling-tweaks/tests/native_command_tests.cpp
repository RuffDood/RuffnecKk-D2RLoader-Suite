#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "native_fingerprint.hpp"
#include "players_command.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace ruffneckk::player_scaling;

// Execute the attested native setter, relocating only its two RIP-relative
// references into this test allocation. This catches the downstream p8 reject
// that a mock of the Offline Difficulty setting cannot reproduce.
int main() {
    auto* memory = static_cast<std::uint8_t*>(VirtualAlloc(
        nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!memory) return 1;
    std::memcpy(memory, native::ArtificialPlayerCountSetterEntry.data(),
        native::ArtificialPlayerCountSetterEntry.size());
    constexpr std::int32_t stateOffset = 2048;
    const std::int32_t loadDisplacement = stateOffset - 6;
    const std::int32_t storeDisplacement = stateOffset - 18;
    std::memcpy(memory + 2, &loadDisplacement, sizeof(loadDisplacement));
    std::memcpy(memory + 14, &storeDisplacement, sizeof(storeDisplacement));
    auto* count = reinterpret_cast<std::int32_t*>(memory + stateOffset);
    auto setter = reinterpret_cast<void(*)(std::int32_t)>(memory);
    DWORD oldProtection{};
    if (!VirtualProtect(memory, 4096, PAGE_EXECUTE_READWRITE, &oldProtection)) return 1;
    FlushInstructionCache(GetCurrentProcess(), memory, 19);
    *count = 2;
    setter(16);
    if (*count != 2) return 1;
    std::cout << "Reproduced native rejection: p2 + request p16 -> p2\n";
    std::copy(native::ArtificialPlayerCountLimitPatched.begin(),
        native::ArtificialPlayerCountLimitPatched.end(), memory + 9);
    FlushInstructionCache(GetCurrentProcess(), memory, 19);
    Config config{};
    config.maximumCommandPlayers = 64;
    bool passed = true;
    for (const auto requested : {1, 2, 8, 9, 16, 21, 64, 65, 0, -1}) {
        *count = 2;
        const auto expected = ApplyPlayersCommand(count, requested, config,
            [](void*, std::int32_t, std::int32_t) {},
            [&](void*, std::int32_t accepted) { setter(accepted); });
        if (*count != expected) {
            std::cerr << "FAIL: requested " << requested << ", expected "
                << expected << ", native count " << *count << '\n';
            passed = false;
        }
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    if (passed) std::cout << "PASS: configured count reaches native artificial-count state\n";
    return passed ? 0 : 1;
}
