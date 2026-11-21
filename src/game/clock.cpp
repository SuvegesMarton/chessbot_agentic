#include "game/clock.hpp"

namespace {

constexpr auto TICK_INTERVAL = std::chrono::milliseconds(100);

inline Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

}  // namespace

namespace chessclock {

Clock::Clock() = default;

Clock::~Clock() {
    stop();
}

void Clock::set_flag_callback(FlagCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    on_flag_ = std::move(cb);
}

void Clock::set_tick_callback(TickCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    on_tick_ = std::move(cb);
}

void Clock::start(std::int64_t w_ms, std::int64_t b_ms,
                  std::int64_t winc_ms, std::int64_t binc_ms,
                  Color running_side) {
    if (tick_thread_.joinable() &&
        tick_thread_.get_id() != std::this_thread::get_id()) {
        tick_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lk(mutex_);
        w_ms_.store(w_ms);
        b_ms_.store(b_ms);
        winc_ = winc_ms;
        binc_ = binc_ms;
        running_side_.store(running_side);
        flagged_.store(false);
        flagged_color_.store(Color::White);
        running_.store(true);
        last_tick_ = std::chrono::steady_clock::now();
    }

    tick_thread_ = std::thread(&Clock::tick_loop, this);
}

void Clock::tick_loop() {
    std::unique_lock<std::mutex> lk(mutex_);
    std::uint64_t tick_count = 0;
    while (running_.load(std::memory_order_relaxed)) {
        cv_.wait_for(lk, TICK_INTERVAL,
                     [this] { return !running_.load(std::memory_order_relaxed); });
        if (!running_.load(std::memory_order_relaxed)) break;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_tick_).count();
        last_tick_ = now;

        FlagCallback fcb = on_flag_;
        TickCallback tcb = on_tick_;
        (void)fcb;
        lk.unlock();

        if (elapsed > 0) {
            deduct(static_cast<std::int64_t>(elapsed));
        }

        ++tick_count;
        if (tcb && (tick_count % 2u) == 0u) {
            tcb(w_ms_.load(std::memory_order_relaxed),
                b_ms_.load(std::memory_order_relaxed));
        }

        lk.lock();
    }
}

FlagCallback Clock::deduct_locked(std::int64_t ms) {
    Color c = running_side_.load(std::memory_order_relaxed);
    std::int64_t cur =
        (c == Color::White ? w_ms_ : b_ms_).load(std::memory_order_relaxed);
    std::int64_t nv = cur - ms;
    if (nv < 0) nv = 0;
    (c == Color::White ? w_ms_ : b_ms_).store(nv, std::memory_order_relaxed);

    if (nv == 0) {
        fire_flag_locked(c);
        return on_flag_;
    }
    return FlagCallback{};
}

void Clock::fire_flag_locked(Color c) {
    if (flagged_.exchange(true, std::memory_order_relaxed)) return;
    flagged_color_.store(c, std::memory_order_relaxed);
    running_.store(false, std::memory_order_relaxed);
}

void Clock::deduct(std::int64_t ms) {
    FlagCallback cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_.load(std::memory_order_relaxed) ||
            flagged_.load(std::memory_order_relaxed)) {
            return;
        }
        cb = deduct_locked(ms);
    }
    if (cb) cb(flagged_color_.load(std::memory_order_relaxed));
}

void Clock::on_move(Color mover) {
    FlagCallback cb;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_.load(std::memory_order_relaxed) ||
            flagged_.load(std::memory_order_relaxed)) {
            return;
        }
        auto now = std::chrono::steady_clock::now();
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick_)
                .count();
        last_tick_ = now;
        cb = deduct_locked(static_cast<std::int64_t>(elapsed));
        if (!cb) {
            std::int64_t cur =
                (mover == Color::White ? w_ms_ : b_ms_).load(
                    std::memory_order_relaxed);
            std::int64_t inc =
                (mover == Color::White ? winc_ : binc_);
            (mover == Color::White ? w_ms_ : b_ms_).store(cur + inc,
                                                           std::memory_order_relaxed);
            running_side_.store(opposite(mover), std::memory_order_relaxed);
        }
    }
    if (cb) cb(flagged_color_.load(std::memory_order_relaxed));
}

void Clock::stop() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        running_.store(false, std::memory_order_relaxed);
    }
    cv_.notify_all();
    if (tick_thread_.joinable()) {
        if (tick_thread_.get_id() == std::this_thread::get_id()) {
            tick_thread_.detach();
        } else {
            tick_thread_.join();
        }
    }
}

}  // namespace chessclock
