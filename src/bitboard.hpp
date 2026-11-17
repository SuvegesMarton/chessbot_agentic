#pragma once

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

inline Bitboard north(Bitboard b) { return b << 8; }
inline Bitboard south(Bitboard b) { return b >> 8; }
inline Bitboard east(Bitboard b) { return (b & ~Bitboard{0x8080808080808080ULL}) << 1; }
inline Bitboard west(Bitboard b) { return (b & ~Bitboard{0x0101010101010101ULL}) >> 1; }
inline Bitboard north_east(Bitboard b) { return (b & ~Bitboard{0x8080808080808080ULL}) << 9; }
inline Bitboard north_west(Bitboard b) { return (b & ~Bitboard{0x0101010101010101ULL}) << 7; }
inline Bitboard south_east(Bitboard b) { return (b & ~Bitboard{0x8080808080808080ULL}) >> 7; }
inline Bitboard south_west(Bitboard b) { return (b & ~Bitboard{0x0101010101010101ULL}) >> 9; }
