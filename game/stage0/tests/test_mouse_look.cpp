#include "ichor/platform/mouse_look.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/free_look.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;
using Catch::Approx;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr float kDegToRad = 3.14159265f / 180.0f;

struct Fight {
    Scene scene{7};
    Stage0 stage;
    Fight()
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(kDt);
        stage.resetFight(scene);
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
    }
    float yaw() const { return scene.registry().get<Combatant>(scene.find(stage.playerId())).yaw; }
};

// What the platform records: one look axis per tick, from raw mouse motion.
std::vector<float> recordedAxis(MouseLook& mouse, float xrelPerTick, int ticks)
{
    std::vector<float> axis;
    for (int i = 0; i < ticks; ++i) {
        mouse.addMotion(xrelPerTick);
        axis.push_back(std::clamp(mouse.step(kDt), -1.0f, 1.0f));
    }
    return axis;
}

float turnedBy(const std::vector<float>& axis)
{
    Fight fight;
    const float before = fight.yaw();
    for (const float look : axis) {
        InputFrame frame;
        frame.lookX = look;
        fight.scene.tick(kDt, frame);
    }
    return fight.yaw() - before;
}

}

TEST_CASE("a recorded mouse delta turns the camera by the expected angle", "[mouselook]")
{
    MouseLook mouse;
    mouse.setHeld(true);
    const std::vector<float> axis = recordedAxis(mouse, 12.5f, 45); // 750 units per second, half a full turn rate
    float expected = 0.0f;
    for (const float look : axis) expected += look * kLookRateDegPerSec * kDegToRad * static_cast<float>(kDt);
    CHECK(expected > 0.5f);
    CHECK(turnedBy(axis) == Approx(expected).epsilon(0.02));
}

TEST_CASE("the mouse does not turn the camera unless the right button is held", "[mouselook]")
{
    MouseLook mouse;
    const std::vector<float> axis = recordedAxis(mouse, 12.5f, 45);
    CHECK(*std::max_element(axis.begin(), axis.end()) == 0.0f);
    CHECK(turnedBy(axis) == Approx(0.0f).margin(1e-6f));

    mouse.setHeld(true);
    const std::vector<float> held = recordedAxis(mouse, 12.5f, 45);
    CHECK(turnedBy(held) > 0.5f);
}

TEST_CASE("releasing the button stops the turn at once and drops what was pending", "[mouselook]")
{
    MouseLook mouse;
    mouse.setHeld(true);
    recordedAxis(mouse, 12.5f, 30);
    mouse.addMotion(500.0f);
    mouse.setHeld(false);
    CHECK(mouse.step(kDt) == 0.0f);
    mouse.setHeld(true);
    CHECK(mouse.step(kDt) == 0.0f);
}

TEST_CASE("sensitivity scales the turn, left is negative, hostile values are ignored", "[mouselook]")
{
    MouseLook slow;
    slow.setHeld(true);
    MouseLook fast;
    fast.setHeld(true);
    fast.setSensitivity(2.0f);
    const float a = recordedAxis(slow, 6.0f, 20).back();
    const float b = recordedAxis(fast, 6.0f, 20).back();
    CHECK(b == Approx(2.0f * a).epsilon(1e-4));

    MouseLook left;
    left.setHeld(true);
    CHECK(turnedBy(recordedAxis(left, -12.5f, 30)) < -0.3f);

    MouseLook hostile;
    hostile.setHeld(true);
    hostile.setSensitivity(std::numeric_limits<float>::quiet_NaN());
    CHECK(hostile.sensitivity() == 1.0f);
    hostile.addMotion(std::numeric_limits<float>::infinity());
    CHECK(hostile.step(kDt) == 0.0f);
    CHECK(hostile.step(std::numeric_limits<double>::quiet_NaN()) == 0.0f);
}
