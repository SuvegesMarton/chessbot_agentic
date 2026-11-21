#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/types.hpp"
#include "core/move.hpp"

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

    Piece piece_on(Square s) const;
    std::array<Piece, 64> board_snapshot() const;
    std::string to_fen() const;

    std::uint64_t hash() const { return zobrist_hash; }
    
    Square king_square(Color c) const;
    bool in_check(Color c) const;
};
