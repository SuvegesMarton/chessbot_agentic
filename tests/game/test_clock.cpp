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

int main() {
    flag_at_zero();
    only_running_side_drains();
    increment_and_switch();
    no_flag_after_stop();
    flag_callback_once();
    real_time_smoke();
    double_start_safe();

    if (failures == 0) { std::cout << "All clock tests passed\n"; return 0; }
    std::cerr << failures << " clock test(s) failed\n";
    return 1;
}
