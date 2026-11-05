#include "gold_retry.hpp"
#include <iostream>

namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
int main() {
    using namespace Reimagined::EmbeddedTeleportPickup;
    using Decision = GoldRetryGuard::Decision;
    GoldRetryGuard guard;
    const GoldWallet room{900, 1000}, full{1000, 1000}, over{1200, 1000};
    CHECK(guard.Check(1, full, 0) == Decision::Full);
    CHECK(guard.Check(1, over, 100) == Decision::Full);
    CHECK(guard.Check(1, room, 200) == Decision::Ready); // Spending/depositing resumes pickup.
    guard.Attempt(1, room, 200);
    CHECK(guard.Check(1, room, 201) == Decision::CoolingDown);
    CHECK(guard.Check(1, room, 1199) == Decision::CoolingDown);
    CHECK(guard.Check(1, room, 1200) == Decision::Ready);
    CHECK(guard.Check(2, room, 300) == Decision::Ready); // A different player is independent.
    guard.Attempt(1, room, 1300);
    CHECK(guard.Check(1, {901, 1000}, 1301) == Decision::Ready); // Progress bypasses the delay.
    guard.Attempt(1, {901, 1000}, 1301);
    CHECK(guard.Check(1, {800, 1000}, 1302) == Decision::Ready); // Spending does too.
    guard.Attempt(1, {800, 1000}, 1302);
    CHECK(guard.Check(1, {800, 2000}, 1303) == Decision::Ready); // Use a mod's observed cap.
    guard.Attempt(1, {800, 2000}, 1303);
    CHECK(guard.Check(1, {2000, 2000}, 1304) == Decision::Full);
    CHECK(guard.Check(1, {800, 2000}, 1305) == Decision::Ready);
    guard.Attempt(1, room, 2000);
    guard.Reset();
    CHECK(guard.Check(1, room, 2001) == Decision::Ready); // New session/reused player ID.
    for (const auto wallet : {GoldWallet{-1, 1000}, GoldWallet{1, 0}, GoldWallet{1, UINT32_MAX}})
        CHECK(guard.Check(1, wallet, 2002) == Decision::Unavailable);
    CHECK(guard.Check(UINT32_MAX, room, 2002) == Decision::Unavailable);
    guard.Attempt(1, room, 3000);
    CHECK(guard.Check(1, room, 2999) == Decision::Ready); // Clock discontinuity cannot pin a cooldown.
    for (std::uint32_t id = 0; id <= GoldRetryGuard::Capacity; ++id) {
        CHECK(guard.Check(id, room, 4000 + id) == Decision::Ready);
        guard.Attempt(id, room, 4000 + id);
    }
    std::cout << "Full/over-cap wallets, no-progress cooldown, wallet/cap changes, independent players and session reset; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
