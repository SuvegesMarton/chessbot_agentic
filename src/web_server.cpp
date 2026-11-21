#define _WIN32_WINNT 0x0A00
#include "web_server.hpp"

#include "game.hpp"
#include "httplib.h"
#include "move.hpp"
#include "position.hpp"
#include "search.hpp"

#include <iostream>
#include <mutex>
#include <string>
#include <sstream>
#include <chrono>

namespace {

std::string piece_char(Piece p) {
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
    return "\"" + std::string(1, c) + "\"";
}

std::string build_state(GameSession& session) {
    std::string j = "{\"type\":\"state\",\"fen\":\"" + session.fen() + "\",\"board\":[";
    auto snap = session.board_snapshot();
    for (int i = 0; i < 64; ++i) {
        if (i) j += ",";
        j += piece_char(snap[i]);
    }
    j += "],\"side\":\"";
    j += session.side_to_move() == Color::White ? "w" : "b";
    j += "\",\"orientation\":\"" + session.orientation_string() + "\",\"clocks\":{\"w_ms\":null,\"b_ms\":null},\"status\":\"" + session.status() + "\",\"last_move\":";
    std::string lm = session.last_move_uci();
    if (lm.empty()) j += "null";
    else j += "\"" + lm + "\"";
    j += "}";
    return j;
}

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

void apply_moves_strict(GameSession& session, std::string_view moves_str) {
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
                break;
            }
        }
    }
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
        send(build_state(session));
        return;
    }

    session.play_move(result.best_move);
    send("bestmove " + move_to_uci(result.best_move));
    send(build_state(session));
}

}  // namespace

void run_web_server(int port) {
    GameSession session;
    GameConfig config;
    config.white = PlayerType::Human;
    config.black = PlayerType::Bot;
    config.bot_depth = 6;
    config.movetime_ms = 300;
    session.new_game(config);
    std::mutex mtx;

    httplib::Server svr;
    svr.set_mount_point("/", "web", {{"Cache-Control", "no-store"}});

    svr.WebSocket("/ws", [&session, &mtx, &config](const httplib::Request&, httplib::ws::WebSocket& ws) {
        auto send = [&ws](const std::string& s) {
            std::cerr << "[chessbot] -> " << s << "\n";
            ws.send(s);
        };
        std::cerr << "[chessbot] websocket client connected; sending state\n";
        {
            std::lock_guard<std::mutex> lk(mtx);
            send(build_state(session));
        }
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

            bool emit = false;
            {
                std::lock_guard<std::mutex> lk(mtx);
                if (line == "uci") {
                    send("uciok");
                } else if (line == "isready") {
                    send("readyok");
                } else if (line == "ucinewgame") {
                    session.new_game(config);
                    emit = true;
                } else if (line.rfind("legal ", 0) == 0) {
                    std::string sq_str = line.substr(6);
                    Square sq = parse_square(sq_str);
                    if (sq != Square::None) {
                        auto moves = session.legal_moves_for_square(sq);
                        send(build_legal_response(sq_str, moves));
                    } else {
                        send(build_legal_response(sq_str, {}));
                    }
                } else if (line.rfind("position", 0) == 0) {
                    if (line.find("startpos") != std::string::npos) {
                        session.reset_startpos();
                        auto mi = line.find("moves");
                        if (mi != std::string::npos) {
                            apply_moves_strict(session, line.substr(mi + 5));
                        }
                        emit = true;
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
                        if (session.set_fen(fenpart)) {
                            apply_moves_strict(session, movespart);
                        }
                        emit = true;
                    }
                } else if (line.rfind("go", 0) == 0) {
                    SearchLimits limits = parse_go_command(line);
                    Searcher searcher(session.position_ref());
                    searcher.set_info_callback([&send](const SearchInfo& info) {
                        send_info_line(send, info);
                    });
                    SearchResult result = searcher.search(limits);
                    send("bestmove " + move_to_uci(result.best_move));
                } else if (line == "stop") {
                    send("readyok");
                }
                if (emit) {
                    send(build_state(session));
                    if (session.is_bot_turn() && !session.game_over()) {
                        run_bot_turn(send, session);
                    }
                }
            }
        }
    });

    svr.listen("0.0.0.0", port);
}