#include "position.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>

#include "bitboard.hpp"

namespace {

constexpr std::uint8_t castling_wk = 1 << 0;
constexpr std::uint8_t castling_wq = 1 << 1;
constexpr std::uint8_t castling_bk = 1 << 2;
constexpr std::uint8_t castling_bq = 1 << 3;

inline Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

struct Zobrist {
    std::array<std::array<std::array<std::uint64_t, 64>, 6>, 2> piece{};
    std::array<std::uint64_t, 16> castling{};
    std::array<std::uint64_t, 8> en_passant{};
    std::uint64_t side = 0;
};

const Zobrist& zobrist() {
    static const Zobrist table = [] {
        Zobrist z{};
        std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
        auto next = [&seed] {
            seed = splitmix64(seed);
            return seed;
        };
        for (int c = 0; c < 2; ++c)
            for (int t = 0; t < 6; ++t)
                for (int s = 0; s < 64; ++s)
                    z.piece[c][t][s] = next();
        for (int i = 0; i < 16; ++i) z.castling[i] = next();
        for (int f = 0; f < 8; ++f) z.en_passant[f] = next();
        z.side = next();
        return z;
    }();
    return table;
}

inline std::uint64_t piece_key(Color c, PieceType t, Square s) {
    return zobrist().piece[static_cast<int>(c)][static_cast<int>(t)][static_cast<int>(s)];
}

inline void xor_castling(std::uint64_t& h, std::uint8_t rights) {
    if (rights & castling_wk) h ^= zobrist().castling[0];
    if (rights & castling_wq) h ^= zobrist().castling[1];
    if (rights & castling_bk) h ^= zobrist().castling[2];
    if (rights & castling_bq) h ^= zobrist().castling[3];
}

inline void xor_en_passant(std::uint64_t& h, Square ep) {
    if (ep != Square::None) h ^= zobrist().en_passant[file_of(ep)];
}

std::uint64_t compute_hash(const Position& pos) {
    std::uint64_t h = 0;
    for (int c = 0; c < 2; ++c) {
        for (int t = 0; t < 6; ++t) {
            Bitboard b = pos.pieces[c][t];
            while (b) {
                Square s = square_of(b);
                b &= b - 1;
                h ^= piece_key(static_cast<Color>(c), static_cast<PieceType>(t), s);
            }
        }
    }
    xor_castling(h, pos.castling_rights);
    xor_en_passant(h, pos.en_passant);
    if (pos.side_to_move == Color::Black) h ^= zobrist().side;
    return h;
}

}  // namespace

Square Position::king_square(Color c) const {
    Bitboard kings = pieces[static_cast<int>(c)][static_cast<int>(PieceType::King)];
    if (kings == 0) return Square::None;
    return square_of(kings);
}

bool Position::in_check(Color c) const {
    Square ks = king_square(c);
    if (ks == Square::None) return false;
    return is_square_attacked(*this, ks, opposite(c));
}

bool is_square_attacked(const Position& pos, Square sq, Color byColor) {
    const int c = static_cast<int>(byColor);
    // knights
    if (knight_attacks[static_cast<int>(sq)] & pos.pieces[c][static_cast<int>(PieceType::Knight)]) return true;
    // king
    if (king_attacks[static_cast<int>(sq)] & pos.pieces[c][static_cast<int>(PieceType::King)]) return true;
    // pawns
    if (byColor == Color::White) {
        if (pawn_attacks_black[static_cast<int>(sq)] & pos.pieces[c][static_cast<int>(PieceType::Pawn)]) return true;
    } else {
        if (pawn_attacks_white[static_cast<int>(sq)] & pos.pieces[c][static_cast<int>(PieceType::Pawn)]) return true;
    }
    // sliding
    Bitboard rookQ = pos.pieces[c][static_cast<int>(PieceType::Rook)] | pos.pieces[c][static_cast<int>(PieceType::Queen)];
    Bitboard bishopQ = pos.pieces[c][static_cast<int>(PieceType::Bishop)] | pos.pieces[c][static_cast<int>(PieceType::Queen)];
    if (rook_attacks(sq, pos.all_occupancy) & rookQ) return true;
    if (bishop_attacks(sq, pos.all_occupancy) & bishopQ) return true;
    return false;
}

