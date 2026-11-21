#include "game/eval.hpp"
#include "game/position.hpp"
#include "core/bitboard.hpp"

#include <array>
#include <algorithm>

namespace eval {

namespace {

constexpr int MATE = 30000;

constexpr int material_value[6] = {
    100,   // Pawn
    320,   // Knight
    330,   // Bishop
    500,   // Rook
    900,   // Queen
    0      // King
};

constexpr int phase_weight[6] = {
    0,     // Pawn
    1,     // Knight
    1,     // Bishop
    2,     // Rook
    4,     // Queen
    0      // King
};

inline constexpr std::array<int, 64> PAWN_MG = {
    0,   0,   0,   0,   0,   0,   0,   0,
    50,  50,  50,  50,  50,  50,  50,  50,
    10,  10,  20,  30,  30,  20,  10,  10,
    5,   5,   10,  25,  25,  10,  5,   5,
    0,   0,   0,   20,  20,  0,   0,   0,
    5,   -5,  -10, 0,   0,   -10, -5,  5,
    5,   10,  10,  -20, -20, 10,  10,  5,
    0,   0,   0,   0,   0,   0,   0,   0
};

inline constexpr std::array<int, 64> PAWN_EG = {
    0,   0,   0,   0,   0,   0,   0,   0,
    80,  80,  80,  80,  80,  80,  80,  80,
    50,  50,  50,  50,  50,  50,  50,  50,
    30,  30,  30,  30,  30,  30,  30,  30,
    20,  20,  20,  20,  20,  20,  20,  20,
    10,  10,  10,  10,  10,  10,  10,  10,
    5,   5,   5,   5,   5,   5,   5,   5,
    0,   0,   0,   0,   0,   0,   0,   0
};

inline constexpr std::array<int, 64> KNIGHT_MG = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0,   0,   0,   0,   -20, -40,
    -30, 0,   10,  15,  15,  10,  0,   -30,
    -30, 5,   15,  20,  20,  15,  5,   -30,
    -30, 0,   15,  20,  20,  15,  0,   -30,
    -30, 5,   10,  15,  15,  10,  5,   -30,
    -40, -20, 0,   5,   5,   0,   -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

inline constexpr std::array<int, 64> KNIGHT_EG = {
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20, 0,   0,   0,   0,   -20, -40,
    -30, 5,   10,  10,  10,  10,  5,   -30,
    -30, 0,   10,  10,  10,  10,  0,   -30,
    -30, 5,   10,  10,  10,  10,  5,   -30,
    -30, 0,   10,  10,  10,  10,  0,   -30,
    -40, -20, 0,   0,   0,   0,   -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50
};

inline constexpr std::array<int, 64> BISHOP_MG = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   5,   10,  10,  5,   0,   -10,
    -10, 5,   5,   10,  10,  5,   5,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10,
    -10, 10,  10,  10,  10,  10,  10,  -10,
    -10, 5,   0,   0,   0,   0,   5,   -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

inline constexpr std::array<int, 64> BISHOP_EG = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10,
    -10, 0,   10,  10,  10,  10,  0,   -10,
    -10, 10,  10,  10,  10,  10,  10,  -10,
    -10, 5,   0,   0,   0,   0,   5,   -10,
    -20, -10, -10, -10, -10, -10, -10, -20
};

inline constexpr std::array<int, 64> ROOK_MG = {
    0,   0,   0,   0,   0,   0,   0,   0,
    5,   10,  10,  10,  10,  10,  10,  5,
    -5,  0,   0,   0,   0,   0,   0,   -5,
    -5,  0,   0,   0,   0,   0,   0,   -5,
    -5,  0,   0,   0,   0,   0,   0,   -5,
    -5,  0,   0,   0,   0,   0,   0,   -5,
    -5,  0,   0,   0,   0,   0,   0,   -5,
    0,   0,   0,   5,   5,   0,   0,   0
};

inline constexpr std::array<int, 64> ROOK_EG = {
    0,   0,   0,   0,   0,   0,   0,   0,
    5,   10,  10,  10,  10,  10,  10,  5,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0
};

inline constexpr std::array<int, 64> QUEEN_MG = {
    -20, -10, -10, -5,  -5,  -10, -10, -20,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   5,   5,   5,   5,   0,   -10,
    -5,  0,   5,   5,   5,   5,   0,   -5,
    0,   0,   5,   5,   5,   5,   0,   -5,
    -10, 5,   5,   5,   5,   5,   0,   -10,
    -10, 0,   5,   0,   0,   0,   0,   -10,
    -20, -10, -10, -5,  -5,  -10, -10, -20
};

inline constexpr std::array<int, 64> QUEEN_EG = {
    -20, -10, -10, -5,  -5,  -10, -10, -20,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -10, 0,   5,   5,   5,   5,   0,   -10,
    -5,  0,   5,   5,   5,   5,   0,   -5,
    0,   0,   5,   5,   5,   5,   0,   -5,
    -10, 5,   5,   5,   5,   5,   0,   -10,
    -10, 0,   0,   0,   0,   0,   0,   -10,
    -20, -10, -10, -5,  -5,  -10, -10, -20
};

inline constexpr std::array<int, 64> KING_MG = {
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
    20,  20,  0,   0,   0,   0,   20,  20,
    20,  30,  10,  0,   0,   10,  30,  20
};

inline constexpr std::array<int, 64> KING_EG = {
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10, 0,   0,   -10, -20, -30,
    -30, -10, 20,  30,  30,  20,  -10, -30,
    -30, -10, 30,  40,  40,  30,  -10, -30,
    -30, -10, 30,  40,  40,  30,  -10, -30,
    -30, -10, 20,  30,  30,  20,  -10, -30,
    -30, -30, 0,   0,   0,   0,   -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50
};

inline const std::array<int, 64>& pst_mg(PieceType pt) {
    switch (pt) {
        case PieceType::Pawn:   return PAWN_MG;
        case PieceType::Knight: return KNIGHT_MG;
        case PieceType::Bishop: return BISHOP_MG;
        case PieceType::Rook:   return ROOK_MG;
        case PieceType::Queen:  return QUEEN_MG;
        case PieceType::King:   return KING_MG;
        default:                return PAWN_MG;
    }
}

inline const std::array<int, 64>& pst_eg(PieceType pt) {
    switch (pt) {
        case PieceType::Pawn:   return PAWN_EG;
        case PieceType::Knight: return KNIGHT_EG;
        case PieceType::Bishop: return BISHOP_EG;
        case PieceType::Rook:   return ROOK_EG;
        case PieceType::Queen:  return QUEEN_EG;
        case PieceType::King:   return KING_EG;
        default:                return PAWN_EG;
    }
}

inline int piece_value(PieceType pt) {
    return material_value[static_cast<int>(pt)];
}

inline int piece_phase_weight(PieceType pt) {
    return phase_weight[static_cast<int>(pt)];
}

inline int pst_value(const std::array<int, 64>& table, Square s, Color c) {
    int idx = static_cast<int>(s);
    if (c == Color::White) idx ^= 56;
    return table[idx];
}

inline Bitboard piece_bb(const Position& pos, Color c, PieceType pt) {
    return pos.pieces[static_cast<int>(c)][static_cast<int>(pt)];
}

} // namespace

