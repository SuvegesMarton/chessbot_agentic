#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/types.hpp"
#include "core/bitboard.hpp"

constexpr std::size_t MOVE_LIST_CAPACITY = 256;

struct Move {
    Square from = Square::None;
    Square to = Square::None;
    PieceType promotion = PieceType::None;
};

struct Undo {
    Move move{};
    PieceType captured = PieceType::None;
    Square en_passant = Square::None;
    std::uint8_t castling_rights = 0;
    std::uint8_t halfmove_clock = 0;
    std::uint64_t hash = 0;
};

struct MoveList {
    std::array<Move, MOVE_LIST_CAPACITY> moves{};
    std::size_t count = 0;
    void push(const Move& m) { if (count < MOVE_LIST_CAPACITY) moves[count++] = m; }
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
    std::size_t size() const { return count; }
};

inline char promotion_char(PieceType p) {
    switch (p) {
        case PieceType::Queen: return 'q';
        case PieceType::Rook: return 'r';
        case PieceType::Bishop: return 'b';
        case PieceType::Knight: return 'n';
        default: return '\0';
    }
}

inline std::string move_to_uci(const Move& m) {
    std::string s;
    s.resize(4);
    s[0] = static_cast<char>('a' + file_of(m.from));
    s[1] = static_cast<char>('1' + rank_of(m.from));
    s[2] = static_cast<char>('a' + file_of(m.to));
    s[3] = static_cast<char>('1' + rank_of(m.to));
    if (m.promotion != PieceType::None) {
        s.push_back(promotion_char(m.promotion));
    }
    return s;
}

inline Move parse_uci(std::string_view s) {
    Move m{};
    if (s.size() < 4) return m;
    int f1 = s[0] - 'a';
    int r1 = s[1] - '1';
    int f2 = s[2] - 'a';
    int r2 = s[3] - '1';
    if (f1 < 0 || f1 >= 8 || r1 < 0 || r1 >= 8 ||
        f2 < 0 || f2 >= 8 || r2 < 0 || r2 >= 8) {
        return Move{Square::None, Square::None, PieceType::None};
    }
    m.from = make_square(f1, r1);
    m.to = make_square(f2, r2);
    if (s.size() >= 5) {
        switch (s[4]) {
            case 'q': m.promotion = PieceType::Queen; break;
            case 'r': m.promotion = PieceType::Rook; break;
            case 'b': m.promotion = PieceType::Bishop; break;
            case 'n': m.promotion = PieceType::Knight; break;
            default: break;
        }
    }
    return m;
}