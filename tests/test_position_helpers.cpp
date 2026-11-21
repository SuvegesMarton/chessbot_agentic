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
// to_fen
// ---------------------------------------------------------------------------

void to_fen_starting_position() {
    Position p;
    p.set_starting_position();
    check(p.to_fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          "to_fen startpos");
}

void to_fen_roundtrip() {
    const char* fens[] = {
        "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
        "4k3/8/8/8/8/8/8/4K3 b - - 0 1",
        "8/8/8/8/8/8/4k3/4K3 w - - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    };
    for (const char* fen : fens) {
        Position p;
        check(p.set_fen(fen), "to_fen roundtrip set_fen");
        check(p.to_fen() == fen, "to_fen roundtrip exact");
    }
}

void to_fen_after_move() {
    Position p;
    p.set_starting_position();
    Move e2e4{Square::E2, Square::E4, PieceType::None};
    p.make_move(e2e4);
    check(p.to_fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
          "to_fen after e2e4");
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

// ---------------------------------------------------------------------------
// make_move / unmake_move reversibility and hash consistency
// ---------------------------------------------------------------------------

void make_unmake_reversibility_captures() {
    Position p;
    p.set_starting_position();
    std::uint64_t h0 = p.hash();

    // White plays e2e4, black e7e5, white d2d4, black captures e5d4
    Move m1{Square::E2, Square::E4, PieceType::None};
    Move m2{Square::E7, Square::E5, PieceType::None};
    Move m3{Square::D2, Square::D4, PieceType::None};
    Move m4{Square::E5, Square::D4, PieceType::None};

    Undo u1 = p.make_move(m1);
    Undo u2 = p.make_move(m2);
    Undo u3 = p.make_move(m3);
    Undo u4 = p.make_move(m4);

    // After black captures e5d4, black pawn is on d4
    check(p.piece_on(Square::D4).color == Color::Black && p.piece_on(Square::D4).type == PieceType::Pawn,
          "capture: black pawn on d4");
    check(p.piece_on(Square::E5).type == PieceType::None, "capture: e5 empty after capture");

    p.unmake_move(u4);
    // After unmaking capture: white pawn on d4, black pawn on e5
    check(p.piece_on(Square::D4).color == Color::White && p.piece_on(Square::D4).type == PieceType::Pawn,
          "unmake capture: white pawn on d4");
    check(p.piece_on(Square::E5).color == Color::Black && p.piece_on(Square::E5).type == PieceType::Pawn,
          "unmake capture: black pawn on e5");

    p.unmake_move(u3);
    // After unmaking d2d4: white pawn on d2
    check(p.piece_on(Square::D2).color == Color::White && p.piece_on(Square::D2).type == PieceType::Pawn,
          "unmake d2d4: white pawn on d2");

    p.unmake_move(u2);
    // After unmaking e7e5: black pawn on e7
    check(p.piece_on(Square::E7).color == Color::Black && p.piece_on(Square::E7).type == PieceType::Pawn,
          "unmake e7e5: black pawn on e7");

    p.unmake_move(u1);
    // After unmaking e2e4: white pawn on e2, back to startpos
    check(p.piece_on(Square::E2).color == Color::White && p.piece_on(Square::E2).type == PieceType::Pawn,
          "unmake e2e4: white pawn on e2");

    check(p.hash() == h0, "hash restored after full unmake");
    check(p.side_to_move == Color::White, "side restored after unmake");
}

void make_unmake_reversibility_promotions() {
    Position p;
    check(p.set_fen("8/4P3/8/8/8/8/8/4k3 w - - 0 1"), "set_fen promotion test");
    std::uint64_t h0 = p.hash();

    // White promotes e7-e8=Q
    Move m{Square::E7, Square::E8, PieceType::Queen};
    Undo u = p.make_move(m);

    check(p.piece_on(Square::E8).color == Color::White && p.piece_on(Square::E8).type == PieceType::Queen,
          "promotion: queen on e8");
    check(p.piece_on(Square::E7).type == PieceType::None, "promotion: e7 empty");

    p.unmake_move(u);

    check(p.hash() == h0, "hash restored after promotion unmake");
    check(p.piece_on(Square::E7).color == Color::White && p.piece_on(Square::E7).type == PieceType::Pawn,
          "pawn restored on e7");
    check(p.piece_on(Square::E8).type == PieceType::None, "e8 empty after unmake");
}

void make_unmake_reversibility_castling() {
    Position p;
    check(p.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), "set_fen castling test");
    std::uint64_t h0 = p.hash();

    // White kingside castling: e1g1
    Move m1{Square::E1, Square::G1, PieceType::None};
    Undo u1 = p.make_move(m1);

    check(p.piece_on(Square::G1).color == Color::White && p.piece_on(Square::G1).type == PieceType::King,
          "castling: king on g1");
    check(p.piece_on(Square::F1).color == Color::White && p.piece_on(Square::F1).type == PieceType::Rook,
          "castling: rook on f1");
    check(p.piece_on(Square::E1).type == PieceType::None, "castling: e1 empty");
    check(p.piece_on(Square::H1).type == PieceType::None, "castling: h1 empty");

    p.unmake_move(u1);

    check(p.hash() == h0, "hash restored after castling unmake");
    check(p.piece_on(Square::E1).color == Color::White && p.piece_on(Square::E1).type == PieceType::King,
          "king restored on e1");
    check(p.piece_on(Square::H1).color == Color::White && p.piece_on(Square::H1).type == PieceType::Rook,
          "rook restored on h1");
    check(p.castling_rights == 0xF, "castling rights restored");

    // Black queenside castling: e8c8
    p.set_fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1");
    h0 = p.hash();

    Move m2{Square::E8, Square::C8, PieceType::None};
    Undo u2 = p.make_move(m2);

    check(p.piece_on(Square::C8).color == Color::Black && p.piece_on(Square::C8).type == PieceType::King,
          "black castling: king on c8");
    check(p.piece_on(Square::D8).color == Color::Black && p.piece_on(Square::D8).type == PieceType::Rook,
          "black castling: rook on d8");

    p.unmake_move(u2);

    check(p.hash() == h0, "hash restored after black castling unmake");
    check(p.piece_on(Square::E8).color == Color::Black && p.piece_on(Square::E8).type == PieceType::King,
          "black king restored on e8");
    check(p.piece_on(Square::A8).color == Color::Black && p.piece_on(Square::A8).type == PieceType::Rook,
          "black rook restored on a8");
}

void make_unmake_reversibility_en_passant() {
    Position p;
    // White pawn on e4, black pawn on f4, black to move, ep square e3
    check(p.set_fen("rnbqkbnr/pppppppp/8/8/4Pp2/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 3"), "set_fen en passant");
    std::uint64_t h0 = p.hash();

    // Black captures en passant: f4e3 (black pawn on f4 captures to e3)
    Move m{Square::F4, Square::E3, PieceType::None};
    Undo u = p.make_move(m);

    check(p.piece_on(Square::E3).color == Color::Black && p.piece_on(Square::E3).type == PieceType::Pawn,
          "en passant: black pawn on e3");
    check(p.piece_on(Square::E4).type == PieceType::None, "en passant: e4 empty (captured pawn removed)");
    check(p.piece_on(Square::F4).type == PieceType::None, "en passant: f4 empty (capturing pawn moved)");

    p.unmake_move(u);

    check(p.hash() == h0, "hash restored after en passant unmake");
    check(p.piece_on(Square::E4).color == Color::White && p.piece_on(Square::E4).type == PieceType::Pawn,
          "white pawn restored on e4");
    check(p.piece_on(Square::F4).color == Color::Black && p.piece_on(Square::F4).type == PieceType::Pawn,
          "black pawn restored on f4");
}

void make_unmake_reversibility_castling_rights() {
    // Moving king or rook should lose castling rights
    Position p;
    p.set_starting_position();
    std::uint64_t h0 = p.hash();

    // White king moves
    Move m1{Square::E1, Square::E2, PieceType::None};
    Undo u1 = p.make_move(m1);
    check(p.castling_rights == 0xC, "white king move loses KQ rights"); // only black rights remain

    p.unmake_move(u1);
    check(p.castling_rights == 0xF, "castling rights restored after king move unmake");
    check(p.hash() == h0, "hash restored");

    // White rook moves from h1
    p.set_starting_position();
    Move m2{Square::H1, Square::H2, PieceType::None};
    Undo u2 = p.make_move(m2);
    check((p.castling_rights & 0x1) == 0, "white rook move loses K right");

    p.unmake_move(u2);
    check(p.castling_rights == 0xF, "castling rights restored after rook move unmake");

    // Capturing on rook square loses opponent's castling rights
    p.set_fen("r3k2r/8/8/8/8/8/8/7R w KQkq - 0 1");
    Move m3{Square::H1, Square::A8, PieceType::None}; // capture black rook on a8
    Undo u3 = p.make_move(m3);
    check((p.castling_rights & 0x8) == 0, "capture on a8 loses black Q right");

    p.unmake_move(u3);
    check(p.castling_rights == 0xF, "castling rights restored after capture unmake");
}

void make_unmake_reversibility_halfmove_clock() {
    Position p;
    p.set_starting_position();
    check(p.halfmove_clock == 0, "start halfmove clock is 0");

    // Non-pawn, non-capture: increments
    Move m1{Square::G1, Square::F3, PieceType::None}; // Nf3
    Undo u1 = p.make_move(m1);
    check(p.halfmove_clock == 1, "halfmove clock increments after knight move");

    Move m2{Square::G8, Square::F6, PieceType::None}; // Nf6
    Undo u2 = p.make_move(m2);
    check(p.halfmove_clock == 2, "halfmove clock increments again");

    // Pawn move: resets to 0
    Move m3{Square::E2, Square::E4, PieceType::None};
    Undo u3 = p.make_move(m3);
    check(p.halfmove_clock == 0, "halfmove clock resets after pawn move");

    // Capture: resets to 0
    p.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    Move m4{Square::D7, Square::D5, PieceType::None}; // d5
    Undo u4 = p.make_move(m4);
    Move m5{Square::E4, Square::D5, PieceType::None}; // exd5
    Undo u5 = p.make_move(m5);
    check(p.halfmove_clock == 0, "halfmove clock resets after capture");

    p.unmake_move(u5);
    p.unmake_move(u4);
    p.unmake_move(u3);
    p.unmake_move(u2);
    p.unmake_move(u1);
    check(p.halfmove_clock == 0, "halfmove clock restored after full unmake");
}

void make_unmake_reversibility_fullmove_number() {
    Position p;
    p.set_starting_position();
    check(p.fullmove_number == 1, "start fullmove is 1");

    // White moves
    Move m1{Square::E2, Square::E4, PieceType::None};
    Undo u1 = p.make_move(m1);
    check(p.fullmove_number == 1, "fullmove stays 1 after white move");

    // Black moves
    Move m2{Square::E7, Square::E5, PieceType::None};
    Undo u2 = p.make_move(m2);
    check(p.fullmove_number == 2, "fullmove increments after black move");

    // White moves
    Move m3{Square::G1, Square::F3, PieceType::None};
    Undo u3 = p.make_move(m3);
    check(p.fullmove_number == 2, "fullmove stays 2 after white move");

    // Black moves
    Move m4{Square::G8, Square::F6, PieceType::None};
    Undo u4 = p.make_move(m4);
    check(p.fullmove_number == 3, "fullmove increments after black move");

    p.unmake_move(u4);
    check(p.fullmove_number == 2, "fullmove restored after unmake black");
    p.unmake_move(u3);
    check(p.fullmove_number == 2, "fullmove stays after unmake white");
    p.unmake_move(u2);
    check(p.fullmove_number == 1, "fullmove restored after unmake black");
    p.unmake_move(u1);
    check(p.fullmove_number == 1, "fullmove restored after unmake white");
}

void make_unmake_reversibility_en_passant_square() {
    Position p;
    p.set_starting_position();
    check(p.en_passant == Square::None, "start no ep square");

    // White double push creates ep square
    Move m1{Square::E2, Square::E4, PieceType::None};
    Undo u1 = p.make_move(m1);
    check(p.en_passant == Square::E3, "ep square set to e3 after e2e4");

    // Black non-pawn move clears ep square
    Move m2{Square::G8, Square::F6, PieceType::None};
    Undo u2 = p.make_move(m2);
    check(p.en_passant == Square::None, "ep square cleared after black non-pawn move");

    p.unmake_move(u2);
    check(p.en_passant == Square::E3, "ep square restored after unmake");
    p.unmake_move(u1);
    check(p.en_passant == Square::None, "ep square cleared after full unmake");
}

void make_unmake_reversibility_occupancy_bitboards() {
    Position p;
    p.set_starting_position();
    
    Bitboard occ0 = p.all_occupancy;
    Bitboard wocc0 = p.occupancy[0];
    Bitboard bocc0 = p.occupancy[1];

    Move m1{Square::E2, Square::E4, PieceType::None};
    Undo u1 = p.make_move(m1);

    // White pawn moved from e2 to e4
    check(!is_set(p.occupancy[0], Square::E2), "e2 cleared from white occupancy");
    check(is_set(p.occupancy[0], Square::E4), "e4 set in white occupancy");
    check(!is_set(p.all_occupancy, Square::E2), "e2 cleared from all occupancy");
    check(is_set(p.all_occupancy, Square::E4), "e4 set in all occupancy");

    p.unmake_move(u1);
    check(p.all_occupancy == occ0, "all occupancy restored after e2e4 unmake");
    check(p.occupancy[0] == wocc0, "white occupancy restored");
    check(p.occupancy[1] == bocc0, "black occupancy restored");

    // Capture test with fresh position
    p.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    Bitboard occ1 = p.all_occupancy;
    Bitboard wocc1 = p.occupancy[0];
    Bitboard bocc1 = p.occupancy[1];

    Move m2{Square::D7, Square::D5, PieceType::None};
    Undo u2 = p.make_move(m2);
    Move m3{Square::E4, Square::D5, PieceType::None};
    Undo u3 = p.make_move(m3);

    check(!is_set(p.occupancy[1], Square::D5), "captured black pawn removed from black occupancy");
    check(is_set(p.occupancy[0], Square::D5), "white pawn on d5 in white occupancy");

    p.unmake_move(u3);
    // After unmaking exd5: black pawn on d5
    check(is_set(p.occupancy[1], Square::D5), "black pawn restored on d5 after unmake capture");
    check(!is_set(p.occupancy[0], Square::D5), "white pawn removed from d5");

    p.unmake_move(u2);
    // After unmaking d7d5: black pawn on d7, d5 empty
    check(!is_set(p.occupancy[1], Square::D5), "d5 empty after full unmake");
    check(is_set(p.occupancy[1], Square::D7), "black pawn on d7 after full unmake");

    check(p.all_occupancy == occ1, "all occupancy restored after capture unmake");
    check(p.occupancy[0] == wocc1, "white occupancy restored");
    check(p.occupancy[1] == bocc1, "black occupancy restored");
}

int main() {
    piece_on_starting_position();
    piece_on_custom_fen();
    board_snapshot_starting_position();
    board_snapshot_empty_board();
    to_fen_starting_position();
    to_fen_roundtrip();
    to_fen_after_move();
    attacked_starting_position();
    attacked_pawn_direction();
    attacked_knight_and_king();
    attacked_sliding_pieces();
    attacked_sliding_blocked_by_piece();
    make_unmake_reversibility_captures();
    make_unmake_reversibility_promotions();
    make_unmake_reversibility_castling();
    make_unmake_reversibility_en_passant();
    make_unmake_reversibility_castling_rights();
    make_unmake_reversibility_halfmove_clock();
    make_unmake_reversibility_fullmove_number();
    make_unmake_reversibility_en_passant_square();
    make_unmake_reversibility_occupancy_bitboards();

    if (failures == 0) {
        std::cout << "All position helper tests passed\n";
        return 0;
    }

    std::cerr << failures << " position helper test(s) failed\n";
    return 1;
}