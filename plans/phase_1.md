# Phase 1 — Board Display (Execution Plan)

Source: `plans/general.md` (Phase 1, lines 91-95) and Phase 0 foundations (lines 82-89).  
Scope: first deliverable — serve `web/`, open a WebSocket at `/ws`, emit `state` on connect, and render the board in the browser from `state.board` with correct orientation and last-move highlight.

## 0. Current state and constraints

- **Existing code:** `src/types.hpp`, `src/bitboard.hpp`, `src/position.hpp/.cpp`, `src/main.cpp`; tests `tests/test_main.cpp`, `tests/test_bitboard.cpp`, `tests/test_position.cpp`.
- **Missing for Phase 1:** `third_party/cpp-httplib/`, `web/`, `src/move.hpp`, `src/game.hpp/.cpp`, `src/web_server.hpp/.cpp`, `Position` helpers (`piece_on`, `board_snapshot`, `to_fen`), `main` argument parsing/server mode, CMake wiring for new targets/libs and the broken `test_bitboard` registration.
- **Build/test (AGENTS.md):** C++20, `cmake -B build && cmake --build build -j4`, `ctest --test-dir build --output-on-failure`. Non-MSVC flags `-O3 -march=native -Wall -Wextra`; MSVC `/O2 /W4`.
- **Performance/safety:** no heap allocations (`new`, `std::vector` resize, `std::string`) in `search`/`movegen` hot paths; keep `make_move`/`unmake_move` reversible and hash-consistent. Phase 1 has no search/movegen hot path, but new code must follow project style and keep data structures fixed-size where appropriate (`MoveList`/arrays) and avoid unnecessary allocations in per-message handling.
- **Git:** work on `feature/phase-1-board-display` (or similar), Conventional Commits, draft PR when ready. Do not commit unless explicitly requested.
- **Validation rule:** compile and run `ctest` after modifications; never alter perft reference counts (none added in Phase 1).

## 1. Ordered implementation steps

### Step 1 — Fix and prepare CMake/build baseline (Phase 0 item 4/5 prerequisite)
**Files:** `CMakeLists.txt`

**Changes:**
1. Keep existing flags/standard. Add `add_executable(test_bitboard tests/test_bitboard.cpp)` so the already-registered `add_test(NAME test_bitboard ...)` has a built target (matches `plans/general.md` watch-out).
2. Add include path for vendored headers: `include_directories` or per-target `target_include_directories(... PRIVATE src ${CMAKE_SOURCE_DIR}/third_party)`.
3. Define the engine/core source list variable (or repeat explicitly) to include new Phase 0/1 translation units as they are added: at minimum `src/position.cpp`, `src/game.cpp`, `src/web_server.cpp` (and later movegen/search/eval/clock files from future phases). For Phase 1, build `chessbot` from `src/main.cpp src/position.cpp src/game.cpp src/web_server.cpp`.
4. Link platform networking libs required by `cpp-httplib` on Windows (`ws2_32` / `crypt32` as needed by the pinned version) via `target_link_libraries(chessbot PRIVATE ...)` guarded by `if(WIN32)`.
5. Do **not** add `perft`/`test_movegen` in Phase 1 (wired in Phase 2 per plan).

**Rationale:** ensures a green baseline (`ctest` passes for existing tests) before adding server/frontend code.

### Step 2 — Vendor `cpp-httplib` with WebSocket support (Phase 0 item 4)
**Files:** `third_party/cpp-httplib/httplib.h` (new), optionally `third_party/cpp-httplib/VERSION.txt` or a short `README.md` note (new)

**Changes:**
1. Vendor a single-header release of `cpp-httplib` that includes WebSocket support (pin an explicit release/tag/commit in a version note). Choose a version known to compile with MSVC `/W4` where feasible; if warnings appear, scope suppressions narrowly in our CMake for the third-party include/target only (do not weaken project warnings globally).
2. Place header at `third_party/cpp-httplib/httplib.h` so includes use `#include "httplib.h"` or `#include <httplib.h>` depending on include dirs; prefer consistent project style (quoted or angle) decided once.
3. No runtime/package manager dependency; no Node toolchain.

**Rationale:** required for HTTP static serving and the `/ws` WebSocket endpoint per architecture.

### Step 3 — Minimal `move.hpp` types/utilities (Phase 0 item 1, minimal for Phase 1)
**Files:** `src/move.hpp` (new)

