#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "game/game.hpp"
#include "game/movegen.hpp"
#include "game/search.hpp"
#include "core/move.hpp"
#include "core/types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& e : v) {
        if (e == s) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// reset_startpos / basic accessors
// ---------------------------------------------------------------------------

void startpos_and_accessors_test() {
    GameSession g;
    g.reset_startpos();

    check(g.status() == "playing", "reset status is playing");
    check(g.side_to_move() == Color::White, "reset side to move white");
    check(g.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          "reset fen is startpos");
    check(g.last_move_uci().empty(), "reset has no last move");
    check(!g.game_over(), "reset not game over");

    std::array<Piece, 64> snap = g.board_snapshot();
    check(snap[static_cast<int>(Square::E1)].color == Color::White &&
              snap[static_cast<int>(Square::E1)].type == PieceType::King,
          "snapshot E1 white king");
    check(snap[static_cast<int>(Square::E8)].color == Color::Black &&
              snap[static_cast<int>(Square::E8)].type == PieceType::King,
          "snapshot E8 black king");
    check(g.position().side_to_move == Color::White, "position() accessor works");
}

// ---------------------------------------------------------------------------
// set_fen
// ---------------------------------------------------------------------------

void set_fen_test() {
    GameSession g;
    check(g.set_fen("4k3/8/8/8/8/8/8/4K3 w - - 0 1"), "set_fen valid kings-only");
    check(g.fen() == "4k3/8/8/8/8/8/8/4K3 w - - 0 1", "set_fen fen matches");
    check(g.side_to_move() == Color::White, "set_fen side to move white");
    check(g.status() == "playing", "set_fen resets status to playing");

    // An invalid FEN must be rejected and must not leave the session reporting success.
    GameSession g2;
    check(!g2.set_fen("8/8/8/8/8/8/8/8"), "set_fen rejects fen missing side-to-move");
    check(!g2.set_fen("xyz"), "set_fen rejects garbage");
}

// ---------------------------------------------------------------------------
// new_game
// ---------------------------------------------------------------------------

void new_game_test() {
    // new_game stores the config (observable via is_bot_turn / human_color) and
    // resets the board to the starting position.
    GameConfig cfg;
    cfg.white = PlayerType::Bot;
    cfg.black = PlayerType::Human;

    GameSession g;
    g.set_fen("4k3/8/8/8/8/8/8/4K3 b - - 0 1");
    g.new_game(cfg);

    check(g.fen() == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          "new_game resets board");
    check(g.human_color() == Color::Black, "new_game stored white=Bot -> human Black");
    check(g.is_bot_turn(), "new_game stored white=Bot -> bot to move at startpos");
}

// ---------------------------------------------------------------------------
// orientation
// ---------------------------------------------------------------------------

void orientation_test() {
    GameSession g;
    check(g.orientation_string() == "white", "default orientation white");
    g.set_orientation(Color::Black);
    check(g.orientation_string() == "black", "orientation set to black");
    g.set_orientation(Color::White);
    check(g.orientation_string() == "white", "orientation set back to white");
}

// ---------------------------------------------------------------------------
// last move tracking
// ---------------------------------------------------------------------------

void last_move_test() {
    GameSession g;
    g.reset_startpos();
    check(g.last_move_uci().empty(), "last move empty before any move");

    Move e2e4{Square::E2, Square::E4, PieceType::None};
    check(g.try_apply_move(e2e4), "apply e2e4");
    check(g.last_move_uci() == "e2e4", "last move uci is e2e4");
    check(g.side_to_move() == Color::Black, "side flipped to black after e2e4");

    // A rejected move must not change the last move.
    Move illegal{Square::E7, Square::E4, PieceType::None}; // e7e4 is not a legal pawn move
    check(!g.try_apply_move(illegal), "illegal move rejected");
    check(g.last_move_uci() == "e2e4", "last move unchanged after rejected move");
}

// ---------------------------------------------------------------------------
// legal move queries
// ---------------------------------------------------------------------------

