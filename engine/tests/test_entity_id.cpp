#include "ichor/core/entity_id.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

using namespace ichor;

TEST_CASE("entityId golden values are pinned")
{
    // If these change, every saved entity delta points at the wrong entity.
    STATIC_REQUIRE(entityId(0, 0, 0, 0) == 0x2130748aaac80268ull);
    STATIC_REQUIRE(entityId(1, 1, 0, 0) == 0xc965d6348c188782ull);
    STATIC_REQUIRE(entityId(1, 1, 2, 3) == 0x6b360221edd5a20aull);
    STATIC_REQUIRE(entityId(0x123456789abcdef0ull, 42, 7, 9) == 0x707651da18c3f0e8ull);
    STATIC_REQUIRE(entityId(1, 2, 1, 0) == 0x100a84829efc04afull);
}

TEST_CASE("entityId has no collisions across a large input grid")
{
    std::vector<EntityId> ids;
    for (std::uint64_t seed = 0; seed < 4; ++seed)
        for (std::uint64_t poi = 0; poi < 40; ++poi)
            for (std::uint32_t room = 0; room < 40; ++room)
                for (std::uint32_t slot = 0; slot < 40; ++slot)
                    ids.push_back(entityId(seed, poi, room, slot));

    std::sort(ids.begin(), ids.end());
    REQUIRE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    REQUIRE(ids.size() == 4u * 40 * 40 * 40);
}

TEST_CASE("entityId does not depend on creation order")
{
    constexpr std::uint64_t seed = 99;
    std::vector<EntityId> forward;
    for (std::uint32_t room = 0; room < 5; ++room)
        for (std::uint32_t slot = 0; slot < 5; ++slot)
            forward.push_back(entityId(seed, 7, room, slot));

    std::vector<EntityId> reversed;
    for (std::uint32_t room = 5; room-- > 0;)
        for (std::uint32_t slot = 5; slot-- > 0;)
            reversed.push_back(entityId(seed, 7, room, slot));
    std::reverse(reversed.begin(), reversed.end());

    REQUIRE(forward == reversed);
}

TEST_CASE("entityId changes with every input")
{
    const EntityId base = entityId(5, 6, 7, 8);
    REQUIRE(entityId(6, 6, 7, 8) != base);
    REQUIRE(entityId(5, 7, 7, 8) != base);
    REQUIRE(entityId(5, 6, 8, 8) != base);
    REQUIRE(entityId(5, 6, 7, 9) != base);
}

TEST_CASE("entityId differs between worlds for the same structure")
{
    for (std::uint64_t poi = 0; poi < 100; ++poi)
        REQUIRE(entityId(1, poi, 2, 3) != entityId(2, poi, 2, 3));
}

TEST_CASE("entityId inputs do not cancel or commute")
{
    REQUIRE(entityId(3, 3, 0, 0) != entityId(0, 0, 0, 0));
    REQUIRE(entityId(1, 2, 3, 4) != entityId(2, 1, 3, 4));
    REQUIRE(entityId(1, 1, 2, 2) != entityId(1, 1, 3, 3));
    REQUIRE(entityId(1, 2, 3, 4) != entityId(1, 2, 4, 3));
}