**Changes/content structure:**
1. Keep `Move`/`Undo` definitions consolidated with `position.hpp` to avoid duplication (plan watch-out): either
   - move `Move`/`Undo` declarations into `move.hpp` and have `position.hpp` `#include "move.hpp"`, or
   - define only UCI helpers and a fixed-capacity `MoveList` in `move.hpp` while leaving `Move` in `position.hpp` for now with a clear TODO to consolidate in Phase 2. **Preferred for Phase 1:** include `Move` in `move.hpp` and forward/include from `position.hpp` to establish the single source.
2. Define `MoveList` as a fixed-capacity container with no heap allocation, e.g. `struct MoveList { std::array<Move, 256> moves{}; std::size_t count = 0; void push(const Move&); };` (capacity sufficient for chess; exact API simple).
3. Declare/implement inline helpers:
   - `std::string move_to_uci(const Move&)` / or write into a fixed buffer; note: returning `std::string` allocates — acceptable outside hot paths (UI/protocol), but keep an optional non-allocating form if desired. For Phase 1 UI state only, a small helper is enough.
   - `bool parse_uci_move(std::string_view, Move& out)` parsing `e2e4`, promotions like `e7e8q` (case-insensitive promotion char), returning false on malformed input. Full legality validation is Phase 2; parsing only checks format/squares/promotion piece.
4. Keep header-only or split `.cpp` only if needed; simple helpers can be `inline` in header.

**Rationale:** establishes UCI move string handling and fixed `MoveList` per AGENTS/general plan without duplicating core structs.

### Step 4 — Extend `Position` with board/FEN helpers (Phase 0 item 3)
**Files:** `src/position.hpp`, `src/position.cpp`

**Changes:**
1. `PieceType`/`Color` helpers (if not already present): add `piece_on(Square sq)` returning an optional-like result the codebase can use (e.g. a struct `{Color c; PieceType t; bool empty;}` or `std::optional<Piece>`-equivalent without adding heavy deps; since `<optional>` is fine in non-hot code, choose the simplest clear form consistent with style). Implementation scans `pieces[color][type]` bitboards or derives from occupancy+piece bitboards.
2. `board_snapshot()` returning a 64-element fixed array representing squares A1..H8 in a documented, consistent order (recommend index = `file + rank*8` matching `make_square`/`Square` enum order A1=0..H8=63) with codes: `"P","N","B","R","Q","K"` for white, `"p","n","b","r","q","k"` for black, `null`/empty marker for empty. For JSON emission from C++, represent as `std::array<std::optional<char>,64>` or `std::array<std::string_view,64>`/sentinel; exact C++ type chosen to make JSON serialization straightforward in `web_server`.
3. `std::string to_fen() const` (or `void to_fen(char* buf, ...)`) to serialize current position into a FEN string (piece placement, side, castling, en-passant, halfmove, fullmove). This is required because `state.fen` must be emitted on connect and after updates.
4. (Optional but useful) `is_square_attacked` is a Phase 0/2 need; Phase 1 display does not require it, so defer unless trivial to add in `bitboard.hpp`/`position` now. Do not block Phase 1 on movegen.

**Verification detail:** `to_fen` round-trips conceptually with `set_fen` for supported fields; ensure castling/en-passant formatting matches UCI/FEN rules (`-` when none, correct square name like `e3`).

### Step 5 — Minimal `GameSession` (Phase 1 item 2)
**Files:** `src/game.hpp` (new), `src/game.cpp` (new)

**Changes/content structure:**
1. `class GameSession` (or `struct`) holding at minimum:
   - `Position pos;` (current board state)
   - orientation/mode metadata minimal for Phase 1 if needed (e.g. `Color orientation = Color::White` or derive from side-to-move policy; general plan states "Flips board orientation for black human" — for Phase 1 display, expose orientation in `state`, default white unless configured)
   - last-move tracking for highlight: `std::optional<Move>` / `Move last_move; bool has_last_move;` (fixed, no heap)
   - placeholders/stubs for future clocks/players/status are acceptable but keep Phase 1 minimal and explicit.
2. Constructor/init: `reset_startpos()` calling `pos.set_starting_position()` and clearing last-move/highlight state.
3. `bool set_position_fen(std::string_view fen)` and/or `bool apply_uci_moves(...)` minimal hooks for later phases; Phase 1 requires at least a valid initial position and ability to represent different starting FENs for validation (e.g. `reset_startpos()` and optionally `set_fen` path used by server/tests).
4. Accessors for `state` emission: `const Position& position() const`, `fen()`, `board_snapshot()`, `side_to_move()`, `orientation()`, `last_move_uci_or_null()`, `status()` (e.g. `"playing"` initially).

