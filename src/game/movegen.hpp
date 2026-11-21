#pragma once

#include "core/move.hpp"
#include "game/position.hpp"

namespace movegen {

void generate_pseudo_legal(const Position& pos, MoveList& out);
void generate_legal(Position& pos, MoveList& out);

bool in_check(const Position& pos, Color c);
bool is_checkmate(Position& pos);
bool is_stalemate(Position& pos);

std::uint64_t perft(Position& pos, int depth);

} // namespace movegen