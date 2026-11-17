# Phase 4 — Clocks and Bot-vs-Bot (Execution Plan)

Source: `plans/general.md` (Phase 4, lines 113-117, plus protocol lines 72-76 and watch-out line 138), `AGENTS.md`, `plans/phase_0.md` … `plans/phase_3.md`.
Scope: add a per-side countdown clock with increment and flag detection (`clock`), a game loop that drives both sides in `bot_bot` mode (`game`), `game_over` on flag / mate / stalemate / draw rules, and the frontend clock display + game-over overlay. No networked play, PGN, or alternate time-control UI (out of scope per general.md).

## 0. Baseline, prerequisites, and constraints

- **Depends on Phase 2/3 deliverables (must exist first).** Phase 4 consumes these interfaces; if any are missing, finish the earlier phase first (do not re-architect here):
  - `movegen::generate_legal(Position&, MoveList&)`, `movegen::in_check(const Position&, Color)`, `movegen::is_checkmate(Position&)`, `movegen::is_stalemate(Position&)` (`src/movegen.hpp`).
  - `Position::to_fen()`, `Position::piece_on`, `Position::board_snapshot()`, reversible `make_move`/`unmake_move` with Zobrist (`src/position.hpp/.cpp`).
  - `GameSession` with `Position pos_`, `orientation`, `last_move`, `status`, `new_game(GameConfig)`, `is_bot_turn()`, `compute_limits()`, `play_move(Move)`, `maybe_play_bot(on_info)`, `set_position_fen()`, `position()`/`position_ref()`, `fen()`, `side_to_move()`, `status()`, `last_move_uci()` (`src/game.hpp/.cpp`, from Phases 1-3).
  - `SearchLimits { max_depth, movetime_ms, wtime, btime, winc, binc }`, `SearchResult`, `SearchInfo`, `Searcher` with `search(limits)` driven by a `steady_clock` deadline computed from `movetime_ms` (`src/search.hpp/.cpp`, Phase 3).
  - `src/web_server.cpp` with the WebSocket UCI loop (`uci`/`isready`/`ucinewgame`/`position`/`go`/`stop`), `state`/`legal`/`info`/`bestmove` emission; `web/index.html`, `web/style.css`, `web/app.js` rendering the board, legal highlights, thinking indicator (Phases 1-3).
- **Existing code reused as-is:** `src/types.hpp` (`Square`, `Color`, `PieceType`), `src/bitboard.hpp` (attack tables, `is_square_attacked`), `src/move.hpp` (`MoveList`, `move_to_uci`, `parse_uci`).
- **AGENTS.md rules (must follow):**
  - C++20; non-MSVC flags `-O3 -march=native -Wall -Wextra`, MSVC `/O2 /W4`.
  - Always `cmake -B build && cmake --build build -j4` then `ctest --test-dir build --output-on-failure` after changes.
  - Zero heap allocations inside `search`/`movegen` hot paths; use fixed-size stack arrays (`std::array`, `MoveList`). Phase 4's clock uses atomics/mutex (not a hot path); the game loop allocates `std::string` only per emitted line (allowed, same as Phase 3 `info`/`bestmove` lines). The repetition-history buffer is a fixed `std::array<std::uint64_t, N>` member.
  - Keep `make_move`/`unmake_move` reversible; search already asserts Zobrist consistency in debug builds — do not touch that code.
  - Never alter `perft` reference counts in `tests/`.
- **Style to match:** header/impl split like `position.hpp/.cpp`, `movegen.hpp/.cpp`; `namespace`-scoped types where natural (`namespace chessclock`); tests are small `main()` programs using `static int failures` + `check(bool, const char*)` printing `FAIL: …` to `std::cerr` and returning 1 (see `tests/test_bitboard.cpp`); almost no comments in code — only for non-obvious invariants; JSON built by hand with `std::string`/`std::to_string` (no JSON library).
- **Key design rule (general.md watch-out):** the clock ticks on a background thread feeding **atomic** millisecond values; the search reads its budget from a `steady_clock` deadline computed at `go` time, never by polling the clock thread.

## 1. Ordered implementation steps

### Step 1 — Clock (`src/clock.hpp`, `src/clock.cpp`)

**Files:** `src/clock.hpp` (new), `src/clock.cpp` (new).

