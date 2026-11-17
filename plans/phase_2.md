# Phase 2 — Legal Move Highlighting (Execution Plan)

Source: `plans/general.md` (Phase 2, lines 97-103), `AGENTS.md`, `plans/phase_0.md`, `plans/phase_1.md`.  
Scope: implement legal move generation and game-state queries (`in_check`, `is_checkmate`, `is_stalemate`), perft validation harness, server-side legal-target replies for UI highlighting, and strict UCI-move validation/application over the existing WebSocket flow. No search/eval/clock/bot work (Phases 3-4).

## 0. Baseline, assumptions, and constraints

- **Current repo state (observed):** `src/move.hpp` provides `MoveList` + `move_to_uci`/`parse_uci`; `Move`/`Undo` live in `src/position.hpp`; `src/bitboard.hpp` already provides `is_square_attacked(const Position&, Square, Color)` and attack tables/sliders; `Position` has `piece_on`/`board_snapshot`, reversible `make_move`/`unmake_move` with Zobrist updates; `src/web_server.cpp` handles minimal `uci/isready/ucinewgame/position/go/stop` and applies parsed moves without legality checks; `CMakeLists.txt` builds `chessbot` and tests (`test_main`, `test_bitboard`, `test_position`) but has no perft/movegen target and no `web/` frontend wired in this snapshot.
- **Phase 1 dependency:** UI highlighting requires a working frontend/WebSocket client and a `state` emission path. If `web/` assets or `state` JSON emission are incomplete, include the minimal Phase-1-completion tasks in this phase only where required to exercise legal highlighting (clearly marked), without rearchitecting.
- **AGENTS.md rules (must follow):**
  - C++20, `-O3 -march=native -Wall -Wextra` (MSVC `/O2 /W4`).
  - Always `cmake -B build && cmake --build build -j4` and `ctest --test-dir build --output-on-failure` after modifying code.
  - **Zero heap allocations** (`new`, `std::vector` resizing, `std::string`) inside `search` or `movegen` hot paths; use fixed-size stack arrays (`std::array` or custom `MoveList`).
  - Keep `make_move` / `unmake_move` strictly reversible and verified against Zobrist hash consistency assertions in debug builds.
  - **Never alter `perft` expected reference counts in `tests/` to make a failing test pass.**
- **Style/patterns to match:** header/impl split like `position.hpp/.cpp`; bit helpers and attack tables in `bitboard.hpp`; `MoveList` in `move.hpp`; tests as small `main()` programs using `check()`/`failures` counters and `std::cerr` on failure (see `tests/test_bitboard.cpp`, `tests/test_position.cpp`); no comments added unless necessary.

## 1. Ordered implementation steps

### Step 1 — Consolidate move types/helpers (prerequisite hygiene)
**Files:** `src/move.hpp`, `src/position.hpp`, `src/position.cpp`, any includers as needed.

**Changes:**
1. Resolve `Move`/`Undo` duplication per general plan: keep a single definition (preferred: define `Move`/`Undo` in `src/move.hpp` and have `src/position.hpp` include `move.hpp`, or keep in `position.hpp` and make `move.hpp` include/use it consistently) so `MoveList`, `move_to_uci`, `parse_uci`, `Position::make_move/unmake_move`, and movegen all share one type.
2. Ensure `MoveList` remains fixed-capacity (`std::array<Move, 256>` or similar) with no heap allocations; `push` must be bounds-safe (current pattern is acceptable). Keep `move_to_uci`/`parse_uci` as non-hot conversion helpers (allocations allowed only outside hot paths).
3. Add/confirm a lightweight invalid-move sentinel convention used by parsing/validation (e.g. `from == Square::None || to == Square::None`) so callers can reject malformed UCI without exceptions.
4. Do not change observable behavior of existing tests except to fix includes/build after consolidation.

**Rationale:** movegen, perft, and protocol validation all pass `Move`/`MoveList` across boundaries; one canonical definition avoids ABI/layout mismatches.

