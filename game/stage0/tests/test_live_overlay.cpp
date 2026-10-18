#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

bool contains(const std::vector<std::string>& lines, const std::string& needle)
{
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

}

TEST_CASE("the live overlay shows the skirmisher rows from ai_knobs", "[live_overlay]")
{
    Scene scene(1);
    Stage0 stage;
    stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
    scene.tick(1.0 / 60.0);
    stage.resetFight(scene);
    for (int i = 0; i < 30; ++i) scene.tick(1.0 / 60.0);

    std::vector<std::string> lines;
    stage.overlay(scene, lines);

    for (const char* label : {"state", "desire", "spacing", "timer", "tracking", "pressure"}) {
        INFO(label);
        CHECK(contains(lines, std::string(label) + " "));
    }
    CHECK(contains(lines, "want 2.50"));
    CHECK(contains(lines, "deg off"));
}
