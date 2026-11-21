#pragma once

#include <functional>
#include <string>
#include <string_view>

#include "search.hpp"

class GameSession;

void run_web_server(int port);

namespace web {
    void send_info_line(const std::function<void(const std::string&)>& send, const SearchInfo& info);
    void run_bot_turn(const std::function<void(const std::string&)>& send, GameSession& session);
    SearchLimits parse_go_command(std::string_view line);
}