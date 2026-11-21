#include "game/search.hpp"
#include "game/eval.hpp"
#include "game/movegen.hpp"
#include "game/position.hpp"
#include "core/bitboard.hpp"
#include "core/move.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <vector>

namespace {

constexpr int TT_SIZE = 1 << 20;
constexpr int INFINITE = 1000000;
constexpr int QDEPTH_LIMIT = 64;

inline int mvv_lva(PieceType victim, PieceType attacker) {
    static constexpr int piece_value[6] = {100, 320, 330, 500, 900, 10000};
    return piece_value[static_cast<int>(victim)] * 16 - piece_value[static_cast<int>(attacker)];
}

inline Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

} // namespace

Searcher::Searcher(Position& pos) : pos_(pos) {
    tt_.assign(TT_SIZE, {});
}

SearchResult Searcher::search(const SearchLimits& limits) {
    nodes_ = 0;
    stopped_ = false;
    max_ply_ = 0;

    using Clock = std::chrono::steady_clock;
    if (limits.movetime_ms > 0) {
        deadline_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count() + limits.movetime_ms * 1'000'000;
    } else {
        deadline_ = 0;
    }

    MoveList legal;
    movegen::generate_legal(pos_, legal);
    if (legal.size() == 0) {
        result_.best_move = {Square::None, Square::None, PieceType::None};
        result_.score = movegen::in_check(pos_, pos_.side_to_move) ? (-MATE) : 0;
        result_.depth = 0;
        result_.nodes = 0;
        return result_;
    }

    Move fallback_move = legal.moves[0];

    for (int d = 1; d <= limits.max_depth; ++d) {
        int alpha = -INFINITE;
        int beta = INFINITE;
        int best_score = -INFINITE;
        Move best_move = fallback_move;

        for (std::size_t i = 0; i < legal.size(); ++i) {
            const Move& m = legal.moves[i];
            Undo u = pos_.make_move(m);
            int score = -negamax(d - 1, -beta, -alpha, 1);
            pos_.unmake_move(u);

            if (stopped_) break;

            if (score > best_score) {
                best_score = score;
                best_move = m;
            }
            if (score > alpha) {
                alpha = score;
            }
        }

        if (stopped_) break;

        result_.best_move = best_move;
        result_.score = best_score;
        result_.depth = d;
        result_.nodes = nodes_;

        if (on_info_) {
            on_info_({d, best_score, nodes_, pv_[0][0]});
        }
    }

    if (result_.depth == 0) {
        result_.best_move = fallback_move;
        result_.score = 0;
    }

    return result_;
}

void Searcher::check_time() {
    if (deadline_ == 0) return;
    if ((nodes_ & 2047) != 0) return;

    using Clock = std::chrono::steady_clock;
    auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch()).count());
    if (now >= deadline_) {
        stopped_ = true;
    }
}

bool Searcher::stopped_now() {
    check_time();
    return stopped_;
}