**Header (`src/clock.hpp`):**
```cpp
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

#include "types.hpp"

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

    // Begin countdown (ms) and start the tick thread. running_side_ starts White.
    void start(std::int64_t w_ms, std::int64_t b_ms,
               std::int64_t winc_ms, std::int64_t binc_ms);
    void stop();  // stops countdown + tick thread; idempotent

    bool running() const { return running_.load(std::memory_order_relaxed); }
    bool flagged() const { return flagged_.load(std::memory_order_relaxed); }
    Color flagged_color() const { return flagged_color_.load(std::memory_order_relaxed); }

    std::int64_t remaining_ms(Color c) const {
        return c == Color::White ? w_ms_.load(std::memory_order_relaxed)
                                 : b_ms_.load(std::memory_order_relaxed);
    }
    std::int64_t increment_ms(Color c) const {
        return c == Color::White ? winc_ : binc_;   // set only in start(); immutable while running
    }
    Color running_side() const { return running_side_.load(std::memory_order_relaxed); }

    // Called once when `mover` completes a move: account elapsed time for the
    // running side, add mover's increment, switch running side.
    void on_move(Color mover);

    // Test/simulation hook: subtract `ms` from the running side and evaluate flag.
    void deduct(std::int64_t ms);

private:
    void tick_loop();
    void deduct_locked(std::int64_t ms);  // caller holds mutex_
    void fire_flag_locked(Color c);       // caller holds mutex_; sets flag, copies callback

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
```

**Implementation (`src/clock.cpp`):**
1. Constants (anonymous namespace): `constexpr auto TICK_INTERVAL = std::chrono::milliseconds(100);`.
2. `start(w,b,winc,binc)`: lock `mutex_`; store `w_ms_/b_ms_/winc_/binc_` (atomics + plain members under lock), `running_side_ = Color::White`, `flagged_ = false`, `running_ = true`, `last_tick_ = steady_clock::now()`; unlock; if `tick_thread_.joinable()` join first (guard against double-start); launch `tick_thread_ = std::thread(&Clock::tick_loop, this)`.
3. `tick_loop()`:
   ```
   std::unique_lock lk(mutex_);
   while (running_.load()) {
       cv_.wait_for(lk, TICK_INTERVAL, [this]{ return !running_.load(); });
       if (!running_.load()) break;
       auto now = steady_clock::now();
       auto elapsed = duration_cast<milliseconds>(now - last_tick_).count();
       last_tick_ = now;
       lk.unlock();
       if (elapsed > 0) deduct(static_cast<std::int64_t>(elapsed));   // locks internally
       // periodic UI tick (read atomics only; no lock needed for the values)
       if (on_tick_) { auto cb = on_tick_; cb(w_ms_.load(), b_ms_.load()); }
       lk.lock();
   }
   ```
   Note: `deduct()` re-locks `mutex_`; keep the critical sections short. The tick callback is invoked from the tick thread — it must only do thread-safe work (see Step 4 emitter with send mutex).
4. `deduct(ms)` / `deduct_locked(ms)`:
   - lock `mutex_`; if `!running_ || flagged_` → return.
   - `Color c = running_side_`; subtract `ms` from that side's atomic (`fetch_sub` then clamp: if result `< 0` store `0`).
   - If the running side reached `0` → `fire_flag_locked(c)`.
5. `fire_flag_locked(c)`: `if (flagged_.exchange(true)) return;` then `flagged_color_ = c; running_ = false;` copy `on_flag_` to a local; (caller unlocks before invoking the callback — invoke after `mutex_.unlock()` in `deduct`/`on_move` to avoid deadlock if the callback calls back into the session).
6. `on_move(mover)`:
   - lock `mutex_`; if `!running_ || flagged_` → unlock, return (game already over; no increment after flag).
   - Account elapsed time for the side that was running (normally `mover`): `now = steady_clock::now()`, `elapsed = now - last_tick_`, `last_tick_ = now`; call `deduct_locked(elapsed_ms)`; if a flag fired → copy callback, unlock, invoke, return.
   - Add increment to `mover`: `remaining(mover) += increment(mover)` (store back, clamp to `>= 0`).
   - `running_side_ = opposite(mover)`; unlock.
7. `stop()`: lock; `running_ = false`; unlock; `cv_.notify_all()`; if `tick_thread_.joinable()` and not called from the tick thread → `join()`. (Track thread id or document that `stop()` must not be called from `on_tick_`/`on_flag_` callbacks.)
8. Destructor: `stop()`.
9. Flag detection semantics: only the **running** side can flag; flagged side's ms is clamped to `0`; `flagged()` stays true until the next `start()`.

**Design notes:**
- All cross-thread reads (`remaining_ms`, `flagged`, `running_side`) are `std::atomic` relaxed loads — the search path (Step 3) reads `remaining_ms` at most once per move, never in a node loop.
- `deduct(ms)` is the deterministic test hook: tests never depend on wall-clock sleeps except one optional short real-time smoke test.
- No heap allocations: single `std::thread`, `std::mutex`, `std::condition_variable`, atomics; `std::function` callbacks are set once at wiring time (not per tick).

