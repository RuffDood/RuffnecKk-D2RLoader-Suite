#include "item_retry.hpp"
#include <iostream>
#include <limits>
namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
int main() {
    using namespace RuffnecKk::AutoPickup;
    ItemRetryCooldown retry;
    const PickupIdentity a{1, 10, 100}, b{1, 11, 100}, otherPlayer{2, 10, 100}, reusedId{1, 10, 101};
    CHECK(retry.Ready(a, 100) && retry.TryArm(a, 100));
    CHECK(!retry.Ready(a, 101) && !retry.TryArm(a, 200));
    CHECK(retry.Ready(b, 200) && retry.Ready(otherPlayer, 200) && retry.Ready(reusedId, 200));
    CHECK(retry.TryArm(b, 200) && retry.TryArm(otherPlayer, 200));
    CHECK(!retry.Ready(a, 1099) && retry.Ready(a, 1100));
    CHECK(retry.TryArm(a, 1100)); // Skips did not extend the original deadline.
    CHECK(!retry.Ready(b, 1199) && retry.Ready(b, 1200));
    CHECK(!retry.Ready(a, 1200));
    retry.Reset(); CHECK(retry.Ready(a, 1201));
    CHECK(retry.TryArm(a, 5000) && retry.Ready(a, 4000)); // Clock rollback clears stale deadlines.
    CHECK(!retry.TryArm({UINT32_MAX, 1, 100}, 4001));
    CHECK(!retry.Ready({1, UINT32_MAX, 100}, 4001));
    retry.Reset();
    for (std::uint32_t i = 0; i < ItemRetryCooldown::Capacity; ++i) CHECK(retry.TryArm({1, i, 100}, 100));
    CHECK(!retry.TryArm({2, 12345, 100}, 101)); // Never evict a live entry and retry it every scan.
    CHECK(!retry.Ready({1, 0, 100}, 101));
    CHECK(retry.TryArm({2, 12345, 100}, 1100)); // Expired entries can be reused.
    CHECK(retry.Ready({1, 1, 100}, 1100));
    retry.Reset();
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    CHECK(retry.TryArm(a, maximum - 100) && !retry.Ready(a, maximum - 1));
    CHECK(retry.Ready(a, maximum)); // Saturation never wraps the deadline into the past.
    std::cout << "Item deadlines, player/item/code isolation, invalid IDs, reset, clock rollback and bounded cache pressure; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
