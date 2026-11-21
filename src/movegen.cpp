#include "movegen.hpp"

#include "bitboard.hpp"
#include "position.hpp"

namespace {

inline Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

constexpr std::uint8_t castling_wk = 1 << 0;
constexpr std::uint8_t castling_wq = 1 << 1;
constexpr std::uint8_t castling_bk = 1 << 2;
constexpr std::uint8_t castling_bq = 1 << 3;

void add_quiet_moves(MoveList& out, Square from, Bitboard targets) {
    while (targets) {
        Square to = square_of(targets);
        targets &= targets - 1;
        out.push(Move{from, to, PieceType::None});
    }
}

void add_capture_moves(MoveList& out, Square from, Bitboard targets) {
    while (targets) {
        Square to = square_of(targets);
        targets &= targets - 1;
        out.push(Move{from, to, PieceType::None});
    }
}

void add_promotion_moves(MoveList& out, Square from, Square to) {
    out.push(Move{from, to, PieceType::Queen});
    out.push(Move{from, to, PieceType::Rook});
    out.push(Move{from, to, PieceType::Bishop});
    out.push(Move{from, to, PieceType::Knight});
}

} // namespace

namespace movegen {

void generate_pseudo_legal(const Position& pos, MoveList& out) {
    out.count = 0;

    const Color us = pos.side_to_move;
    const Color them = opposite(us);
    const Bitboard own_occ = pos.occupancy[static_cast<int>(us)];
    const Bitboard enemy_occ = pos.occupancy[static_cast<int>(them)];
    const Bitboard all_occ = pos.all_occupancy;
    const int pawn_dir = (us == Color::White) ? 8 : -8;
    const int pawn_start_rank = (us == Color::White) ? 1 : 6;
    const int pawn_promo_rank = (us == Color::White) ? 7 : 0;

    // Pawns
    Bitboard pawns = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::Pawn)];
    while (pawns) {
        Square from = square_of(pawns);
        pawns &= pawns - 1;

        const int r = rank_of(from);
        const int f = file_of(from);
        const Square to = make_square(f, r + pawn_dir / 8);

        // Single push
        if (to != Square::None && (all_occ & square_bit(to)) == 0) {
            if (rank_of(to) == pawn_promo_rank) {
                add_promotion_moves(out, from, to);
            } else {
                out.push(Move{from, to, PieceType::None});

                // Double push
                if (r == pawn_start_rank) {
                    const Square to2 = make_square(f, r + 2 * pawn_dir / 8);
                    if (to2 != Square::None && (all_occ & square_bit(to2)) == 0) {
                        if (rank_of(to2) == pawn_promo_rank) {
                            add_promotion_moves(out, from, to2);
                        } else {
                            out.push(Move{from, to2, PieceType::None});
                        }
                    }
                }
            }
        }

        // Captures
        const Bitboard* pawn_attacks_bb = (us == Color::White) ? pawn_attacks_white.data() : pawn_attacks_black.data();
        Bitboard attacks = pawn_attacks_bb[static_cast<int>(from)] & enemy_occ;
        while (attacks) {
            Square to_cap = square_of(attacks);
            attacks &= attacks - 1;
            if (rank_of(to_cap) == pawn_promo_rank) {
                add_promotion_moves(out, from, to_cap);
            } else {
                out.push(Move{from, to_cap, PieceType::None});
            }
        }

        // En passant
        if (pos.en_passant != Square::None) {
            Bitboard ep_attacks = pawn_attacks_bb[static_cast<int>(from)] & square_bit(pos.en_passant);
            if (ep_attacks) {
                out.push(Move{from, pos.en_passant, PieceType::None});
            }
        }
    }

    // Knights
    Bitboard knights = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::Knight)];
    while (knights) {
        Square from = square_of(knights);
        knights &= knights - 1;
        Bitboard targets = knight_attacks[static_cast<int>(from)] & ~own_occ;
        add_quiet_moves(out, from, targets & ~enemy_occ);
        add_capture_moves(out, from, targets & enemy_occ);
    }

    // Bishops
    Bitboard bishops = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::Bishop)];
    while (bishops) {
        Square from = square_of(bishops);
        bishops &= bishops - 1;
        Bitboard targets = bishop_attacks(from, all_occ) & ~own_occ;
        add_quiet_moves(out, from, targets & ~enemy_occ);
        add_capture_moves(out, from, targets & enemy_occ);
    }

    // Rooks
    Bitboard rooks = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::Rook)];
    while (rooks) {
        Square from = square_of(rooks);
        rooks &= rooks - 1;
        Bitboard targets = rook_attacks(from, all_occ) & ~own_occ;
        add_quiet_moves(out, from, targets & ~enemy_occ);
        add_capture_moves(out, from, targets & enemy_occ);
    }

    // Queens
    Bitboard queens = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::Queen)];
    while (queens) {
        Square from = square_of(queens);
        queens &= queens - 1;
        Bitboard targets = queen_attacks(from, all_occ) & ~own_occ;
        add_quiet_moves(out, from, targets & ~enemy_occ);
        add_capture_moves(out, from, targets & enemy_occ);
    }

    // King
    Bitboard king_bb = pos.pieces[static_cast<int>(us)][static_cast<int>(PieceType::King)];
    if (king_bb) {
        Square from = square_of(king_bb);
        Bitboard targets = king_attacks[static_cast<int>(from)] & ~own_occ;
        add_quiet_moves(out, from, targets & ~enemy_occ);
        add_capture_moves(out, from, targets & enemy_occ);

        // Castling
        if (!in_check(pos, us)) {
            const int rank = rank_of(from);
            // Kingside
            if ((us == Color::White && (pos.castling_rights & castling_wk)) ||
                (us == Color::Black && (pos.castling_rights & castling_bk))) {
                Square f1 = make_square(5, rank);
                Square g1 = make_square(6, rank);
                if ((all_occ & (square_bit(f1) | square_bit(g1))) == 0) {
                    if (!is_square_attacked(pos, f1, them) && !is_square_attacked(pos, g1, them)) {
                        out.push(Move{from, g1, PieceType::None});
                    }
                }
            }
            // Queenside
            if ((us == Color::White && (pos.castling_rights & castling_wq)) ||
                (us == Color::Black && (pos.castling_rights & castling_bq))) {
                Square d1 = make_square(3, rank);
                Square c1 = make_square(2, rank);
                Square b1 = make_square(1, rank);
                if ((all_occ & (square_bit(d1) | square_bit(c1) | square_bit(b1))) == 0) {
                    if (!is_square_attacked(pos, d1, them) && !is_square_attacked(pos, c1, them)) {
                        out.push(Move{from, c1, PieceType::None});
                    }
                }
            }
        }
    }
}

void generate_legal(Position& pos, MoveList& out) {
    out.count = 0;
    MoveList pseudo;
    generate_pseudo_legal(pos, pseudo);

    const Color us = pos.side_to_move;
    const Color them = opposite(us);

    for (const Move& m : pseudo) {
        Undo u = pos.make_move(m);
        Square ks = pos.king_square(us);
        if (ks != Square::None && !is_square_attacked(pos, ks, them)) {
            out.push(m);
        }
        pos.unmake_move(u);
    }
}

bool in_check(const Position& pos, Color c) {
    return pos.in_check(c);
}

bool is_checkmate(Position& pos) {
    if (!in_check(pos, pos.side_to_move)) return false;
    MoveList legal;
    generate_legal(pos, legal);
    return legal.size() == 0;
}

bool is_stalemate(Position& pos) {
    if (in_check(pos, pos.side_to_move)) return false;
    MoveList legal;
    generate_legal(pos, legal);
    return legal.size() == 0;
}

std::uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;
    MoveList list;
    generate_legal(pos, list);
    if (depth == 1) return list.size();
    std::uint64_t nodes = 0;
    for (const Move& m : list) {
        Undo u = pos.make_move(m);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(u);
    }
    return nodes;
}

} // namespace movegen