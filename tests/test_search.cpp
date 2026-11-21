#include <cstdint>
#include <iostream>
#include <chrono>
#include <thread>

#include "search.hpp"
#include "eval.hpp"
#include "position.hpp"
#include "movegen.hpp"
#include "move.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void search_returns_legal_move() {
    Position p;
    p.set_starting_position();
    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 4;
    limits.movetime_ms = 200;
    SearchResult r = s.search(limits);

    MoveList legal;
    movegen::generate_legal(p, legal);
    bool found = false;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (legal.moves[i].from == r.best_move.from &&
            legal.moves[i].to == r.best_move.to &&
            legal.moves[i].promotion == r.best_move.promotion) {
            found = true;
            break;
        }
    }
    check(found, "search returns a legal move");
    check(r.depth >= 1, "search completes at least depth 1");
}

void search_mate_in_1() {
    Position p;
    // White to move, mate in 1: Rh8#
    p.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1");
    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 4;
    limits.movetime_ms = 500;
    SearchResult r = s.search(limits);

    MoveList legal;
    movegen::generate_legal(p, legal);
    bool found = false;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (legal.moves[i].from == r.best_move.from &&
            legal.moves[i].to == r.best_move.to &&
            legal.moves[i].promotion == r.best_move.promotion) {
            found = true;
            break;
        }
    }
    check(found, "mate in 1 returns a legal move");
    check(r.score >= MATE - 100, "mate in 1 score near MATE");
}

void search_mate_in_2() {
    Position p;
    // Simple mate in 2: White has Qh5, Black Kh8, White to move Qh7#
    p.set_fen("7k/5Q2/8/8/8/8/8/7K w - - 0 1");
    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 6;
    limits.movetime_ms = 1000;
    SearchResult r = s.search(limits);

    MoveList legal;
    movegen::generate_legal(p, legal);
    bool found = false;
    for (std::size_t i = 0; i < legal.size(); ++i) {
        if (legal.moves[i].from == r.best_move.from &&
            legal.moves[i].to == r.best_move.to &&
            legal.moves[i].promotion == r.best_move.promotion) {
            found = true;
            break;
        }
    }
    check(found, "mate in 2 returns a legal move");
}

void search_time_budget_respected() {
    Position p;
    p.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 64;
    limits.movetime_ms = 100;
    auto start = std::chrono::steady_clock::now();
    SearchResult r = s.search(limits);
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    check(ms < 1000, "search respects time budget (returns in under 1s)");
    check(r.depth >= 1, "search completes at least depth 1");
}

void search_terminal_handling() {
    // Stalemate
    Position p;
    p.set_fen("k7/2Q5/K7/8/8/8/8/8 b - - 0 1");
    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 4;
    limits.movetime_ms = 100;
    SearchResult r = s.search(limits);
    check(r.best_move.from == Square::None, "stalemate returns no move");

    // Checkmate
    p.set_fen("7k/8/8/8/8/8/qr6/K7 w - - 0 1");
    Searcher s2(p);
    SearchResult r2 = s2.search(limits);
    check(r2.best_move.from == Square::None, "checkmate returns no move");
}

void search_reversibility() {
    Position p;
    p.set_starting_position();
    std::uint64_t hash_before = p.hash();
    Color side_before = p.side_to_move;

    Searcher s(p);
    SearchLimits limits;
    limits.max_depth = 4;
    limits.movetime_ms = 200;
    SearchResult r = s.search(limits);

    std::uint64_t hash_after = p.hash();
    Color side_after = p.side_to_move;
    check(hash_before == hash_after, "position hash unchanged after search");
    check(side_before == side_after, "side to move unchanged after search");
}

} // namespace

int main() {
    search_returns_legal_move();
    search_mate_in_1();
    search_mate_in_2();
    search_time_budget_respected();
    search_terminal_handling();
    search_reversibility();

    if (failures == 0) {
        std::cout << "All search tests passed\n";
        return 0;
    }
    std::cerr << failures << " search test(s) failed\n";
    return 1;
}