### Step 2 — Add position/query helpers required by movegen
**Files:** `src/position.hpp`, `src/position.cpp` (and optionally `src/bitboard.hpp` only if a non-invasive helper is needed).

**Changes/content:**
1. Add `Square king_square(Color c) const` (or equivalent) by locating the king bit in `pieces[c][King]` (use `lsb`/`square_of` from `bitboard.hpp`). This is required for legality filtering and `in_check`.
2. Add `bool in_check(Color c) const`:
   - `king_sq = king_square(c)`; if none, define behavior defensively but positions in tests are legal.
   - return `is_square_attacked(*this, king_sq, opposite(c))`.
3. (Recommended for debug correctness) keep/strengthen Zobrist/hash reversibility checks in debug builds inside `make_move`/`unmake_move` or via asserts in tests/perft driver: after `make_move`+`unmake_move` from a known hash, `zobrist_hash` must equal the saved hash and recomputed hash (if a recompute helper exists or is added for debug-only use). Any debug-only recomputation must not run in hot release paths.
4. Ensure `piece_on` and occupancy/bitboards remain consistent after special moves (castling rook move, en-passant capture removal square, promotion replacement) — verify against existing `make_unmake_reversibility` and extend if needed (see Step 7).

**Rationale:** legal generation is "generate pseudo-legal, make, reject if own king attacked, unmake"; `in_check`/king lookup are the core predicates.

### Step 3 — Implement move generation API (`src/movegen.hpp/.cpp`)
**Files:** `src/movegen.hpp` (new), `src/movegen.cpp` (new), `CMakeLists.txt` (compile/link movegen into targets that need it).

**Public API (proposed, match project style):**
```cpp
#pragma once
#include "move.hpp"
#include "position.hpp"

namespace movegen {

void generate_pseudo_legal(const Position& pos, MoveList& out);
void generate_legal(Position& pos, MoveList& out);

bool in_check(const Position& pos, Color c);
bool is_checkmate(Position& pos);   // side-to-move checkmated
bool is_stalemate(Position& pos);   // side-to-move stalemated

std::uint64_t perft(Position& pos, int depth);

} // namespace movegen
```
(Exact namespace/class form may vary; keep signatures simple and header declarations matched by `.cpp` definitions. `generate_legal`/`is_checkmate`/`is_stalemate` take `Position&` because legality testing performs make/unmake.)

**Pseudo-legal generation (`generate_pseudo_legal`) — design details:**
- Clear `out.count = 0` at entry (or require caller to); append only, never allocate.
- Determine `us = pos.side_to_move`, `them = opposite(us)`, own/occupancy bitboards from `pos.occupancy` and `pos.all_occupancy`.
- **Pawns:** iterate pawn bitboard with `lsb`/pop loop (no heap). For white (rank increases): single push to `r+1` if empty; double push from rank 2 to rank 4 if both empty; captures to `r+1,f±1` if occupied by enemy. For black mirror (rank decreases, double from rank 7). Promotions when reaching rank 8 (white) / rank 1 (black): generate 4 moves with `promotion = Queen/Rook/Bishop/Knight`. En passant: if `pos.en_passant != Square::None` and a pawn attacks `ep` square per pawn-attack rules, generate capture to `ep` (captured pawn square handled by `Position::make_move`; move is pseudo-legal here, legality filter will reject exposed-king cases).
- **Knights/Kings:** use `knight_attacks[sq]` / `king_attacks[sq]` from `bitboard.hpp`, exclude own occupancy (`& ~pos.occupancy[us]`).
- **Sliding pieces (bishops/rooks/queens):** use existing `bishop_attacks`, `rook_attacks`, `queen_attacks` with `pos.all_occupancy`, exclude own occupancy. These are on-the-fly ray functions in `bitboard.hpp` (no heap, no tables to allocate) — acceptable for correctness-first Phase 2.
- **Castling (pseudo-legal conditions):** generate king-two-square moves only when:
  - side-to-move not in check,
  - relevant castling right present (`KQkq` bits consistent with `Position` encoding),
  - squares between king and rook are empty,
  - king does not pass through or end on an attacked square (use `is_square_attacked` on transit and destination squares; rook-square occupancy/attacks handled by empty/attack checks). Rook presence on corner is implied by legal positions/castling rights; if desired, defensively verify rook bit exists.