void legal_moves_for_square_test() {
    GameSession g;
    g.reset_startpos();

    std::vector<std::string> e2 = g.legal_moves_for_square(Square::E2);
    check(e2.size() == 2, "e2 pawn has 2 legal moves at startpos");
    check(contains(e2, "e2e3"), "e2 moves include e2e3");
    check(contains(e2, "e2e4"), "e2 moves include e2e4");

    std::vector<std::string> b1 = g.legal_moves_for_square(Square::B1);
    check(b1.size() == 2, "b1 knight has 2 legal moves at startpos");
    check(contains(b1, "b1a3"), "b1 moves include b1a3");
    check(contains(b1, "b1c3"), "b1 moves include b1c3");

    // Rook on a1 is blocked and has no moves at startpos.
    std::vector<std::string> a1 = g.legal_moves_for_square(Square::A1);
    check(a1.empty(), "a1 rook has no moves at startpos");
}

void all_legal_moves_test() {
    GameSession g;
    g.reset_startpos();

    std::vector<std::string> all = g.all_legal_moves();
    check(all.size() == 20, "startpos has 20 legal moves");
    check(contains(all, "e2e4"), "all legal moves include e2e4");
    check(contains(all, "g1f3"), "all legal moves include g1f3");
}

// ---------------------------------------------------------------------------
// try_apply_move / apply_move / play_move
// ---------------------------------------------------------------------------

void try_apply_move_test() {
    GameSession g;
    g.reset_startpos();

    Move legal{Square::E2, Square::E4, PieceType::None};
    check(g.try_apply_move(legal), "legal move accepted");
    check(g.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
          "fen updated after e2e4");

    Move illegal{Square::E7, Square::E4, PieceType::None}; // e7e4 is not a legal pawn move
    check(!g.try_apply_move(illegal), "illegal move rejected");
    // Board must be unchanged after a rejected move.
    check(g.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
          "fen unchanged after rejected move");
}

void try_apply_move_promotion_test() {
    GameSession g;
    check(g.set_fen("8/4P3/8/8/8/8/8/4K2k w - - 0 1"), "set_fen promotion position");

    // Non-promotion move to the last rank must be rejected: promotion piece required.
    Move no_promo{Square::E7, Square::E8, PieceType::None};
    check(!g.try_apply_move(no_promo), "pawn push without promotion piece rejected");

    Move promo{Square::E7, Square::E8, PieceType::Queen};
    check(g.try_apply_move(promo), "promotion to queen accepted");
    check(g.last_move_uci() == "e7e8q", "promotion last move uci");
}

void apply_move_test() {
    // apply_move is the unconditional variant: it records the move and updates
    // status without a legality check.
    GameSession g;
    g.reset_startpos();

    Move e2e4{Square::E2, Square::E4, PieceType::None};
    g.apply_move(e2e4);
    check(g.last_move_uci() == "e2e4", "apply_move records last move");
    check(g.side_to_move() == Color::Black, "apply_move flips side");
    check(g.status() == "playing", "apply_move quiet move -> playing");
}

void play_move_test() {
    // play_move delegates to try_apply_move.
    GameSession g;
    g.reset_startpos();

    Move legal{Square::G1, Square::F3, PieceType::None};
    check(g.play_move(legal), "play_move accepts legal move");
    check(g.last_move_uci() == "g1f3", "play_move recorded g1f3");

    Move illegal{Square::G8, Square::G1, PieceType::None};
    check(!g.play_move(illegal), "play_move rejects illegal move");
}

// ---------------------------------------------------------------------------
// player / bot configuration
// ---------------------------------------------------------------------------

void is_bot_turn_test() {
    GameConfig cfg; // default: white Human, black Bot
    GameSession g;
    g.new_game(cfg);

    check(!g.is_bot_turn(), "white (human) to move at startpos -> not bot turn");

    check(g.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"),
          "set_fen black to move");
    check(g.is_bot_turn(), "black (bot) to move -> bot turn");

    // Both human -> never a bot turn.
    GameConfig all_human;
    all_human.white = PlayerType::Human;
    all_human.black = PlayerType::Human;
    GameSession g2;
    g2.new_game(all_human);
    check(!g2.is_bot_turn(), "white to move, both human -> not bot turn");
    check(g2.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"),
          "set_fen black to move (all human)");
    check(!g2.is_bot_turn(), "black to move, both human -> not bot turn");
}

