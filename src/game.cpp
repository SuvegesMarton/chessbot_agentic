#include "game.hpp"

#include "movegen.hpp"

#include <vector>

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