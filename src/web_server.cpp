#include "web_server.hpp"

#define _WIN32_WINNT 0x0A00
#include "httplib.h"
#include "position.hpp"
#include "move.hpp"

#include <string>

void run_web_server(int port) {
    httplib::Server svr;
    svr.WebSocket("/ws", [](const httplib::Request&, httplib::ws::WebSocket& ws) {
        Position pos;
        pos.set_starting_position();
        while (true) {
            std::string msg;
            auto r = ws.read(msg);
            if (r == httplib::ws::Fail) break;
            if (r == httplib::ws::Timeout) continue;
            std::string line = msg;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();

            if (line == "uci") {
                ws.send("uciok");
            } else if (line == "isready") {
                ws.send("readyok");
            } else if (line == "ucinewgame") {
                pos.set_starting_position();
            } else if (line.rfind("position", 0) == 0) {
                if (line.find("startpos") != std::string::npos) {
                    pos.set_starting_position();
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
                                pos.make_move(m);
                            }
                        }
                    }
                } else if (line.find("fen") != std::string::npos) {
                    // minimal: not fully parsing fen here beyond startpos
                }
            } else if (line.rfind("go", 0) == 0) {
                ws.send("bestmove 0000");
            } else if (line == "stop") {
                ws.send("readyok");
            }
        }
    });
    svr.listen("0.0.0.0", port);
}
