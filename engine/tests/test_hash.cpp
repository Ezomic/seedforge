#include "ichor/core/hash.h"

#include <catch2/catch_test_macros.hpp>

using namespace ichor;

TEST_CASE("hash64 is stable across builds")
{
    // A pinned value: if this changes, every saved entity ID changes with it.
    STATIC_REQUIRE(hash64("") == 14695981039346656037ull);
    STATIC_REQUIRE(hash64("a") == 0xaf63dc4c8601ec8cull);
    REQUIRE(hash64("chest") != hash64("Chest"));
}

TEST_CASE("hashCombine is order sensitive")
{
    REQUIRE(hashCombine(1, 2) != hashCombine(2, 1));
    REQUIRE(hashCombine(hash64("world"), 7) == hashCombine(hash64("world"), 7));
}