### Step 2 — Game session: clocks, bot-vs-bot loop, game-over (`src/game.hpp`, `src/game.cpp`)

**Files:** `src/game.hpp` (modify), `src/game.cpp` (modify). Keep all Phase 1-3 fields/accessors intact.

**Header additions:**
```cpp
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>

#include "clock.hpp"
#include "movegen.hpp"
#include "search.hpp"

struct GameResult {
    bool over = false;
    const char* result = "";   // "1-0" | "0-1" | "1/2"
    const char* reason = "";   // "checkmate" | "flag" | "stalemate" | "fifty-move" | "threefold repetition"
};

class GameSession {
public:
    // ... existing Phase 1-3 API unchanged ...
    using LineEmitter = std::function<void(const std::string&)>;  // one JSON/UCI line per call

    void new_game(const GameConfig& cfg);       // extend Phase 3: resets position, clocks, result, history
    bool set_position_fen(std::string_view fen); // Phase 1/2; Phase 4 resets clock+result+history on success

    void set_emitter(LineEmitter emit);          // thread-safe sink used by clock callbacks + bot loop
    void request_stop() { stop_requested_.store(true); }
    bool stop_requested() const { return stop_requested_.load(); }

    const chessclock::Clock& clock() const { return clock_; }
    chessclock::Clock& clock() { return clock_; }

    const GameResult& result() const;           // mutex-protected snapshot
    bool game_over() const;
    GameResult poll_flag();                      // returns result (over=true if flag newly consumed)
    GameResult check_end();                      // flag + mate/stalemate + draw rules; sets result_ once

    // Phase 3, extended: validate + make_move + clock_.on_move(mover) + history + check_end.
    bool play_move(const Move& m);

    // Phase 3, extended: skip when game over / flagged; do not apply move if flag fired mid-search.
    bool maybe_play_bot(std::function<void(const SearchInfo&)> on_info);

    // NEW: blocking self-play loop (bot_bot mode). Emits state/clock/info/bestmove/game_over.
    void run_bot_bot(LineEmitter emit);

    std::string state_json() const;              // extended: clocks non-null when clock running
    std::string clock_json() const;              // {"type":"clock","w_ms":N,"b_ms":N}
    std::string game_over_json() const;          // {"type":"game_over","result":"…","reason":"…"}

private:
    void handle_clock_flag(Color flagged);       // clock thread callback: set result_, emit once
    void push_history();

    // ... existing members ...
    chessclock::Clock clock_;
    GameResult result_;
    std::atomic<bool> game_over_emitted_{false};
    std::atomic<bool> stop_requested_{false};
    std::array<std::uint64_t, 2048> hash_history_{};  // repetition detection (optional rule)
    std::size_t hash_len_ = 0;
    mutable std::mutex result_mutex_;            // guards result_ / game_over_emitted_ reads+writes
    mutable std::mutex emit_mutex_;              // serializes emitter calls from clock thread + loop
    LineEmitter emit_;
};
```

**Implementation (`src/game.cpp`):**
1. `new_game(cfg)`:
   - `stop_requested_ = false; game_over_emitted_ = false;`
   - reset position (existing `reset_startpos()` / `pos_.set_starting_position()`), clear `last_move`, `hash_len_ = 0`, push initial hash, `result_ = {}`.
   - store `config_ = cfg` (existing Phase 3 member).
   - if `cfg.wtime > 0 || cfg.btime > 0` → `clock_.start(cfg.wtime, cfg.btime, cfg.winc, cfg.binc)`; else `clock_.stop()`.
   - `clock_.set_flag_callback([this](Color c){ handle_clock_flag(c); });`
   - `clock_.set_tick_callback([this](std::int64_t w, std::int64_t b){ emit_line(clock_json_from(w,b)); });` (tick callback throttled in Step 4 or here to ~5 Hz).
2. `set_position_fen(fen)`: on success also reset `last_move`, `result_ = {}`, `game_over_emitted_ = false`, `hash_len_ = 0`, push hash, restart clock with `config_` times (so test/loading a FEN starts a fresh clock).
3. `set_emitter(emit)`: lock `emit_mutex_`; `emit_ = std::move(emit)`. `emit_line(line)`: lock `emit_mutex_`; if `emit_` call it — this makes all emissions (game thread, WS handler thread, clock tick thread) safe.
4. `handle_clock_flag(flagged)`:
   - lock `result_mutex_`; if `result_.over` → return.
   - `result_.over = true; result_.result = (flagged == Color::White ? "0-1" : "1-0"); result_.reason = "flag";`
   - unlock; `emit_line(game_over_json())` (guarded by `game_over_emitted_.exchange(true)` so `game_over` is emitted exactly once across clock thread and game loop).