void Position::set_starting_position() {
    pieces = {};
    occupancy = {0, 0};
    all_occupancy = 0;
    side_to_move = Color::White;
    castling_rights = castling_wk | castling_wq | castling_bk | castling_bq;
    en_passant = Square::None;
    halfmove_clock = 0;
    fullmove_number = 1;

    auto place = [this](Color c, PieceType t, Square s) {
        pieces[static_cast<int>(c)][static_cast<int>(t)] |= square_bit(s);
        occupancy[static_cast<int>(c)] |= square_bit(s);
        all_occupancy |= square_bit(s);
    };

    const int back_rank[8] = {3, 1, 2, 4, 5, 2, 1, 3};  // R N B Q K B N R
    for (int f = 0; f < 8; ++f) {
        PieceType pt = static_cast<PieceType>(back_rank[f]);
        place(Color::White, pt, make_square(f, 0));
        place(Color::Black, pt, make_square(f, 7));
        place(Color::White, PieceType::Pawn, make_square(f, 1));
        place(Color::Black, PieceType::Pawn, make_square(f, 6));
    }

    zobrist_hash = compute_hash(*this);
}

bool Position::set_fen(std::string_view fen) {
    pieces = {};
    occupancy = {0, 0};
    all_occupancy = 0;
    side_to_move = Color::White;
    castling_rights = 0;
    en_passant = Square::None;
    halfmove_clock = 0;
    fullmove_number = 1;

    std::size_t i = 0;
    const auto skip_spaces = [&fen, &i] {
        while (i < fen.size() && fen[i] == ' ') ++i;
    };

    int rank = 7;
    int file = 0;
    while (i < fen.size() && fen[i] != ' ') {
        const char ch = fen[i];
        if (ch == '/') {
            --rank;
            file = 0;
        } else if (ch >= '1' && ch <= '8') {
            file += ch - '0';
        } else {
            Color color;
            PieceType type;
            switch (ch) {
                case 'P': color = Color::White; type = PieceType::Pawn; break;
                case 'N': color = Color::White; type = PieceType::Knight; break;
                case 'B': color = Color::White; type = PieceType::Bishop; break;
                case 'R': color = Color::White; type = PieceType::Rook; break;
                case 'Q': color = Color::White; type = PieceType::Queen; break;
                case 'K': color = Color::White; type = PieceType::King; break;
                case 'p': color = Color::Black; type = PieceType::Pawn; break;
                case 'n': color = Color::Black; type = PieceType::Knight; break;
                case 'b': color = Color::Black; type = PieceType::Bishop; break;
                case 'r': color = Color::Black; type = PieceType::Rook; break;
                case 'q': color = Color::Black; type = PieceType::Queen; break;
                case 'k': color = Color::Black; type = PieceType::King; break;
                default: return false;
            }
            const Square s = make_square(file, rank);
            pieces[static_cast<int>(color)][static_cast<int>(type)] |= square_bit(s);
            occupancy[static_cast<int>(color)] |= square_bit(s);
            all_occupancy |= square_bit(s);
            ++file;
        }
        ++i;
    }
    skip_spaces();
    if (i >= fen.size()) return false;

    side_to_move = (fen[i] == 'w') ? Color::White : Color::Black;
    ++i;
    skip_spaces();
    if (i >= fen.size()) return false;

    while (i < fen.size() && fen[i] != ' ') {
        switch (fen[i]) {
            case 'K': castling_rights |= castling_wk; break;
            case 'Q': castling_rights |= castling_wq; break;
            case 'k': castling_rights |= castling_bk; break;
            case 'q': castling_rights |= castling_bq; break;
            default: break;
        }
        ++i;
    }
    skip_spaces();
    if (i >= fen.size()) return false;

    if (fen[i] != '-') {
        if (i + 1 >= fen.size()) return false;
        const int f = fen[i] - 'a';
        const int r = fen[i + 1] - '1';
        if (f < 0 || f >= 8 || r < 0 || r >= 8) return false;
        en_passant = make_square(f, r);
        i += 2;
    } else {
        ++i;
    }
    skip_spaces();

    int value = 0;
    while (i < fen.size() && fen[i] >= '0' && fen[i] <= '9') {
        value = value * 10 + (fen[i] - '0');
        ++i;
    }
    halfmove_clock = static_cast<std::uint8_t>(value);

    skip_spaces();
    if (i >= fen.size()) {
        fullmove_number = 1;
    } else {
        value = 0;
        while (i < fen.size() && fen[i] >= '0' && fen[i] <= '9') {
            value = value * 10 + (fen[i] - '0');
            ++i;
        }
        fullmove_number = static_cast<std::uint16_t>(value);
    }

    zobrist_hash = compute_hash(*this);
    return true;
}

