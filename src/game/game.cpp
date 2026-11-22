#include "game/game.hpp"

#include "game/movegen.hpp"
#include "game/search.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// Build clock JSON from explicit millisecond values so the tick callback does
// not depend on the clock's live atomics (they may have advanced since the tick).
std::string make_clock_json(std::int64_t w_ms, std::int64_t b_ms) {
    return std::string("{\"type\":\"clock\",\"w_ms\":") + std::to_string(w_ms) +
           ",\"b_ms\":" + std::to_string(b_ms) + "}";
}

// Matches the server's piece_char: uppercase = White, "null" for empty squares.
std::string piece_char_json(Piece p) {
    if (p.type == PieceType::None) return "null";
    char c;
    switch (p.type) {
        case PieceType::Pawn: c = 'p'; break;
        case PieceType::Knight: c = 'n'; break;
        case PieceType::Bishop: c = 'b'; break;
        case PieceType::Rook: c = 'r'; break;
        case PieceType::Queen: c = 'q'; break;
        case PieceType::King: c = 'k'; break;
        default: c = '?'; break;
    }
    if (p.color == Color::White) c = static_cast<char>(c - ('a' - 'A'));
    return std::string("\"") + c + "\"";
}

// Same format as the server's send_info_line.
std::string info_line(const SearchInfo& info) {
    std::string line = "info depth " + std::to_string(info.depth) +
                       " score cp " + std::to_string(info.score) +
                       " nodes " + std::to_string(info.nodes);
    if (info.pv_move.from != Square::None) {
        line += " pv " + move_to_uci(info.pv_move);
    }
    return line;
}

}  // namespace

void GameSession::reset_startpos() {
    pos.set_starting_position();
    has_last_move = false;
    status_ = "playing";
}

bool GameSession::set_fen(std::string_view fen) {
    if (!pos.set_fen(fen)) return false;
    has_last_move = false;
    status_ = "playing";
    result_ = GameResult{};
    game_over_emitted_.store(false, std::memory_order_relaxed);
    hash_len_ = 0;
    push_history();
    clock_.stop();                    // a loaded position starts with a stopped clock
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
    if (game_over()) return false;
    MoveList legal;
    movegen::generate_legal(pos, legal);
    for (const Move& legal_move : legal) {
        if (legal_move.from == m.from && legal_move.to == m.to && legal_move.promotion == m.promotion) {
            Color mover = pos.side_to_move;   // capture BEFORE make_move
            last_move = legal_move;
            has_last_move = true;
            pos.make_move(legal_move);
            update_status();
            start_clock_if_needed(mover);
            clock_.on_move(mover);
            push_history();
            check_end();
            return true;
        }
    }
    return false;
}

void GameSession::new_game(const GameConfig& cfg) {
    config_ = cfg;
    reset_startpos();                 // sets pos start, has_last_move=false, status_="playing"
    stop_requested_.store(false, std::memory_order_relaxed);
    game_over_emitted_.store(false, std::memory_order_relaxed);
    result_ = GameResult{};
    hash_len_ = 0;
    push_history();                   // record initial hash
    clock_.set_flag_callback([this](Color c) { handle_clock_flag(c); });
    clock_.set_tick_callback([this](std::int64_t w, std::int64_t b) {
        emit_line(make_clock_json(w, b));
    });
    clock_.stop();                    // do NOT auto-start; server/run_bot_bot call start_clock()
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
    limits.wtime = config_.wtime;
    limits.btime = config_.btime;
    limits.winc = config_.winc;
    limits.binc = config_.binc;

    if (clock_.running() || clock_.flagged()) {
        std::int64_t remaining = clock_.running() ? clock_.remaining_ms(side) : 0;
        std::int64_t inc = clock_.running() ? clock_.increment_ms(side) : 0;
        if (clock_.flagged() || remaining <= 0) {
            limits.movetime_ms = 0;
        } else {
            std::int64_t mt = remaining / 20 + inc * 3 / 4;
            if (mt < 10) mt = 10;
            if (mt > remaining / 2) mt = remaining / 2;
            limits.movetime_ms = mt;
        }
    } else {
        std::int64_t remaining = (side == Color::White) ? config_.wtime : config_.btime;
        std::int64_t inc = (side == Color::White) ? config_.winc : config_.binc;
        if (remaining > 0) {
            std::int64_t mt = remaining / 20 + inc * 3 / 4;
            if (mt < 10) mt = 10;
            if (mt > remaining / 2) mt = remaining / 2;
            limits.movetime_ms = mt;
        } else {
            limits.movetime_ms = config_.movetime_ms;
        }
    }
    return limits;
}