5. `play_move(m)` (extend Phase 3 version):
   - lock `result_mutex_`; if `result_.over` → return false. (Validate + `pos_.make_move` as in Phase 3 — keep legality validation against `movegen::generate_legal` first; on illegal → false, no state change.)
   - `Color mover = pos_.side_to_move` **before** `make_move` (make_move flips side).
   - `clock_.on_move(mover);` then `push_history();` then `check_end()`.
6. `check_end()`:
   - `poll_flag()` first (flag has priority — a flag that fired while a move was being made ends the game on time).
   - if `movegen::is_checkmate(pos_)`: winner = opposite(side_to_move) → result `"1-0"`/`"0-1"`, reason `"checkmate"`.
   - else if `movegen::is_stalemate(pos_)`: `"1/2"`, `"stalemate"`.
   - else if `pos_.halfmove_clock >= 100`: `"1/2"`, `"fifty-move rule"`.
   - else if repetition enabled (optional, recommended — cheap): count occurrences of `pos_.zobrist_hash` in `hash_history_[0..hash_len_)`; if `>= 3` → `"1/2"`, `"threefold repetition"`.
   - set `result_` under `result_mutex_` only if not already `over` (first terminal condition wins).
   - Insufficient material is **out of scope** (general.md); leave `"playing"`.
7. `poll_flag()`: `Color c; if (clock_.flagged() && !clock_flagged_consumed) …` — simplest: if `clock_.flagged()` and `!result_.over` → build the same result as `handle_clock_flag` (without re-emitting if already emitted). Because `handle_clock_flag` fires from the tick thread promptly, `poll_flag` mainly covers the race where the flag fired between ticks; both paths funnel through the `game_over_emitted_` guard.
8. `compute_limits()` (Phase 3, extended to read the clock):
   ```
   Color side = pos_.side_to_move;
   remaining = clock_.running() ? clock_.remaining_ms(side)
                                : (side == White ? config_.wtime : config_.btime);
   inc       = clock_.running() ? clock_.increment_ms(side)
                                : (side == White ? config_.winc : config_.binc);
   if (clock_.flagged() || remaining <= 0) return limits with movetime_ms = 0;   // caller skips search
   if (remaining > 0) {
       budget = remaining / 20 + inc * 3 / 4;
       budget = clamp(budget, 10, remaining / 2);   // never exceed half remaining => bot cannot self-flag
   } else {
       budget = config_.movetime_ms;                 // Phase 3 fallback (default 300)
   }
   limits.max_depth   = config_.bot_depth;
   limits.movetime_ms = budget;
   limits.wtime/btime/winc/binc = config_ values;   // carried for `go` parity
   ```
   This is the **only** clock read on the search path; `search.cpp` keeps computing its own `steady_clock` deadline from `limits.movetime_ms` — no clock-thread polling inside search.
9. `maybe_play_bot(on_info)` (extend Phase 3):
   - return false if `!is_bot_turn() || game_over()`.
   - run `Searcher` with `compute_limits()`; if `movetime_ms == 0` (flagged) → return false.
   - after `search()` returns: re-check `game_over()` (a flag may have fired during the search via `handle_clock_flag`); if over → return false **without applying** the move.
   - else `play_move(best_move)`.
10. `run_bot_bot(emit)`:
    ```
    set_emitter(emit);
    emit_line(state_json());
    while (!game_over() && !stop_requested()) {
        if (!maybe_play_bot([&](const SearchInfo& i){ emit_line(info_line(i)); })) break;
        emit_line("bestmove " + move_to_uci(last_move_));   // optional in bot_bot; harmless + useful for UI log
        emit_line(clock_json());
        emit_line(state_json());
    }
    if (game_over()) emit_line(game_over_json());           // exactly-once guarded by game_over_emitted_
    clock_.stop();                                          // freeze clocks at game end
    ```
    `run_bot_bot` runs on a dedicated game thread owned by the WS handler (Step 4), so the handler keeps reading frames (`stop` works between moves).
11. `state_json()`: copy Phase 1 contract (`type`,`fen`,`board[64]`,`side`,`orientation`,`status`,`last_move`) and fill `clocks`: when `clock_.running() || clock_.flagged()` → `{"w_ms":<n>,"b_ms":<n>}` (numbers from `remaining_ms`), else `{"w_ms":null,"b_ms":null}`. `status` = `result_.over ? result_.reason : "playing"`.
12. `clock_json()`: `{"type":"clock","w_ms":<w>,"b_ms":<b>}`; `game_over_json()`: `{"type":"game_over","result":"<r>","reason":"<reason>"}`.
13. `push_history()`: `if (hash_len_ < hash_history_.size()) hash_history_[hash_len_++] = pos_.zobrist_hash;` — fixed array, no heap.