Undo Position::make_move(const Move& move) {
    Undo undo;
    undo.move = move;
    undo.en_passant = en_passant;
    undo.castling_rights = castling_rights;
    undo.halfmove_clock = halfmove_clock;
    undo.hash = zobrist_hash;

    xor_castling(zobrist_hash, castling_rights);
    xor_en_passant(zobrist_hash, en_passant);

    const Color us = side_to_move;
    const Color them = opposite(us);
    const Square from = move.from;
    const Square to = move.to;
    const PieceType promotion = move.promotion;

    auto remove_piece = [this](Color c, PieceType t, Square s) {
        pieces[static_cast<int>(c)][static_cast<int>(t)] &= ~square_bit(s);
        occupancy[static_cast<int>(c)] &= ~square_bit(s);
        all_occupancy &= ~square_bit(s);
        zobrist_hash ^= piece_key(c, t, s);
    };
    auto place_piece = [this](Color c, PieceType t, Square s) {
        pieces[static_cast<int>(c)][static_cast<int>(t)] |= square_bit(s);
        occupancy[static_cast<int>(c)] |= square_bit(s);
        all_occupancy |= square_bit(s);
        zobrist_hash ^= piece_key(c, t, s);
    };

    PieceType moving = PieceType::None;
    for (int t = 0; t < 6; ++t) {
        if (is_set(pieces[static_cast<int>(us)][t], from)) {
            moving = static_cast<PieceType>(t);
            break;
        }
    }

    const bool is_en_passant = moving == PieceType::Pawn && to == en_passant && from != to;
    Square capture_square = to;
    PieceType captured = PieceType::None;
    if (is_en_passant) {
        captured = PieceType::Pawn;
        capture_square = make_square(file_of(to), rank_of(from));
    } else {
        for (int t = 0; t < 6; ++t) {
            if (is_set(pieces[static_cast<int>(them)][t], to)) {
                captured = static_cast<PieceType>(t);
                break;
            }
        }
    }
    undo.captured = captured;

    remove_piece(us, moving, from);
    if (captured != PieceType::None) {
        remove_piece(them, captured, capture_square);
    }

    Square rook_from = Square::None;
    Square rook_to = Square::None;
    const bool is_castling =
        moving == PieceType::King && std::abs(file_of(to) - file_of(from)) == 2;
    if (is_castling) {
        const int rank = rank_of(from);
        if (file_of(to) > file_of(from)) {
            rook_from = make_square(7, rank);
            rook_to = make_square(5, rank);
        } else {
            rook_from = make_square(0, rank);
            rook_to = make_square(3, rank);
        }
        remove_piece(us, PieceType::Rook, rook_from);
    }

    const PieceType placed = promotion != PieceType::None ? promotion : moving;
    place_piece(us, placed, to);

    if (is_castling) {
        place_piece(us, PieceType::Rook, rook_to);
    }

    auto clear_castling_for = [this](Square s) {
        switch (s) {
            case Square::A1: castling_rights &= ~castling_wq; break;
            case Square::H1: castling_rights &= ~castling_wk; break;
            case Square::E1: castling_rights &= ~(castling_wk | castling_wq); break;
            case Square::A8: castling_rights &= ~castling_bq; break;
            case Square::H8: castling_rights &= ~castling_bk; break;
            case Square::E8: castling_rights &= ~(castling_bk | castling_bq); break;
            default: break;
        }
    };
    clear_castling_for(from);
    clear_castling_for(to);
    if (is_en_passant) {
        clear_castling_for(capture_square);
    }

    en_passant = Square::None;
    if (moving == PieceType::Pawn && std::abs(rank_of(to) - rank_of(from)) == 2) {
        en_passant = make_square(file_of(from), (rank_of(from) + rank_of(to)) / 2);
    }

    if (moving == PieceType::Pawn || captured != PieceType::None) {
        halfmove_clock = 0;
    } else {
        ++halfmove_clock;
    }

    if (us == Color::Black) {
        ++fullmove_number;
    }

    xor_castling(zobrist_hash, castling_rights);
    xor_en_passant(zobrist_hash, en_passant);

    side_to_move = them;
    zobrist_hash ^= zobrist().side;

    return undo;
}

Piece Position::piece_on(Square s) const {
    for (int c = 0; c < 2; ++c) {
        for (int t = 0; t < 6; ++t) {
            if (is_set(pieces[c][t], s)) {
                return {static_cast<Color>(c), static_cast<PieceType>(t)};
            }
        }
    }
    return {Color::White, PieceType::None};
}

