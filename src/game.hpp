#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include "position.hpp"
#include "types.hpp"
#include "move.hpp"
#include "movegen.hpp"
#include "search.hpp"

enum class PlayerType { Human, Bot };

struct GameConfig {
    PlayerType white = PlayerType::Human;
    PlayerType black = PlayerType::Bot;
    int bot_depth = 6;
    std::int64_t movetime_ms = 300;
    std::int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
};

class GameSession {
public:
    void reset_startpos();
    bool set_fen(std::string_view fen);

    const Position& position() const { return pos; }
    Position& position_ref() { return pos; }
    std::string fen() const { return pos.to_fen(); }
    std::array<Piece, 64> board_snapshot() const { return pos.board_snapshot(); }
    Color side_to_move() const { return pos.side_to_move; }
    std::string orientation_string() const { return orientation == Color::White ? "white" : "black"; }
    std::string last_move_uci() const { return has_last_move ? move_to_uci(last_move) : std::string(); }
    std::string status() const { return status_; }

    void set_orientation(Color c) { orientation = c; }

    std::vector<std::string> legal_moves_for_square(Square sq) const;
    std::vector<std::string> all_legal_moves() const;

    bool try_apply_move(const Move& m);
    void apply_move(const Move& m) { last_move = m; has_last_move = true; pos.make_move(m); update_status(); }

    void new_game(const GameConfig& cfg);
    bool is_bot_turn() const;
    Color human_color() const;
    SearchLimits compute_limits() const;
    bool play_move(const Move& m);
    bool maybe_play_bot(std::function<void(const SearchInfo&)> on_info);
    bool game_over() const;

private:
    void update_status();

    Position pos;
    Color orientation = Color::White;
    Move last_move{};
    bool has_last_move = false;
    std::string status_ = "playing";

    GameConfig config_;
};