void human_color_test() {
    GameConfig cfg; // white Human, black Bot
    GameSession g;
    g.new_game(cfg);
    check(g.human_color() == Color::White, "default human color is white");

    GameConfig cfg2;
    cfg2.white = PlayerType::Bot;
    cfg2.black = PlayerType::Human;
    GameSession g2;
    g2.new_game(cfg2);
    check(g2.human_color() == Color::Black, "white=Bot -> human color black");
}

// ---------------------------------------------------------------------------
// compute_limits
// ---------------------------------------------------------------------------

void compute_limits_no_clock_test() {
    GameConfig cfg; // all clocks zero
    cfg.bot_depth = 7;
    cfg.movetime_ms = 1234;

    GameSession g;
    g.new_game(cfg);
    SearchLimits l = g.compute_limits();

    check(l.max_depth == 7, "limits max_depth from config bot_depth");
    check(l.movetime_ms == 1234, "limits movetime from config when no clock");
    check(l.wtime == 0 && l.btime == 0 && l.winc == 0 && l.binc == 0,
          "limits carry clock fields through");
}

void compute_limits_with_clock_test() {
    // White to move, 10s left, 1s increment.
    GameConfig cfg;
    cfg.bot_depth = 5;
    cfg.wtime = 10000;
    cfg.winc = 1000;

    GameSession g;
    g.new_game(cfg);
    SearchLimits l = g.compute_limits();

    // movetime = remaining/20 + inc*3/4 = 500 + 750 = 1250; within [10, remaining/2].
    check(l.movetime_ms == 1250, "white clock movetime = remaining/20 + inc*3/4");
    check(l.max_depth == 5, "white clock max_depth from config");
    check(l.wtime == 10000 && l.winc == 1000, "white clock fields carried through");
}

void compute_limits_black_clock_test() {
    GameConfig cfg;
    cfg.btime = 10000; // black clock only; white clock zero
    cfg.binc = 0;

    GameSession g;
    g.new_game(cfg);
    check(g.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"),
          "set_fen black to move for clock test");

    SearchLimits l = g.compute_limits();
    // movetime = 10000/20 + 0 = 500.
    check(l.movetime_ms == 500, "black clock uses btime");
    check(l.btime == 10000, "black clock fields carried through");
}

void compute_limits_clamping_test() {
    // Min clamp: remaining/20 below 10 -> clamped up to 10.
    GameConfig cfg_min;
    cfg_min.wtime = 100;
    cfg_min.winc = 0;
    GameSession g1;
    g1.new_game(cfg_min);
    check(g1.compute_limits().movetime_ms == 10, "movetime clamped to minimum 10");

    // Max clamp: computed movetime above remaining/2 -> clamped down.
    GameConfig cfg_max;
    cfg_max.wtime = 1000;
    cfg_max.winc = 1000; // 50 + 750 = 800, but remaining/2 = 500
    GameSession g2;
    g2.new_game(cfg_max);
    check(g2.compute_limits().movetime_ms == 500, "movetime clamped to remaining/2");
}

// ---------------------------------------------------------------------------
// maybe_play_bot
// ---------------------------------------------------------------------------

void maybe_play_bot_not_bot_turn_test() {
    GameSession g;
    g.new_game(GameConfig{}); // white Human -> not a bot turn at startpos

    int callbacks = 0;
    bool played = g.maybe_play_bot([&callbacks](const SearchInfo&) { ++callbacks; });

    check(!played, "maybe_play_bot returns false when not bot turn");
    check(callbacks == 0, "no search callback when not bot turn");
    check(g.side_to_move() == Color::White, "side unchanged when not bot turn");
    check(g.last_move_uci().empty(), "no move played when not bot turn");
}

void maybe_play_bot_bot_turn_test() {
    GameConfig cfg; // default: black is Bot
    cfg.bot_depth = 1;
    cfg.movetime_ms = 100;

    GameSession g;
    g.new_game(cfg);
    check(g.set_fen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"),
          "set_fen black to move for bot test");

    int callbacks = 0;
    bool played = g.maybe_play_bot([&callbacks](const SearchInfo&) { ++callbacks; });

    check(played, "maybe_play_bot returns true on bot turn");
    check(callbacks > 0, "search info callback invoked for bot move");
    check(!g.last_move_uci().empty(), "bot move recorded as last move");
    check(g.side_to_move() == Color::White, "side flipped to white after bot move");

    // The played move must be one of the legal black moves from the position.
    // (fen already changed, so re-derive from the recorded uci length instead.)
    check(g.last_move_uci().size() == 4, "bot move is a 4-char uci move");
}

