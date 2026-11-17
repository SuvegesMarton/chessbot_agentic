# Phase 3 — Human vs Bot (Execution Plan)

Source: `plans/general.md` (Phase 3, lines 105-111), `AGENTS.md`, `plans/phase_0.md`, `plans/phase_1.md`, `plans/phase_2.md`.
Scope: add a static evaluation (`eval`), a time-budgeted alpha-beta searcher with a transposition table and quiescence (`search`), wire the bot turn into `GameSession` + the WebSocket UCI loop so the bot replies to human moves, and update the frontend to show a "thinking…" indicator and lock input during the bot turn. No full clock countdown / bot-vs-bot / draw-rule coverage (those are Phase 4).

## 0. Baseline, assumptions, and constraints

- **Depends on Phase 2 deliverables (must exist first).** Phase 3 calls into the move generator and the session built in earlier phases. If any of these are missing, finish Phase 2 first (do not re-architect here):
  - `movegen::generate_legal(Position&, MoveList&)`, `movegen::generate_pseudo_legal(const Position&, MoveList&)`, `movegen::in_check(const Position&, Color)`, `movegen::is_checkmate(Position&)`, `movegen::is_stalemate(Position&)`, `movegen::perft(Position&, int)` (declared in `src/movegen.hpp`).
  - `Position::king_square(Color)`, `Position::in_check(Color)`, `Position::to_fen()` (Phase 2 helpers).
  - `GameSession` holding a `Position`, `orientation`, `last_move`, `status`, plus accessors used by `state` emission (`position()`, `fen()`, `side_to_move()`, `status()`, `last_move_uci()`), and a move-application path that validates against `generate_legal` before `make_move` (Phase 1/2).
  - `src/web_server.cpp` implementing the UCI-over-WebSocket loop (`uci`/`isready`/`ucinewgame`/`position`/`go`/`stop`) and `state`/`legal` emission, and `web/index.html` + `web/style.css` + `web/app.js` rendering the board from `state`.
- **Existing code reused as-is:** `src/types.hpp`, `src/bitboard.hpp` (attack tables, `rook_attacks`/`bishop_attacks`/`queen_attacks`, `knight_attacks`, `king_attacks`, `pawn_attacks`, `is_square_attacked`), `src/position.hpp/.cpp` (`Move`, `Undo`, reversible `make_move`/`unmake_move` with Zobrist), `src/move.hpp` (`MoveList` fixed `std::array`, `move_to_uci`, `parse_uci`).
- **AGENTS.md rules (must follow):**
  - C++20; non-MSVC flags `-O3 -march=native -Wall -Wextra`, MSVC `/O2 /W4`.
  - Always `cmake -B build && cmake --build build -j4` then `ctest --test-dir build --output-on-failure` after changes.
  - **Zero heap allocations** (`new`, `std::vector` growth, `std::string` construction) **inside `search` / `movegen` / `eval` hot paths.** Use fixed-size stack arrays (`std::array`, `MoveList`). The one exception allowed by this plan is the transposition table: allocate it **once** in the `Searcher` constructor (a one-time allocation, never per node) because a multi-megabyte `std::array` as a by-value member would blow the default 1 MB Windows stack.
  - Keep `make_move`/`unmake_move` strictly reversible and verified against Zobrist hash consistency in debug builds (the searcher calls them millions of times).
  - Never alter `perft` expected reference counts in `tests/`.
- **Style to match:** header/impl split like `position.hpp/.cpp` and `movegen.hpp/.cpp`; `namespace`-scoped free functions where natural (`eval::`, `search::`); tests are small `main()` programs using `static int failures` + `check(bool, const char*)` printing `FAIL: …` to `std::cerr` (see `tests/test_bitboard.cpp`); no comments unless clarifying a non-obvious invariant.

## 1. Ordered implementation steps

### Step 1 — Static evaluation (`src/eval.hpp`, `src/eval.cpp`)
**Files:** `src/eval.hpp` (create), `src/eval.cpp` (create).

**Header (`src/eval.hpp`):**
```cpp
#pragma once
#include "position.hpp"

namespace eval {
int evaluate(const Position& pos);  // centipawns, from side_to_move's perspective
}
```

