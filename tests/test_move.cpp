#include <cstdint>
#include <iostream>
#include <string>

#include "move.hpp"
#include "position.hpp"
#include "types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

void move_list_basic() {
    MoveList ml;
    check(ml.size() == 0, "empty MoveList has size 0");
    check(ml.begin() == ml.end(), "empty MoveList begin == end");

    const Move m1{Square::E2, Square::E4, PieceType::None};
    const Move m2{Square::G7, Square::G8, PieceType::Queen};
    const Move m3{Square::A1, Square::A8, PieceType::Rook};

    ml.push(m1);
    ml.push(m2);
    ml.push(m3);

    check(ml.size() == 3, "MoveList size after 3 pushes");
    check(ml.begin() != ml.end(), "non-empty MoveList begin != end");
    check(ml.moves[0].from == Square::E2 && ml.moves[0].to == Square::E4, "MoveList[0] is m1");
    check(ml.moves[1].promotion == PieceType::Queen, "MoveList[1] is m2 promotion");
    check(ml.moves[2].to == Square::A8 && ml.moves[2].promotion == PieceType::Rook,
          "MoveList[2] is m3");
}

void move_list_iteration() {
    MoveList ml;
    const Move expected[] = {
        {Square::A2, Square::A3, PieceType::None},
        {Square::B2, Square::B4, PieceType::None},
        {Square::C2, Square::C3, PieceType::None},
    };
    for (const Move& m : expected) ml.push(m);

    int idx = 0;
    bool ok = true;
    for (const Move& m : ml) {
        if (idx >= 3 || m.from != expected[idx].from || m.to != expected[idx].to) {
            ok = false;
            break;
        }
        ++idx;
    }
    check(ok && idx == 3, "range-for over MoveList yields all moves in order");

    // Non-const iteration via begin/end pointers.
    int count = 0;
    for (Move* it = ml.begin(); it != ml.end(); ++it) ++count;
    check(count == 3, "pointer iteration counts all moves");
}

void move_list_capacity() {
    MoveList ml;
    check(MOVE_LIST_CAPACITY == 256, "MOVE_LIST_CAPACITY is 256");
    for (std::size_t i = 0; i < MOVE_LIST_CAPACITY + 50; ++i) {
        ml.push(Move{Square::A1, Square::A2, PieceType::None});
    }
    check(ml.size() == MOVE_LIST_CAPACITY, "MoveList never exceeds capacity");
    check(ml.moves[MOVE_LIST_CAPACITY - 1].from == Square::A1, "last slot is filled");
}

void move_to_uci_tests() {
    check(move_to_uci(Move{Square::E2, Square::E4, PieceType::None}) == "e2e4",
          "move_to_uci e2e4");
    check(move_to_uci(Move{Square::A1, Square::H8, PieceType::None}) == "a1h8",
          "move_to_uci a1h8");
    check(move_to_uci(Move{Square::C3, Square::D4, PieceType::None}) == "c3d4",
          "move_to_uci c3d4");
    check(move_to_uci(Move{Square::G7, Square::G8, PieceType::Queen}) == "g7g8q",
          "move_to_uci promotion queen");
    check(move_to_uci(Move{Square::E7, Square::E8, PieceType::Rook}) == "e7e8r",
          "move_to_uci promotion rook");
    check(move_to_uci(Move{Square::E7, Square::E8, PieceType::Bishop}) == "e7e8b",
          "move_to_uci promotion bishop");
    check(move_to_uci(Move{Square::E7, Square::E8, PieceType::Knight}) == "e7e8n",
          "move_to_uci promotion knight");
    check(move_to_uci(Move{Square::H2, Square::H1, PieceType::Queen}) == "h2h1q",
          "move_to_uci black-side promotion square");
}

void promotion_char_tests() {
    check(promotion_char(PieceType::Queen) == 'q', "promotion_char queen");
    check(promotion_char(PieceType::Rook) == 'r', "promotion_char rook");
    check(promotion_char(PieceType::Bishop) == 'b', "promotion_char bishop");
    check(promotion_char(PieceType::Knight) == 'n', "promotion_char knight");
    check(promotion_char(PieceType::Pawn) == '\0', "promotion_char pawn -> null");
    check(promotion_char(PieceType::None) == '\0', "promotion_char none -> null");
}

void parse_uci_tests() {
    Move m = parse_uci("e2e4");
    check(m.from == Square::E2 && m.to == Square::E4, "parse_uci e2e4 squares");
    check(m.promotion == PieceType::None, "parse_uci e2e4 no promotion");

    m = parse_uci("a1h8");
    check(m.from == Square::A1 && m.to == Square::H8, "parse_uci a1h8");

    m = parse_uci("e7e8q");
    check(m.from == Square::E7 && m.to == Square::E8, "parse_uci e7e8q squares");
    check(m.promotion == PieceType::Queen, "parse_uci e7e8q queen");

    check(parse_uci("e7e8r").promotion == PieceType::Rook, "parse_uci e7e8r rook");
    check(parse_uci("e7e8b").promotion == PieceType::Bishop, "parse_uci e7e8b bishop");
    check(parse_uci("e7e8n").promotion == PieceType::Knight, "parse_uci e7e8n knight");

    // Unknown promotion char still yields valid squares, no promotion.
    m = parse_uci("e7e8k");
    check(m.from == Square::E7 && m.to == Square::E8, "parse_uci unknown promo keeps squares");
    check(m.promotion == PieceType::None, "parse_uci unknown promo -> none");

    // Too short -> default Move with None fields.
    m = parse_uci("e2e");
    check(m.from == Square::None && m.to == Square::None && m.promotion == PieceType::None,
          "parse_uci too short -> None move");
    m = parse_uci("e2");
    check(m.from == Square::None && m.to == Square::None,
          "parse_uci two chars -> None move");
    m = parse_uci("");
    check(m.from == Square::None && m.to == Square::None,
          "parse_uci empty -> None move");

    // Out-of-range files/ranks.
    m = parse_uci("e9e4");
    check(m.from == Square::None && m.to == Square::None, "parse_uci rank 9 rejected");
    m = parse_uci("z2z4");
    check(m.from == Square::None && m.to == Square::None, "parse_uci file z rejected");
    m = parse_uci("e2e4x");  // trailing junk is ignored, squares valid
    check(m.from == Square::E2 && m.to == Square::E4, "parse_uci ignores trailing junk");
}

void uci_roundtrip_tests() {
    const Move moves[] = {
        {Square::E2, Square::E4, PieceType::None},
        {Square::G1, Square::F3, PieceType::None},
        {Square::A7, Square::A8, PieceType::Queen},
        {Square::H2, Square::H1, PieceType::Knight},
        {Square::B1, Square::C3, PieceType::None},
    };
    for (const Move& orig : moves) {
        Move parsed = parse_uci(move_to_uci(orig));
        check(parsed.from == orig.from && parsed.to == orig.to &&
                  parsed.promotion == orig.promotion,
              "move_to_uci/parse_uci roundtrip preserves move");
    }
}

}  // namespace

int main() {
    move_list_basic();
    move_list_iteration();
    move_list_capacity();
    move_to_uci_tests();
    promotion_char_tests();
    parse_uci_tests();
    uci_roundtrip_tests();

    if (failures == 0) {
        std::cout << "All move tests passed\n";
        return 0;
    }

    std::cerr << failures << " move test(s) failed\n";
    return 1;
}