#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "core/types.hpp"

namespace chessclock {

using FlagCallback = std::function<void(Color)>;
using TickCallback = std::function<void(std::int64_t w_ms, std::int64_t b_ms)>;

class Clock {
public:
    Clock();
    ~Clock();
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    void set_flag_callback(FlagCallback cb);
    void set_tick_callback(TickCallback cb);

    // running_side defaults to White (normal game start). A position with Black
    // to move passes Black so only the side actually to move drains its clock.
    void start(std::int64_t w_ms, std::int64_t b_ms,
               std::int64_t winc_ms, std::int64_t binc_ms,
               Color running_side = Color::White);
    void stop();

    bool running() const { return running_.load(std::memory_order_relaxed); }
    bool flagged() const { return flagged_.load(std::memory_order_relaxed); }
    Color flagged_color() const { return flagged_color_.load(std::memory_order_relaxed); }

    std::int64_t remaining_ms(Color c) const {
        return c == Color::White ? w_ms_.load(std::memory_order_relaxed)
                                 : b_ms_.load(std::memory_order_relaxed);
    }
    std::int64_t increment_ms(Color c) const {
        return c == Color::White ? winc_ : binc_;
    }
    Color running_side() const { return running_side_.load(std::memory_order_relaxed); }

    // Call once when `mover` completes a move: account elapsed time for the
    // running side, add mover's increment, switch running side.
    void on_move(Color mover);

    // Deterministic test/simulation hook: subtract `ms` from the running side
    // and evaluate flag.
    void deduct(std::int64_t ms);

private:
    void tick_loop();
    FlagCallback deduct_locked(std::int64_t ms);  // caller holds mutex_; returns cb to fire (empty if none)
    void fire_flag_locked(Color c);               // caller holds mutex_

    std::atomic<std::int64_t> w_ms_{0};
    std::atomic<std::int64_t> b_ms_{0};
    std::int64_t winc_ = 0;
    std::int64_t binc_ = 0;
    std::atomic<Color> running_side_{Color::White};
    std::atomic<bool> running_{false};
    std::atomic<bool> flagged_{false};
    std::atomic<Color> flagged_color_{Color::White};

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread tick_thread_;
    std::chrono::steady_clock::time_point last_tick_;
    FlagCallback on_flag_;
    TickCallback on_tick_;
};

}  // namespace chessclock