**Design (`src/eval.cpp`):**
1. **Material values (centipawns):** `P=100, N=320, B=330, R=500, Q=900, K=0`. Kings are scored 0 (their safety comes from PST); checkmate is handled by search, not eval.
2. **Piece-square tables, tapered (midgame `mg` / endgame `eg`).** Tables are written in reading order (rank 8 first, a8..h8 then a7..h7 … a1..h1) exactly as in the classic "Simplified Evaluation Function" set below; index with a vertical flip:
   - White piece on square `s` (A1=0..H8=63): `table[s ^ 56]`.
   - Black piece on square `s`: `table[s]` (tables are left-right symmetric, so no file flip needed).
   - Add `sign = (color==White ? +1 : -1)` to the running `mg`/`eg` sums.
3. **Tables to embed** (as `inline constexpr std::array<int,64>`; values are opening/endgame guesses and are tunable):
   - `Pawn mg`: `{0,0,0,0,0,0,0,0, 50,50,50,50,50,50,50,50, 10,10,20,30,30,20,10,10, 5,5,10,25,25,10,5,5, 0,0,0,20,20,0,0,0, 5,-5,-10,0,0,-10,-5,5, 5,10,10,-20,-20,10,10,5, 0,0,0,0,0,0,0,0}`
   - `Pawn eg`: `{0,0,0,0,0,0,0,0, 80,80,80,80,80,80,80,80, 50,50,50,50,50,50,50,50, 30,30,30,30,30,30,30,30, 20,20,20,20,20,20,20,20, 10,10,10,10,10,10,10,10, 5,5,5,5,5,5,5,5, 0,0,0,0,0,0,0,0}`
   - `Knight mg`: `{-50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,0,10,15,15,10,0,-30, -30,5,15,20,20,15,5,-30, -30,0,15,20,20,15,0,-30, -30,5,10,15,15,10,5,-30, -40,-20,0,5,5,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50}`
   - `Knight eg`: `{-50,-40,-30,-30,-30,-30,-40,-50, -40,-20,0,0,0,0,-20,-40, -30,5,10,10,10,10,5,-30, -30,0,10,10,10,10,0,-30, -30,5,10,10,10,10,5,-30, -30,0,10,10,10,10,0,-30, -40,-20,0,0,0,0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50}`
   - `Bishop mg`: `{-20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,10,10,5,0,-10, -10,5,5,10,10,5,5,-10, -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20}`
   - `Bishop eg`: `{-20,-10,-10,-10,-10,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,10,10,10,10,0,-10, -10,0,10,10,10,10,0,-10, -10,0,10,10,10,10,0,-10, -10,10,10,10,10,10,10,-10, -10,5,0,0,0,0,5,-10, -20,-10,-10,-10,-10,-10,-10,-20}`
   - `Rook mg`: `{0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, -5,0,0,0,0,0,0,-5, 0,0,0,5,5,0,0,0}`
   - `Rook eg`: `{0,0,0,0,0,0,0,0, 5,10,10,10,10,10,10,5, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0}`
   - `Queen mg`: `{-20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5, 0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,5,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20}`
   - `Queen eg`: `{-20,-10,-10,-5,-5,-10,-10,-20, -10,0,0,0,0,0,0,-10, -10,0,5,5,5,5,0,-10, -5,0,5,5,5,5,0,-5, 0,0,5,5,5,5,0,-5, -10,5,5,5,5,5,0,-10, -10,0,0,0,0,0,0,-10, -20,-10,-10,-5,-5,-10,-10,-20}`
   - `King mg`: `{-30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10, 20,20,0,0,0,0,20,20, 20,30,10,0,0,10,30,20}`
   - `King eg`: `{-50,-40,-30,-20,-20,-30,-40,-50, -30,-20,-10,0,0,-10,-20,-30, -30,-10,20,30,30,20,-10,-30, -30,-10,30,40,40,30,-10,-30, -30,-10,30,40,40,30,-10,-30, -30,-10,20,30,30,20,-10,-30, -30,-30,0,0,0,0,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50}`
4. **Game phase** for tapering: `phase_weight = {0,1,1,2,4,0}` for `P,N,B,R,Q,K`; `phase = Σ weight[pt]` over all pieces, clamped to `[0,24]` (start position = 24). Then:
   `score_pst = (mg * phase + eg * (24 - phase)) / 24`.