- Do not filter moves that leave king in check here except castling’s through-check rule is intrinsic to castling legality; all other king-safety filtering happens in legal generation.

**Legal filtering (`generate_legal`):**
- Generate pseudo-legal moves into a local or provided buffer, then for each move:
  - `undo = pos.make_move(m);`
  - if king of `us` (side that moved) is **not** attacked by `them` (`!is_square_attacked(pos, king_square(us), them)`), keep the move in `out`.
  - `pos.unmake_move(undo);`
- Use stack/local `MoveList` for pseudo-legal candidates to avoid heap and to allow `out` to be filled with only legal moves. This is the canonical legality test; it correctly handles pins, en-passant-discovered-check, promotions, and castling.

**Game-state queries:**
- `in_check(pos, c)`: as in Step 2 (can delegate to `Position::in_check` or inline same logic).
- `is_checkmate(pos)`: `in_check(pos, pos.side_to_move)` is true AND `generate_legal` yields `count == 0`.
- `is_stalemate(pos)`: `in_check(pos, pos.side_to_move)` is false AND `generate_legal` yields `count == 0`.
- Implement efficiently enough for UI use (single legal generation per query/call site; do not allocate).

**Perft:**
```cpp
std::uint64_t perft(Position& pos, int depth) {
  if (depth == 0) return 1;
  MoveList list; generate_legal(pos, list);
  if (depth == 1) return list.count; // optional small optimization
  std::uint64_t nodes = 0;
  for (each move m in list) { Undo u = pos.make_move(m); nodes += perft(pos, depth-1); pos.unmake_move(u); }
  return nodes;
}
```
- No heap allocations in recursion; `MoveList` is stack-allocated (fixed array). Recursion depth equals perft depth (small in tests).
- Optionally provide a divide/perft-divide helper for debugging (not required by ctest), but keep test harness output deterministic.

### Step 4 — Perft test harness (`tests/test_movegen.cpp`) with reference positions
**Files:** `tests/test_movegen.cpp` (new), `CMakeLists.txt` (wire executable + tests).

**Harness behavior:**
- Follow existing test style: `static int failures = 0; void check(bool, const char*)` prints `FAIL: ...` to `std::cerr`; `main` returns 1 on any failure.
- For each case, set position from FEN, run `movegen::perft(pos, depth)`, compare to exact expected node count, and report mismatches with position name, depth, expected, and actual.
- Keep perft depths modest enough for default `ctest` runtime while still catching common movegen bugs (castling, ep, promotions, pins). Deeper divide runs can be manual.

**Reference positions and expected counts (MUST NOT be altered to pass):**
1. **Startpos** — FEN: `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1`
   - d1: 20, d2: 400, d3: 8902, d4: 197281 (include d4 in ctest only if runtime is acceptable; otherwise run d1-d3 in ctest and document d4 as manual verification — choose and state explicitly in implementation; prefer including at least through d3, and d4 if fast enough in Release).
2. **Kiwipete** — FEN: `r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1`
   - d1: 48, d2: 2039, d3: 97862
3. **Position 3** — FEN: `8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1`
   - d1: 14, d2: 191, d3: 2812, d4: 43238
4. **Position 4** — FEN: `r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1`
   - d1: 6, d2: 264, d3: 9467
5. **Position 4 mirrored** — FEN: `r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1`
   - d1: 6, d2: 264, d3: 9467
6. **Position 5** — FEN: `rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8`
   - d1: 44, d2: 1486, d3: 62379