**Rationale:** separates UI/session state from raw `Position` and gives `web_server` a single place to read state for the JSON `state` message.

### Step 6 — `web_server`: HTTP statics + WebSocket `/ws` + UCI-over-WS loop (Phase 0 item 6, Phase 1 item 1)
**Files:** `src/web_server.hpp` (new), `src/web_server.cpp` (new)

**Changes/content structure:**
1. `class WebServer` (or free functions + context struct) configured with:
   - `std::string static_dir` (path to `web/`, default relative to working dir or resolved from executable location; document expected run-from-build behavior)
   - `int port` (default e.g. 8080)
   - reference/ownership of a `GameSession`
2. HTTP static serving via `cpp-httplib`:
   - mount/serve files under `web/` at `/` (e.g. `set_mount_point("/", static_dir)` or explicit handlers for `/`, `/index.html`, `/style.css`, `/app.js`)
   - correct `Content-Type` for `.html/.css/.js` (httplib usually infers; verify)
3. WebSocket endpoint `/ws`:
   - on connect: create/associate client context as needed; immediately emit a `state` JSON message for the current `GameSession` (text frame, one JSON object per frame as per watch-out)
   - on receive: parse incoming text frames as UCI text lines/commands; handle minimal set needed now and safe no-ops for others:
     - `uci` -> respond `id name chessbot`, `id author ...` (optional minimal) and `uciok`
     - `isready` -> respond `readyok`
     - `ucinewgame` -> reset session to startpos, emit updated `state`
     - `position startpos [moves ...]` / `position fen <fen...> [moves ...]` -> for Phase 1, support setting startpos or FEN and track/store the position (applying move list can be stubbed if full move application depends on Phase 2 legality/move parsing integration; but plan says "tracks UCI position state" — implement best-effort: parse and, if moves can be applied with existing `make_move` + UCI parsing, apply sequentially; if a move cannot be resolved without movegen, respond with an error/log and keep prior state or set position-only). Document the Phase 1 limitation clearly in code comments/plan and ensure board display still works for FEN/startpos.
     - `go ...`, `stop` -> accept and ignore/no-op for Phase 1 (or reply minimally) without crashing
   - on disconnect: clean up client bookkeeping.
4. Outgoing message helpers:
   - `send_text(ws, std::string_view line)` (UCI line) and `send_json(ws, std::string_view json)` or build JSON strings carefully.
   - `emit_state(...)` builds and sends the `state` extension JSON described below.
5. Threading/lifetime: run server on main thread via `listen`/`listen_after_bind` or a simple blocking call from `main` in server mode; ensure `GameSession` access is safe if httplib uses threads per connection (check pinned httplib behavior) — if multi-threaded, protect session with a mutex for Phase 1 simplicity; keep critical sections short and avoid allocations in hot search paths (none yet).
6. Logging: minimal `std::cerr`/`std::cout` diagnostics acceptable; avoid noisy output on normal operation.

**Rationale:** satisfies Phase 1 item 1 and provides the UCI-over-WebSocket transport foundation for later phases.

### Step 7 — `main.cpp` argument parsing and server mode (Phase 0 item 5)
**Files:** `src/main.cpp`

**Changes:**
1. Parse args:
   - `--port <n>` (default 8080)
   - `--uci` to run UCI REPL mode on stdin/stdout (preserve/implement minimal existing behavior if needed later; for now can print `ready` or enter a simple loop compatible with future UCI work — but do not break Phase 1 default)
   - default (no `--uci`): start `web_server` serving `web/` and listening for WebSocket connections.
2. Resolve web static directory: try relative `web` from current working directory and/or alongside executable as fallback; fail with clear error if not found.
3. Construct `GameSession`, set startpos, construct/start `WebServer`.
4. Keep includes minimal (`<iostream>`, `<string>`, `<cstring>`/parsing helpers, project headers).

**Rationale:** single-process model with default web UI mode per architecture.

### Step 8 — Frontend: HTML/CSS/JS board rendering (Phase 1 item 3)
**Files:** `web/index.html` (new), `web/style.css` (new), `web/app.js` (new)

**`web/index.html` content structure:**
- `<!doctype html>`, `<html lang="en">`, `<meta charset="utf-8">`, viewport meta.
- `<title>Chessbot</title>`, link `style.css`.
- Minimal layout: header/status bar (`#status`, side to move, connection state), board container (`#board` as 8x8 grid), optional info area for FEN/debug text.
- Script `app.js` loaded at end of body (or `defer`).
- No external CDN/framework dependencies.

