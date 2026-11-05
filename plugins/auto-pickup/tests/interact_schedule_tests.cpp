#include "engine.hpp"
#include <cstdlib>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
using namespace AutoInteract;
int main() {
    Schedule s;
    auto ticket = s.Enter(1, 7);
    CHECK(ticket && s.Current(ticket) && s.Player() == 7);
    CHECK(!s.Enter(1, 7)); // duplicate readiness never adds a second callback chain
    CHECK(s.Due(ticket, 100, 100)); CHECK(!s.Due(ticket, 199, 100));
    CHECK(s.Due(ticket, 200, 100)); CHECK(s.Due(ticket, 1, 100));
    const auto old = ticket;
    ticket = s.Enter(2, 7); CHECK(ticket && !s.Current(old) && !s.Due(old, 1000, 100));
    CHECK(s.Due(ticket, 1000, 100));
    s.Stop(); CHECK(!s.Current(ticket));
    ticket = s.Enter(2, 8); CHECK(s.Current(ticket) && s.Player() == 8);
    s.Stop(); // queue rejection retires all work
    CHECK(!s.Due(ticket, 2000, 100));
    CHECK(!s.Enter(3, UINT32_MAX));
    std::cout << "session/scheduling passed\n";
}
