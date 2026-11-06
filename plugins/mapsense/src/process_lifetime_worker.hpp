#pragma once
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace RuffnecKk::MapSense::Detail {
// These workers belong to the plugin process lifetime. Normal plugin unload
// MUST signal its stop condition and call join before unloading executable code.
// ExitProcess kills other threads before DLL static destruction and may skip the
// Loader unload callback. A global std::thread then terminates the process again.
// Keep its bookkeeping outside static destruction: explicit join releases it;
// on process teardown the OS reclaims it. Never join under the loader lock, detach
// a live worker, or disguise an exception thrown by a worker.
class ProcessLifetimeWorker final {
public:
    constexpr ProcessLifetimeWorker() noexcept = default;
    ProcessLifetimeWorker(const ProcessLifetimeWorker&) = delete;
    auto operator=(const ProcessLifetimeWorker&) -> ProcessLifetimeWorker& = delete;
    ~ProcessLifetimeWorker() = default;

    template<class Function>
    void Start(Function&& function) {
        if (thread_ != nullptr) throw std::logic_error("worker already started");
        auto next = std::make_unique<std::thread>(std::forward<Function>(function));
        thread_ = next.release();
    }
    [[nodiscard]] auto joinable() const noexcept -> bool {
        return thread_ != nullptr && thread_->joinable();
    }
    void join() {
        if (thread_ == nullptr) return;
        thread_->join();
        delete std::exchange(thread_, nullptr);
    }
private:
    std::thread* thread_{};
};
} // namespace RuffnecKk::MapSense::Detail
