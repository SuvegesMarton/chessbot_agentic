# Phase 0 — Foundations (unblocks everything)

## Overview

Phase 0 delivers the primitive building blocks the rest of the project depends on:
move representation/UCI conversion, square-attack queries, position helpers (piece_on,
board snapshot), vendored HTTP/WebSocket library, argument parsing in main.cpp, and a
WebSocket-based UCI text loop in web_server. It also fixes the broken test_bitboard target.

No other phases should begin before Phase 0 lands and `ctest` is green.

## Conventions to follow (from AGENTS.md / general.md)

- C++20, compiled with `-O3 -march=native -Wall -Wextra` (MSVC `/O2 /W4`).
- Zero heap allocations (`new`, `std::vector` resizing, `std::string`) inside search/movegen hot paths; use fixed-size stack arrays (`std::array` or a custom `MoveList`).
- `make_move`/`unmake_move` strictly reversible; keep Zobrist consistency assertions in debug builds.
- Never alter perft reference counts in tests.
- Consolidate `Move`/`Undo` (already in `position.hpp`) with `move.hpp` — keep `MoveList`/UCI helpers in `move.hpp` only; do not duplicate `Move`/`Undo`.
- Pin a specific cpp-httplib release in `third_party/` (WebSocket API + MSVC `/W4` warnings-as-errors risk).

## Ordered steps

### Step 1 — Create `src/move.hpp` with `MoveList`, `move_to_uci`, `parse_uci`

Files: create `src/move.hpp` (header-only, inline helpers).

- Include `types.hpp` and `<array>`, `<cstdint>`, `<string_view>`.
- Do NOT redefine `Move`/`Undo` (already in `position.hpp`); `move.hpp` only adds helpers.
- Define:
  ```cpp
  constexpr std::size_t MOVE_LIST_CAPACITY = 256;
  struct MoveList {
      std::array<Move, MOVE_LIST_CAPACITY> moves{};
      std::size_t count = 0;
      void push(const Move& m) { if (count < MOVE_LIST_CAPACITY) moves[count++] = m; }
      Move* begin() { return moves.data(); }
      Move* end() { return moves.data() + count; }
      const Move* begin() const { return moves.data(); }
      const Move* end() const { return moves.data() + count; }
      std::size_t size() const { return count; }
  };
  ```
- `std::string move_to_uci(const Move& m)` — squares as `a1`..`h8`, append promotion char (`q/r/b/n`) when `promotion != PieceType::None`. (Returning std::string is fine here: this is a conversion helper, not a hot path.)
- `Move parse_uci(std::string_view s)` — parse 4-5 chars (`e2e4`, `e7e8q`); return `Move{...}` with `Square::None` fields on failure. Use `make_square(file, rank)`.

### Step 2 — Add `is_square_attacked` to `src/bitboard.hpp`

Files: modify `src/bitboard.hpp`.

- Add declaration/definition:
  ```cpp
  bool is_square_attacked(const Position& pos, Square sq, Color byColor);
  ```
  But bitboard.hpp must not include position.hpp (avoid cycle). Instead take occupancy + piece bitboards as parameters, OR include position.hpp. Simplest: define in `position.hpp`/`.cpp` using existing tables. However the plan item says `bitboard.hpp`: `is_square_attacked(pos, sq, byColor)` using existing attack tables. To keep the signature, include `position.hpp` from bitboard.hpp is circular (position includes types only). Resolution: put the helper in `bitboard.hpp` as a free function taking `const Position&`, and `#include "position.hpp"` at the bottom of bitboard.hpp (after types) — but position.hpp doesn't include bitboard.hpp currently? It doesn't. Actually position.hpp includes types.hpp only. So bitboard.hpp can `#include "position.hpp"` safely. Do that.