### Step 3 — Search time-budget integration (`src/search.hpp/.cpp` — no code change expected)

**Files:** verify only. `SearchLimits` already carries `wtime/btime/winc/binc` (Phase 3). The Phase 4 contract:
- `GameSession::compute_limits()` derives `movetime_ms` from the clock (Step 2.8) and caps it at `remaining/2` so the bot's own flag is impossible from search overrun.
- `Searcher::search()` converts `movetime_ms` into a `steady_clock::now() + movetime_ms` deadline and checks it every ~2048 nodes (Phase 3) — **do not** add clock reads here. If `movetime_ms == 0` the game loop skips searching entirely (game is over).
- If a flag fires mid-search (clock callback), `handle_clock_flag` sets `result_`; `maybe_play_bot` discards the move. Search itself needs no change.

### Step 4 — Server: bot-vs-bot orchestration, clock events, game_over (`src/web_server.cpp`, `src/web_server.hpp`)

**Files:** `src/web_server.cpp` (modify), `src/web_server.hpp` (modify only if helpers need declaring).

1. **Thread-safe sender.** Introduce a small `struct WsSender { httplib::ws::WebSocket* ws; std::mutex m; void send(std::string_view line) { std::lock_guard lk(m); ws->send(line); } };` — **all** socket writes (handler thread, game thread, clock tick thread) go through it. cpp-httplib ws writes are not internally synchronized; this mutex is mandatory now that three threads emit.
2. **New client commands (UCI text lines):**
   - `newgame human [wtime <ms> btime <ms> winc <ms> binc <ms>]` — human(White) vs bot; optional clock params (default from Phase 3 config; if any time given, clocks start).
   - `newgame bot [wtime <ms> btime <ms> winc <ms> binc <ms>]` — bot vs bot; starts `run_bot_bot` on a new game thread.
   - `ucinewgame` — keep Phase 3 behavior: stop any running bot game (`session.request_stop()`, join thread), reset with current config.
   - `position startpos moves …` / `position fen <fen> moves …` — Phase 2/3 validation unchanged; **rejected with `info string busy` while a bot game thread is active** (avoids racing the game thread on the position); accepted otherwise.
   - `go wtime <ms> btime <ms> winc <ms> binc <ms> movetime <ms>` — Phase 3 manual search unchanged; if it carries clock params, also `(re)start` the clock via a new `GameSession::configure_clock(w,b,winc,binc)` helper.
   - `stop` — `session.request_stop()`; the bot loop exits between moves (a move in flight is bounded by its `movetime`); the loop then emits final `state` + `game_over` if terminal. Handler keeps reading frames because the loop runs on its own thread.
3. **Bot-game thread management (per connection):**
   - `std::thread bot_thread_;` + `std::atomic<bool> bot_running_{false};`
   - On `newgame bot`: if `bot_thread_.joinable()` → `session.request_stop(); bot_thread_.join();` then `session.new_game(cfg)` (clocks start inside `new_game`), `bot_running_ = true`, `bot_thread_ = std::thread([&, emit]{ session.run_bot_bot(emit); bot_running_ = false; });`
   - The `emit` lambda passed to `run_bot_bot` calls `sender.send(line)` (mutex-guarded).
   - On disconnect or `ucinewgame`: same stop+join. Join from the handler thread only (never from `on_tick_`/`on_flag_`).
4. **Clock event emission:**
   - `session.set_emitter([&sender](const std::string& line){ sender.send(line); });` — this is the sink used by the clock tick callback (`{"type":"clock",…}` ~5 Hz) **and** by `handle_clock_flag` (immediate `game_over`), independent of the game thread.
   - Additionally emit `clock_json()` after every applied move (in `run_bot_bot` and in the human-move path) and once on `newgame` (initial times).
   - Tick-callback throttle: in `Clock`, invoke `on_tick_` every 2nd tick (~5 Hz) to keep WS traffic modest; each emission is one small text frame.
5. **Human-vs-bot path (Phase 3 flow, extended):** after a human `position … moves …` applies a move, call the existing `run_bot_turn(ws, session)`; then `sender.send(session.clock_json())`. If the human's flag fires while the handler is idle waiting for the next frame, `handle_clock_flag` pushes `game_over` from the tick thread (this is why the emitter + send mutex exist).
6. **`game_over` emission is centralized:** only `GameSession::handle_clock_flag` and the end of `run_bot_bot` call `game_over_json()`, both guarded by `game_over_emitted_` — exactly one `game_over` frame per game. The server never builds it ad hoc.
7. **`state` on connect:** unchanged Phase 1 behavior; `clocks` now populated when a clock is running (e.g. after `newgame … wtime …`).

