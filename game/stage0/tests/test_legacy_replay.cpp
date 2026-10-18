#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"
#include "../../../engine/tests/test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

constexpr double kDt = 1.0 / 60.0;

InputFrame scripted(int tick)
{
    InputFrame frame;
    const int phase = (tick / 40) % 6;
    frame.moveX = phase == 1 ? 1.0f : phase == 3 ? -0.7f : 0.0f;
    frame.moveY = phase == 0 ? 1.0f : phase == 2 ? -1.0f : phase == 4 ? 0.5f : 0.0f;
    if (tick % 97 == 5) frame.buttons |= InputFrame::Attack;
    if (tick % 151 == 7) frame.buttons |= InputFrame::Heavy;
    if (tick % 61 > 50) frame.buttons |= InputFrame::Block;
    if (tick % 173 == 11) frame.buttons |= InputFrame::Dodge;
    return frame;
}

struct Fight {
    Scene scene{7};
    Stage0 stage;
    Fight()
    {
        stage.install(scene, {0.0, 0.0, 3.0}, {0.0, 0.0, -2.0});
        scene.tick(kDt);
        stage.resetFight(scene);
    }
    const Combatant& player() const { return scene.registry().get<Combatant>(scene.find(stage.playerId())); }
    glm::dvec3 playerAt() const { return scene.registry().get<Transform>(scene.find(stage.playerId())).position; }
    void settle()
    {
        for (int i = 0; i < 30; ++i) scene.tick(kDt);
    }
};

void writeV1(const std::filesystem::path& path, const std::vector<InputFrame>& frames)
{
    std::ofstream out(path, std::ios::binary);
    const auto put = [&](const auto& value) { out.write(reinterpret_cast<const char*>(&value), sizeof(value)); };
    out.write("IINP", 4);
    put(std::uint32_t{1});
    put(static_cast<std::uint32_t>(frames.size()));
    for (const InputFrame& frame : frames) {
        put(frame.buttons);
        put(frame.moveX);
        put(frame.moveY);
    }
}

// Captured from the build before free look existed: scripted() for 1200 ticks on seed 7, no settle ticks.
constexpr std::uint64_t kVersion1Hash = 0xd92a377e198b011eULL;

}

TEST_CASE("a version 1 input file loads as legacy controls and reproduces its hash", "[freelook][replay]")
{
    std::vector<InputFrame> frames;
    for (int tick = 0; tick < 1200; ++tick) frames.push_back(scripted(tick));
    const auto path = test::tempPath("ichor_v1.iinp");
    writeV1(path, frames);

    InputStream stream;
    REQUIRE(stream.load(path));
    std::filesystem::remove(path);
    REQUIRE(stream.legacyControls);
    REQUIRE(stream.frames == frames);

    Fight fight;
    fight.stage.setLegacyControls(stream.legacyControls);
    for (const InputFrame& frame : stream.frames) fight.scene.tick(kDt, frame);
    CHECK(fight.scene.stateHash() == kVersion1Hash);

    // Without the legacy flag the same inputs play out differently, so the check above is not vacuous.
    Fight modern;
    for (const InputFrame& frame : stream.frames) modern.scene.tick(kDt, frame);
    CHECK(modern.scene.stateHash() != kVersion1Hash);
}

TEST_CASE("a version 2 input file keeps look and is not legacy", "[freelook][replay]")
{
    InputStream stream;
    stream.frames.push_back({InputFrame::LockOn, 0.5f, -1.0f, 0.75f});
    stream.frames.push_back({0, 0.0f, 0.0f, -1.0f});
    const auto path = test::tempPath("ichor_v2.iinp");
    REQUIRE(stream.save(path));
    InputStream loaded;
    loaded.legacyControls = true;
    REQUIRE(loaded.load(path));
    std::filesystem::remove(path);
    CHECK_FALSE(loaded.legacyControls);
    CHECK(loaded.frames == stream.frames);
}

TEST_CASE("a truncated or lying version 1 file is refused", "[freelook][replay]")
{
    const auto path = test::tempPath("ichor_v1_bad.iinp");
    {
        std::ofstream out(path, std::ios::binary);
        const std::uint32_t version = 1;
        const std::uint32_t count = 0xFFFFFFFFu;
        out.write("IINP", 4);
        out.write(reinterpret_cast<const char*>(&version), 4);
        out.write(reinterpret_cast<const char*>(&count), 4);
    }
    InputStream stream;
    CHECK_FALSE(stream.load(path));
    CHECK(stream.frames.empty());
    CHECK_FALSE(stream.legacyControls);
    std::filesystem::remove(path);
}

