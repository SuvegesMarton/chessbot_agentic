#include "game.hpp"

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
