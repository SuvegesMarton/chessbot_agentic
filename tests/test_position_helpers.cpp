#include <array>
#include <cstdint>
#include <iostream>

#include "bitboard.hpp"
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

bool piece_matches(Piece p, Color c, PieceType t) {
    return p.color == c && p.type == t;
}

// ---------------------------------------------------------------------------
// piece_on
// ---------------------------------------------------------------------------

void piece_on_starting_position() {
    Position p;
    p.set_starting_position();

    check(piece_matches(p.piece_on(Square::A1), Color::White, PieceType::Rook),
          "piece_on A1 white rook");
    check(piece_matches(p.piece_on(Square::B1), Color::White, PieceType::Knight),
          "piece_on B1 white knight");
    check(piece_matches(p.piece_on(Square::C1), Color::White, PieceType::Bishop),
          "piece_on C1 white bishop");
    check(piece_matches(p.piece_on(Square::D1), Color::White, PieceType::Queen),
          "piece_on D1 white queen");
    check(piece_matches(p.piece_on(Square::E1), Color::White, PieceType::King),
          "piece_on E1 white king");
    check(piece_matches(p.piece_on(Square::E2), Color::White, PieceType::Pawn),
          "piece_on E2 white pawn");
    check(piece_matches(p.piece_on(Square::A8), Color::Black, PieceType::Rook),
          "piece_on A8 black rook");
    check(piece_matches(p.piece_on(Square::D8), Color::Black, PieceType::Queen),
          "piece_on D8 black queen");
    check(piece_matches(p.piece_on(Square::E8), Color::Black, PieceType::King),
          "piece_on E8 black king");
    check(piece_matches(p.piece_on(Square::E7), Color::Black, PieceType::Pawn),
          "piece_on E7 black pawn");

    // Empty squares report PieceType::None.
    check(p.piece_on(Square::E4).type == PieceType::None, "piece_on E4 empty -> None");
    check(p.piece_on(Square::D4).type == PieceType::None, "piece_on D4 empty -> None");
}

void piece_on_custom_fen() {
    Position p;
    check(p.set_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), "set_fen kings only");
    check(piece_matches(p.piece_on(Square::E1), Color::White, PieceType::King),
          "custom fen white king E1");
    check(piece_matches(p.piece_on(Square::E8), Color::Black, PieceType::King),
          "custom fen black king E8");
    check(p.piece_on(Square::A1).type == PieceType::None, "custom fen A1 empty");
    check(p.piece_on(Square::H8).type == PieceType::None, "custom fen H8 empty");
}

// ---------------------------------------------------------------------------
// board_snapshot
// ---------------------------------------------------------------------------

void board_snapshot_starting_position() {
    Position p;
    p.set_starting_position();
    std::array<Piece, 64> snap = p.board_snapshot();

    check(snap.size() == 64, "snapshot has 64 entries");
    check(piece_matches(snap[static_cast<int>(Square::A1)], Color::White, PieceType::Rook),
          "snapshot A1 white rook");
    check(piece_matches(snap[static_cast<int>(Square::E1)], Color::White, PieceType::King),
          "snapshot E1 white king");
    check(piece_matches(snap[static_cast<int>(Square::E2)], Color::White, PieceType::Pawn),
          "snapshot E2 white pawn");
    check(piece_matches(snap[static_cast<int>(Square::E8)], Color::Black, PieceType::King),
          "snapshot E8 black king");
    check(piece_matches(snap[static_cast<int>(Square::D8)], Color::Black, PieceType::Queen),
          "snapshot D8 black queen");
    check(snap[static_cast<int>(Square::E4)].type == PieceType::None,
          "snapshot E4 empty -> None");

    // Snapshot must agree with piece_on for every square.
    bool consistent = true;
    for (int sq = 0; sq < 64; ++sq) {
        Piece a = snap[sq];
        Piece b = p.piece_on(static_cast<Square>(sq));
        if (a.type != b.type) {
            consistent = false;
            break;
        }
    }
    check(consistent, "snapshot matches piece_on on all 64 squares");
}