**`web/style.css` content structure:**
- Board as CSS grid: 8 columns × 8 rows, fixed square sizing (responsive-friendly within reason), border.
- Light/dark square colors (classic checkered pattern via classes or `nth-child` logic consistent with rank/file mapping).
- Piece styling: centered Unicode glyphs, large font-size, `user-select: none`.
- `.highlight-last-move` (or similar) overlay/background for origin and destination squares of `last_move`.
- Orientation support: either render squares in DOM order based on orientation (flip ranks/files in JS) or use CSS transform/class; prefer JS-driven square order for clarity.
- Status/thinking placeholders styled simply (thinking used later).

**`web/app.js` content structure:**
1. **State/model:** hold latest `state` object (`fen`, `board[64]`, `side`, `orientation`, `clocks`, `status`, `last_move`).
2. **WebSocket client:**
   - connect to `ws://<host>/ws` (or `wss` if https; for local dev `ws` is fine) on load.
   - on open: optionally send `uci`/`isready` if useful for debugging, but not required because server emits `state` on connect; keep simple.
   - on message: parse text frame; if JSON parse succeeds and message represents `state` (e.g. has `fen`/`board` fields or a `type:"state"` discriminator — pick one consistent format and document it), update model and re-render; if line-based UCI (`info`, `bestmove`, etc.), update status/log area as appropriate (minimal in Phase 1).
   - on close/error: show disconnected status and optional reconnect/backoff (simple).
3. **Board rendering:**
   - Map `state.board` (64 entries, index 0=A1..63=H8 as defined by backend contract) to the 8x8 grid in correct visual orientation:
     - `orientation: "white"` (or white side): rank 8 at top to rank 1 at bottom, files a-h left to right.
     - `orientation: "black"`: flipped.
   - Unicode glyph mapping for piece codes: e.g. `K` ♔, `Q` ♕, `R` ♖, `B` ♗, `N` ♘, `P` ♙, `k` ♚, `q` ♛, `r` ♜, `b` ♝, `n` ♞, `p` ♟; `null`/empty -> empty square.
   - Apply `.highlight-last-move` to `last_move` origin/destination squares when present (parse UCI squares like `e2e4` to indices).
   - Preserve square coloring independent of orientation.
4. **Interaction (minimal for Phase 1):** no legal-move highlighting or move submission required yet; clicks may be no-op or only select visually later. Do not send `go/position` moves except what is needed for validation tooling if any.
5. **Utilities:** `squareNameToIndex("e2")`, index-to-coords helpers, safe JSON/text parsing.

**Rationale:** vanilla HTML/CSS/JS with no Node toolchain; renders directly from `state.board`.

### Step 9 — `state` JSON message format (contract)
**Emitted by server:** on WebSocket connect, and after any position/state change handled in Phase 1 (e.g. `ucinewgame`, `position ...`).

**Message framing:** one JSON object per WebSocket text frame.

**Recommended shape (explicit, UI-friendly):**
```json
{
  "type": "state",
  "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "board": ["R","N","B","R","Q","K","B","N","P","P","P","P","P","P","P","P",null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,null,"p","p","p","p","p","p","p","p","r","n","b","r","q","k","b","n"],
  "side": "w",
  "orientation": "white",
  "clocks": {"w_ms": null, "b_ms": null},
  "status": "playing",
  "last_move": null
}
```

**Field definitions:**
- `type`: `"state"` (discriminator so frontend can distinguish from future JSON extensions like `legal`, `clock`, `game_over`).
- `fen`: full FEN string of current position.
- `board`: array of length 64, index `sq = file + rank*8` with `A1=0`, `B1=1`, ..., `H1=7`, `A2=8`, ..., `H8=63`. Values: `"P","N","B","R","Q","K","p","n","b","r","q","k"` or `null` for empty.
- `side`: `"w"` or `"b"` (side to move).
- `orientation`: `"white"` or `"black"` for board flip (default `"white"` in Phase 1).
- `clocks`: `{ "w_ms": <number|null>, "b_ms": <number|null> }` (null/omitted values acceptable in Phase 1; clocks implemented Phase 4).
- `status`: string such as `"playing"` now; later `"checkmate"`, `"stalemate"`, `"flag"`, etc.
- `last_move`: `null` or UCI string like `"e2e4"` / `"e7e8q"` representing the most recently applied move (for highlight).

**Notes:** keep JSON compact and stable; avoid sending `legal` moves in `state` (per watch-out). Include all keys even when null for simpler frontend handling.

## 2. File-by-file summary

