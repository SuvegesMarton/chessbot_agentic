# Chess Web UI — Implementation Plan

## Goal
Add a browser-based user interface for the UCI-style chess engine in this repo. The UI
renders the board now, and is structured to add legal-move highlighting, human-vs-bot,
and bot-vs-bot with clocks without rearchitecting.

## Decisions (resolved)
- **UI modality:** Web-based. C++ backend serves a single page; browser renders board.
- **Process model:** Single process. The `chessbot` executable gains a server mode that
  links the engine in-process (no IPC). A separate UCI executable can still be added later.
- **Engine scope:** Include the minimal engine pieces the UI needs — legal movegen (move
  highlighting + game-end detection), a basic search/eval (bot), and clock/time control.
- **Transport:** **WebSocket** carrying raw UCI text lines (static files served over HTTP;
  all game traffic over a single WebSocket connection). Chosen over HTTP + SSE for
  low-latency bidirectional client↔server messaging without polling. Requires a WebSocket-
  capable HTTP lib (cpp-httplib supports WebSocket).
- **Dependencies:** `cpp-httplib` (header-only) vendored/fetched for HTTP statics + WebSocket;
  **no JS framework** — vanilla HTML/CSS/JS so there is no Node toolchain.
- **Testing:** Extend `ctest` (movegen perft, protocol/state transitions). Per AGENTS.md,
  never edit perft reference counts to pass a test.

## Architecture

### Backend (C++)
```
src/
  main.cpp                 # arg parse: default = serve web; --uci = UCI REPL
  types.hpp                # (existing) add move notation helpers? keep separate
  bitboard.hpp             # (existing) add `attacked_by()` / `is_square_attacked`
  position.hpp/.cpp        # (existing) add piece_on(Square), board snapshot, legal helpers
  movegen.hpp/.cpp         # NEW: pseudo-legal + legal move generation, in-check, game end
  eval.hpp/.cpp            # NEW: material + tapered PST, mobility (minimal)
  search.hpp/.cpp          # NEW: iterative deepening negamax + alpha-beta + TT; time budget
  clock.hpp                # NEW: per-side clock, increment, flag detection (steady_clock)
  move.hpp                 # NEW: Move<->UCI string, MoveList (fixed capacity, no heap)
  game.hpp/.cpp            # NEW: GameSession = position + clocks + players + game loop
  web_server.hpp/.cpp      # NEW: cpp-httplib static file server + WebSocket channel; UCI text loop
web/
  index.html
  style.css
  app.js                   # render board, input, WebSocket client sending UCI text
tests/
  test_movegen.cpp         # perft reference positions
  test_protocol.cpp        # game session state transitions (human/bot/clocks)
```

### Frontend (browser)
- Renders an 8x8 grid; pieces drawn as Unicode glyphs (no image assets needed).
- Human move input: click source -> click target (drag optional). Promotion = small chooser.
- Sends UCI text lines over WebSocket (`position ... moves ...`, `go ...`, `stop`).
- Consumes UCI text lines (`uciok`, `readyok`, `bestmove ...`, `info ...`) plus non-standard
  JSON extensions for legal targets, clock ticks, and `game_over` status.
- Flips board orientation for black human.

### Protocol (UCI text over WebSocket)

Client -> Server (UCI text lines):
- `uci` → identify engine; server replies `uciok`
- `isready` → server replies `readyok`
- `ucinewgame` → reset to startpos
- `position startpos moves e2e4 e7e5` → set position from UCI move list
- `position fen <fen> moves ...` → set position from FEN
- `go wtime <ms> btime <ms> winc <ms> binc <ms> movetime <ms>` → start search
- `stop` → stop search

Server -> Client (UCI text lines):
- `uciok`, `readyok`
- `info depth N score cp <score> pv e2e4 e7e5 ...` → search progress / "thinking"
- `bestmove e2e4` → best move found

Server -> Client (non-standard JSON extensions over WebSocket, for UI state):
- `state`: `{fen, board:[64], side, orientation, clocks, status, last_move}`
- `legal`: `{square, targets:["e4","e3",...]}`  → legal destination squares for `square`
- `clock`: `{w_ms, b_ms}`
- `game_over`: `{result:"1-0|0-1|1/2", reason}`

`board` = 64-entry array, codes like `P,N,B,R,Q,K,p,n,...` or `null` per square.

## Phases

