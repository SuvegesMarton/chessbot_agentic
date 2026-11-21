#include "game/game.hpp"

#include "game/movegen.hpp"
#include "game/search.hpp"

#include <vector>
#include <algorithm>

void GameSession::reset_startpos() {
    pos.set_starting_position();
    has_last_move = false;
    status_ = "playing";
}

bool GameSession::set_fen(std::string_view fen) {
    if (!pos.set_fen(fen)) return false;
    has_last_move = false;
    status_ = "playing";
    return true;
}

void GameSession::update_status() {
    if (movegen::is_checkmate(pos)) {
        status_ = "checkmate";
    } else if (movegen::is_stalemate(pos)) {
        status_ = "stalemate";
    } else if (pos.in_check(pos.side_to_move)) {
        status_ = "check";
    } else {
        status_ = "playing";
    }
}

std::vector<std::string> GameSession::legal_moves_for_square(Square sq) const {
    std::vector<std::string> result;
    MoveList legal;
    movegen::generate_legal(const_cast<Position&>(pos), legal);
    for (const Move& m : legal) {
        if (m.from == sq) {
            result.push_back(move_to_uci(m));
        }
    }
    return result;
}

std::vector<std::string> GameSession::all_legal_moves() const {
    std::vector<std::string> result;
    MoveList legal;
    movegen::generate_legal(const_cast<Position&>(pos), legal);
    for (const Move& m : legal) {
        result.push_back(move_to_uci(m));
    }
    return result;
}

bool GameSession::try_apply_move(const Move& m) {
    MoveList legal;
    movegen::generate_legal(pos, legal);
    for (const Move& legal_move : legal) {
        if (legal_move.from == m.from && legal_move.to == m.to && legal_move.promotion == m.promotion) {
            last_move = legal_move;
            has_last_move = true;
            pos.make_move(legal_move);
            update_status();
            return true;
        }
    }
    return false;
}

void GameSession::new_game(const GameConfig& cfg) {
    config_ = cfg;
    reset_startpos();
}

bool GameSession::is_bot_turn() const {
    if (game_over()) return false;
    Color side = pos.side_to_move;
    if (side == Color::White) return config_.white == PlayerType::Bot;
    return config_.black == PlayerType::Bot;
}

Color GameSession::human_color() const {
    if (config_.white == PlayerType::Human) return Color::White;
    return Color::Black;
}

SearchLimits GameSession::compute_limits() const {
    SearchLimits limits;
    limits.max_depth = config_.bot_depth;

    Color side = pos.side_to_move;
    std::int64_t remaining = (side == Color::White) ? config_.wtime : config_.btime;
    std::int64_t inc = (side == Color::White) ? config_.winc : config_.binc;

    if (remaining > 0) {
        std::int64_t movetime = remaining / 20 + inc * 3 / 4;
        if (movetime < 10) movetime = 10;
        if (movetime > remaining / 2) movetime = remaining / 2;
        limits.movetime_ms = movetime;
    } else {
        limits.movetime_ms = config_.movetime_ms;
    }

    limits.wtime = config_.wtime;
    limits.btime = config_.btime;
    limits.winc = config_.winc;
    limits.binc = config_.binc;

    return limits;
}

bool GameSession::play_move(const Move& m) {
    return try_apply_move(m);
}

bool GameSession::maybe_play_bot(std::function<void(const SearchInfo&)> on_info) {
    if (!is_bot_turn() || game_over()) return false;

    Searcher searcher(pos);
    searcher.set_info_callback(on_info);
    SearchLimits limits = compute_limits();
    SearchResult result = searcher.search(limits);

    if (result.best_move.from == Square::None) return false;

    play_move(result.best_move);
    return true;
}

bool GameSession::game_over() const {
    return status_ == "checkmate" || status_ == "stalemate";
}