### Step 5 — Frontend: clock display + game-over overlay (`web/index.html`, `web/style.css`, `web/app.js`)

**Files:** `web/index.html` (modify), `web/style.css` (modify), `web/app.js` (modify). Vanilla JS/CSS only.

**`web/index.html`:**
- Add a player-bar area above/below the board with two clock elements: `<div id="clock-w" class="clock">--:--</div>` and `<div id="clock-b" class="clock">--:--</div>` (label them White/Black; mark the active one per `state.side` / `clock` messages).
- Add a hidden overlay: `<div id="game-over" class="overlay hidden"><div class="panel"><h2 id="go-result"></h2><p id="go-reason"></p><button id="btn-newgame">New game</button></div></div>`.
- Add small controls: a mode/newgame bar with buttons `New (human)` and `New (bot vs bot)` sending `newgame human` / `newgame bot` (time control fields optional; keep minimal — fixed default like 5+2 minutes can be sent as `newgame bot wtime 300000 btime 300000 winc 2000 binc 2000`).

**`web/style.css`:**
- `.clock { font-variant-numeric: tabular-nums; padding; border-radius; }`, `.clock.active { outline/highlight }`, `.clock.low { color: red; }` (toggle `low` when `< 20000` ms).
- `.overlay { position: fixed; inset: 0; background: rgba(0,0,0,.5); display: flex; center; }`, `.overlay.hidden { display: none; }`, `.panel { background: white; padding; border-radius; text-align: center; }`.
- Keep Phase 3 `.thinking` and Phase 2 `.legal-target` styles untouched.

**`web/app.js`:**
1. **Model:** add `clocks = {w_ms: null, b_ms: null}`; `activeSide = null`.
2. **Message handling (extend existing dispatcher):**
   - `{"type":"clock","w_ms":N,"b_ms":N}` → store; `renderClocks()`.
   - `{"type":"game_over","result":R,"reason":X}` → show overlay (`#go-result` = `R` mapped to "White wins"/"Black wins"/"Draw", `#go-reason` = X), set `gameOver = true`, lock input (`inputLocked = true`), stop the thinking indicator.
   - `{"type":"state",…}` → existing render; if `state.status !== "playing"` also treat as terminal (covers flag/mate even if overlay message raced); update `activeSide = state.side`; set `.active` class on the corresponding clock.
3. **`renderClocks()`:** format ms → `M:SS` normally, `M:SS.d` (tenths) when `< 20000`; write `textContent` of `#clock-w`/`#clock-b`; toggle `.low` (< 20 s) and `.active` (=== `state.side`) classes. Null clocks render `--:--` and are skipped.
4. **Newgame buttons:** `btn-newgame` and the mode buttons send the corresponding `newgame …` line over the WS; on send, hide the overlay and reset `gameOver = false` optimistically (authoritative state arrives via the next `state`).
5. **Input gating:** existing Phase 3 lock logic extended — `inputLocked = inputLocked || gameOver || state.status !== "playing"`; while `newgame bot` self-play runs, all `state.side` values are bot turns, so the existing `state.side !== humanColor` gate keeps the board read-only.
6. **Clocks null handling:** when `state.clocks` is null (no clock configured), keep `--:--` and don't claim a side is active.

### Step 6 — CMake wiring (`CMakeLists.txt`)

**Files:** `CMakeLists.txt` (modify).

1. Add `src/clock.cpp` to the `chessbot` executable (append to the Phase 2/3 list `src/main.cpp src/position.cpp src/movegen.cpp src/game.cpp src/web_server.cpp src/eval.cpp src/search.cpp`).
2. Threads: `find_package(Threads REQUIRED)` and `target_link_libraries(chessbot PRIVATE Threads::Threads ws2_32)` (guarded by `if(WIN32)` for `ws2_32` as today; MSVC/MinGW put `std::thread` in the CRT, but linking `Threads::Threads` is harmless and portable).
3. New tests:
   ```cmake
   add_executable(test_clock tests/test_clock.cpp src/clock.cpp)
   target_include_directories(test_clock PRIVATE src)
   add_test(NAME test_clock COMMAND test_clock)

   add_executable(test_game tests/test_game.cpp
       src/position.cpp src/movegen.cpp src/eval.cpp src/search.cpp src/game.cpp src/clock.cpp)
   target_include_directories(test_game PRIVATE src)
   add_test(NAME test_game COMMAND test_game)
   ```
   (If Phase 2 introduced a `chess_core` static library, link these against it instead of repeating sources.)