bool GameSession::play_move(const Move& m) {
    return try_apply_move(m);
}

bool GameSession::maybe_play_bot(std::function<void(const SearchInfo&)> on_info) {
    if (!is_bot_turn() || game_over()) return false;

    SearchLimits limits = compute_limits();
    if (limits.movetime_ms <= 0) return false;       // flagged / no time

    Searcher searcher(pos);
    searcher.set_info_callback(on_info);
    SearchResult result = searcher.search(limits);

    if (game_over()) return false;                    // a flag fired mid-search; discard
    if (result.best_move.from == Square::None) return false;

    play_move(result.best_move);
    return true;
}

bool GameSession::game_over() const {
    std::lock_guard<std::mutex> lk(result_mutex_);
    return result_.over || status_ == "checkmate" || status_ == "stalemate";
}

GameResult GameSession::result() const {
    std::lock_guard<std::mutex> lk(result_mutex_);
    return result_;
}

void GameSession::handle_clock_flag(Color flagged) {
    {
        std::lock_guard<std::mutex> lk(result_mutex_);
        if (result_.over) return;
        result_.over = true;
        result_.result = (flagged == Color::White) ? "0-1" : "1-0";
        result_.reason = "flag";
    }
    if (consume_game_over_emit()) emit_line(game_over_json());
}

GameResult GameSession::poll_flag() {
    if (clock_.flagged()) handle_clock_flag(clock_.flagged_color());
    return result();
}

GameResult GameSession::check_end() {
    poll_flag();
    GameResult r;
    bool terminal = false;
    if (movegen::is_checkmate(pos)) {
        r = {true, (pos.side_to_move == Color::White ? "0-1" : "1-0"), "checkmate"};
        terminal = true;
    } else if (movegen::is_stalemate(pos)) {
        r = {true, "1/2", "stalemate"};
        terminal = true;
    } else if (pos.halfmove_clock >= 100) {
        r = {true, "1/2", "fifty-move rule"};
        terminal = true;
    } else if (repetition_count() >= 3) {
        r = {true, "1/2", "threefold repetition"};
        terminal = true;
    }
    if (terminal) {
        std::lock_guard<std::mutex> lk(result_mutex_);
        if (!result_.over) result_ = r;
    }
    if (consume_game_over_emit() && game_over()) emit_line(game_over_json());
    return result();
}

std::size_t GameSession::repetition_count() const {
    std::size_t n = 0;
    for (std::size_t i = 0; i < hash_len_; ++i)
        if (hash_history_[i] == pos.zobrist_hash) ++n;
    return n;
}

void GameSession::push_history() {
    if (hash_len_ < hash_history_.size()) hash_history_[hash_len_++] = pos.zobrist_hash;
}

void GameSession::set_emitter(LineEmitter emit) {
    std::lock_guard<std::mutex> lk(emit_mutex_);
    emit_ = std::move(emit);
}

void GameSession::emit_line(const std::string& line) {
    std::lock_guard<std::mutex> lk(emit_mutex_);
    if (emit_) emit_(line);
}

bool GameSession::consume_game_over_emit() {
    std::lock_guard<std::mutex> lk(result_mutex_);
    if (!result_.over) return false;
    // exchange(true) returns the previous value: false the first time the game
    // is over (so this call should emit) and true afterwards (already emitted).
    return !game_over_emitted_.exchange(true, std::memory_order_acq_rel);
}

