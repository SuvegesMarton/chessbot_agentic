#include <iostream>
#include <string>

#include "web/web_server.hpp"

int main(int argc, char** argv) {
    int port = 8080;
    bool uci = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (a == "--uci") {
            uci = true;
        }
    }
    if (uci) {
        std::cout << "readyok\n";
        return 0;
    }
    run_web_server(port);
    return 0;
}