7. **Position 6** — FEN: `r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10`
   - d1: 46, d2: 2079, d3: 89890

**Critical rule:** if any count differs, fix the move generator / make-unmake / attack logic; **do not change the expected numbers above**.

### Step 5 — Wire movegen and perft into CMake
**Files:** `CMakeLists.txt`

**Changes:**
1. Add movegen sources to the build:
   - either `add_library(chess_core STATIC src/position.cpp src/movegen.cpp ...)` linked by `chessbot` and tests, or append `src/movegen.cpp` to `chessbot` and to `test_movegen`/other tests that need it. Prefer a small static/object library or explicit source lists to avoid duplication and keep link lines clear.
2. Ensure `target_include_directories(... PRIVATE src ...)` covers movegen users.
3. Add perft test executable and register with ctest:
   ```cmake
   add_executable(test_movegen tests/test_movegen.cpp src/position.cpp src/movegen.cpp)
   target_include_directories(test_movegen PRIVATE src)
   add_test(NAME test_movegen COMMAND test_movegen)
   ```
   (Adjust source/library linkage to match the chosen structure; include all translation units required for symbols used by perft/legal generation.)
4. Keep existing tests (`test_main`, `test_bitboard`, `test_position`) building and registered.
5. On Windows/MSVC, preserve existing `/O2 /W4` flags; resolve any new warnings without weakening project-wide warning policy except narrowly for third-party headers if already necessary.

### Step 6 — Server: `legal` request/response and strict move validation
**Files:** `src/web_server.hpp`, `src/web_server.cpp`, `src/position.hpp/.cpp` or `src/game.hpp/.cpp` (if introduced), `src/movegen.hpp/.cpp`, `src/move.hpp`.

**Protocol additions (WebSocket, text frames):**
1. **Client -> Server (UI extension):** request legal destinations for a selected square. Define a simple, unambiguous format compatible with the current line-based parser, e.g.:
   - `legal <square>` where `<square>` is algebraic like `e2` (UCI square notation), **or**
   - a JSON text frame `{"type":"legal","square":"e2"}` if the server already parses JSON frames.
   Choose one format and document it in code and here; recommended for minimal parser changes: accept `legal e2` line command.
2. **Server -> Client (JSON extension):** reply with legal destination squares for that source:
   ```json
   {"type":"legal","square":"e2","targets":["e3","e4"]}
   ```
   - `targets` are UCI destination squares (e.g. `"e4"`, `"e1"` for castling target square in UCI, `"e8q"` only if you choose to include promotion suffix — prefer destinations as squares and let frontend handle promotion chooser, or include full UCI move strings; **pick one and be consistent**. Recommended: `"targets":["a7","a8q","a8r","a8b","a8n"]` style or send full legal UCI moves from the square; alternatively `targets` as to-squares and a separate `promotions` map — simplest is returning full UCI strings/moves originating from `square` in a `moves` array, while also providing `targets` for highlight. Concretely: `{"type":"legal","square":"e7","moves":["e7e8q","e7e8r","e7e8b","e7e8n"]}` or include both; ensure frontend can highlight all legal destination squares.)
   - If the square has no legal moves or is invalid/empty/not owned by side-to-move as appropriate, return an empty list (not an error that crashes the connection).
3. **Server -> Client (state):** when position changes (after applied move, `ucinewgame`, `position ...`), emit a `state` JSON message (per Phase 1 contract: `type`, `fen`, `board[64]`, `side`, `orientation`, `clocks`, `status`, `last_move`). Legal moves are **not** broadcast in `state` except on demand via `legal` replies (per general plan watch-out).