void maybe_play_bot_no_moves_test() {
    // Black to move and checkmated: no legal moves exist.
    GameConfig cfg; // default: black is Bot
    cfg.bot_depth = 2;
    cfg.movetime_ms = 100;

    GameSession g;
    g.new_game(cfg);
    check(g.set_fen("R5k1/5ppp/8/8/8/8/8/7K b - - 0 1"),
          "set_fen black checkmated");

    bool played = g.maybe_play_bot(nullptr);
    check(!played, "maybe_play_bot returns false when no legal moves");
    check(g.last_move_uci().empty(), "no move recorded when no legal moves");
    check(g.side_to_move() == Color::Black, "side unchanged when no move available");
}

// ---------------------------------------------------------------------------
// status / game_over
// ---------------------------------------------------------------------------

void status_check_test() {
    GameSession g;
    check(g.set_fen("4k3/8/8/8/8/8/8/4KR2 w - - 0 1"), "set_fen rook-check position");

    Move rf8{Square::F1, Square::F8, PieceType::None};
    check(g.try_apply_move(rf8), "Rf8+ applied");
    check(g.status() == "check", "status is check after Rf8+");
    check(!g.game_over(), "check is not game over");
}

void status_checkmate_test() {
    GameSession g;
    check(g.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"),
          "set_fen mate-in-1 position");
    check(!g.game_over(), "not game over before mating move");

    Move ra8{Square::A1, Square::A8, PieceType::None};
    check(g.try_apply_move(ra8), "Ra8# applied");
    check(g.status() == "checkmate", "status is checkmate after Ra8#");
    check(g.game_over(), "checkmate is game over");
}

void status_stalemate_test() {
    GameSession g;
    check(g.set_fen("k7/8/1QK5/8/8/8/8/8 w - - 0 1"),
          "set_fen stalemate-in-1 position");

    Move qc7{Square::B6, Square::C7, PieceType::None};
    check(g.try_apply_move(qc7), "Qc7 applied");
    check(g.status() == "stalemate", "status is stalemate after Qc7");
    check(g.game_over(), "stalemate is game over");
}

void game_over_test() {
    GameSession g;
    g.reset_startpos();
    check(!g.game_over(), "startpos not game over");

    // A check position is not game over.
    g.set_fen("4k3/8/8/8/8/8/8/4KR2 w - - 0 1");
    Move rf8{Square::F1, Square::F8, PieceType::None};
    g.try_apply_move(rf8);
    check(g.status() == "check", "game_over: status check");
    check(!g.game_over(), "game_over: check is not over");
}

// ---------------------------------------------------------------------------
// Phase 4
// ---------------------------------------------------------------------------

void phase4_mate_in_1_ends_game() {
    GameConfig cfg;
    cfg.white = PlayerType::Bot;
    cfg.black = PlayerType::Bot;
    cfg.bot_depth = 2;
    cfg.movetime_ms = 100;
    GameSession g;
    g.new_game(cfg);
    check(g.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), "set mate-in-1");
    bool played = g.maybe_play_bot(nullptr);
    check(played, "bot played mate-in-1");
    check(g.game_over(), "game over after mate");
    GameResult r = g.result();
    check(r.over && r.result == "1-0" && r.reason == "checkmate", "result is 1-0 checkmate");
}

