#include <cstdint>
#include <iostream>

#include "core/bitboard.hpp"
#include "core/types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void basic_bit_operations() {
    check(popcount(0ULL) == 0, "popcount zero");
    check(popcount(0b1011ULL) == 3, "popcount counts set bits");
    check(popcount(~0ULL) == 64, "popcount all bits");

    check(ctz(Bitboard{1} << 0) == 0, "ctz lsb at 0");
    check(ctz(Bitboard{1} << 7) == 7, "ctz lsb at 7");
    check(ctz((Bitboard{1} << 3) | (Bitboard{1} << 9)) == 3, "ctz lowest set bit");

    check(lsb((Bitboard{1} << 3) | (Bitboard{1} << 9)) == (Bitboard{1} << 3), "lsb isolates lowest bit");
    check(square_of(Bitboard{1} << 27) == static_cast<Square>(27), "square_of returns index");
}

void square_and_bit_helpers() {
    check(file_of(Square::A1) == 0, "file of A1");
    check(rank_of(Square::A1) == 0, "rank of A1");
    check(file_of(Square::H8) == 7, "file of H8");
    check(rank_of(Square::H8) == 7, "rank of H8");
    check(make_square(3, 4) == static_cast<Square>(35), "make_square file/rank");
    check(square_bit(Square::C2) == (Bitboard{1} << 10), "square_bit sets correct bit");

    Bitboard b = 0;
    b = set_bit(b, Square::D4);
    check(is_set(b, Square::D4), "is_set after set_bit");
    check(!is_set(b, Square::E4), "is_set false for unset square");

    b = toggle_bit(b, Square::D4);
    check(!is_set(b, Square::D4), "toggle clears set bit");
    b = toggle_bit(b, Square::D4);
    check(is_set(b, Square::D4), "toggle sets cleared bit");

    b = clear_bit(b, Square::D4);
    check(b == 0, "clear_bit removes bit");
}

void knight_attack_tests() {
    check(knight_attacks[static_cast<int>(Square::B1)] ==
              (square_bit(Square::A3) | square_bit(Square::C3) | square_bit(Square::D2)),
          "knight attacks from b1");
    check(knight_attacks[static_cast<int>(Square::E4)] ==
              (square_bit(Square::D6) | square_bit(Square::F6) | square_bit(Square::G5) |
               square_bit(Square::G3) | square_bit(Square::F2) | square_bit(Square::D2) |
               square_bit(Square::C3) | square_bit(Square::C5)),
          "knight attacks from e4");
    check(popcount(knight_attacks[static_cast<int>(Square::A1)]) == 2, "knight on corner has 2 moves");
    check(popcount(knight_attacks[static_cast<int>(Square::E4)]) == 8, "knight in center has 8 moves");
}

void king_attack_tests() {
    check(king_attacks[static_cast<int>(Square::A1)] ==
              (square_bit(Square::A2) | square_bit(Square::B1) | square_bit(Square::B2)),
          "king attacks from a1");
    check(popcount(king_attacks[static_cast<int>(Square::A1)]) == 3, "king on corner has 3 moves");
    check(popcount(king_attacks[static_cast<int>(Square::E4)]) == 8, "king in center has 8 moves");
    check(popcount(king_attacks[static_cast<int>(Square::H5)]) == 5, "king on edge has 5 moves");
}

void pawn_attack_tests() {
    check(pawn_attacks_white[static_cast<int>(Square::E4)] ==
              (square_bit(Square::D5) | square_bit(Square::F5)),
          "white pawn attacks from e4");
    check(pawn_attacks_black[static_cast<int>(Square::E4)] ==
              (square_bit(Square::D3) | square_bit(Square::F3)),
          "black pawn attacks from e4");
    check(pawn_attacks(Color::White, Square::A2) == square_bit(Square::B3),
          "white pawn on a-file attacks one square");
    check(pawn_attacks(Color::Black, Square::H7) == square_bit(Square::G6),
          "black pawn on h-file attacks one square");
    check(pawn_attacks_white[static_cast<int>(Square::E8)] == 0, "white pawn on rank 8 has no attacks");
    check(pawn_attacks_black[static_cast<int>(Square::E1)] == 0, "black pawn on rank 1 has no attacks");
}

void sliding_attack_tests() {
    const Bitboard empty = 0;

    Bitboard rook_e4_empty = rook_attacks(Square::E4, empty);
    check(is_set(rook_e4_empty, Square::E1) && is_set(rook_e4_empty, Square::E8) &&
              is_set(rook_e4_empty, Square::A4) && is_set(rook_e4_empty, Square::H4),
          "rook on empty board reaches board edges");
    check(!is_set(rook_e4_empty, Square::E4), "rook attacks exclude origin square");
    check(popcount(rook_e4_empty) == 14, "rook on empty board has 14 attacks");

    Bitboard occ = square_bit(Square::E6) | square_bit(Square::G4) | square_bit(Square::E2) | square_bit(Square::B4);
    Bitboard rook_e4_blocked = rook_attacks(Square::E4, occ);
    check(is_set(rook_e4_blocked, Square::E6), "rook includes blocking square north");
    check(!is_set(rook_e4_blocked, Square::E7), "rook blocked beyond north blocker");
    check(is_set(rook_e4_blocked, Square::G4), "rook includes blocking square east");
    check(!is_set(rook_e4_blocked, Square::H4), "rook blocked beyond east blocker");
    check(is_set(rook_e4_blocked, Square::E2), "rook includes blocking square south");
    check(!is_set(rook_e4_blocked, Square::E1), "rook blocked beyond south blocker");
    check(is_set(rook_e4_blocked, Square::B4), "rook includes blocking square west");
    check(!is_set(rook_e4_blocked, Square::A4), "rook blocked beyond west blocker");

    Bitboard bishop_e4_empty = bishop_attacks(Square::E4, empty);
    check(is_set(bishop_e4_empty, Square::B1) && is_set(bishop_e4_empty, Square::H7) &&
              is_set(bishop_e4_empty, Square::A8) && is_set(bishop_e4_empty, Square::H1),
          "bishop on empty board reaches diagonals");
    check(popcount(bishop_e4_empty) == 13, "bishop on empty board has 13 attacks");

    occ = square_bit(Square::G6) | square_bit(Square::C2);
    Bitboard bishop_e4_blocked = bishop_attacks(Square::E4, occ);
    check(is_set(bishop_e4_blocked, Square::G6), "bishop includes blocking square ne");
    check(!is_set(bishop_e4_blocked, Square::H7), "bishop blocked beyond ne blocker");
    check(is_set(bishop_e4_blocked, Square::C2), "bishop includes blocking square sw");
    check(!is_set(bishop_e4_blocked, Square::B1), "bishop blocked beyond sw blocker");

    Bitboard queen_e4 = queen_attacks(Square::E4, empty);
    check(queen_e4 == (rook_e4_empty | bishop_e4_empty), "queen attacks combine rook and bishop");
}

}  // namespace

int main() {
    basic_bit_operations();
    square_and_bit_helpers();
    knight_attack_tests();
    king_attack_tests();
    pawn_attack_tests();
    sliding_attack_tests();

    if (failures == 0) {
        std::cout << "All bitboard tests passed\n";
        return 0;
    }

    std::cerr << failures << " bitboard test(s) failed\n";
    return 1;
}
