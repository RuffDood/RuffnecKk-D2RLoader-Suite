#ifdef NDEBUG
#undef NDEBUG
#endif
#include "teleport_dispatch.hpp"
#include <cassert>
#include <vector>
using namespace ruffneckk::cast_triggers;
int main() {
    // Reproduce native deferred serialization: it samples position when drained.
    // The pre-move notification must avoid that queue, for owner and observers.
    for (int target : {90,110}) {
        for (bool eligible : {false,true}) {
            int position=100, owner=0, observer=0, queued=0;
            void* clients[]{&owner,&observer};
            std::vector<int> origins;
            auto send=[&] { teleport::NotifyObservers(&owner,clients,2,
                [&](void*) { origins.push_back(position); }); };
            teleport::NotifyBeforeMove(eligible,[&] { send(); return true; },
                [&] { ++queued; });
            position=target;
            if (queued) send();
            assert(origins.size()==2 && origins[0]==(eligible ? 100 : target));
            assert(origins[1]==origins[0] && queued==(eligible ? 0 : 1));
        }
    }
    int fallback=0;
    teleport::NotifyBeforeMove(true,[] { return false; },[&] { ++fallback; });
    assert(fallback==1);
    // A target on either side must produce a nonzero direction before movement.
    for (int destination : {90,110}) {
        int position=100, origin=-1, aim=0, dispatches=0;
        bool dispatched=false;
        auto proc=[&] { origin=position; aim=destination-position; ++dispatches;
            teleport::BeforeMove(dispatched,[&] { ++dispatches; }); };
        teleport::BeforeMove(dispatched,proc);
        position=destination;
        if (!dispatched) proc(); // normal post-handler fallback must be skipped
        assert(origin==100 && aim==(destination==90 ? -10 : 10) && dispatches==1);
    }
    int outer=1, inner=2;
    int* active=&outer;
    {
        teleport::PointerScope scope(active,&inner);
        assert(active==&inner);
        { teleport::PointerScope nested(active,static_cast<int*>(nullptr)); assert(!active); }
        assert(active==&inner);
    }
    assert(active==&outer);
}
