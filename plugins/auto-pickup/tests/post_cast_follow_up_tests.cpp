#include "follow_up.hpp"
#include <iostream>

namespace {
int failures{};
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " #x "\n"; ++failures; } } while (false)
}
int main() {
    using RuffnecKk::AutoPickup::FollowUpWindow;
    FollowUpWindow w;
    CHECK(w.Arm(1, 42, 0, 100, 2000) == 0); // No live session yet.
    w.Enter(1);
    auto ticket = w.Arm(1, 42, 0, 100, 2000);
    CHECK(ticket != 0);
    CHECK(w.Arm(1, 42, 10, 100, 2000) == 0); // One queued callback.
    CHECK(w.Poll(ticket, 99, 100).count == 0);
    auto batch = w.Poll(ticket, 100, 100);
    CHECK(batch.again && batch.count == 1 && batch.players[0] == 42 && batch.game == 1);
    CHECK(w.Poll(ticket, 199, 100).count == 0);
    CHECK(w.Poll(ticket, 200, 100).count == 1);
    // Model a cast accepted at t=0 and a landing at t=300, with no more input.
    // The runtime resolves this identity's current position in each due scan.
    batch = w.Poll(ticket, 300, 100);
    CHECK(batch.again && batch.count == 1 && batch.players[0] == 42);
    CHECK(w.Poll(ticket, 2010, 100).again == false);
    CHECK(!w.IsCurrent(ticket));

    ticket = w.Arm(1, 42, 3000, 100, 2000);
    CHECK(ticket != 0);
    CHECK(w.Arm(1, 42, 4900, 100, 2000) == 0); // Latest action extends window.
    CHECK(w.Poll(ticket, 5100, 100).count == 1);
    CHECK(!w.Poll(ticket, 6900, 100).again);

    ticket = w.Arm(1, 42, 7000, 100, 2000);
    w.Enter(1); // Repeated LocalPlayerReady must not invalidate work.
    CHECK(w.IsCurrent(ticket));
    w.Reset(2, false); // Leave.
    CHECK(!w.Poll(ticket, 7100, 100).again);
    CHECK(w.Arm(1, 42, 7100, 100, 2000) == 0);
    w.Enter(3);
    auto newTicket = w.Arm(1, 42, 7200, 100, 2000); // Even if IDs/address reused.
    CHECK(newTicket != 0 && newTicket != ticket);
    CHECK(!w.Poll(ticket, 7300, 100).again);
    CHECK(w.Poll(newTicket, 7300, 100).count == 1);
    ticket = w.Arm(2, 43, 7400, 100, 2000); // Different authoritative game.
    CHECK(ticket != 0 && ticket != newTicket);
    CHECK(!w.Poll(newTicket, 7500, 100).again);
    CHECK(w.Poll(ticket, 7500, 100).game == 2);

    w.QueueFailed(newTicket); // Old callback cannot retire current work.
    CHECK(w.IsCurrent(ticket));
    w.QueueFailed(ticket);
    CHECK(!w.IsCurrent(ticket));
    CHECK(w.Arm(2, 43, 7600, 100, 2000) != 0); // Retry after queue failure.
    CHECK(w.Arm(0, 43, 7600, 100, 2000) == 0);
    CHECK(w.Arm(2, FollowUpWindow::InvalidId, 7600, 100, 2000) == 0);

    w.Reset(4, true);
    ticket = w.Arm(2, 0, 8000, 100, 2000);
    for (std::uint32_t id = 1; id < FollowUpWindow::Capacity; ++id)
        CHECK(w.Arm(2, id, 8000, 100, 2000) == 0);
    CHECK(w.Arm(2, 100, 8000, 100, 2000) == 0); // Full, no duplicate queue.
    CHECK(w.Poll(ticket, 8100, 100).count == FollowUpWindow::Capacity);
    CHECK(!w.Poll(ticket, 10000, 100).again);
    CHECK(w.Arm(2, 100, 10001, 100, 2000) != 0); // Expired slots reused.
    std::cout << "Follow-up cadence, delayed landing, expiry, coalescing, session and queue failure checks; failures: " << failures << '\n';
    return failures ? 1 : 0;
}