**Incoming move handling and validation:**
1. On `position startpos [moves ...]` / `position fen <fen> [moves ...]`:
   - Set/reset the base position.
   - For each move token in order: parse with `parse_uci`; generate `generate_legal` for the current position and accept the move **only if it exactly matches a legal move** (compare `from`, `to`, and `promotion` — treat unspecified promotion in UCI as invalid for promotion moves; require `q/r/b/n` suffix as sent). Apply with `pos.make_move(legal_move)`.
   - If any token is malformed or illegal: **reject** it, do not partially apply an illegal move, and respond with a clear error signal (e.g. `info string illegal move <tok>` and/or keep state unchanged for that token — define behavior: safest is stop applying further moves from that command and preserve last valid state, then emit current `state`). Never silently accept illegal moves.
2. For any future/alternate client command that submits a single move, validate against `generate_legal` in the same way before `make_move`.
3. Use `move_to_uci`/comparison against generated legal `Move`s (not string guessing) to ensure promotion and special moves are validated correctly.
4. `go`/`stop` remain stubs in Phase 2 except as needed for protocol stability (search/bot is Phase 3).

**State/status helpers (recommended):**
- Add `std::string Position::to_fen() const` (or equivalent serializer) if not already present, so `state.fen` is accurate after any move sequence; include side, castling rights, en-passant target, halfmove clock, fullmove number.
- Derive `status` for UI: `"checkmate"` if `is_checkmate`, `"stalemate"` if `is_stalemate`, else `"playing"` (draw-by-repetition/50-move/insufficient material can remain out of scope unless trivial).

### Step 7 — Frontend: highlight legal destinations on square selection
**Files:** `web/index.html`, `web/style.css`, `web/app.js` (create or modify as needed for Phase 1 completion).

**Changes:**
1. **Selection flow:** on click/tap of a board square, if it contains a piece of the side to move (based on latest `state`), send a legal-request for that square (e.g. WS text `legal e2`). Track selected source square in UI state.
2. **Handle `legal` response:** parse `{"type":"legal", ...}` and highlight all legal destination squares/targets for the selected source (CSS class like `.legal-target`, `.legal-capture` optional). Clear highlights on deselect, on new selection, on `state` update, or on move submission.
3. **Rendering inputs:** consume `state.board[64]` with the documented indexing/orientation (A1=0..H8=63, white orientation = rank 8 at top unless specified otherwise) and `state.last_move` highlight; keep Unicode piece rendering consistent with Phase 1 plan.
4. **Move submission integration (if present):** when a highlighted target is chosen, send the corresponding UCI move via the existing `position ... moves ...` flow or the project’s chosen command; rely on server-side legality as the source of truth and re-render from emitted `state`. Illegal input must be visibly rejected (e.g. ignore click, status message) but correctness is enforced server-side.
5. **No framework/build tooling:** vanilla JS/CSS/HTML only.

**Note:** if Phase 1 frontend is absent, implement the minimal board render + WS connect + `state` handling required to demonstrate legal highlighting end-to-end; keep scope tight to Phase 2 goals.

### Step 8 — Strengthen reversibility/hash tests for special moves
**Files:** `tests/test_position.cpp` (modify/add cases) and/or `tests/test_movegen.cpp`.

**Changes:**
1. Add make/unmake round-trip cases covering: captures, promotions, castling (both sides), en passant, and moves that change castling rights/ep/halfmove/fullmove counters. For each, assert side, castling rights, ep square, clocks, occupancies/piece placement, and `zobrist_hash` equality with pre-move state.
2. These tests complement perft (perft is the strong correctness oracle); they provide fast pinpoint failures.
3. Do not weaken existing assertions.

## 2. Files summary

