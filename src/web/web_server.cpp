#define _WIN32_WINNT 0x0A00
#include "web/web_server.hpp"

#include "game/game.hpp"
#include "httplib.h"
#include "core/move.hpp"
#include "game/position.hpp"
#include "game/search.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <thread>
#include <vector>

namespace {

struct WsSender {
    httplib::ws::WebSocket* ws = nullptr;
    std::mutex m;
    void send(std::string_view line) {
        std::lock_guard<std::mutex> lk(m);
        std::cerr << "[chessbot] -> " << line << "\n";
        ws->send(std::string(line));
    }
};

std::string build_legal_response(const std::string& square, const std::vector<std::string>& moves) {
    std::string j = "{\"type\":\"legal\",\"square\":\"" + square + "\",\"moves\":[";
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i) j += ",";
        j += "\"" + moves[i] + "\"";
    }
    j += "]}";
    return j;
}

Square parse_square(std::string_view s) {
    if (s.size() != 2) return Square::None;
    int f = s[0] - 'a';
    int r = s[1] - '1';
    if (f < 0 || f >= 8 || r < 0 || r >= 8) return Square::None;
    return make_square(f, r);
}

bool apply_moves_strict(GameSession& session, std::string_view moves_str) {
    std::size_t i = 0;
    while (i < moves_str.size()) {
        while (i < moves_str.size() && moves_str[i] == ' ') ++i;
        if (i >= moves_str.size()) break;
        std::size_t j = moves_str.find(' ', i);
        if (j == std::string::npos) j = moves_str.size();
        std::string tok = std::string(moves_str.substr(i, j - i));
        i = j;
        Move m = parse_uci(tok);
        if (m.from != Square::None) {
            if (!session.try_apply_move(m)) {
                std::cerr << "[chessbot] illegal move rejected: " << tok << "\n";
                return false;
            }
        }
    }
    return true;
}

SearchLimits parse_go_command(std::string_view line) {
    SearchLimits limits;
    limits.max_depth = 64;

    std::istringstream iss(std::string(line.substr(2)));
    std::string token;
    while (iss >> token) {
        if (token == "movetime") {
            iss >> limits.movetime_ms;
        } else if (token == "depth") {
            iss >> limits.max_depth;
        } else if (token == "wtime") {
            iss >> limits.wtime;
        } else if (token == "btime") {
            iss >> limits.btime;
        } else if (token == "winc") {
            iss >> limits.winc;
        } else if (token == "binc") {
            iss >> limits.binc;
        }
    }
    return limits;
}

void send_info_line(const std::function<void(const std::string&)>& send, const SearchInfo& info) {
    std::string line = "info depth " + std::to_string(info.depth) +
                       " score cp " + std::to_string(info.score) +
                       " nodes " + std::to_string(info.nodes);
    if (info.pv_move.from != Square::None) {
        line += " pv " + move_to_uci(info.pv_move);
    }
    send(line);
}

void run_bot_turn(const std::function<void(const std::string&)>& send, GameSession& session) {
    if (!session.is_bot_turn() || session.game_over()) return;

    Searcher searcher(session.position_ref());
    searcher.set_info_callback([&send](const SearchInfo& info) {
        send_info_line(send, info);
    });

    SearchResult result = searcher.search(session.compute_limits());

    if (result.best_move.from == Square::None) {
        send(session.clock_json());
        send(session.state_json());
        return;
    }

    session.play_move(result.best_move);
    send("bestmove " + move_to_uci(result.best_move));
    send(session.clock_json());
    send(session.state_json());
}

}  // namespace