int Searcher::negamax(int depth, int alpha, int beta, int ply) {
    check_time();
    if (stopped_) return 0;

    nodes_++;
    if (ply > max_ply_) max_ply_ = ply;

    if (depth <= 0) {
        return quiesce(alpha, beta, ply);
    }

    std::uint64_t key = pos_.hash();
    std::size_t idx = key & (TT_SIZE - 1);
    TTEntry& entry = tt_[idx];
    bool tt_hit = (entry.key == key);

    if (tt_hit && entry.depth >= depth) {
        if (entry.flag == 1) return entry.score;
        if (entry.flag == 2 && entry.score >= beta) return entry.score;
        if (entry.flag == 3 && entry.score <= alpha) return entry.score;
    }

    Move tt_move = tt_hit ? entry.best : Move{};

    MoveList legal;
    movegen::generate_legal(pos_, legal);
    if (legal.size() == 0) {
        bool in_chk = movegen::in_check(pos_, pos_.side_to_move);
        return in_chk ? (-MATE + ply) : 0;
    }

    std::array<ScoredMove, 256> scored;
    std::size_t scored_count = 0;

    for (std::size_t i = 0; i < legal.size(); ++i) {
        const Move& m = legal.moves[i];
        int s = 0;
        if (m.from == tt_move.from && m.to == tt_move.to && m.promotion == tt_move.promotion) {
            s = 1000000;
        } else {
            PieceType victim = PieceType::None;
            for (int t = 0; t < 6; ++t) {
                if (is_set(pos_.pieces[static_cast<int>(opposite(pos_.side_to_move))][t], m.to)) {
                    victim = static_cast<PieceType>(t);
                    break;
                }
            }
            if (victim != PieceType::None) {
                PieceType attacker = PieceType::None;
                for (int t = 0; t < 6; ++t) {
                    if (is_set(pos_.pieces[static_cast<int>(pos_.side_to_move)][t], m.from)) {
                        attacker = static_cast<PieceType>(t);
                        break;
                    }
                }
                s = mvv_lva(victim, attacker);
            } else if (m.promotion != PieceType::None) {
                s = 900000 + static_cast<int>(m.promotion) * 1000;
            } else if (m.from == killers_[ply][0].from && m.to == killers_[ply][0].to) {
                s = 8000;
            } else if (m.from == killers_[ply][1].from && m.to == killers_[ply][1].to) {
                s = 7000;
            }
        }
        scored[scored_count++] = {m, s};
    }

    for (std::size_t i = 1; i < scored_count; ++i) {
        ScoredMove tmp = scored[i];
        std::size_t j = i;
        while (j > 0 && scored[j - 1].score < tmp.score) {
            scored[j] = scored[j - 1];
            --j;
        }
        scored[j] = tmp;
    }

    int best_score = -INFINITE;
    Move best_move = scored[0].move;
    int alpha_orig = alpha;

    for (std::size_t i = 0; i < scored_count; ++i) {
        const Move& m = scored[i].move;
        Undo u = pos_.make_move(m);
        int score = -negamax(depth - 1, -beta, -alpha, ply + 1);
        pos_.unmake_move(u);

        #ifndef NDEBUG
        assert(pos_.hash() == u.hash);
        #endif

        if (stopped_) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;
        }
        if (score > alpha) {
            alpha = score;
            pv_[ply][ply] = m;
            for (int k = ply + 1; k < max_ply_; ++k) {
                pv_[ply][k] = pv_[ply + 1][k];
            }
        }
        if (alpha >= beta) {
            if (m.promotion == PieceType::None) {
                for (int t = 0; t < 6; ++t) {
                    if (is_set(pos_.pieces[static_cast<int>(opposite(pos_.side_to_move))][t], m.to)) {
                        goto store_tt;
                    }
                }
                killers_[ply][1] = killers_[ply][0];
                killers_[ply][0] = m;
            }
            break;
        }
    }

store_tt:
    TTEntry& e = tt_[idx];
    e.key = key;
    e.depth = static_cast<std::int16_t>(depth);
    e.best = best_move;
    e.score = static_cast<std::int32_t>(best_score);
    if (best_score <= alpha_orig) e.flag = 3;
    else if (best_score >= beta) e.flag = 2;
    else e.flag = 1;

    return best_score;
}

int Searcher::quiesce(int alpha, int beta, int ply) {
    check_time();
    if (stopped_) return 0;

    nodes_++;
    if (ply >= QDEPTH_LIMIT) return eval::evaluate(pos_);

    int stand = eval::evaluate(pos_);
    if (stand >= beta) return beta;
    if (stand > alpha) alpha = stand;

    MoveList legal;
    movegen::generate_legal(pos_, legal);

    std::array<ScoredMove, 256> scored;
    std::size_t scored_count = 0;

    for (std::size_t i = 0; i < legal.size(); ++i) {
        const Move& m = legal.moves[i];
        bool is_capture = false;
        PieceType victim = PieceType::None;
        for (int t = 0; t < 6; ++t) {
            if (is_set(pos_.pieces[static_cast<int>(opposite(pos_.side_to_move))][t], m.to)) {
                victim = static_cast<PieceType>(t);
                is_capture = true;
                break;
            }
        }
        if (!is_capture && m.promotion == PieceType::None) continue;

        int s = 0;
        if (is_capture) {
            PieceType attacker = PieceType::None;
            for (int t = 0; t < 6; ++t) {
                if (is_set(pos_.pieces[static_cast<int>(pos_.side_to_move)][t], m.from)) {
                    attacker = static_cast<PieceType>(t);
                    break;
                }
            }
            s = mvv_lva(victim, attacker);
        } else {
            s = 900000 + static_cast<int>(m.promotion) * 1000;
        }
        scored[scored_count++] = {m, s};
    }

    for (std::size_t i = 1; i < scored_count; ++i) {
        ScoredMove tmp = scored[i];
        std::size_t j = i;
        while (j > 0 && scored[j - 1].score < tmp.score) {
            scored[j] = scored[j - 1];
            --j;
        }
        scored[j] = tmp;
    }

    for (std::size_t i = 0; i < scored_count; ++i) {
        const Move& m = scored[i].move;
        Undo u = pos_.make_move(m);
        int score = -quiesce(-beta, -alpha, ply + 1);
        pos_.unmake_move(u);

        #ifndef NDEBUG
        assert(pos_.hash() == u.hash);
        #endif

        if (stopped_) return 0;

        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) break;
        }
    }

    return alpha;
}