- Implementation: for each piece type of `byColor`, compute attack bitboards (knights/kings/pawns via tables; rooks/bishops/queens via sliding with `pos.all_occupancy`) and test `sq`. Must exclude friendly blockers correctly (sliding functions already stop at any occupancy including own pieces; that's correct for attacks INTO sq — a piece attacks sq if line of sight not blocked).

### Step 3 — `piece_on(Square)` and `board_snapshot()` in position

Files: modify `src/position.hpp` and `src/position.cpp`.

- In `position.hpp`, add:
  ```cpp
  Piece piece_on(Square s) const;
  std::array<Piece, 64> board_snapshot() const;  // Piece{Color, PieceType::None} for empty? Use optional-ish: encode empty as PieceType::None.
  ```
  Also `#include <array>` already present.
- Implement `piece_on`: scan 6 piece types x 2 colors; return `{color, type}` or `{Color::White, PieceType::None}` if empty.
- Implement `board_snapshot`: fill 64 entries using `piece_on` or direct bitboard scan.

### Step 4 — Vendor cpp-httplib

Files: create `third_party/httplib.h` (single header). Add directory `third_party/`.

- Pin a specific release, e.g. v0.18.3 (latest stable supporting WebSocket). Download `httplib.h` from the release tag:
  `https://github.com/yhirose/cpp-httplib/releases/download/v0.18.3/httplib.h`
- Place at `third_party/httplib.h`.
- No CMake change for the header itself (header-only); include path added in Step 5/6.

### Step 5 — Update `CMakeLists.txt`

Files: modify `CMakeLists.txt`.

Changes:
1. Add `third_party` to include dirs for targets that need httplib (web_server, chessbot).
2. Fix `test_bitboard`: add `add_executable(test_bitboard tests/test_bitboard.cpp src/...?)` — test_bitboard only needs bitboard.hpp (header-only) + types.hpp, no .cpp. So:
   ```
   add_executable(test_bitboard tests/test_bitboard.cpp)
   target_include_directories(test_bitboard PRIVATE src)
   ```
3. Add `web_server` sources later (Step 6): `add_executable` not needed for web_server if embedded in chessbot. Instead chessbot binary links web_server.cpp. Add:
   ```
   add_executable(chessbot src/main.cpp src/position.cpp src/web_server.cpp)
   target_include_directories(chessbot PRIVATE src third_party)
   ```
   web_server.cpp includes httplib.h.
4. Keep existing test targets.

WebSocket support in cpp-httplib is enabled by defining `CPPHTTPLIB_OPENSSL_SUPPORT`? No — WebSocket is built-in (no macro needed for basic WebSocket in recent versions; OpenSSL only for HTTPS/WSS). For plain HTTP+WS, no macro. Note: on Windows, link `ws2_32` and `winmm`? cpp-httplib requires `ws2_32` on Windows. Add:
   ```
   if(WIN32)
     target_link_libraries(chessbot PRIVATE ws2_32)
   endif()
   ```

### Step 6 — Rewrite `src/main.cpp`

Files: modify `src/main.cpp`.

- Parse args: `--port <n>` (default 8080), `--uci` (run UCI REPL, not implemented beyond stub printing ready), default starts `web_server`.
- Call `run_web_server(port)` from web_server.hpp, or UCI loop if `--uci`.
- Include `web_server.hpp`.

### Step 7 — Create `src/web_server.hpp/.cpp`

Files: create `src/web_server.hpp` and `src/web_server.cpp`.

- `web_server.hpp`: declare `void run_web_server(int port);`
- `web_server.cpp`: include `httplib.h`, set up `httplib::Server`, WebSocket handler at `/ws` (cpp-httplib API: `svr.WebSocket("/ws", [](const httplib::Request&, httplib::ws::WebSocket& ws){ ... })` — actual API in v0.18: `svr.set_mount_point` etc. WebSocket handler signature: `svr.WebSocket("/ws", [](const httplib::Request &req, httplib::ws::WebSocket &ws) { ... })`).
- Inside WS handler: loop reading text frames; parse each as a UCI line; dispatch:
  - `uci` → send `uciok`
  - `isready` → send `readyok`
  - `ucinewgame` → reset position to startpos (hold a Position)
  - `position startpos moves ...` / `position fen <fen> moves ...` → set position
  - `go ...` → (Phase 0: reply `bestmove 0000` or stub; full search in Phase 3) — at minimum echo/ack
  - `stop` → ack
- Send responses as text frames (one line per frame).
- Static file serving of `web/` deferred to Phase 1; Phase 0 only WS endpoint + UCI loop. (Plan says web_server: UCI text parser/loop over WebSocket.)

### Step 8 — Build & verify

Commands:
```
cmake -B build && cmake --build build -j4
ctest --test-dir build --output-on-failure
```
Expected: chessbot builds, test_bitboard/test_position/test_main pass.

## Files summary

Created:
- `src/move.hpp`
- `src/web_server.hpp`
- `src/web_server.cpp`
- `third_party/httplib.h`

Modified:
- `src/bitboard.hpp` (add include position.hpp + is_square_attacked)
- `src/position.hpp` / `src/position.cpp` (piece_on, board_snapshot)
- `src/main.cpp`
- `CMakeLists.txt`

## Notes / watch-outs

- `is_square_attacked` must use existing attack tables (knight_attacks, king_attacks, pawn_attacks, rook/bishop/queen_attacks) and `pos.all_occupancy` for sliding blockers.
- Keep `Move`/`Undo` definitions only in `position.hpp`; `move.hpp` only adds MoveList + UCI helpers.
- Pin cpp-httplib to a specific release tag; document version in a comment at top of httplib.h usage or in this plan.
- MSVC `/W4`: httplib.h may produce warnings; if warnings-as-errors is on, may need `/WX-` for the translation unit including httplib. Currently no /WX, so fine.
- No heap in hot paths: MoveList uses std::array; move_to_uci/parse_uci are conversion helpers (allowed to use std::string).