5. **Small mobility bonus** (cheap bitboard mobility, no movegen): for each knight/bishop/rook/queen of each side, `mob += popcount(attacks(sq, occ) & ~own_occupancy)`, weighted small (e.g. `+2` cp per move for N/B, `+1` for R/Q, sign-adjusted). This is a handful of bitboard ops per piece; keep weight small so material/PST dominate.
6. **Combine** (White's perspective first, then flip for side to move):
   ```
   int mat = Σ sign*material[pt];
   int pst = tapered(...);
   int mob = mobility(...);
   int white_score = mat + pst + mob;
   return pos.side_to_move == Color::White ? white_score : -white_score;
   ```
   Return type is `int` (centipawns). No allocations; iterate bitboards with `lsb`/pop loops or `while (bb) { sq = square_of(bb); bb &= bb-1; ... }`.

### Step 2 — Search (`src/search.hpp`, `src/search.cpp`)
**Files:** `src/search.hpp` (create), `src/search.cpp` (create).

**Header (`src/search.hpp`):**
```cpp
#pragma once
#include <cstdint>
#include <functional>
#include "move.hpp"
#include "position.hpp"

struct SearchLimits {
    int max_depth = 64;
    std::int64_t movetime_ms = 0;          // 0 => no time limit (depth-bound only)
    std::int64_t wtime = 0, btime = 0, winc = 0, binc = 0;  // carried for Phase 4
};

struct SearchResult {
    Move best_move{Square::None, Square::None, PieceType::None};
    int score = 0;                          // cp, side-to-move perspective
    int depth = 0;                          // last fully completed depth
    std::uint64_t nodes = 0;
};

struct SearchInfo {                           // emitted on `info` lines
    int depth = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    Move pv_move{Square::None, Square::None, PieceType::None};  // first PV move
};

class Searcher {
public:
    explicit Searcher(Position& pos);
    SearchResult search(const SearchLimits& limits);
    void stop() { stopped_ = true; }
    void set_info_callback(std::function<void(const SearchInfo&)> cb) { on_info_ = std::move(cb); }
private:
    int negamax(int depth, int alpha, int beta, int ply);
    int quiesce(int alpha, int beta, int ply);
    void check_time();
    bool stopped_now();

    Position& pos_;
    std::vector<TTEntry> tt_;      // allocated once in ctor (see note below)
    std::array<std::array<Move,2>, 128> killers_{};   // per-ply killer moves
    std::array<std::array<Move,128>, 128> pv_{};      // triangular PV table
    std::uint64_t nodes_ = 0;
    std::uint64_t deadline_ = 0;   // steady_clock ns; 0 => none
    bool stopped_ = false;
    std::function<void(const SearchInfo&)> on_info_;
    SearchResult result_;
    // ...
};
```
`TTEntry` is defined in `search.cpp` (private to the implementation) — the header only needs it as the element type of `tt_`, so define `struct TTEntry;`-style or move the `tt_` member into a PIMPL-free but `search.cpp`-visible form. **Simplest compliant choice:** declare `Searcher` with a `std::vector<TTEntry>` where `TTEntry` is a nested `struct` declared in the header (fields are plain integers + `Move`), sized in the constructor.

**Implementation notes (`src/search.cpp`):**
1. **Transposition table (fixed size, one-time allocation).** Define:
   ```cpp
   struct TTEntry { std::uint64_t key = 0; std::int32_t score = 0; std::int16_t depth = -1; std::uint8_t flag = 0; Move best{}; };  // flag: 0 empty,1 exact,2 lower,3 upper
   ```
   Constructor: `tt_.assign(TT_SIZE, {});` with `TT_SIZE = 1<<20` (1,048,576 entries, ~16 bytes each ≈ 16 MB, heap-allocated **once**). Index = `key & (TT_SIZE-1)`; store the full 64-bit key and verify on probe (`entry.key == key`). On store, replace unconditionally for v1 (no aging/replacement scheme needed for Phase 3); set `depth`, `score`, `flag`, `best`.
2. **Iterative deepening.** `for (d = 1; d <= limits.max_depth; ++d) { search root at depth d; if (!stopped_) { save result_.best_move/score/depth; emit info; } else break; }`. Root search iterates legal moves (`movegen::generate_legal`), orders them (Step 2.4), calls `negamax(d-1, -beta, -alpha)` and tracks the best. Use a full-window root or a simple alpha update; keep it simple (no aspiration windows in v1).
3. **Negamax + alpha-beta** (`int negamax(int depth, int alpha, int beta, int ply)`):
   - `check_time()` every 2048 nodes; if past deadline set `stopped_` and unwind (return `0` / alpha).
   - TT probe: on `depth sufficient` hit, use stored score/flag to narrow `(alpha,beta)`; return on exact/lower/upper cutoffs. Order TT `best` move first.
   - Quiescence at `depth <= 0` (Step 2.5) instead of static eval.
   - `movegen::generate_legal(pos_, list)`. If `list.count == 0`: return `movegen::in_check(pos_, us) ? (-MATE + ply) : 0` (checkmate vs stalemate). (`MATE = 30000`.)
   - For each move: `Undo u = pos_.make_move(m);` recurse `-negamax(depth-1, -beta, -alpha, ply+1);` `pos_.unmake_move(u);`. In debug builds (`#ifndef NDEBUG`) assert `pos_.hash() == u.hash` after unmake to enforce Zobrist reversibility cheaply. Update `alpha`; break on `alpha >= beta`. Store killer on quiet beta-cutoffs. Store TT entry.
   - Handle `stopped_` propagation: if `stopped_` becomes true mid-loop, return immediately (the ID loop discards this unfinished depth).
4. **Move ordering.** Score each legal move: TT best move = huge; captures by MVV-LVA (`victim_value*16 - attacker_value`); killers next; rest 0. Sort with a small insertion sort over a local `std::array<ScoredMove,256>` (no heap). This is cheap and materially improves pruning.
5. **Quiescence** (`int quiesce(int alpha, int beta, int ply)`) for tactical stability:
   - `int stand = eval::evaluate(pos_);` if `stand >= beta` return `beta`; if `stand > alpha` `alpha = stand`.
   - `check_time()` every 2048 nodes.
   - Generate legal moves, keep only captures/promotions (v1 filters `generate_legal` output by whether the `to` square holds an enemy piece or the move is a promotion; a dedicated `generate_legal_captures` can be added later). Recurse `-quiesce(-beta,-alpha,ply+1)`. Update alpha/beta. Add a `ply`-based cap (e.g. stop at `ply >= 64` or a `qdepth` limit) to bound worst case.
6. **Time management.** At `search()` entry compute `deadline_` from `steady_clock::now()` + `limits.movetime_ms` (if `>0`); store `start`. `check_time()` compares `steady_clock::now()` to `deadline_`. If `movetime_ms == 0` and `max_depth` is finite, run depth-bound only. Never rely on the (Phase 4) clock thread; the deadline is computed locally.
7. **Result.** `search()` returns `result_` = last fully completed iteration (`best_move`, `score`, `depth`, `nodes`). If no iteration completed (e.g. movetime so small even depth 1 aborted), fall back to the first legal move from `generate_legal` at the root so the bot always returns something legal (or `best_move = {None}` if none legal — game already over).
8. **Info emission.** After each completed iteration, if `on_info_` is set, call `on_info_({d, score, nodes, pv_[0][0]})` — once per depth (not per node), so no hot-path allocation concern.

### Step 3 — Bot-turn wiring in `GameSession` (`src/game.hpp`, `src/game.cpp`)
**Files:** `src/game.hpp` (modify), `src/game.cpp` (modify). Keep Phase 1/2 fields/accessors intact; add:
1. **Player/config types:**
   ```cpp
   enum class PlayerType { Human, Bot };
   struct GameConfig {
       PlayerType white = PlayerType::Human;
       PlayerType black = PlayerType::Bot;
       int bot_depth = 6;                 // depth cap for the depth-limited bot
       std::int64_t movetime_ms = 300;    // fallback when no clock info
       std::int64_t wtime = 0, btime = 0, winc = 0, binc = 0;  // clock params (countdown is Phase 4)
   };
   ```
2. **New members/methods on `GameSession`:**
   - `void new_game(const GameConfig& cfg)` — reset to startpos (or keep current `reset_startpos`) and store `config_`.
   - `bool is_bot_turn() const` — `config_` player for `pos.side_to_move` is `Bot`.
   - `Color human_color() const` — the color whose `PlayerType == Human` (for frontend orientation / input lock).
   - `SearchLimits compute_limits() const` — derive time controls for the side to move:
     ```
     remaining = (side==White ? config_.wtime : config_.btime);
     inc       = (side==White ? config_.winc  : config_.binc);
     if (remaining > 0) { movetime = clamp(remaining/20 + inc*3/4, 10, remaining/2); }
     else               { movetime = config_.movetime_ms; }
     limits.max_depth = config_.bot_depth; limits.movetime_ms = movetime;
     limits.wtime/btime/winc/binc = config_...;  // carried for Phase 4
     ```
     (Full per-side countdown/flag is Phase 4; here we only *read* configured values and pick a per-move budget. If neither clock nor movetime is set, default `movetime_ms` to 300.)
   - `bool play_move(const Move& m)` — validate against `movegen::generate_legal` and apply via `pos_.make_move`, updating `last_move`/status exactly like the Phase 2 human path (reuse the existing validation helper). Returns `false` on illegal move (no state change). This is what both human and bot moves funnel through.
   - `bool maybe_play_bot(std::function<void(const SearchInfo&)> on_info)` — convenience wrapper: if `!is_bot_turn()` or game already over, return `false`; else construct a `Searcher` on `pos_`, set `on_info`, run `search(compute_limits())`, and if `best_move` is valid call `play_move(best_move)`; return whether a bot move was applied. **Note:** the actual socket emission of `info`/`bestmove` is driven by the `on_info` callback supplied by `web_server`, so `game.cpp` stays transport-agnostic.
   - `bool game_over() const` / `std::string status() const` — already present from Phase 2 (`is_checkmate`/`is_stalemate`); reuse so `maybe_play_bot` skips search when the game is over.

### Step 4 — Server: run the bot turn over the WebSocket (`src/web_server.cpp`, `src/web_server.hpp`)
**Files:** `src/web_server.cpp` (modify), `src/web_server.hpp` (modify only if new helpers need declaring).

**Changes:**
1. **Add a `run_bot_turn(ws, GameSession&)` helper** in the anonymous namespace of `web_server.cpp`:
   - If `!session.is_bot_turn()` or `session.game_over()` → return.
   - `Searcher searcher(session.position_ref());` (expose a `Position&` accessor on `GameSession`, e.g. `Position& position_ref()`; Phase 1 already exposes `position()` returning `const Position&` — add a non-const overload or make `Searcher` take `Position&`).
   - `searcher.set_info_callback([&ws](const SearchInfo& i){ send_info_line(ws, i); });`
   - `SearchResult r = searcher.search(session.compute_limits());`
   - If `r.best_move.from == Square::None` (no legal move) → emit `game_over`/status and return (game already finished).
   - `session.play_move(r.best_move);` (validates + applies + updates `last_move`).
   - `send_text(ws, "bestmove " + move_to_uci(r.best_move));`
   - `emit_state(ws, session);` (so `state.side`, `state.last_move`, `state.status` reflect the bot's move).
2. **Trigger points** (call `run_bot_turn` after the position reflects a completed human move):
   - After successfully applying an incoming `position startpos moves …` / `position fen … moves …` (Phase 2 move-application path).
   - After an incoming single-move command if the Phase 2 server supports one (e.g. `go`/`move`); otherwise rely on the `position … moves …` flow the frontend already uses.
   - On `go …` from the client (manual search): build `SearchLimits` from the command tokens (`movetime`, `wtime`, `btime`, `winc`, `binc`, `depth`) and run a search on the current position, streaming `info` and finishing with `bestmove` (do **not** auto-apply for a bare `go`; auto-apply only for the bot-turn path). This keeps the UCI `go`/`bestmove` contract usable for future bot-vs-bot (Phase 4).
   - `stop`: accept and no-op in Phase 3 (search runs synchronously bounded by `movetime` and completes before the handler reads the next frame; true async stop is a Phase 4 concern). Document this in code.
3. **Info line format** (`send_info_line`): `info depth <d> score cp <s> nodes <n> pv <first_pv_uci>`. This is standard UCI `info` text; the frontend parses it to show "thinking…". Keep one line per frame (text frame).
4. **State/move emission:** reuse the existing `emit_state` from Phase 1/2 (`type:"state"`, `fen`, `board[64]`, `side`, `orientation`, `clocks`, `status`, `last_move`). After the bot move, `state.last_move` is the bot's UCI move and `state.side` flips back to the human. No new JSON extension is required beyond optionally echoing the bot's move via the standard `bestmove` line (the frontend already consumes `bestmove`).
5. **Threading:** keep the per-connection handler synchronous for Phase 3; `Searcher`/`GameSession` are used only on that connection's thread. If the pinned cpp-httplib spawns threads per connection, guard shared `GameSession` with a `std::mutex` (short critical sections) as in Phase 1. Do not hold the mutex during the search (it can run for `movetime`); copy/lock only around state mutation and socket writes as the existing code does.

### Step 5 — Frontend: "thinking…" indicator + input lock (`web/app.js`, `web/index.html`, `web/style.css`)
**Files:** `web/app.js` (modify), `web/index.html` (modify), `web/style.css` (modify). Vanilla JS/CSS only (no framework/build).

**Changes in `web/app.js`:**
1. **Model additions:** track `humanColor` (from `state.orientation`/`state.side` and the known config, e.g. human is White by default), `thinking = false`, `inputLocked = false`.
2. **Handle `info` lines:** when a text frame starts with `info ` and contains `depth`/`score`, parse `depth` and `score cp`; set `thinking = true`, update the status bar text to e.g. `thinking… depth 4 (cp +35)`. Show a spinner/`…` via a CSS class on the status element.
3. **Handle `bestmove`:** on `bestmove <uci>` (or `bestmove 0000`), set `thinking = false`, clear the thinking indicator. Input is re-enabled when the next `state` arrives with `side` equal to the human color and `status == "playing"` (the server emits `state` right after `bestmove`).
4. **Input lock:** set `inputLocked = true` immediately after the human submits a move (optimistic) and whenever `state.side != humanColor`. While locked, ignore board clicks for both selection and move submission (both square-selection `legal` requests and move sends are gated). Re-enable on the `state` where it is the human's turn again.
5. **Board click gating:** wrap the existing selection/move handlers so they early-return when `inputLocked || thinking`. Keep Phase 2 legal-highlight behavior unchanged when unlocked.
6. **Status element:** ensure a `#status` (or equivalent) element exists in `web/index.html` for the thinking text; add a `.thinking` style (e.g. muted italic or animated dots) in `web/style.css`.

**`web/index.html`:** confirm/add a status element (e.g. `<div id="status">…</div>`) near the board if not already present from Phase 1; no structural change to the board grid.
**`web/style.css`:** add `.thinking { ... }` (visual cue) and any `.input-locked` cursor/overlay style (optional).

### Step 6 — CMake wiring (`CMakeLists.txt`)
**Files:** `CMakeLists.txt` (modify).
1. **Add new engine sources** to the `chessbot` executable (append to the existing source list that already contains `src/main.cpp src/position.cpp src/movegen.cpp src/game.cpp src/web_server.cpp` from Phase 2):
   ```cmake
   add_executable(chessbot
       src/main.cpp src/position.cpp src/movegen.cpp src/game.cpp src/web_server.cpp
       src/eval.cpp src/search.cpp)
   ```
2. **Keep include dirs** (`src` and `third_party`) and Windows `ws2_32` link as-is.
3. **Add test executables + registration** (Phase 2 already builds `test_movegen`; add eval/search tests that also need movegen):
   ```cmake
   add_executable(test_eval tests/test_eval.cpp src/position.cpp src/movegen.cpp src/eval.cpp)
   target_include_directories(test_eval PRIVATE src)
   add_test(NAME test_eval COMMAND test_eval)

   add_executable(test_search tests/test_search.cpp src/position.cpp src/movegen.cpp src/eval.cpp src/search.cpp)
   target_include_directories(test_search PRIVATE src)
   add_test(NAME test_search COMMAND test_search)
   ```
   (If Phase 2 introduced a `chess_core` static library, link these tests against it instead of repeating sources — match whatever structure Phase 2 chose.)
4. Do not weaken project warnings; resolve any new `-Wextra`/`/W4` warnings in our code (not third-party headers).

### Step 7 — Tests (`tests/test_eval.cpp`, `tests/test_search.cpp`)
**Files:** `tests/test_eval.cpp` (create), `tests/test_search.cpp` (create). Follow the `check()`/`failures` style of existing tests.

**`tests/test_eval.cpp`:**
1. **Symmetry / startpos ≈ 0:** startpos `eval::evaluate` is small (symmetric material & PST; assert `std::abs(score) <= 30`, since mobility is symmetric too).
2. **Material dominance:** FEN with White up a queen (e.g. remove Black's queen from a midgame FEN) → `evaluate` strongly positive (e.g. `> +700`).
3. **Side-to-move perspective:** same FEN but with `w`/`b` swapped in the FEN's side field → eval negates (assert `eval_b == -eval_w` within rounding tolerance).
4. **King PST sanity (optional):** a bare-kings + advanced-pawn endgame where the advanced pawn is far advanced → positive for the pawn owner.

**`tests/test_search.cpp`:**
1. **Returns a legal move:** startpos, `movetime 200`, `max_depth 4` → `best_move` is in `generate_legal` list.
2. **Mate in 1:** a mate-in-1 FEN (e.g. `"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"` → `Ra8#`; adjust to a verified mate-in-1 you confirm during implementation) → at depth ≥ 1 the returned move delivers mate and `score` is near `MATE` (≥ `MATE-100`).
3. **Mate in 2:** a known mate-in-2 FEN → with `max_depth ≥ 4` the search finds the forced mate line's first move (verify by making the move and confirming a mate-in-1 reply exists for the opponent's best defense).
4. **Time budget respected:** `movetime 100` returns in well under ~1 s and with `depth >= 1` on a normal middlegame FEN (loose bound; CI-safe).
5. **Terminal handling:** a stalemate FEN → `best_move.from == Square::None` (no legal moves); a checkmated FEN → same.
6. **Reversibility under search:** run a shallow search and assert the position's `hash()` and `side_to_move` are unchanged afterward (proves search's make/unmake is balanced), and in debug builds rely on the in-search `assert(pos_.hash() == u.hash)`.

> Keep mate FENs verified by hand/perft during implementation; do not weaken any existing test.

## 2. Files summary

| File | Action | Purpose |
|---|---|---|
| `src/eval.hpp` | create | `eval::evaluate(const Position&)` declaration |
| `src/eval.cpp` | create | material + tapered PST + small mobility, side-to-move score |
| `src/search.hpp` | create | `SearchLimits`, `SearchResult`, `SearchInfo`, `Searcher` class |
| `src/search.cpp` | create | iterative deepening negamax + alpha-beta + TT + quiescence + time budget |
| `src/game.hpp` | modify | `PlayerType`, `GameConfig`, `is_bot_turn`/`compute_limits`/`play_move`/`maybe_play_bot` |
| `src/game.cpp` | modify | bot-turn logic, clock-param time derivation, move application reuse |
| `src/web_server.cpp` | modify | `run_bot_turn`, `info`/`bestmove` emission, bot-turn trigger after human move, `go` search |
| `src/web_server.hpp` | modify (if needed) | helper declarations only |
| `web/app.js` | modify | parse `info`/`bestmove`, thinking indicator, input lock during bot turn |
| `web/index.html` | modify | ensure status element for thinking text |
| `web/style.css` | modify | `.thinking` style, optional input-locked cue |
| `tests/test_eval.cpp` | create | eval symmetry/material/perspective tests |
| `tests/test_search.cpp` | create | legal move, mate-in-1/2, time budget, terminal, reversibility tests |
| `CMakeLists.txt` | modify | add `eval.cpp`/`search.cpp` to `chessbot`; add `test_eval`/`test_search` exes + `add_test` |

## 3. Verification steps

### 3.1 Build and automated tests (required)
From repo root `D:\Documents\coding\cpp_projects\chessbot_agentic`:
```powershell
cmake -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```
Expected: `test_main`, `test_bitboard`, `test_position`, `test_movegen`, `test_eval`, `test_search` all pass with no `FAIL` lines and exit code 0. Perft reference counts are unchanged.

### 3.2 Manual validation — human vs bot
1. Start the server from repo root so `web/` resolves: `.\build\chessbot.exe` (or `--port 8080`).
2. Open `http://localhost:8080/`; confirm the initial `state` renders startpos and it is the human's turn (White).
3. Play a human move (e.g. `e2e4`) via the UI: confirm input locks immediately, the status shows `thinking…` with increasing `depth`/`score` from `info` lines, then a `bestmove` arrives, the bot's move appears on the board (`state.last_move`), and input unlocks.
4. **Complete game:** play a full game to mate/stalemate vs the depth-limited bot (default `bot_depth = 6`, `movetime = 300ms`); confirm the bot always replies with a legal move and the game reaches a terminal `status`.
5. **Depth-limited sanity:** temporarily set a low `bot_depth` (e.g. 2) and confirm the bot still moves legally but plays weaker (it should not hang or return an illegal move).
6. **Illegal input rejected:** attempt an illegal move (wrong side, blocked pawn, move into check, malformed UCI, missing promotion suffix) → server rejects it (Phase 2 validation), state is unchanged, no crash; a legal move then succeeds.
7. **`info`/`bestmove` protocol:** via browser devtools WS frames, confirm `info depth N score cp S …` lines are text frames and exactly one `bestmove <uci>` terminates each search.
8. **No regressions:** board rendering, legal highlighting (Phase 2), and `legal` responses still work when it is the human's turn.

### 3.3 Acceptance criteria (Phase 3)
- `eval` produces a side-to-move score combining material, tapered PST, and a small mobility term.
- `search` returns a legal best move under a time/depth budget, uses iterative deepening + alpha-beta + a fixed TT + quiescence, and leaves the position unchanged (hash/side) after searching.
- On the bot's turn the server issues a `go` with clock-derived (or `movetime`) time control, streams `info` lines, receives/produces `bestmove`, applies it, and emits `state`/move; the human can complete a game vs the depth-limited bot, and illegal input is rejected.
- Frontend shows "thinking…" on `info` lines and disables board input during the bot turn, re-enabling when `bestmove`/`state` returns control to the human.

## 4. Watch-outs specific to this codebase

- **Stack vs heap for the TT:** a `std::array<TTEntry, 1<<20>` as a by-value member is ~16 MB and will overflow the default 1 MB Windows stack. Allocate `tt_` once in the constructor (`std::vector<TTEntry>` sized to `TT_SIZE`, or `std::unique_ptr<TTEntry[]>`). This single up-front allocation is allowed; the rule forbids per-node allocations, which we avoid entirely.
- **No `std::string`/vector growth in hot loops:** move ordering uses a fixed `std::array<ScoredMove,256>`; PV uses `std::array<std::array<Move,128>,128>`; killers use `std::array<std::array<Move,2>,128>`. Only build the `bestmove`/`info` strings once per emitted line (outside the node loop).
- **Time check granularity:** checking `steady_clock` every node is too slow; check every ~2048 nodes. Ensure the deadline is `steady_clock`-based (monotonic), never `system_clock`.
- **Reversibility:** search calls `make_move`/`unmake_move` heavily; any drift corrupts the TT (Zobrist keys) and the board. Add a debug-only `assert(pos_.hash() == u.hash)` after unmake in `negamax` (guarded by `NDEBUG`) and keep the existing `make_unmake_reversibility` test green.
- **Quiescence cost:** filtering captures from `generate_legal` allocates nothing but is more work than a dedicated capture generator; acceptable for Phase 3 correctness. Cap quiescence depth (e.g. `ply >= 64` or a `qdepth` limit) to avoid pathological tails.
- **`stop` is a no-op in Phase 3:** the search runs synchronously on the connection thread and cannot be interrupted by an incoming `stop` frame until it returns. Do not pretend otherwise; full async stop belongs to Phase 4 (background search thread).
- **Clock values are inputs, not a countdown:** Phase 3 only *reads* `wtime/btime/winc/binc` from config/`go` to derive a per-move `movetime`. The live per-side countdown and flag detection are Phase 4 (`clock.hpp`); do not implement a tick thread here.
- **Frontend input lock must be driven by server truth:** re-enable input only on a `state` where `side == humanColor` and `status == "playing"`; do not re-enable merely on `bestmove`, because the `state` that follows carries the authoritative side/status.
- **`go` semantics:** a bare client `go` runs a search and emits `bestmove` but must not auto-apply (that is the bot-turn path). Keep the two paths distinct to avoid the human's position being mutated by a manual `go`.