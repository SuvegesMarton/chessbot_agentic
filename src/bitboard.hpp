#pragma once

#include <array>
#include <bit>
#include <cstdint>

#include "types.hpp"

inline int popcount(Bitboard b) { return std::popcount(b); }

inline int ctz(Bitboard b) { return std::countr_zero(b); }

inline Bitboard lsb(Bitboard b) { return b & -b; }

inline Square square_of(Bitboard b) { return static_cast<Square>(ctz(b)); }

inline int file_of(Square s) { return static_cast<int>(s) & 7; }

inline int rank_of(Square s) { return static_cast<int>(s) >> 3; }

inline Square make_square(int file, int rank) {
    return static_cast<Square>(rank * 8 + file);
}

inline Bitboard square_bit(Square s) {
    return Bitboard{1} << static_cast<int>(s);
}

inline bool is_set(Bitboard b, Square s) {
    return (b & square_bit(s)) != 0;
}

inline Bitboard set_bit(Bitboard b, Square s) {
    return b | square_bit(s);
}

inline Bitboard clear_bit(Bitboard b, Square s) {
    return b & ~square_bit(s);
}

inline Bitboard toggle_bit(Bitboard b, Square s) {
    return b ^ square_bit(s);
}

namespace {

constexpr std::array<int, 8> knight_dx = {1, 2, 2, 1, -1, -2, -2, -1};
constexpr std::array<int, 8> knight_dy = {2, 1, -1, -2, -2, -1, 1, 2};

constexpr std::array<int, 8> king_dx = {-1, -1, -1, 0, 0, 1, 1, 1};
constexpr std::array<int, 8> king_dy = {-1, 0, 1, -1, 1, -1, 0, 1};

constexpr std::array<Bitboard, 64> generate_knight_attacks() {
    std::array<Bitboard, 64> t{};
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7;
        int r = sq >> 3;
        Bitboard b = 0;
        for (int i = 0; i < 8; ++i) {
            int nf = f + knight_dx[i];
            int nr = r + knight_dy[i];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                b |= Bitboard{1} << (nr * 8 + nf);
            }
        }
        t[sq] = b;
    }
    return t;
}

constexpr std::array<Bitboard, 64> generate_king_attacks() {
    std::array<Bitboard, 64> t{};
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7;
        int r = sq >> 3;
        Bitboard b = 0;
        for (int i = 0; i < 8; ++i) {
            int nf = f + king_dx[i];
            int nr = r + king_dy[i];
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                b |= Bitboard{1} << (nr * 8 + nf);
            }
        }
        t[sq] = b;
    }
    return t;
}

constexpr std::array<Bitboard, 64> generate_pawn_attacks_white() {
    std::array<Bitboard, 64> t{};
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7;
        int r = sq >> 3;
        Bitboard b = 0;
        if (r + 1 < 8) {
            if (f - 1 >= 0) b |= Bitboard{1} << ((r + 1) * 8 + (f - 1));
            if (f + 1 < 8) b |= Bitboard{1} << ((r + 1) * 8 + (f + 1));
        }
        t[sq] = b;
    }
    return t;
}

constexpr std::array<Bitboard, 64> generate_pawn_attacks_black() {
    std::array<Bitboard, 64> t{};
    for (int sq = 0; sq < 64; ++sq) {
        int f = sq & 7;
        int r = sq >> 3;
        Bitboard b = 0;
        if (r - 1 >= 0) {
            if (f - 1 >= 0) b |= Bitboard{1} << ((r - 1) * 8 + (f - 1));
            if (f + 1 < 8) b |= Bitboard{1} << ((r - 1) * 8 + (f + 1));
        }
        t[sq] = b;
    }
    return t;
}

}  // namespace

inline constexpr std::array<Bitboard, 64> knight_attacks = generate_knight_attacks();
inline constexpr std::array<Bitboard, 64> king_attacks = generate_king_attacks();
inline constexpr std::array<Bitboard, 64> pawn_attacks_white = generate_pawn_attacks_white();
inline constexpr std::array<Bitboard, 64> pawn_attacks_black = generate_pawn_attacks_black();

inline Bitboard pawn_attacks(Color c, Square s) {
    return c == Color::White ? pawn_attacks_white[static_cast<int>(s)] : pawn_attacks_black[static_cast<int>(s)];
}

// Sliding attacks via ray iteration.
inline Bitboard rook_attacks(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    int f = static_cast<int>(sq) & 7;
    int r = static_cast<int>(sq) >> 3;
    // north
    for (int rr = r + 1; rr < 8; ++rr) {
        Square s = make_square(f, rr);
        attacks |= square_bit(s);
        if (occ & square_bit(s)) break;
    }
    // south
    for (int rr = r - 1; rr >= 0; --rr) {
        Square s = make_square(f, rr);
        attacks |= square_bit(s);
        if (occ & square_bit(s)) break;
    }
    // east
    for (int ff = f + 1; ff < 8; ++ff) {
        Square s = make_square(ff, r);
        attacks |= square_bit(s);
        if (occ & square_bit(s)) break;
    }
    // west
    for (int ff = f - 1; ff >= 0; --ff) {
        Square s = make_square(ff, r);
        attacks |= square_bit(s);
        if (occ & square_bit(s)) break;
    }
    return attacks;
}

inline Bitboard bishop_attacks(Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    int f = static_cast<int>(sq) & 7;
    int r = static_cast<int>(sq) >> 3;
    const int df[] = {1, 1, -1, -1};
    const int dr[] = {1, -1, 1, -1};
    for (int d = 0; d < 4; ++d) {
        int ff = f, rr = r;
        while (true) {
            ff += df[d];
            rr += dr[d];
            if (ff < 0 || ff >= 8 || rr < 0 || rr >= 8) break;
            Square s = make_square(ff, rr);
            attacks |= square_bit(s);
            if (occ & square_bit(s)) break;
        }
    }
    return attacks;
}

inline Bitboard queen_attacks(Square sq, Bitboard occ) {
    return rook_attacks(sq, occ) | bishop_attacks(sq, occ);
}

// Forward declaration to break include cycle
class Position;

bool is_square_attacked(const Position& pos, Square sq, Color byColor);
