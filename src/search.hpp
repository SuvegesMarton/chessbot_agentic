#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "move.hpp"
#include "position.hpp"

constexpr int MATE = 30000;

struct SearchLimits {
    int max_depth = 64;
    std::int64_t movetime_ms = 0;
    std::int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
};

struct SearchResult {
    Move best_move{Square::None, Square::None, PieceType::None};
    int score = 0;
    int depth = 0;
    std::uint64_t nodes = 0;
};

struct SearchInfo {
    int depth = 0;
    int score = 0;
    std::uint64_t nodes = 0;
    Move pv_move{Square::None, Square::None, PieceType::None};
};

struct TTEntry {
    std::uint64_t key = 0;
    std::int32_t score = 0;
    std::int16_t depth = -1;
    std::uint8_t flag = 0;
    Move best{};
};

struct ScoredMove {
    Move move{};
    int score = 0;
};

class Searcher {
public:
    explicit Searcher(Position& pos);
    SearchResult search(const SearchLimits& limits);
    void stop() { stopped_ = true; }
    void set_info_callback(std::function<void(const SearchInfo&)> cb) { on_info_ = std::move(cb); }

private:
    int negamax(int depth, int alpha, int beta, int ply);
    int quiesce(int alpha, int beta, int ply);
    void check_time();
    bool stopped_now();

    Position& pos_;
    std::vector<TTEntry> tt_;
    std::array<std::array<Move, 2>, 128> killers_{};
    std::array<std::array<Move, 128>, 128> pv_{};
    std::uint64_t nodes_ = 0;
    std::uint64_t deadline_ = 0;
    bool stopped_ = false;
    std::function<void(const SearchInfo&)> on_info_;
    SearchResult result_;
    int max_ply_ = 0;
};