4. Keep all existing targets/tests (`test_main`, `test_bitboard`, `test_position`, `test_movegen`, `test_eval`, `test_search`) unchanged.

### Step 7 — Tests (`tests/test_clock.cpp`, `tests/test_game.cpp`)

**Files:** `tests/test_clock.cpp` (new), `tests/test_game.cpp` (new). Follow the `check()`/`failures` style of existing tests.

**`tests/test_clock.cpp`** (deterministic; uses `deduct()`):
1. **Flag fires at zero:** `start(500, 5000, 0, 0)`; `deduct(499)` → `!flagged()`; `deduct(1)` → `flagged()`, `flagged_color() == White`, `remaining_ms(White) == 0`.
2. **Only running side drains:** `start(1000, 1000, 0, 0)`; `deduct(300)` → `remaining_ms(White) == 700`, `remaining_ms(Black) == 1000`.
3. **Increment on move + side switch:** `start(1000, 1000, 500, 0)`; `on_move(White)` → `remaining_ms(White)` in `[1490, 1500]` (increment minus ~0 elapsed, tolerance for real time), `running_side() == Black`; then `deduct(400)` → `remaining_ms(Black) == 600`, White unchanged.
4. **No flag after stop:** `start(1000,1000,0,0)`; `stop()`; `deduct(5000)` → `!flagged()`; `running() == false`.
5. **Flag callback fires once:** register a counting callback; `start(300, 5000, 0, 0)`; `deduct(300)`; assert count == 1 and `flagged()`.
6. **Real-time smoke (optional, short):** `start(120, 5000, 0, 0)`; `std::this_thread::sleep_for(250ms)`; `flagged()` true — guards that the tick thread actually runs (keep sleep ≤ 300 ms for CI speed).
7. **Double start is safe:** `start()` twice; no crash, second call resets values.

**`tests/test_game.cpp`** (no sockets; drives `GameSession` directly):
1. **Mate-in-1 ends game:** config both `Bot`, `bot_depth = 2`, `movetime_ms = 100`, no clocks; `set_position_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1")`; `maybe_play_bot(noop)` → true; `game_over()` true; `result().result == "1-0"`, `reason == "checkmate"` (verify the FEN is really mate-in-1 `Ra8#` during implementation; swap to another verified mate FEN if needed — do not weaken the assertion).
2. **Stalemate detected immediately:** `set_position_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")` (Black to move, stalemated); `check_end()` → over, `"1/2"`, `"stalemate"`.
3. **Flag ends game on time:** `new_game(cfg)` with `wtime = 300, btime = 60000`; `session.clock().deduct(301)` (White to move is running) → `clock().flagged()`; `poll_flag()` → over, `result().result == "0-1"`, `reason == "flag"`; then `play_move(anyLegalMove)` returns false and the position is unchanged (compare `fen()`).
4. **game_over emitted once:** `handle_clock_flag` reachable via public path only — instead assert `game_over()` stays true and `check_end()`/`poll_flag()` called twice do not change `result()` (idempotent).
5. **Fifty-move rule (recommended):** construct/set a FEN with `halfmove_clock` at 99, make a quiet non-pawn non-capture move (e.g. a knight move) → `check_end()` → `"1/2"`, `"fifty-move rule"`.
6. **Threefold repetition (optional but recommended):** from startpos play `g1f3 g8f6 f3g1 f6g8` twice via `play_move`; after the second cycle `check_end()` → `"1/2"`, `"threefold repetition"` (relies on `push_history` + `hash_history_`; if the implementer skips repetition, delete this case and note it).
7. **Search leaves position intact:** run `maybe_play_bot` on a normal middlegame FEN and assert `pos hash()`/`side_to_move` changed only by exactly one legal move (compare against `movegen::generate_legal` membership of `last_move`).

## 2. Protocol additions (summary)

**Client → Server (WebSocket text frames):**
- `newgame human|bot [wtime <ms> btime <ms> winc <ms> binc <ms>]`
- `ucinewgame` (reset; stops a running bot game)
- `position startpos moves …` / `position fen <fen> moves …` (rejected while a bot game runs)
- `go [wtime … btime … winc … binc …] [movetime <ms>]` (Phase 3 manual search; clock params also (re)start clocks)
- `stop` (stops the bot-vs-bot loop between moves / flags the search request)

**Server → Client (text frames):**
- UCI: `uciok`, `readyok`, `info depth N score cp S nodes N pv …`, `bestmove <uci>`
- JSON extensions:
  - `{"type":"clock","w_ms":<int>,"b_ms":<int>}` — initial (newgame), after each move, and ~5 Hz while running
  - `{"type":"game_over","result":"1-0"|"0-1"|"1/2","reason":"checkmate"|"flag"|"stalemate"|"fifty-move rule"|"threefold repetition"}` — exactly once
  - `state` (Phase 1 contract; `clocks` numeric when a clock is active, `status` becomes the terminal reason when over)

