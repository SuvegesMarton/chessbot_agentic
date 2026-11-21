#define _WIN32_WINNT 0x0A00
#include "web_server.hpp"

#include "game.hpp"
#include "httplib.h"
#include "move.hpp"
#include "position.hpp"

#include <iostream>
#include <mutex>
#include <string>

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
    return std::string("\"") + c + "\"";
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
}  // namespace

void run_web_server(int port) {
    GameSession session;
    session.reset_startpos();
    std::mutex mtx;

    httplib::Server svr;
    // no-store keeps the browser from serving a stale app.js/style.css after a
    // rebuild; static assets are tiny and served from localhost.
    svr.set_mount_point("/", "web", {{"Cache-Control", "no-store"}});

    svr.WebSocket("/ws", [&session, &mtx](const httplib::Request&, httplib::ws::WebSocket& ws) {
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
                    session.reset_startpos();
                    emit = true;
                } else if (line.rfind("position", 0) == 0) {
                    if (line.find("startpos") != std::string::npos) {
                        session.reset_startpos();
                        auto mi = line.find("moves");
                        if (mi != std::string::npos) {
                            std::string rest = line.substr(mi + 5);
                            std::size_t i = 0;
                            while (i < rest.size()) {
                                while (i < rest.size() && rest[i] == ' ') ++i;
                                if (i >= rest.size()) break;
                                std::size_t j = rest.find(' ', i);
                                if (j == std::string::npos) j = rest.size();
                                std::string tok = rest.substr(i, j - i);
                                i = j;
                                Move m = parse_uci(tok);
                                if (m.from != Square::None) {
                                    session.apply_move(m);
                                }
                            }
                        }
                        emit = true;
                    } else if (line.find("fen") != std::string::npos) {
                        auto fi = line.find("fen");
                        std::string after = line.substr(fi + 3);
                        // trim leading space
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
                        // trim trailing spaces
                        while (!fenpart.empty() && fenpart.back() == ' ') fenpart.pop_back();
                        if (session.set_fen(fenpart)) {
                            // apply moves
                            std::size_t i = 0;
                            while (i < movespart.size()) {
                                while (i < movespart.size() && movespart[i] == ' ') ++i;
                                if (i >= movespart.size()) break;
                                std::size_t j = movespart.find(' ', i);
                                if (j == std::string::npos) j = movespart.size();
                                std::string tok = movespart.substr(i, j - i);
                                i = j;
                                Move m = parse_uci(tok);
                                if (m.from != Square::None) {
                                    session.apply_move(m);
                                }
                            }
                        }
                        emit = true;
                    }
                } else if (line.rfind("go", 0) == 0) {
                    send("bestmove 0000");
                } else if (line == "stop") {
                    send("readyok");
                }
                if (emit) {
                    send(build_state(session));
                }
            }
        }
    });

    svr.listen("0.0.0.0", port);
}
