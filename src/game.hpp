#pragma once

#include <array>
#include <string>
#include <string_view>

#include "position.hpp"
#include "types.hpp"
#include "move.hpp"

class GameSession {
public:
    void reset_startpos();
    bool set_fen(std::string_view fen);

    const Position& position() const { return pos; }
    std::string fen() const { return pos.to_fen(); }
    std::array<Piece, 64> board_snapshot() const { return pos.board_snapshot(); }
    Color side_to_move() const { return pos.side_to_move; }
    std::string orientation_string() const { return orientation == Color::White ? "white" : "black"; }
    std::string last_move_uci() const { return has_last_move ? move_to_uci(last_move) : std::string(); }
    std::string status() const { return status_; }

    void set_orientation(Color c) { orientation = c; }
    void apply_move(const Move& m) { last_move = m; has_last_move = true; pos.make_move(m); }

private:
    Position pos;
    Color orientation = Color::White;
    Move last_move{};
    bool has_last_move = false;
    std::string status_ = "playing";
};
