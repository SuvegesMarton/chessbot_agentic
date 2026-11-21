#include <cstdint>
#include <iostream>
#include <string>

#include "game/movegen.hpp"
#include "game/position.hpp"
#include "core/types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void run_perft_test(const char* name, const char* fen, int depth, std::uint64_t expected) {
    Position pos;
    if (!pos.set_fen(fen)) {
        std::cerr << "FAIL: " << name << " - invalid FEN\n";
        ++failures;
        return;
    }
    std::uint64_t nodes = movegen::perft(pos, depth);
    if (nodes != expected) {
        std::cerr << "FAIL: " << name << " depth " << depth
                  << " expected " << expected << " got " << nodes << "\n";
        ++failures;
    }
}

void startpos_perft() {
    const char* fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    run_perft_test("Startpos", fen, 1, 20);
    run_perft_test("Startpos", fen, 2, 400);
    run_perft_test("Startpos", fen, 3, 8902);
    run_perft_test("Startpos", fen, 4, 197281);
}

void kiwipete_perft() {
    const char* fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    run_perft_test("Kiwipete", fen, 1, 48);
    run_perft_test("Kiwipete", fen, 2, 2039);
    run_perft_test("Kiwipete", fen, 3, 97862);
}

void position3_perft() {
    const char* fen = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    run_perft_test("Position3", fen, 1, 14);
    run_perft_test("Position3", fen, 2, 191);
    run_perft_test("Position3", fen, 3, 2812);
    run_perft_test("Position3", fen, 4, 43238);
}

void position4_perft() {
    const char* fen = "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1";
    run_perft_test("Position4", fen, 1, 6);
    run_perft_test("Position4", fen, 2, 264);
    run_perft_test("Position4", fen, 3, 9467);
}

void position4_mirrored_perft() {
    const char* fen = "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1";
    run_perft_test("Position4Mirrored", fen, 1, 6);
    run_perft_test("Position4Mirrored", fen, 2, 264);
    run_perft_test("Position4Mirrored", fen, 3, 9467);
}

void position5_perft() {
    const char* fen = "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8";
    run_perft_test("Position5", fen, 1, 44);
    run_perft_test("Position5", fen, 2, 1486);
    run_perft_test("Position5", fen, 3, 62379);
}

void position6_perft() {
    const char* fen = "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10";
    run_perft_test("Position6", fen, 1, 46);
    run_perft_test("Position6", fen, 2, 2079);
    run_perft_test("Position6", fen, 3, 89890);
}

void in_check_tests() {
    Position p;
    p.set_starting_position();
    check(!movegen::in_check(p, Color::White), "startpos white not in check");
    check(!movegen::in_check(p, Color::Black), "startpos black not in check");

    // White in check: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 -> e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 e1g1 f8c5 f3e5 c5f2
    p.set_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    // Give check: Qh5
    p.set_fen("rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 2 3");
    // Actually let's test a simple check position
    p.set_fen("8/8/8/8/8/8/4k3/4K2R w K - 0 1");
    check(movegen::in_check(p, Color::White), "white in check from rook");
}

void checkmate_stalemate_tests() {
    // White checkmated: white king on a1, black queen on a2, black rook on b1
    Position p;
    p.set_fen("7k/8/8/8/8/8/qr6/K7 w - - 0 1");
    check(movegen::is_checkmate(p), "white checkmated");

    // Stalemate: black king on a8, white queen on c7, white king on c6
    p.set_fen("k7/2Q5/K7/8/8/8/8/8 b - - 0 1");
    check(movegen::is_stalemate(p), "black stalemated");
}

void legal_generation_tests() {
    Position p;
    p.set_starting_position();
    MoveList legal;
    movegen::generate_legal(p, legal);
    check(legal.size() == 20, "startpos has 20 legal moves");

    // Test after 1. e4
    Move m = {Square::E2, Square::E4, PieceType::None};
    p.make_move(m);
    movegen::generate_legal(p, legal);
    check(legal.size() == 20, "after e4 black has 20 legal moves");
}

} // namespace

int main() {
    startpos_perft();
    kiwipete_perft();
    position3_perft();
    position4_perft();
    position4_mirrored_perft();
    position5_perft();
    position6_perft();
    in_check_tests();
    checkmate_stalemate_tests();
    legal_generation_tests();

    if (failures == 0) {
        std::cout << "All movegen tests passed\n";
        return 0;
    }

    std::cerr << failures << " movegen test(s) failed\n";
    return 1;
}