void board_snapshot_empty_board() {
    Position p;
    check(p.set_fen("8/8/8/8/8/8/8/8 w - - 0 1"), "set_fen empty board");
    std::array<Piece, 64> snap = p.board_snapshot();
    bool all_empty = true;
    for (const Piece& pc : snap) {
        if (pc.type != PieceType::None) {
            all_empty = false;
            break;
        }
    }
    check(all_empty, "empty board snapshot is all None");
}

// ---------------------------------------------------------------------------
// is_square_attacked
// ---------------------------------------------------------------------------

void attacked_starting_position() {
    Position p;
    p.set_starting_position();

    // White pawn on C2 attacks D3 (and B3); black has nothing near rank 3.
    check(is_square_attacked(p, Square::D3, Color::White), "start D3 attacked by white");
    check(is_square_attacked(p, Square::B3, Color::White), "start B3 attacked by white");
    check(!is_square_attacked(p, Square::D3, Color::Black), "start D3 not attacked by black");

    // White knight on B1 attacks A3 and C3.
    check(is_square_attacked(p, Square::A3, Color::White), "start A3 attacked by knight");
    check(is_square_attacked(p, Square::C3, Color::White), "start C3 attacked by knight");

    // Black knight on B8 attacks A6 and C6.
    check(is_square_attacked(p, Square::A6, Color::Black), "start A6 attacked by black knight");
    check(is_square_attacked(p, Square::C6, Color::Black), "start C6 attacked by black knight");

    // E4 is untouched in the starting position.
    check(!is_square_attacked(p, Square::E4, Color::White), "start E4 not attacked by white");
    check(!is_square_attacked(p, Square::E4, Color::Black), "start E4 not attacked by black");

    // Squares occupied by white pieces are defended by white.
    check(is_square_attacked(p, Square::D2, Color::White), "start D2 defended by white");
}

void attacked_pawn_direction() {
    Position p;
    check(p.set_fen("4k3/8/8/8/4P3/8/8/4K3 w - - 0 1"), "set_fen white pawn e4");
    // White pawn on E4 attacks D5 and F5 (forward only).
    check(is_square_attacked(p, Square::D5, Color::White), "white pawn e4 attacks D5");
    check(is_square_attacked(p, Square::F5, Color::White), "white pawn e4 attacks F5");
    check(!is_square_attacked(p, Square::D3, Color::White), "white pawn e4 does not attack D3");
    check(!is_square_attacked(p, Square::F3, Color::White), "white pawn e4 does not attack F3");
    check(!is_square_attacked(p, Square::D5, Color::Black), "black does not attack D5 here");

    Position pb;
    check(pb.set_fen("4k3/8/8/4p3/8/8/8/4K3 b - - 0 1"), "set_fen black pawn e5");
    // Black pawn on E5 attacks D4 and F4 (down the board).
    check(is_square_attacked(pb, Square::D4, Color::Black), "black pawn e5 attacks D4");
    check(is_square_attacked(pb, Square::F4, Color::Black), "black pawn e5 attacks F4");
    check(!is_square_attacked(pb, Square::D6, Color::Black), "black pawn e5 does not attack D6");
}