std::string Position::to_fen() const {
    std::string s;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Square sq = make_square(file, rank);
            Piece pc = piece_on(sq);
            if (pc.type == PieceType::None) {
                ++empty;
            } else {
                if (empty > 0) { s += static_cast<char>('0' + empty); empty = 0; }
                char c;
                switch (pc.type) {
                    case PieceType::Pawn: c = 'p'; break;
                    case PieceType::Knight: c = 'n'; break;
                    case PieceType::Bishop: c = 'b'; break;
                    case PieceType::Rook: c = 'r'; break;
                    case PieceType::Queen: c = 'q'; break;
                    case PieceType::King: c = 'k'; break;
                    default: c = '?'; break;
                }
                if (pc.color == Color::White) c = static_cast<char>(c - ('a' - 'A'));
                s.push_back(c);
            }
        }
        if (empty > 0) s += static_cast<char>('0' + empty);
        if (rank > 0) s.push_back('/');
    }
    s.push_back(' ');
    s.push_back(side_to_move == Color::White ? 'w' : 'b');
    s += " ";
    std::string castle;
    if (castling_rights & castling_wk) castle += 'K';
    if (castling_rights & castling_wq) castle += 'Q';
    if (castling_rights & castling_bk) castle += 'k';
    if (castling_rights & castling_bq) castle += 'q';
    if (castle.empty()) castle = "-";
    s += castle;
    s += " ";
    if (en_passant == Square::None) {
        s += "-";
    } else {
        s.push_back(static_cast<char>('a' + file_of(en_passant)));
        s.push_back(static_cast<char>('1' + rank_of(en_passant)));
    }
    s += " ";
    s += std::to_string(halfmove_clock);
    s += " ";
    s += std::to_string(fullmove_number);
    return s;
}

std::array<Piece, 64> Position::board_snapshot() const {
    std::array<Piece, 64> arr{};
    for (int sq = 0; sq < 64; ++sq) {
        arr[sq] = piece_on(static_cast<Square>(sq));
    }
    return arr;
}
void Position::unmake_move(const Undo& undo) {
    const Move move = undo.move;
    const Color us = opposite(side_to_move);
    const Color them = side_to_move;
    const Square from = move.from;
    const Square to = move.to;

    auto remove_piece = [this](Color c, PieceType t, Square s) {
        pieces[static_cast<int>(c)][static_cast<int>(t)] &= ~square_bit(s);
        occupancy[static_cast<int>(c)] &= ~square_bit(s);
        all_occupancy &= ~square_bit(s);
    };
    auto place_piece = [this](Color c, PieceType t, Square s) {
        pieces[static_cast<int>(c)][static_cast<int>(t)] |= square_bit(s);
        occupancy[static_cast<int>(c)] |= square_bit(s);
        all_occupancy |= square_bit(s);
    };

    PieceType moved_type = PieceType::None;
    for (int t = 0; t < 6; ++t) {
        if (is_set(pieces[static_cast<int>(us)][t], to)) {
            moved_type = static_cast<PieceType>(t);
            break;
        }
    }

    const bool is_castling =
        moved_type == PieceType::King && std::abs(file_of(to) - file_of(from)) == 2;

    if (is_castling) {
        const int rank = rank_of(from);
        Square rook_from;
        Square rook_to;
        if (file_of(to) > file_of(from)) {
            rook_from = make_square(7, rank);
            rook_to = make_square(5, rank);
        } else {
            rook_from = make_square(0, rank);
            rook_to = make_square(3, rank);
        }
        remove_piece(us, PieceType::Rook, rook_to);
        remove_piece(us, PieceType::King, to);
        place_piece(us, PieceType::Rook, rook_from);
        place_piece(us, PieceType::King, from);
    } else {
        PieceType original = moved_type;
        if (move.promotion != PieceType::None) {
            remove_piece(us, move.promotion, to);
            original = PieceType::Pawn;
        } else {
            remove_piece(us, moved_type, to);
        }
        place_piece(us, original, from);
    }

    if (undo.captured != PieceType::None) {
        const bool is_en_passant = undo.captured == PieceType::Pawn && to == undo.en_passant &&
                                   from != to;
        const Square capture_square =
            is_en_passant ? make_square(file_of(to), rank_of(from)) : to;
        place_piece(them, undo.captured, capture_square);
    }

    en_passant = undo.en_passant;
    castling_rights = undo.castling_rights;
    halfmove_clock = undo.halfmove_clock;
    if (us == Color::Black) {
        --fullmove_number;
    }
    side_to_move = us;
    zobrist_hash = undo.hash;
}