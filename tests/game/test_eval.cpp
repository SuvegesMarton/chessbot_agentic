#include <cstdint>
#include <iostream>
#include <cmath>

#include "game/eval.hpp"
#include "game/position.hpp"
#include "game/movegen.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void eval_symmetry_startpos() {
    Position p;
    p.set_starting_position();
    int score = eval::evaluate(p);
    check(std::abs(score) <= 30, "startpos eval near zero (symmetry)");
}

void eval_material_dominance() {
    Position p;
    p.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int score_w = eval::evaluate(p);
    p.set_fen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int score_w_no_bq = eval::evaluate(p);
    check(score_w_no_bq - score_w > 700, "white up a queen scores > 700 cp");
}

void eval_side_to_move_perspective() {
    Position p;
    p.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    int score_w = eval::evaluate(p);
    p.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1");
    int score_b = eval::evaluate(p);
    check(score_w == -score_b, "eval negates when side to move flips");
}

void eval_king_pst_sanity() {
    Position p;
    p.set_fen("8/8/8/8/8/8/4P3/4K2k w - - 0 1");
    int score_w = eval::evaluate(p);
    p.set_fen("8/8/8/8/8/8/4p3/4k2K b - - 0 1");
    int score_b = eval::evaluate(p);
    check(score_w > 0, "advanced pawn positive for white");
    check(score_b > 0, "advanced pawn positive for black (from their perspective)");
}

} // namespace

int main() {
    eval_symmetry_startpos();
    eval_material_dominance();
    eval_side_to_move_perspective();
    eval_king_pst_sanity();

    if (failures == 0) {
        std::cout << "All eval tests passed\n";
        return 0;
    }
    std::cerr << failures << " eval test(s) failed\n";
    return 1;
}