void attacked_knight_and_king() {
    Position p;
    check(p.set_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), "set_fen kings only");
    // White king on E1 attacks D1, D2, E2, F2, F1.
    check(is_square_attacked(p, Square::D1, Color::White), "white king e1 attacks D1");
    check(is_square_attacked(p, Square::E2, Color::White), "white king e1 attacks E2");
    check(is_square_attacked(p, Square::F2, Color::White), "white king e1 attacks F2");
    check(!is_square_attacked(p, Square::E3, Color::White), "white king e1 not attack E3");
    check(!is_square_attacked(p, Square::E1, Color::White), "king does not attack its own square");

    Position kn;
    check(kn.set_fen("4k3/8/8/8/5N2/8/8/4K3 w - - 0 1"), "set_fen white knight f4");
    // Knight on F4 attacks D3, D5, E2, E6, G2, G6, H3, H5.
    check(is_square_attacked(kn, Square::D3, Color::White), "knight f4 attacks D3");
    check(is_square_attacked(kn, Square::D5, Color::White), "knight f4 attacks D5");
    check(is_square_attacked(kn, Square::E6, Color::White), "knight f4 attacks E6");
    check(is_square_attacked(kn, Square::H5, Color::White), "knight f4 attacks H5");
    check(!is_square_attacked(kn, Square::F5, Color::White), "knight f4 does not attack F5");
}

void attacked_sliding_pieces() {
    // Rook on an open file/rank.
    Position rook;
    check(rook.set_fen("4k3/8/8/8/8/8/8/4R1K1 w - - 0 1"), "set_fen white rook e1");
    check(is_square_attacked(rook, Square::E5, Color::White), "rook e1 attacks E5");
    check(is_square_attacked(rook, Square::A1, Color::White), "rook e1 attacks A1");
    check(is_square_attacked(rook, Square::E8, Color::White), "rook e1 attacks E8");
    check(!is_square_attacked(rook, Square::D4, Color::White), "rook e1 does not attack D4");

    // Bishop on diagonals.
    Position bishop;
    check(bishop.set_fen("4k3/8/8/8/8/2B5/8/4K3 w - - 0 1"), "set_fen white bishop c3");
    check(is_square_attacked(bishop, Square::A1, Color::White), "bishop c3 attacks A1");
    check(is_square_attacked(bishop, Square::H8, Color::White), "bishop c3 attacks H8");
    check(is_square_attacked(bishop, Square::A5, Color::White), "bishop c3 attacks A5");
    check(is_square_attacked(bishop, Square::D2, Color::White), "bishop c3 attacks D2 (diagonal)");
    check(!is_square_attacked(bishop, Square::D3, Color::White), "bishop c3 does not attack D3");

    // Queen combines rook and bishop lines.
    Position queen;
    check(queen.set_fen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"), "set_fen white queen d1");
    check(is_square_attacked(queen, Square::D8, Color::White), "queen d1 attacks D8 (file)");
    check(is_square_attacked(queen, Square::H5, Color::White), "queen d1 attacks H5 (diagonal)");
    check(is_square_attacked(queen, Square::E2, Color::White), "queen d1 attacks E2 (diagonal)");
    check(!is_square_attacked(queen, Square::E3, Color::White), "queen d1 does not attack E3");
}

void attacked_sliding_blocked_by_piece() {
    // White rook on E1, white pawn on E4: pawn blocks the rook's view up the file.
    Position p;
    check(p.set_fen("4k3/8/8/8/4P3/8/8/4R1K1 w - - 0 1"), "set_fen rook e1 pawn e4");
    check(is_square_attacked(p, Square::E4, Color::White), "rook attacks blocked pawn square E4");
    check(is_square_attacked(p, Square::E2, Color::White), "rook attacks E2 below pawn");
    // Rook view up the file stops at the pawn on E4; E5 is beyond it. The pawn
    // itself only attacks D5/F5, so E5 is not attacked by White at all.
    check(!is_square_attacked(p, Square::E5, Color::White), "E5 not attacked (rook blocked by pawn)");
}

}  // namespace

int main() {
    piece_on_starting_position();
    piece_on_custom_fen();
    board_snapshot_starting_position();
    board_snapshot_empty_board();
    attacked_starting_position();
    attacked_pawn_direction();
    attacked_knight_and_king();
    attacked_sliding_pieces();
    attacked_sliding_blocked_by_piece();

    if (failures == 0) {
        std::cout << "All position helper tests passed\n";
        return 0;
    }

    std::cerr << failures << " position helper test(s) failed\n";
    return 1;
}