void run_web_server(int port) {
    GameSession session;
    GameConfig config;
    config.white = PlayerType::Human;
    config.black = PlayerType::Bot;
    config.bot_depth = 7;
    config.movetime_ms = 300;
    session.new_game(config);

    httplib::Server svr;
    svr.set_mount_point("/", "web", {{"Cache-Control", "no-store"}});

    svr.WebSocket("/ws", [&session, &config](const httplib::Request&, httplib::ws::WebSocket& ws) {
        WsSender sender{&ws, {}};
        auto send = [&sender](const std::string& s) { sender.send(s); };
        std::thread bot_thread;
        std::atomic<bool> bot_running{false};
        GameConfig local_config = config;
        session.set_emitter([&sender](const std::string& line) { sender.send(line); });
        send(session.state_json());

        auto stop_bot = [&]() {
            session.request_stop();
            if (bot_thread.joinable()) bot_thread.join();
            bot_running.store(false);
        };

        while (true) {
            std::string msg;
            auto r = ws.read(msg);
            if (r == httplib::ws::Fail) {
                std::cerr << "[chessbot] websocket client disconnected\n";
                break;
            }
            if (r == httplib::ws::Timeout) continue;
            std::string line = msg;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
            std::cerr << "[chessbot] <- " << line << "\n";

            if (line == "uci") {
                send("uciok");
            } else if (line == "isready") {
                send("readyok");
            } else if (line == "ucinewgame") {
                stop_bot();
                session.new_game(local_config);
                if (local_config.wtime > 0 || local_config.btime > 0) session.start_clock();
                send(session.state_json());
            } else if (line.rfind("newgame", 0) == 0) {
                stop_bot();
                std::istringstream iss(line);
                std::string cmd, side;
                iss >> cmd >> side;
                GameConfig cfg = local_config;
                if (side == "human") {
                    cfg.white = PlayerType::Human;
                    cfg.black = PlayerType::Bot;
                } else if (side == "bot") {
                    cfg.white = PlayerType::Bot;
                    cfg.black = PlayerType::Bot;
                }
                std::string tok;
                while (iss >> tok) {
                    if (tok == "wtime") { std::int64_t v; iss >> v; cfg.wtime = v; }
                    else if (tok == "btime") { std::int64_t v; iss >> v; cfg.btime = v; }
                    else if (tok == "winc") { std::int64_t v; iss >> v; cfg.winc = v; }
                    else if (tok == "binc") { std::int64_t v; iss >> v; cfg.binc = v; }
                }
                local_config = cfg;
                session.new_game(cfg);
                if (cfg.wtime > 0 || cfg.btime > 0) session.start_clock();
                send(session.state_json());
                send(session.clock_json());
                if (cfg.white == PlayerType::Bot && cfg.black == PlayerType::Bot) {
                    bot_running.store(true);
                    bot_thread = std::thread([&session, &bot_running, &sender]() {
                        session.run_bot_bot([&sender](const std::string& l){ sender.send(l); });
                        bot_running.store(false);
                    });
                }
            } else if (line.rfind("move ", 0) == 0) {
                if (bot_running.load()) {
                    send("info string busy");
                } else if (session.is_bot_turn()) {
                    send("info string busy");
                } else {
                    Move m = parse_uci(line.substr(5));
                    if (m.from != Square::None && session.play_move(m)) {
                        if (session.is_bot_turn() && !session.game_over()) run_bot_turn(send, session);
                        send(session.clock_json());
                    } else {
                        send(session.state_json());
                    }
                }
            } else if (line.rfind("legal ", 0) == 0) {
                if (bot_running.load()) {
                    send("info string busy");
                } else {
                    std::string sq_str = line.substr(6);
                    Square sq = parse_square(sq_str);
                    if (sq != Square::None) {
                        auto moves = session.legal_moves_for_square(sq);
                        send(build_legal_response(sq_str, moves));
                    } else {
                        send(build_legal_response(sq_str, {}));
                    }
                }
            } else if (line.rfind("position", 0) == 0) {
                if (bot_running.load()) {
                    send("info string busy");
                } else {
                    session.clock().stop();
                    bool emit = false;
                    if (line.find("startpos") != std::string::npos) {
                        session.reset_startpos();
                        bool ok = true;
                        auto mi = line.find("moves");
                        if (mi != std::string::npos) {
                            ok = apply_moves_strict(session, line.substr(mi + 5));
                        }
                        emit = ok;
                    } else if (line.find("fen") != std::string::npos) {
                        auto fi = line.find("fen");
                        std::string after = line.substr(fi + 3);
                        std::size_t s = after.find_first_not_of(' ');
                        if (s != std::string::npos) after = after.substr(s);
                        auto mi = after.find("moves");
                        std::string fenpart;
                        std::string movespart;
                        if (mi != std::string::npos) {
                            fenpart = after.substr(0, mi);
                            movespart = after.substr(mi + 5);
                        } else {
                            fenpart = after;
                        }
                        while (!fenpart.empty() && fenpart.back() == ' ') fenpart.pop_back();
                        bool ok = false;
                        if (session.set_fen(fenpart)) {
                            ok = true;
                            if (mi != std::string::npos) {
                                ok = apply_moves_strict(session, movespart);
                            }
                        }
                        emit = ok;
                    }
                    if (emit) {
                        send(session.state_json());
                        if (session.is_bot_turn() && !session.game_over()) {
                            run_bot_turn(send, session);
                        }
                    }
                }
            } else if (line.rfind("go", 0) == 0) {
                if (bot_running.load()) {
                    send("info string busy");
                } else {
                    SearchLimits limits = parse_go_command(line);
                    if (limits.wtime > 0 || limits.btime > 0 || limits.winc > 0 || limits.binc > 0) {
                        session.configure_clock(limits.wtime, limits.btime, limits.winc, limits.binc);
                    }
                    Searcher searcher(session.position_ref());
                    searcher.set_info_callback([&send](const SearchInfo& info) {
                        send_info_line(send, info);
                    });
                    SearchResult result = searcher.search(limits);
                    send("bestmove " + move_to_uci(result.best_move));
                }
            } else if (line == "stop") {
                session.request_stop();
                send("readyok");
            }
        }

        stop_bot();
        session.set_emitter(nullptr);
    });

    svr.listen("0.0.0.0", port);
}

namespace web {
void send_info_line(const std::function<void(const std::string&)>& send, const SearchInfo& info) {
    ::send_info_line(send, info);
}
void run_bot_turn(const std::function<void(const std::string&)>& send, GameSession& session) {
    ::run_bot_turn(send, session);
}
SearchLimits parse_go_command(std::string_view line) {
    return ::parse_go_command(line);
}
}
