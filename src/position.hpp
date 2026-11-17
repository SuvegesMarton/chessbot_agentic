#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "types.hpp"

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

class Position {
public:
    std::array<std::array<Bitboard, 6>, 2> pieces{};
    std::array<Bitboard, 2> occupancy{};
    Bitboard all_occupancy = 0;
    Color side_to_move = Color::White;
    std::uint8_t castling_rights = 0;
    Square en_passant = Square::None;
    std::uint8_t halfmove_clock = 0;
    std::uint16_t fullmove_number = 1;
    std::uint64_t zobrist_hash = 0;

    void set_starting_position();
    bool set_fen(std::string_view fen);
    Undo make_move(const Move& move);
    void unmake_move(const Undo& undo);

    std::uint64_t hash() const { return zobrist_hash; }
};