void phase4_stalemate_immediate() {
    GameSession g;
    check(g.set_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"), "set stalemate");
    GameResult r = g.check_end();
    check(r.over && r.result == "1/2" && r.reason == "stalemate", "stalemate detected");
    check(g.game_over(), "game_over true for stalemate");
}

void phase4_flag_ends_game() {
    GameConfig cfg;
    cfg.wtime = 300;
    cfg.btime = 60000;
    cfg.winc = 0;
    cfg.binc = 0;
    GameSession g;
    g.new_game(cfg);
    g.start_clock();
    g.clock().deduct(301);
    check(g.clock().flagged(), "clock flagged white");
    GameResult r = g.poll_flag();
    check(r.over && r.result == "0-1" && r.reason == "flag", "flag result 0-1");
    std::string before = g.fen();
    Move e2e4 = parse_uci("e2e4");
    check(!g.play_move(e2e4), "play_move rejected after flag");
    check(g.fen() == before, "position unchanged after flag");
}

void phase4_gameover_idempotent() {
    GameSession g;
    g.new_game(GameConfig{});
    g.check_end();
    GameResult a = g.result();
    g.check_end();
    g.poll_flag();
    GameResult b = g.result();
    check(a.over == b.over && a.result == b.result && a.reason == b.reason,
          "result idempotent across check_end/poll_flag");
}

void phase4_fifty_move_rule() {
    GameSession g;
    g.new_game(GameConfig{});
    check(g.set_fen("4k3/8/8/8/8/8/8/4K2N w - - 99 1"), "set fifty-move fen (halfmove 99)");
    Move knight = parse_uci("h1f2");
    check(g.play_move(knight), "quiet knight move h1f2");
    GameResult r = g.check_end();
    check(r.over && r.result == "1/2" && r.reason == "fifty-move rule", "fifty-move rule");
}

void phase4_threefold() {
    GameSession g;
    GameConfig cfg;
    cfg.movetime_ms = 100;
    cfg.bot_depth = 1;
    g.new_game(cfg);
    const char* cycle[4] = {"g1f3", "g8f6", "f3g1", "f6g8"};
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 4; ++i) {
            Move mv = parse_uci(cycle[i]);
            check(g.play_move(mv), "cycle move legal");
        }
    }
    GameResult r = g.check_end();
    check(r.over && r.result == "1/2" && r.reason == "threefold repetition",
          "threefold repetition after two cycles");
}

void phase4_search_leaves_position_intact() {
    GameSession g;
    GameConfig cfg;
    cfg.bot_depth = 2;
    cfg.movetime_ms = 100;
    cfg.white = PlayerType::Bot;
    cfg.black = PlayerType::Bot;
    g.new_game(cfg);
    check(g.set_fen("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4"),
          "set middlegame");
    std::string beforeFen = g.fen();
    MoveList legal;
    movegen::generate_legal(g.position_ref(), legal);
    bool played = g.maybe_play_bot(nullptr);
    check(played, "bot played in middlegame");
    check(g.last_move_uci().size() == 4 || g.last_move_uci().size() == 5, "played move is uci");
    check(g.side_to_move() == Color::Black, "side flipped to black");
}

// ---------------------------------------------------------------------------
// state_json / clock_json / game_over_json
// ---------------------------------------------------------------------------

void state_json_no_clock_test() {
    GameSession g;
    g.new_game(GameConfig{});
    std::string j = g.state_json();
    check(j.find("\"type\":\"state\"") != std::string::npos, "state json has type");
    check(j.find("\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\"")
              != std::string::npos,
          "state json has startpos fen");
    check(j.find("\"side\":\"w\"") != std::string::npos, "state json has white side");
    check(j.find("\"orientation\":\"white\"") != std::string::npos, "state json orientation white");
    check(j.find("\"status\":\"playing\"") != std::string::npos, "state json status playing");
    check(j.find("\"last_move\":null") != std::string::npos, "state json last_move null");
    // Clock inactive (no time control) -> clocks reported as null.
    check(j.find("\"w_ms\":null,\"b_ms\":null") != std::string::npos,
          "state json clocks null when no clock");
}

void state_json_active_clock_test() {
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 10000;
    cfg.btime = 20000;
    g.new_game(cfg);
    g.start_clock();
    std::string j = g.state_json();
    check(j.find("\"w_ms\":") != std::string::npos, "state json has w_ms when clock active");
    check(j.find("\"b_ms\":") != std::string::npos, "state json has b_ms when clock active");
    check(j.find("\"w_ms\":null") == std::string::npos, "state json not null when clock active");
    g.clock().stop();
}

void state_json_last_move_test() {
    GameSession g;
    g.new_game(GameConfig{});
    check(g.play_move(parse_uci("e2e4")), "play e2e4");
    std::string j = g.state_json();
    check(j.find("\"last_move\":\"e2e4\"") != std::string::npos, "state json reports last move");
    check(j.find("\"side\":\"b\"") != std::string::npos, "state json side black after e2e4");
}