| File | Action | Purpose |
|---|---|---|
| `src/move.hpp` | modify | consolidate `Move`/`Undo` usage, keep fixed `MoveList`, UCI helpers |
| `src/position.hpp` | modify | include alignment, `king_square`/`in_check`/`to_fen` declarations as needed |
| `src/position.cpp` | modify | implement helpers, preserve reversible make/unmake + hash consistency |
| `src/bitboard.hpp` | modify (if needed) | only minimal helpers required by movegen; reuse existing attack/`is_square_attacked` |
| `src/movegen.hpp` | create | pseudo-legal/legal generation, check/mate/stalemate, perft API |
| `src/movegen.cpp` | create | movegen implementation (no heap in hot paths) |
| `src/web_server.hpp` | modify | protocol handling declarations/helpers if needed |
| `src/web_server.cpp` | modify | `legal` request handling, legal-move validation, state emission updates |
| `src/game.hpp/.cpp` | create/modify (if used) | session/state wrapper for board/FEN/status/last-move (only if needed) |
| `web/index.html` | create/modify | board UI shell if missing/minimal hooks for selection |
| `web/style.css` | create/modify | legal target and selection/highlight styles |
| `web/app.js` | create/modify | request/receive `legal`, highlight targets, apply moves via validated server flow |
| `tests/test_movegen.cpp` | create | perft reference positions/counts and result reporting |
| `tests/test_position.cpp` | modify (recommended) | extra make/unmake/hash reversibility cases |
| `CMakeLists.txt` | modify | add movegen sources/library, `test_movegen` executable + `add_test` |

## 3. Verification steps

### 3.1 Build and automated tests (required)
From repo root `D:\Documents\coding\cpp_projects\chessbot_agentic`:
```powershell
cmake -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
```
Expected:
- `test_main`, `test_bitboard`, `test_position`, and `test_movegen` all pass.
- `test_movegen` prints no `FAIL` lines and returns 0; perft node counts exactly match the reference values in Step 4 (**never modified**).

### 3.2 Targeted perft debugging (if a failure occurs)
- Reproduce the failing position/depth; add a temporary divide routine locally if needed (do not commit) to isolate the first incorrect move branch.
- Suspect areas in order: castling through-check/rights/rook square, en-passant discovered check, promotions, pawn double pushes/captures, king-adjacency/attack detection, make/unmake of special moves, Zobrist/castling/ep updates.
- Fix generator/state logic until counts match; **do not touch reference expected counts**.

### 3.3 Manual WebSocket/UI validation
1. Run `.\build\chessbot.exe` (or `--port 8080`) from repo root so `web/` assets resolve.
2. Open `http://localhost:8080/` and confirm connection to `/ws` and an initial `state` render.
3. **Legal highlighting:** click `e2` on startpos -> receive `legal` response and see destinations highlighted (`e3`, `e4`); click a piece with no moves or enemy piece on own turn -> empty/no targets. Verify knights, sliding pieces, and king moves highlight correctly in a midgame FEN.
4. **Special moves:** in appropriate FENs, verify highlights/acceptance for castling, promotions (all four), and en passant; verify pinned pieces show restricted/no illegal targets.
5. **Illegal move rejection:** send/attempt an illegal UCI move (wrong side, blocked move, moving into check, malformed token, missing promotion suffix) -> server rejects it (no state corruption) and board/state remains consistent; legal moves apply and update `state.board`, `state.fen`, `state.side`, and `state.last_move`.
6. **Game end indicators:** load a checkmate FEN and a stalemate FEN; confirm `in_check`/mate/stalemate logic reports correctly via `status` (and/or UI) and legal target sets are empty where appropriate.

## 4. Watch-outs specific to this codebase

- `src/bitboard.hpp` currently includes `position.hpp` for `is_square_attacked`; avoid creating new include cycles when adding movegen headers (prefer `movegen.cpp` including what it needs, minimal declarations in headers).
- `Position::unmake_move` implementation must remain the exact inverse of `make_move` for all move kinds; any drift will surface as perft mismatches and hash assertion failures.
- Castling legality must check "in check, through check, into check" squares; en-passant legality must be validated via make/unmake because it can expose a horizontal discovered check on the king.
- Keep all move generation/perft data on the stack (`MoveList`/arrays). Do not introduce `std::vector`, `std::map`, dynamic strings, or logging allocations in movegen/perft loops.
- `parse_uci` currently permits missing promotion handling details; ensure promotion moves are only accepted when the generated legal move includes the same promotion piece. 