int evaluate(const Position& pos) {
    int mg = 0;
    int eg = 0;
    int phase = 0;

    for (int c = 0; c < 2; ++c) {
        Color color = static_cast<Color>(c);
        int sign = (color == Color::White) ? 1 : -1;

        for (int t = 0; t < 6; ++t) {
            PieceType pt = static_cast<PieceType>(t);
            Bitboard bb = piece_bb(pos, color, pt);
            while (bb) {
                Square sq = square_of(bb);
                bb &= bb - 1;
                mg += sign * pst_value(pst_mg(pt), sq, color);
                eg += sign * pst_value(pst_eg(pt), sq, color);
                phase += piece_phase_weight(pt);
            }
        }
    }

    phase = std::clamp(phase, 0, 24);
    int pst = (mg * phase + eg * (24 - phase)) / 24;

    int material = 0;
    for (int c = 0; c < 2; ++c) {
        Color color = static_cast<Color>(c);
        int sign = (color == Color::White) ? 1 : -1;
        for (int t = 0; t < 6; ++t) {
            PieceType pt = static_cast<PieceType>(t);
            int count = popcount(piece_bb(pos, color, pt));
            material += sign * count * piece_value(pt);
        }
    }

    int mobility = 0;
    for (int c = 0; c < 2; ++c) {
        Color color = static_cast<Color>(c);
        int sign = (color == Color::White) ? 1 : -1;
        const Bitboard own_occ = pos.occupancy[c];

        Bitboard knights = piece_bb(pos, color, PieceType::Knight);
        while (knights) {
            Square sq = square_of(knights);
            knights &= knights - 1;
            mobility += sign * 2 * popcount(knight_attacks[static_cast<int>(sq)] & ~own_occ);
        }

        Bitboard bishops = piece_bb(pos, color, PieceType::Bishop);
        while (bishops) {
            Square sq = square_of(bishops);
            bishops &= bishops - 1;
            mobility += sign * 2 * popcount(bishop_attacks(sq, pos.all_occupancy) & ~own_occ);
        }

        Bitboard rooks = piece_bb(pos, color, PieceType::Rook);
        while (rooks) {
            Square sq = square_of(rooks);
            rooks &= rooks - 1;
            mobility += sign * 1 * popcount(rook_attacks(sq, pos.all_occupancy) & ~own_occ);
        }

        Bitboard queens = piece_bb(pos, color, PieceType::Queen);
        while (queens) {
            Square sq = square_of(queens);
            queens &= queens - 1;
            mobility += sign * 1 * popcount(queen_attacks(sq, pos.all_occupancy) & ~own_occ);
        }
    }

    int white_score = material + pst + mobility;
    return (pos.side_to_move == Color::White) ? white_score : -white_score;
}

} // namespace eval