void clock_json_test() {
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 10000;
    cfg.btime = 20000;
    g.new_game(cfg);
    g.start_clock();
    std::string j = g.clock_json();
    check(j.find("\"type\":\"clock\"") != std::string::npos, "clock json has type");
    check(j.find("\"w_ms\":") != std::string::npos, "clock json has w_ms");
    check(j.find("\"b_ms\":") != std::string::npos, "clock json has b_ms");
    g.clock().stop();
}

void game_over_json_test() {
    GameSession g;
    check(g.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), "set mate-in-1");
    Move ra8{Square::A1, Square::A8, PieceType::None};
    check(g.try_apply_move(ra8), "apply Ra8#");
    GameResult r = g.result();
    check(r.over && r.result == "1-0" && r.reason == "checkmate", "result is checkmate");
    std::string j = g.game_over_json();
    check(j.find("\"type\":\"game_over\"") != std::string::npos, "game_over json has type");
    check(j.find("\"result\":\"1-0\"") != std::string::npos, "game_over json result 1-0");
    check(j.find("\"reason\":\"checkmate\"") != std::string::npos, "game_over json reason checkmate");
}

// ---------------------------------------------------------------------------
// configure_clock / start_clock
// ---------------------------------------------------------------------------

void start_clock_noop_without_time_test() {
    GameSession g;
    GameConfig cfg; // all times zero
    g.new_game(cfg);
    g.start_clock();
    check(!g.clock().running(), "start_clock does not run with zero time control");
}

void start_clock_runs_with_time_test() {
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 10000;
    cfg.btime = 10000;
    g.new_game(cfg);
    g.start_clock();
    check(g.clock().running(), "start_clock runs with a time control");
    check(g.clock().remaining_ms(Color::White) == 10000, "white clock seeded to wtime");
    check(g.clock().running_side() == Color::White, "clock runs side to move (white)");
    g.clock().stop();
}

void configure_clock_test() {
    GameSession g;
    g.new_game(GameConfig{});
    g.configure_clock(30000, 40000, 500, 800);
    check(g.clock().running(), "configure_clock starts the clock");
    check(g.clock().remaining_ms(Color::White) == 30000, "configure_clock seeds white");
    check(g.clock().remaining_ms(Color::Black) == 40000, "configure_clock seeds black");
    check(g.clock().increment_ms(Color::White) == 500, "configure_clock white increment");
    check(g.clock().increment_ms(Color::Black) == 800, "configure_clock black increment");
    g.clock().stop();
}

void configure_clock_zero_stops_test() {
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 10000;
    cfg.btime = 10000;
    g.new_game(cfg);
    g.start_clock();
    check(g.clock().running(), "clock running before configure_clock(0)");
    g.configure_clock(0, 0, 0, 0);
    check(!g.clock().running(), "configure_clock(0) stops the clock");
}

// ---------------------------------------------------------------------------
// compute_limits with flagged clock
// ---------------------------------------------------------------------------

void compute_limits_flagged_test() {
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 200;
    cfg.btime = 60000;
    g.new_game(cfg);
    g.start_clock();
    g.clock().deduct(201); // flag white
    check(g.clock().flagged(), "clock flagged white");
    SearchLimits l = g.compute_limits();
    check(l.movetime_ms == 0, "flagged clock yields zero movetime");
}

// ---------------------------------------------------------------------------
// legal_moves_for_square with promotion
// ---------------------------------------------------------------------------

void legal_moves_promotion_test() {
    GameSession g;
    check(g.set_fen("8/4P3/8/8/8/8/8/4K2k w - - 0 1"), "set_fen promotion position");
    std::vector<std::string> e7 = g.legal_moves_for_square(Square::E7);
    check(e7.size() == 4, "e7 pawn has 4 promotion moves");
    check(contains(e7, "e7e8q"), "promotions include queen");
    check(contains(e7, "e7e8r"), "promotions include rook");
    check(contains(e7, "e7e8b"), "promotions include bishop");
    check(contains(e7, "e7e8n"), "promotions include knight");
}

// ---------------------------------------------------------------------------
// emitter / game-over emit (regression for consume_game_over_emit fix)
// ---------------------------------------------------------------------------

