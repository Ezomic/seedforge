#include "ichor/audio/reverb.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/dungeon_reverb.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace ichor;

namespace {

DungeonContent twoRoomDungeon()
{
    BakedDungeon dungeon;
    const graph::NodeId a = dungeon.graph.addNode("entry");
    const graph::NodeId b = dungeon.graph.addNode("challenge");
    dungeon.cells[a] = {0, 0, 0};
    dungeon.cells[b] = {1, 0, 0};
    dungeon.graph.addEdge(a, b, "path");
    const DungeonGreyBox box = buildDungeonGreyBox(dungeon, glm::dvec3(0.0));
    return buildDungeonContent(dungeon, box, {});
}

// The mean energy per frame of what rings on after an impulse, from the second 0.5 s on.
double tailEnergy(const ReverbSettings& settings)
{
    Reverb reverb;
    reverb.setSettings(settings);
    constexpr std::uint32_t kFrames = Reverb::kRate * 3;
    std::vector<float> buffer(static_cast<std::size_t>(kFrames) * 2, 0.0f);
    buffer[0] = 1.0f;
    buffer[1] = 1.0f;
    reverb.process(buffer.data(), kFrames);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = static_cast<std::size_t>(Reverb::kRate / 2) * 2; i < buffer.size(); ++i) {
        sum += static_cast<double>(buffer[i]) * buffer[i];
        ++count;
    }
    return sum / static_cast<double>(count);
}

}  // namespace

TEST_CASE("standing in a dungeon room rings on, the stand-in default does not", "[audio][dungeon_reverb]")
{
    const DungeonContent content = twoRoomDungeon();
    REQUIRE(content.rooms.size() == 2);
    const auto hall = dungeonReverbAt(content, content.rooms[0].centre + glm::dvec3(0.0, 1.0, 0.0));
    REQUIRE(hall.has_value());
    const double inHall = tailEnergy(*hall);
    const double standIn = tailEnergy(ReverbSettings{});
    CHECK(standIn == 0.0);
    CHECK(inHall > 1.0e-9);
}

TEST_CASE("every room of a dungeon gets the preset and ground outside them gets none", "[audio][dungeon_reverb]")
{
    const DungeonContent content = twoRoomDungeon();
    const auto first = dungeonReverbAt(content, content.rooms[0].centre + glm::dvec3(0.0, 1.0, 0.0));
    const auto second = dungeonReverbAt(content, content.rooms[1].centre + glm::dvec3(0.0, 1.0, 0.0));
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK_FALSE(dungeonReverbAt(content, glm::dvec3(0.0, 1.0, 500.0)).has_value());
    CHECK_FALSE(dungeonReverbAt(DungeonContent{}, glm::dvec3(0.0)).has_value());
}
