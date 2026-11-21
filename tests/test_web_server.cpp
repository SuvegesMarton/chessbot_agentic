// cpp-httplib requires Windows 10 (_WIN32_WINNT >= 0x0A00) for CreateFile2.
// MinGW's <atomic> transitively pulls in <_mingw.h>, which defines
// _WIN32_WINNT 0x0601 if it is not already defined, so this must come before
// any other header.
#define _WIN32_WINNT 0x0A00

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "httplib.h"
#include "web_server.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

// Read one logical line from the WebSocket, retrying on read timeouts and
// skipping empty frames. Returns the trimmed line, or an empty string on
// failure.
std::string recv_line(httplib::ws::WebSocketClient& ws) {
    for (int i = 0; i < 60; ++i) {
        std::string frame;
        auto r = ws.read(frame);
        if (r == httplib::ws::Fail) return std::string();
        if (r == httplib::ws::Timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        while (!frame.empty() && (frame.back() == '\n' || frame.back() == '\r')) frame.pop_back();
        if (!frame.empty()) return frame;
    }
    return std::string();
}

// Read lines until we get a bestmove line, return the bestmove line.
std::string recv_bestmove(httplib::ws::WebSocketClient& ws) {
    for (int i = 0; i < 600; ++i) {
        std::string frame;
        auto r = ws.read(frame);
        if (r == httplib::ws::Fail) return std::string();
        if (r == httplib::ws::Timeout) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        while (!frame.empty() && (frame.back() == '\n' || frame.back() == '\r')) frame.pop_back();
        if (!frame.empty() && frame.rfind("bestmove", 0) == 0) {
            return frame;
        }
        // Skip info lines and other lines
    }
    return std::string();
}

}  // namespace

int main() {
    // Derive a port from the current time so rapid reruns never collide with a
    // listening socket left in TIME_WAIT by a previous process.
    const int port = 20000 + static_cast<int>(
        std::chrono::steady_clock::now().time_since_epoch().count() % 20000);

    // Run the real server in a detached thread. run_web_server blocks on
    // listen() for the lifetime of the process; we terminate the process with
    // _Exit below, which avoids tearing down (and potentially crashing on) the
    // detached server thread during static destruction.
    std::thread([port] { run_web_server(port); }).detach();

    // Brief warmup so the server has entered its accept loop, then connect with
    // retries (covers the case where the first attempt lands before bind).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::unique_ptr<httplib::ws::WebSocketClient> ws;
    for (int attempt = 0; attempt < 60; ++attempt) {
        auto c = std::make_unique<httplib::ws::WebSocketClient>(
            "ws://127.0.0.1:" + std::to_string(port) + "/ws");
        c->set_read_timeout(std::chrono::seconds(5));
        if (c->connect()) {
            ws = std::move(c);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    check(ws != nullptr, "server started and client connected");
    if (ws) {
        // The server emits an initial state frame immediately on connect.
        const std::string initial = recv_line(*ws);
        check(initial.find("\"type\":\"state\"") != std::string::npos, "initial state has type");
        check(initial.find("\"fen\"") != std::string::npos, "initial state has fen");
        check(initial.find("\"board\"") != std::string::npos, "initial state has board");

        check(ws->send("uci"), "send uci");
        check(recv_line(*ws) == "uciok", "uci -> uciok");

        check(ws->send("isready"), "send isready");
        check(recv_line(*ws) == "readyok", "isready -> readyok");

        // position fen ... emits an updated state frame; go then answers bestmove.
        check(ws->send("position fen 4k3/8/8/8/8/8/8/4K3 w - - 0 1"), "send position fen");
        const std::string afterFen = recv_line(*ws);
        check(afterFen.find("\"type\":\"state\"") != std::string::npos, "position fen emits state");
        check(afterFen.find("4k3/8/8/8/8/8/8/4K3") != std::string::npos, "state reflects fen");

        check(ws->send("go"), "send go");
        const std::string bm = recv_bestmove(*ws);
        check(bm.rfind("bestmove", 0) == 0, "go -> bestmove");

        check(ws->send("stop"), "send stop");
        check(recv_line(*ws) == "readyok", "stop -> readyok");

        ws->close();
    }

    const int code = failures == 0 ? 0 : 1;
    std::cout << (failures == 0 ? "All web_server tests passed\n"
                                : std::to_string(failures) + " web_server test(s) failed\n");
    std::_Exit(code);
}