## 3. Verification steps

### 3.1 Build and automated tests (required)
From repo root `D:\Documents\coding\cpp_projects\chessbot_agentic`:
```powershell
cmake -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```
Expected: `test_main`, `test_bitboard`, `test_position`, `test_movegen`, `test_eval`, `test_search`, **`test_clock`**, **`test_game`** all pass with no `FAIL` lines and exit code 0; perft reference counts unchanged.

### 3.2 Manual validation — clocks
1. Run `.\build\chessbot.exe --port 8080` from repo root; open `http://localhost:8080/`.
2. `newgame human wtime 60000 btime 60000 winc 2000 binc 2000` (or the UI button): both clocks show `1:00`; after White moves, White's clock jumps by ~+2 s (increment) and Black's starts counting down (~5 updates/s); `.active` highlight follows the side to move; `.low` (red) appears under 20 s.
3. **Flag fires at time:** `newgame human wtime 500 btime 60000 winc 0 binc 0`; do not move for ~1 s → `game_over` overlay appears with "Black wins" / reason `flag`; clocks freeze; board input locked.

### 3.3 Manual validation — bot vs bot
1. Click "New (bot vs bot)" (or send `newgame bot wtime 300000 btime 300000 winc 2000 binc 2000`): clocks count down for both sides, `info depth … score cp …` lines stream, `bestmove` lines appear, board updates move by move (`state.last_move`), status shows `thinking…`.
2. Let it run to a terminal position (or until a flag): verify the `game_over` overlay shows a result + reason consistent with the final board (mate/stalemate/flag) and that clocks stopped.
3. `stop` mid-game: the loop exits between moves (current search completes first, bounded by its movetime), a final `state` arrives, input unlocks, no further moves.
4. Verify in browser devtools WS frames: one JSON object per text frame; exactly one `game_over` frame per game; `clock` frames ~5 Hz.

### 3.4 Acceptance criteria (Phase 4)
- `chessclock::Clock` counts down per side with increment, ticks via `steady_clock` on a ~100 ms background thread, flags the side that reaches zero (atomic ms values; thread-safe start/stop/on_move/deduct).
- `GameSession` drives bot-vs-bot self-play (`run_bot_bot`) emitting `state`/`clock`/`info`/`bestmove`, and `game_over` exactly once on flag / checkmate / stalemate / fifty-move (/threefold).
- Search budget comes from the clock-derived `movetime` (capped at half the remaining time) and a `steady_clock` deadline — never by polling the clock thread.
- Frontend renders live countdowns (active/low styling) and a game-over overlay with result + reason; board input locks after game over.

## 4. Watch-outs specific to this codebase

- **Three emitting threads** (WS handler, bot game thread, clock tick thread): every socket write MUST go through one mutex-guarded sender (Step 4.1); `GameSession::emit_line` serializes emitter calls, and `handle_clock_flag` is the only clock-thread path that emits.
- **Join discipline:** `stop()`/`join()` of the clock or game thread must never run on the tick thread itself (a flag callback calling `stop()` would self-join → deadlock). `stop()` documents/assumes it is called from the handler thread; the tick callback only emits.
- **Flag vs search race:** search is capped at `remaining/2`, so a bot cannot self-flag; if a flag fires anyway (e.g. human flag during bot turn), `handle_clock_flag` wins and `maybe_play_bot` discards the computed move (re-check `game_over()` after `search()`).
- **`game_over` exactly once:** all emission funnels through `game_over_emitted_.exchange(true)`; the server must not send its own copy.
- **No heap in hot paths:** repetition history is a fixed `std::array<std::uint64_t, 2048>` member; `MoveList` unchanged; JSON strings are built per emitted line only (same policy as Phase 3).
- **Don't touch `make_move`/`unmake_move` or search internals:** Phase 4 only orchestrates them; existing Zobrist reversibility asserts/tests must stay green.
- **`std::atomic<Color>`/`std::atomic<bool>`** are fine (trivially copyable); keep `winc_`/`binc_` plain members written only in `start()` under `mutex_` and read via `increment_ms()` (immutable while running).
- **`newgame bot` while a bot game runs:** stop+join the old thread before resetting the session (Step 4.3); reject `position` commands while `bot_running_` to avoid racing the game thread.
- **Windows/MSVC:** keep `/W4` clean in our code (atomics/mutex/thread usage included); link `Threads::Threads` and existing `ws2_32`.