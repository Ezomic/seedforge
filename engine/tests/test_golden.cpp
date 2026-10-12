#include "ichor/core/rng.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;

// Pinned literal values. Every compiler and platform must reproduce these
// exactly; a change here means every replay and every forged world changed.

TEST_CASE("golden: pcg32 reference stream")
{
    // PCG32 with seed 42, stream 54 (pcg32_random_r reference output).
    Rng rng(42, 54);
    REQUIRE(rng.nextU32() == 0xa15c02b7u);
    REQUIRE(rng.nextU32() == 0x7b47f409u);
    REQUIRE(rng.nextU32() == 0xba1d3330u);
    REQUIRE(rng.nextU32() == 0x83d2f293u);
    REQUIRE(rng.nextU32() == 0xbfa4784bu);
    REQUIRE(rng.nextU32() == 0xcbed606eu);
}

TEST_CASE("golden: nextU64 draws high word first")
{
    Rng a(42, 54);
    Rng b(42, 54);
    const std::uint64_t hi = b.nextU32();
    const std::uint64_t lo = b.nextU32();
    REQUIRE(a.nextU64() == ((hi << 32) | lo));
    REQUIRE(a.nextU64() == 0xba1d333083d2f293ull);
}

TEST_CASE("golden: scene state hash after 120 ticks")
{
    Scene scene(1);
    for (int i = 0; i < 120; ++i) {
        scene.tick(1.0 / 60.0);
    }
    REQUIRE(scene.tickCount() == 120);
    REQUIRE(scene.stateHash() == 0xdf2ba91fba40da84ull);
}
