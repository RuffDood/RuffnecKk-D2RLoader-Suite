#pragma once
#include <utility>
#include <cstddef>
namespace ruffneckk::cast_triggers::teleport {
// The native room observer array is unique; the owner may also occur in it.
template<class Send> void NotifyObservers(void* owner, void* const* observers,
        std::size_t count, Send&& send) {
    if (owner) send(owner);
    for (std::size_t i = 0; i < count; ++i) {
        if (observers[i] && observers[i] != owner) send(observers[i]);
    }
}
template<class Immediate, class Queue> void NotifyBeforeMove(bool eligible,
        Immediate&& immediate, Queue&& queue) {
    if (!eligible || !std::forward<Immediate>(immediate)())
        std::forward<Queue>(queue)();
}
template<class T> class PointerScope {
    T*& slot_;
    T* previous_;
public:
    PointerScope(T*& slot, T* current) noexcept : slot_(slot), previous_(slot) { slot = current; }
    ~PointerScope() { slot_ = previous_; }
    PointerScope(const PointerScope&) = delete;
    PointerScope& operator=(const PointerScope&) = delete;
};
template<class Dispatch> void BeforeMove(bool& dispatched, Dispatch&& dispatch) {
    if (dispatched) return;
    // Mark first: a proc may itself relocate or call another skill handler.
    dispatched = true;
    std::forward<Dispatch>(dispatch)();
}
}
