# Project Instructions: UCI Chess Engine

## Stack & Build
- Language: C++20 (compiled with `-O3 -march=native -Wall -Wextra`)
- Build: `cmake -B build && cmake --build build -j4`
- Test: `ctest --test-dir build --output-on-failure`
- **Rule:** Always compile and run `ctest` after modifying code before finishing a task.

## Architecture
- `src/types.hpp`: Main data types used throughout the project
- `src/bitboard.hpp`: 64-bit board representation, bit manipulation helpers, attack tables
- `src/position.hpp/.cpp`: Board state, Zobrist hashing, `make_move` / `unmake_move`
- `src/movegen.hpp/.cpp`: Legal/pseudo-legal move generation and `perft`
- `src/search.hpp/.cpp`: Iterative deepening, alpha-beta, transposition table, quiescence search
- `src/eval.hpp/.cpp`: Static position evaluation (tapered PSTs, mobility, pawn structure)
- `src/uci.hpp/.cpp`: UCI protocol parser and time management loop
- `tests/`: `perft` suites, tactical mate-in-N test positions, UCI protocol tests

## Performance & Safety Rules
- Zero heap allocations (`new`, `std::vector` resizing, `std::string`) inside `search` or `movegen` hot paths; use fixed-size stack arrays (`std::array` or custom `MoveList`).
- Keep `make_move` / `unmake_move` strictly reversible and verified against Zobrist hash consistency assertions in debug builds.
- Never alter `perft` expected reference counts in `tests/` to make a failing test pass.

## Git Workflow
- Create a feature branch (`feature/...`) before starting work.
- Use Conventional Commits and open draft PRs (`gh pr create --draft`).