| File | Action | Purpose |
|---|---|---|
| `CMakeLists.txt` | modify | add `test_bitboard` exe, include dirs, link httplib deps, add `game.cpp`/`web_server.cpp` to `chessbot` |
| `third_party/cpp-httplib/httplib.h` (+ version note) | create | vendored HTTP/WebSocket library (pinned release) |
| `src/move.hpp` | create | `Move`/`Undo` consolidation point, fixed `MoveList`, `move_to_uci`/`parse_uci_move` helpers |
| `src/position.hpp` | modify | include/use `move.hpp` types as needed; declare `piece_on`, `board_snapshot`, `to_fen` |
| `src/position.cpp` | modify | implement `piece_on`, `board_snapshot`, `to_fen` |
| `src/game.hpp` | create | `GameSession` holding `Position`, orientation, last move, status accessors |
| `src/game.cpp` | create | session init/reset and state helpers |
| `src/web_server.hpp` | create | server interface/config |
| `src/web_server.cpp` | create | static file serving, `/ws` endpoint, UCI parse loop, `state` emission |
| `src/main.cpp` | modify | `--port`, `--uci`, default web server startup |
| `web/index.html` | create | page shell and board container |
| `web/style.css` | create | 8x8 grid, piece/square styling, last-move highlight, orientation layout |
| `web/app.js` | create | WebSocket client, `state` parsing, board rendering/highlight |
| `plans/phase_1.md` | create | this plan (deliverable) |

## 3. Verification steps

### 3.1 Build and automated tests
From repository root (`D:\Documents\coding\cpp_projects\chessbot_agentic`):
1. Configure/build: `cmake -B build && cmake --build build -j4`
2. Run tests: `ctest --test-dir build --output-on-failure`
3. Expected: existing tests pass (`test_main`, `test_bitboard`, `test_position`). No new perft tests added in Phase 1.

### 3.2 Manual validation — board display
1. Start server: run `build\chessbot.exe` (or platform equivalent) from repo root so `web/` is discoverable; optionally `--port 8080`.
2. Open `http://localhost:8080/` in a browser.
3. **On-connect state:** confirm WebSocket connects to `/ws` and the initial `state` renders the standard start position (ranks 1-2 white pieces, ranks 7-8 black pieces, correct colors/squares). Verify `side` is white to move and `orientation` is white by default.
4. **FEN correctness:** compare rendered board against `state.fen` shown in UI/debug or via browser devtools WS frames. Cross-check piece placement for:
   - startpos FEN: `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1`
   - a midgame FEN (e.g. after `1.e4 e5 2.Nf3 Nc6`): `r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3` loaded via `position fen ...` if supported, or by initializing session with that FEN for test
   - an asymmetric/endgame FEN with castling/en-passant differences if feasible (e.g. `8/8/8/8/8/8/4k3/4K3 w - - 0 1`) to ensure empty squares and king placement are correct.
5. **Orientation:** if orientation can be set (config/test hook), verify black orientation flips ranks/files visually while square colors remain correct.
6. **Last-move highlight:** when `last_move` is non-null in `state` (e.g. after a `position startpos moves e2e4` flow if implemented), confirm origin and destination squares are highlighted; when null on initial connect, no highlight.
7. **Stability:** reload page multiple times; each connect receives a fresh/consistent `state`. No server crashes on connect/disconnect.

### 3.3 Acceptance criteria (Phase 1)
- `web/` statics served; `/ws` accepts connections and emits `state` on connect.
- `GameSession` owns a `Position` and exposes UCI position state needed for `state`.
- Frontend renders `state.board` as an 8x8 board with Unicode pieces, correct orientation, and last-move highlight.
- Page load shows a board matching the session FEN across multiple starting positions tested manually.

## 4. Notes and deferrals
- **Move application over UCI:** full legal validation and reliable `position ... moves ...` sequencing depend on Phase 2 movegen/parsing integration; Phase 1 must at least support startpos/FEN display and be structured so move application can be completed without rearchitecting.
- **`is_square_attacked` / movegen:** defer to Phase 2 except any tiny helper strictly required now (none for display).
- **Clocks/search/bot:** stubs or null fields only in Phase 1; real behavior in Phases 3-4.
- **Security/robustness:** treat incoming WS data as untrusted input (bounds checks, parse failures -> ignore/log, no crashes). No secrets involved.
- **Consolidation:** resolve `Move`/`Undo` duplication early (`move.hpp` vs `position.hpp`) to match the plan's guidance and reduce churn in Phase 2. 