void GameSession::start_clock() {
    if (config_.wtime > 0 || config_.btime > 0)
        clock_.start(config_.wtime, config_.btime, config_.winc, config_.binc, pos.side_to_move);
    else
        clock_.stop();
}

void GameSession::start_clock_if_needed(Color mover) {
    if (clock_.running() || clock_.flagged()) return;
    std::int64_t w = config_.wtime;
    std::int64_t b = config_.btime;
    if (w == 0 && b == 0 && config_.movetime_ms > 0) {
        w = config_.movetime_ms;
        b = config_.movetime_ms;
    }
    if (w > 0 || b > 0)
        clock_.start(w, b, config_.winc, config_.binc, mover);
}

void GameSession::configure_clock(std::int64_t wtime, std::int64_t btime,
                                  std::int64_t winc, std::int64_t binc) {
    config_.wtime = wtime;
    config_.btime = btime;
    config_.winc = winc;
    config_.binc = binc;
    start_clock();
}

std::string GameSession::clock_json() const {
    if (!clock_.running() && !clock_.flagged() &&
        (config_.wtime > 0 || config_.btime > 0 || config_.movetime_ms > 0)) {
        std::int64_t w = config_.wtime > 0 ? config_.wtime : config_.movetime_ms;
        std::int64_t b = config_.btime > 0 ? config_.btime : config_.movetime_ms;
        return make_clock_json(w, b);
    }
    return make_clock_json(clock_.remaining_ms(Color::White), clock_.remaining_ms(Color::Black));
}

std::string GameSession::game_over_json() const {
    std::lock_guard<std::mutex> lk(result_mutex_);
    return std::string("{\"type\":\"game_over\",\"result\":\"") + result_.result +
           "\",\"reason\":\"" + result_.reason + "\"}";
}

std::string GameSession::state_json() const {
    GameResult r;
    {
        std::lock_guard<std::mutex> lk(result_mutex_);
        r = result_;
    }

    std::string j = "{\"type\":\"state\",\"fen\":\"" + pos.to_fen() + "\",\"board\":[";
    auto snap = pos.board_snapshot();
    for (int i = 0; i < 64; ++i) {
        if (i) j += ",";
        j += piece_char_json(snap[i]);
    }
    j += "],\"side\":\"";
    j += (pos.side_to_move == Color::White ? "w" : "b");
    j += "\",\"orientation\":\"" + orientation_string() + "\",\"clocks\":{";
    bool clock_active = clock_.running() || clock_.flagged();
    std::int64_t w_ms = 0;
    std::int64_t b_ms = 0;
    bool clocks_set = false;
    if (clock_active) {
        w_ms = clock_.remaining_ms(Color::White);
        b_ms = clock_.remaining_ms(Color::Black);
        clocks_set = true;
    } else if (config_.wtime > 0 || config_.btime > 0 || config_.movetime_ms > 0) {
        w_ms = config_.wtime > 0 ? config_.wtime : config_.movetime_ms;
        b_ms = config_.btime > 0 ? config_.btime : config_.movetime_ms;
        clocks_set = true;
    }
    if (clocks_set) {
        j += "\"w_ms\":" + std::to_string(w_ms);
        j += ",\"b_ms\":" + std::to_string(b_ms);
    } else {
        j += "\"w_ms\":null,\"b_ms\":null";
    }
    j += "},\"status\":\"";
    j += r.over ? r.reason : status_;
    j += "\",\"last_move\":";
    std::string lm = has_last_move ? move_to_uci(last_move) : std::string();
    if (lm.empty()) j += "null";
    else j += "\"" + lm + "\"";
    j += "}";
    return j;
}

void GameSession::run_bot_bot(LineEmitter emit) {
    set_emitter(emit);
    emit_line(state_json());
    while (!game_over() && !stop_requested()) {
        if (!maybe_play_bot([this](const SearchInfo& i) { emit_line(info_line(i)); })) break;
        emit_line("bestmove " + last_move_uci());
        emit_line(clock_json());
        emit_line(state_json());
    }
    if (game_over() && consume_game_over_emit()) emit_line(game_over_json());
    clock_.stop();
}