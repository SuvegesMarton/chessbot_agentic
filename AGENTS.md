# Project Instructions: UCI Chess Engine

## Stack & Build
- Language: C++20 (compiled with `-O3 -march=native -Wall -Wextra`)
- Build: `cmake -B build && cmake --build build -j4`
- Test: `ctest --test-dir build --output-on-failure`
- **Rule:** Always compile and run `ctest` after modifying code before finishing a task.

## Architecture

### Core (`src/core/`)
- `src/core/types.hpp`: Main data types used throughout the project
- `src/core/bitboard.hpp`: 64-bit board representation, bit manipulation helpers, attack tables
- `src/core/move.hpp`: Move encoding, decoding, and utilities

### Game Engine (`src/game/`)
- `src/game/position.hpp/.cpp`: Board state, Zobrist hashing, `make_move` / `unmake_move`
- `src/game/movegen.hpp/.cpp`: Legal/pseudo-legal move generation and `perft`
- `src/game/search.hpp/.cpp`: Iterative deepening, alpha-beta, transposition table, quiescence search
- `src/game/eval.hpp/.cpp`: Static position evaluation (tapered PSTs, mobility, pawn structure)
- `src/game/game.hpp/.cpp`: High-level game management, UCI integration

### Web Interface (`src/web/`)
- `src/web/web_server.hpp/.cpp`: HTTP/WebSocket server for web UI

### Entry Points
- `src/main.cpp`: Main entry point

### Tests (`tests/`)
- `tests/core/`: Core type and bitboard tests (`test_main.cpp`, `test_bitboard.cpp`, `test_move.cpp`)
- `tests/game/`: Game engine tests (`test_eval.cpp`, `test_movegen.cpp`, `test_position.cpp`, `test_position_helpers.cpp`, `test_search.cpp`)
- `tests/web/`: Web server tests (`test_web_server.cpp`)

### Third Party
- `third_party/`: External dependencies

## Performance & Safety Rules
- Zero heap allocations (`new`, `std::vector` resizing, `std::string`) inside `search` or `movegen` hot paths; use fixed-size stack arrays (`std::array` or custom `MoveList`).
- Keep `make_move` / `unmake_move` strictly reversible and verified against Zobrist hash consistency assertions in debug builds.
- Never alter `perft` expected reference counts in `tests/` to make a failing test pass.

## Git Workflow
- Create a feature branch (`feature/...`) before starting work.
- Use Conventional Commits and open draft PRs (`gh pr create --draft`).