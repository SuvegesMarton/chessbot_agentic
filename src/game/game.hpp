#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "game/position.hpp"
#include "game/clock.hpp"
#include "core/types.hpp"
#include "core/move.hpp"
#include "game/movegen.hpp"
#include "game/search.hpp"

enum class PlayerType { Human, Bot };

struct GameConfig {
    PlayerType white = PlayerType::Human;
    PlayerType black = PlayerType::Bot;
    int bot_depth = 6;
    std::int64_t movetime_ms = 300;
    std::int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
};

struct GameResult {
    bool over = false;
    std::string result;   // "1-0" | "0-1" | "1/2"
    std::string reason;   // "checkmate" | "flag" | "stalemate" | "fifty-move rule" | "threefold repetition"
};

class GameSession {
public:
    using LineEmitter = std::function<void(const std::string&)>;

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

    // Phase 4 additions
    void set_emitter(LineEmitter emit);
    void request_stop() { stop_requested_.store(true, std::memory_order_relaxed); }
    bool stop_requested() const { return stop_requested_.load(std::memory_order_relaxed); }

    // Start the countdown from config_ times (no-op if no time control). Running
    // side = side to move. Call after new_game / when a time control is set.
    void start_clock();
    // Store new base times and (re)start the clock if it is active.
    void configure_clock(std::int64_t wtime, std::int64_t btime,
                         std::int64_t winc, std::int64_t binc);

    const chessclock::Clock& clock() const { return clock_; }
    chessclock::Clock& clock() { return clock_; }

    GameResult result() const;          // mutex-protected copy
    bool game_over() const;
    GameResult poll_flag();             // consumes a fresh flag into result_
    GameResult check_end();             // flag + mate/stalemate + draw rules; sets result_ once

    void run_bot_bot(LineEmitter emit); // blocking self-play loop

    std::string state_json() const;
    std::string clock_json() const;
    std::string game_over_json() const;

private:
    void update_status();
    void handle_clock_flag(Color flagged);
    void push_history();
    std::size_t repetition_count() const;
    void emit_line(const std::string& line);
    bool consume_game_over_emit();      // returns true if this call should emit

    Position pos;
    Color orientation = Color::White;
    Move last_move{};
    bool has_last_move = false;
    std::string status_ = "playing";

    GameConfig config_;

    chessclock::Clock clock_;
    GameResult result_;
    std::atomic<bool> game_over_emitted_{false};
    std::atomic<bool> stop_requested_{false};
    std::array<std::uint64_t, 2048> hash_history_{};
    std::size_t hash_len_ = 0;
    mutable std::mutex result_mutex_;
    mutable std::mutex emit_mutex_;
    LineEmitter emit_;
};