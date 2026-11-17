#include <cstdint>
#include <iostream>
#include <string>

#include "position.hpp"
#include "types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void starting_position_test() {
    Position p;
    p.set_starting_position();
    check(p.side_to_move == Color::White, "start side to move white");
    check(p.castling_rights == (1 | 2 | 4 | 8), "start castling rights");
    check(p.en_passant == Square::None, "start no ep");
    check(p.halfmove_clock == 0, "start halfmove clock");
    check(p.fullmove_number == 1, "start fullmove");
}

void fen_roundtrip_test() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/8/8/8/8/8/8/8 w - - 0 1",
        "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"
    };
    for (const char* f : fens) {
        Position p;
        check(p.set_fen(f), "set_fen succeeds");
        std::uint64_t h = p.hash();
        (void)h;
    }
}

void make_unmake_reversibility() {
    Position p;
    p.set_starting_position();
    std::uint64_t h0 = p.hash();
    // e2e4
    Move m{Square::E2, Square::E4, PieceType::None};
    Undo u = p.make_move(m);
    check(p.side_to_move == Color::Black, "side flipped");
    p.unmake_move(u);
    check(p.side_to_move == Color::White, "side restored");
    check(p.hash() == h0, "hash restored after unmake");
}

}  // namespace

int main() {
    starting_position_test();
    fen_roundtrip_test();
    make_unmake_reversibility();

    if (failures == 0) {
        std::cout << "All position tests passed\n";
        return 0;
    }
    std::cerr << failures << " position test(s) failed\n";
    return 1;
}