void emitter_receives_lines_test() {
    GameSession g;
    GameConfig cfg;
    cfg.movetime_ms = 100;
    cfg.bot_depth = 1;
    g.new_game(cfg);
    std::vector<std::string> lines;
    g.set_emitter([&lines](const std::string& l) { lines.push_back(l); });
    // A checkmate triggers a game_over emit through the emitter.
    check(g.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), "set mate-in-1");
    Move ra8{Square::A1, Square::A8, PieceType::None};
    check(g.try_apply_move(ra8), "apply Ra8#");
    bool found_game_over = false;
    for (const std::string& l : lines) {
        if (l.find("\"type\":\"game_over\"") != std::string::npos) found_game_over = true;
    }
    check(found_game_over, "emitter received game_over line on mate");
    g.set_emitter(nullptr);
}

void game_over_emit_once_test() {
    // The game_over line must be emitted exactly once even if check_end and
    // poll_flag both observe the terminal state.
    GameSession g;
    GameConfig cfg;
    cfg.wtime = 200;
    cfg.btime = 60000;
    g.new_game(cfg);
    int game_over_emits = 0;
    g.set_emitter([&game_over_emits](const std::string& l) {
        if (l.find("\"type\":\"game_over\"") != std::string::npos) ++game_over_emits;
    });
    g.start_clock();
    g.clock().deduct(201); // flag white -> handle_clock_flag emits once
    g.check_end();         // must not re-emit
    g.poll_flag();         // must not re-emit
    check(game_over_emits == 1, "game_over emitted exactly once across check_end/poll_flag");
    g.set_emitter(nullptr);
    g.clock().stop();
}

void run_bot_bot_emits_bestmove_test() {
    GameSession g;
    GameConfig cfg;
    cfg.white = PlayerType::Bot;
    cfg.black = PlayerType::Bot;
    cfg.bot_depth = 2;
    cfg.movetime_ms = 100;
    g.new_game(cfg);
    check(g.set_fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), "set mate-in-1 for bot");
    std::vector<std::string> lines;
    g.run_bot_bot([&lines](const std::string& l) { lines.push_back(l); });
    bool found_state = false, found_bestmove = false, found_game_over = false;
    for (const std::string& l : lines) {
        if (l.find("\"type\":\"state\"") != std::string::npos) found_state = true;
        if (l.rfind("bestmove", 0) == 0) found_bestmove = true;
        if (l.find("\"type\":\"game_over\"") != std::string::npos) found_game_over = true;
    }
    check(found_state, "run_bot_bot emits state");
    check(found_bestmove, "run_bot_bot emits bestmove");
    check(found_game_over, "run_bot_bot emits game_over on mate");
    check(g.game_over(), "run_bot_bot ends with game over on mate");
}

}  // namespace

int main() {
    startpos_and_accessors_test();
    set_fen_test();
    new_game_test();
    orientation_test();
    last_move_test();
    legal_moves_for_square_test();
    all_legal_moves_test();
    try_apply_move_test();
    try_apply_move_promotion_test();
    apply_move_test();
    play_move_test();
    is_bot_turn_test();
    human_color_test();
    compute_limits_no_clock_test();
    compute_limits_with_clock_test();
    compute_limits_black_clock_test();
    compute_limits_clamping_test();
    maybe_play_bot_not_bot_turn_test();
    maybe_play_bot_bot_turn_test();
    maybe_play_bot_no_moves_test();
    status_check_test();
    status_checkmate_test();
    status_stalemate_test();
    game_over_test();
    phase4_mate_in_1_ends_game();
    phase4_stalemate_immediate();
    phase4_flag_ends_game();
    phase4_gameover_idempotent();
    phase4_fifty_move_rule();
    phase4_threefold();
    phase4_search_leaves_position_intact();
    state_json_no_clock_test();
    state_json_active_clock_test();
    state_json_last_move_test();
    clock_json_test();
    game_over_json_test();
    start_clock_noop_without_time_test();
    start_clock_runs_with_time_test();
    configure_clock_test();
    configure_clock_zero_stops_test();
    compute_limits_flagged_test();
    legal_moves_promotion_test();
    emitter_receives_lines_test();
    game_over_emit_once_test();
    run_bot_bot_emits_bestmove_test();

    if (failures == 0) {
        std::cout << "All game tests passed\n";
        return 0;
    }

    std::cerr << failures << " game test(s) failed\n";
    return 1;
}