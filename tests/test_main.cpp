#include <cstdint>
#include <iostream>

#include "types.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
    if (!condition) {
        std::cerr << "FAIL: " << name << "\n";
        ++failures;
    }
}

bool types_are_sized_correctly() {
    static_assert(sizeof(Bitboard) == sizeof(std::uint64_t));
    static_assert(static_cast<int>(Square::A1) == 0);
    static_assert(static_cast<int>(Square::H8) == 63);
    static_assert(static_cast<int>(Color::White) == 0);
    static_assert(static_cast<int>(PieceType::King) == 5);
    return true;
}

void bitboard_operations() {
    Bitboard b = 0;
    check(b == 0, "empty bitboard is zero");

    b = (Bitboard{1} << 0) | (Bitboard{1} << 63);
    check((b & (Bitboard{1} << 0)) != 0, "bit 0 set");
    check((b & (Bitboard{1} << 63)) != 0, "bit 63 set");

    const Bitboard cleared = b & ~(Bitboard{1} << 0);
    check((cleared & (Bitboard{1} << 0)) == 0, "bit 0 cleared");
    check((cleared & (Bitboard{1} << 63)) != 0, "bit 63 still set");
}

}  // namespace

int main() {
    check(types_are_sized_correctly(), "types are sized correctly");
    bitboard_operations();

    if (failures == 0) {
        std::cout << "All tests passed\n";
        return 0;
    }

    std::cerr << failures << " test(s) failed\n";
    return 1;
}