### Phase 0 — Foundations (unblocks everything)
1. `move.hpp`: `MoveList` (fixed `std::array`, no heap per AGENTS.md), `move_to_uci`,
   `parse_uci`.
2. `bitboard.hpp`: `is_square_attacked(pos, sq, byColor)` using existing attack tables.
3. `position.hpp/.cpp`: `piece_on(Square)` helper + `board_snapshot()` returning the 64-array.
4. Vendor `cpp-httplib` into `third_party/`; wire into `CMakeLists.txt` (WebSocket support).
5. `main.cpp`: parse args (`--port`, `--uci`); default starts `web_server`.
6. `web_server`: UCI text parser/loop over WebSocket (`position`, `go`, `stop`, `isready`, `uci`).

### Phase 1 — Board display (first deliverable)
1. `web_server`: serve `web/` statics; WebSocket endpoint `/ws`; emit `state` on connect.
2. `game` (minimal): holds a `Position`; tracks UCI position state.
3. Frontend: render board from `state.board`, correct orientation, last-move highlight.
4. Validate: load page, board matches FEN from multiple starting positions.

### Phase 2 — Legal move highlighting
1. `movegen`: generate legal moves; `in_check`, `is_checkmate`, `is_stalemate`.
2. `legal` extension: on square selection, server computes legal targets, replies with a
   `legal` message listing destination UCI squares; frontend highlights them.
3. `position`/`go` flow: client sends UCI moves; server validates against legal list,
   applies via `make_move`, rejects illegal moves.
4. Validate: perft on reference positions via `ctest` (do not alter reference counts).

### Phase 3 — Human vs bot
1. `eval`: material + tapered PST (+ small mobility).
2. `search`: iterative deepening negamax + alpha-beta + transposition table; time-budgeted.
3. `game`: on bot turn, send `go` with clock-derived time controls, receive `bestmove`,
   apply it, emit `state`/`move`.
4. Frontend: show "thinking…" on `info` lines, disable input during bot turn.
5. Validate: human can complete a game vs a depth-limited bot; illegal input rejected.

### Phase 4 — Clocks and bot-vs-bot
1. `clock`: per-side countdown with increment, `steady_clock`-driven tick (~100ms), flag.
2. `game`: game loop drives both sides; `mode:"bot_bot"` plays itself; emit `clock` events.
3. `game_over` on flag / mate / stalemate / draw rules (repetition optional).
4. Validate: flags fire at time; bot-vs-bot reaches `game_over`; clock display counts down.

## Risks / trade-offs
- WebSocket carries UCI text for engine moves plus non-standard JSON extensions for UI
  state (legal targets, clocks, game_over). UCI itself has no standard for those UI
  concerns, so they remain custom additions.
- Header-only `cpp-httplib` on Windows is fine but must be added to the build; confirm it
  compiles under MSVC `/W4` (including WebSocket handling).
- Movegen correctness is the backbone — invest in perft tests early (Phase 2) before bot.
- Search is time-boxed against the clock; a weak/1-ply bot is acceptable for Phase 3.

## Validation summary
- `cmake -B build && cmake --build build -j4`
- `ctest --test-dir build --output-on-failure` (add movegen perft + protocol tests)
- Manual: open page, play moves, verify highlights, play a bot, run bot-vs-bot with clock.

## Watch out for
- Consolidate `Move`/`Undo` (already in `position.hpp`) with `move.hpp` to avoid duplication; keep `MoveList`/UCI helpers in `move.hpp`.
- Fix `CMakeLists.txt`: `test_bitboard` is referenced in `add_test` but never built (`add_executable` missing `tests/test_bitboard.cpp`); add it in Phase 0.
- `cpp-httplib` WebSocket API requires a sufficiently recent version; pin a specific release in `third_party/` (MSVC `/W4` warnings-as-errors risk).
- Wire `perft` test harness (`test_movegen` executable) in CMake during Phase 2, not after.
- Clock thread safety: tick on a background thread feeding atomic ms values; search reads budget via `steady_clock` deadline, not the clock thread.
- `state` should include `legal` only on demand; avoid broadcasting full legal move sets on every connect.
- WebSocket framing: text frames, one JSON/UCI line per frame, to simplify parsing.

## Out of scope (for now)
- Networked multiplayer, accounts, persistence, PGN import/export, alternate time controls
  UI beyond basic, drag-and-drop polish, sound, themes, full draw-rule coverage.
