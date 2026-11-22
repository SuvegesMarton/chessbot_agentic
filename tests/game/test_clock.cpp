#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "game/clock.hpp"
#include "core/types.hpp"

namespace {
int failures = 0;
void check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << "\n"; ++failures; }
}
}  // namespace

void flag_at_zero() {
    chessclock::Clock c;
    c.start(500, 5000, 0, 0);
    c.deduct(499);
    check(!c.flagged(), "not flagged at 499 deducted");
    c.deduct(1);
    check(c.flagged(), "flagged at zero");
    check(c.flagged_color() == Color::White, "flag color white");
    check(c.remaining_ms(Color::White) == 0, "white clamped to 0");
}

void only_running_side_drains() {
    chessclock::Clock c;
    c.start(1000, 1000, 0, 0);
    c.deduct(300);
    check(c.remaining_ms(Color::White) == 700, "white drained");
    check(c.remaining_ms(Color::Black) == 1000, "black untouched");
}

void increment_and_switch() {
    chessclock::Clock c;
    c.start(1000, 1000, 500, 0);
    c.on_move(Color::White);
    check(c.running_side() == Color::Black, "running side switched to black");
    check(c.remaining_ms(Color::Black) == 1000, "black clock untouched by white move");
    check(c.remaining_ms(Color::White) > 1000 && c.remaining_ms(Color::White) <= 1500,
          "white got ~+500 increment");
    c.deduct(400);
    check(c.remaining_ms(Color::Black) == 600, "black drained after switch");
    check(c.remaining_ms(Color::White) > 1000, "white unchanged by black drain");
}

void no_flag_after_stop() {
    chessclock::Clock c;
    c.start(1000, 1000, 0, 0);
    c.stop();
    c.deduct(5000);
    check(!c.flagged(), "no flag after stop");
    check(!c.running(), "not running after stop");
}

void flag_callback_once() {
    chessclock::Clock c;
    std::atomic<int> flagCount{0};
    c.set_flag_callback([&](Color) { flagCount.fetch_add(1); });
    c.start(300, 5000, 0, 0);
    c.deduct(300);
    check(c.flagged(), "flagged for callback test");
    check(flagCount.load() == 1, "flag callback fired exactly once");
}

void real_time_smoke() {
    chessclock::Clock c;
    c.start(120, 5000, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    check(c.flagged(), "tick thread flags in real time (120ms clock, 250ms wait)");
}

void double_start_safe() {
    chessclock::Clock c;
    c.start(1000, 1000, 0, 0);
    c.start(2000, 2000, 0, 0);
    check(!c.flagged(), "double start not flagged");
    check(c.remaining_ms(Color::White) == 2000, "second start resets white to 2000");
    check(c.remaining_ms(Color::Black) == 2000, "second start resets black to 2000");
}

void double_start_returns_promptly() {
    // Regression: start() must not block until the previous clock drains to zero.
    // A 3s first clock followed immediately by a restart must return well under
    // that duration (previously it joined the tick thread while it was still
    // running, blocking for the full ~3s drain).
    chessclock::Clock c;
    c.start(3000, 3000, 0, 0);
    auto t0 = std::chrono::steady_clock::now();
    c.start(3000, 3000, 0, 0);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    check(ms < 1500, "second start returns promptly (does not wait for drain)");
    check(!c.flagged(), "prompt restart not flagged");
}

void running_side_black_drains_black() {
    // A position with Black to move starts the clock on Black.
    chessclock::Clock c;
    c.start(5000, 5000, 0, 0, Color::Black);
    c.deduct(1000);
    check(c.remaining_ms(Color::Black) == 4000, "black drained when black to move");
    check(c.remaining_ms(Color::White) == 5000, "white untouched when black to move");
    check(c.running_side() == Color::Black, "running side is black");
}

void increment_ms_accessor() {
    chessclock::Clock c;
    c.start(1000, 2000, 300, 400);
    check(c.increment_ms(Color::White) == 300, "white increment accessor");
    check(c.increment_ms(Color::Black) == 400, "black increment accessor");
}

void on_move_deducts_elapsed_and_adds_increment() {
    chessclock::Clock c;
    c.start(5000, 5000, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    c.on_move(Color::White);
    // ~120ms elapsed deducted from White, no increment, side switched to Black.
    std::int64_t w = c.remaining_ms(Color::White);
    check(w < 5000, "on_move deducted elapsed time from mover");
    check(w > 4000, "on_move did not over-deduct (roughly ~120ms)");
    check(c.running_side() == Color::Black, "on_move switched running side to black");
}

void black_move_adds_black_increment() {
    chessclock::Clock c;
    c.start(5000, 5000, 0, 200, Color::Black);
    c.on_move(Color::Black);
    check(c.running_side() == Color::White, "black move switched running side to white");
    check(c.remaining_ms(Color::Black) > 5000 && c.remaining_ms(Color::Black) <= 5200,
          "black move added ~+200 increment to black");
    check(c.remaining_ms(Color::White) == 5000, "white untouched by black move");
}

void on_move_after_flag_is_noop() {
    chessclock::Clock c;
    c.start(100, 5000, 0, 0);
    c.deduct(100);
    check(c.flagged(), "flagged before on_move");
    Color side_before = c.running_side();
    c.on_move(Color::White);
    check(c.running_side() == side_before, "on_move after flag does not switch side");
    check(!c.running(), "clock stays stopped after on_move post-flag");
}

void restart_clears_flag() {
    chessclock::Clock c;
    c.start(100, 5000, 0, 0);
    c.deduct(100);
    check(c.flagged(), "flagged before restart");
    c.start(5000, 5000, 0, 0);
    check(!c.flagged(), "restart clears flagged state");
    check(c.flagged_color() == Color::White, "restart resets flagged color to white");
    check(c.remaining_ms(Color::White) == 5000, "restart reseeds white clock");
}

void tick_callback_fires() {
    chessclock::Clock c;
    std::atomic<int> ticks{0};
    std::int64_t last_w = -1, last_b = -1;
    c.set_tick_callback([&](std::int64_t w, std::int64_t b) {
        last_w = w;
        last_b = b;
        ticks.fetch_add(1);
    });
    c.start(5000, 6000, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    c.stop();
    check(ticks.load() >= 1, "tick callback fired at least once");
    check(last_w >= 0 && last_b >= 0, "tick callback delivered clock values");
}

int main() {
    flag_at_zero();
    only_running_side_drains();
    increment_and_switch();
    no_flag_after_stop();
    flag_callback_once();
    real_time_smoke();
    double_start_safe();
    double_start_returns_promptly();
    running_side_black_drains_black();
    increment_ms_accessor();
    on_move_deducts_elapsed_and_adds_increment();
    black_move_adds_black_increment();
    on_move_after_flag_is_noop();
    restart_clears_flag();
    tick_callback_fires();

    if (failures == 0) { std::cout << "All clock tests passed\n"; return 0; }
    std::cerr << failures << " clock test(s) failed\n";
    return 1;
}