TEST_CASE("free look is the default: forward moves along the player's facing", "[freelook][fight]")
{
    Fight fight;
    fight.settle();
    InputFrame turn;
    turn.lookX = 1.0f;
    for (int i = 0; i < 30; ++i) fight.scene.tick(kDt, turn); // half a second at 240 deg/s is 120 degrees
    const float yaw = fight.player().yaw;
    CHECK(yaw > 1.5f);
    CHECK(yaw < 2.6f);

    const glm::dvec3 before = fight.playerAt();
    InputFrame forward;
    forward.moveY = 1.0f;
    for (int i = 0; i < 20; ++i) fight.scene.tick(kDt, forward);
    const glm::dvec3 moved = fight.playerAt() - before;
    const glm::dvec3 facing(std::sin(yaw), 0.0, -std::cos(yaw));
    CHECK(glm::dot(glm::normalize(moved), facing) > 0.97);
}

TEST_CASE("the legacy stick still moves in world axes", "[freelook][fight]")
{
    Fight fight;
    fight.stage.setLegacyControls(true);
    fight.settle();
    const glm::dvec3 before = fight.playerAt();
    InputFrame right;
    right.moveX = 1.0f;
    for (int i = 0; i < 20; ++i) fight.scene.tick(kDt, right);
    CHECK(fight.playerAt().x - before.x > 0.5);
}

TEST_CASE("the player does not auto-face the foe under free look, and does under lock-on", "[freelook][fight]")
{
    InputFrame strafe;
    strafe.moveX = 1.0f;

    Fight free;
    free.settle();
    for (int i = 0; i < 90; ++i) free.scene.tick(kDt, strafe);

    Fight locked;
    locked.stage.setLockOn(true);
    locked.settle();
    for (int i = 0; i < 90; ++i) locked.scene.tick(kDt, strafe);
    const glm::dvec3 toFoe = glm::dvec3(0.0, 0.0, -2.0) - locked.playerAt();
    const float wanted = std::atan2(static_cast<float>(toFoe.x), static_cast<float>(-toFoe.z));
    CHECK(std::abs(free.player().yaw) < 0.05f);
    CHECK(std::abs(wanted) > 0.3f); // the player really moved off the line to the foe
    CHECK(std::abs(locked.player().yaw - wanted) < 0.15f);
}

TEST_CASE("the lock-on button toggles on the press, not while held", "[freelook][fight]")
{
    Fight fight;
    fight.settle();
    CHECK_FALSE(fight.stage.lockedOn());
    InputFrame press;
    press.buttons = InputFrame::LockOn;
    for (int i = 0; i < 10; ++i) fight.scene.tick(kDt, press);
    CHECK(fight.stage.lockedOn());
    fight.scene.tick(kDt, InputFrame{});
    for (int i = 0; i < 10; ++i) fight.scene.tick(kDt, press);
    CHECK_FALSE(fight.stage.lockedOn());
}

TEST_CASE("a legacy replay ignores the lock-on button", "[freelook][fight]")
{
    Fight fight;
    fight.stage.setLegacyControls(true);
    fight.settle();
    InputFrame press;
    press.buttons = InputFrame::LockOn;
    fight.scene.tick(kDt, press);
    CHECK(fight.stage.legacyControls());
    CHECK(fight.stage.lockedOn()); // legacy always faces the foe
}

TEST_CASE("a hostile look axis never breaks the facing", "[freelook][fight]")
{
    Fight fight;
    fight.settle();
    InputFrame bad;
    bad.lookX = std::numeric_limits<float>::quiet_NaN();
    bad.moveX = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 30; ++i) fight.scene.tick(kDt, bad);
    CHECK(std::isfinite(fight.player().yaw));
    CHECK(std::isfinite(fight.playerAt().x));
}

TEST_CASE("the tracking clamp still bounds the player's facing through a swing", "[freelook][fight]")
{
    Fight fight;
    fight.settle();
    InputFrame swing;
    swing.buttons = InputFrame::Attack;
    swing.lookX = 1.0f;
    fight.scene.tick(kDt, swing);
    const float start = fight.player().yaw;
    swing.buttons = 0;
    for (int i = 0; i < 40; ++i) fight.scene.tick(kDt, swing);
    const float turned = fight.player().yaw - start;
    CHECK(turned > 0.0f);
    CHECK(turned < 0.25f); // the 30 deg/s wind-up cap for 0.35 s is 0.18 rad; free look alone would turn 2.8 rad
}

TEST_CASE("a scripted free-look fight is deterministic", "[freelook][fight]")
{
    std::uint64_t hashes[2] = {};
    for (std::uint64_t& hash : hashes) {
        Fight fight;
        fight.settle();
        for (int tick = 0; tick < 3600; ++tick) {
            InputFrame frame = scripted(tick);
            frame.lookX = (tick / 120) % 2 == 0 ? 0.3f : -0.3f;
            fight.scene.tick(kDt, frame);
        }
        CHECK(std::isfinite(fight.player().yaw));
        hash = fight.scene.stateHash();
    }
    CHECK(hashes[0] == hashes[1]);
}
