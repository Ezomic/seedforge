#include "ichor/anim/exile.h"
#include "ichor/anim/exile_avatar.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>

using namespace ichor;
using Catch::Approx;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kPi = 3.14159265358979323846f;

const std::filesystem::path kExile = std::filesystem::path(ICHOR_ART_DIR) / "exile";

const ExileKit& exile()
{
    static const ExileKit kit = [] {
        ExileKit k;
        REQUIRE(cookExile(kExile, k));
        return k;
    }();
    return kit;
}

std::unique_ptr<ExileAvatar> readyAvatar()
{
    auto avatar = std::make_unique<ExileAvatar>();
    REQUIRE(avatar->prepare(exile(), 7));
    return avatar;
}

float meanJointDistance(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b)
{
    REQUIRE(a.size() == b.size());
    float total = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) total += glm::length(glm::vec3(a[i][3]) - glm::vec3(b[i][3]));
    return total / static_cast<float>(a.size());
}

}

TEST_CASE("the clip follows the ground speed and a hostile speed stands still", "[exile][avatar]")
{
    CHECK(chooseAvatarClip(0.0f) == AvatarClip::Idle);
    CHECK(chooseAvatarClip(0.29f) == AvatarClip::Idle);
    CHECK(chooseAvatarClip(1.0f) == AvatarClip::Walk);
    CHECK(chooseAvatarClip(2.19f) == AvatarClip::Walk);
    CHECK(chooseAvatarClip(3.0f) == AvatarClip::Run);
    CHECK(chooseAvatarClip(6.0f) == AvatarClip::Run);
    CHECK(chooseAvatarClip(kNaN) == AvatarClip::Idle);
    CHECK(chooseAvatarClip(-3.0f) == AvatarClip::Idle);
    CHECK(chooseAvatarClip(std::numeric_limits<float>::infinity()) == AvatarClip::Idle);
}

TEST_CASE("the stride rate keeps pace with the ground inside a believable range", "[exile][avatar]")
{
    CHECK(avatarClipRate(5.2f, 5.2f) == Approx(1.0f));
    CHECK(avatarClipRate(2.6f, 5.2f) == Approx(0.5f));
    CHECK(avatarClipRate(0.1f, 5.2f) == kAvatarMinRate);
    CHECK(avatarClipRate(50.0f, 5.2f) == kAvatarMaxRate);
    CHECK(avatarClipRate(kNaN, 5.2f) == 1.0f);
    CHECK(avatarClipRate(3.0f, 0.0f) == 1.0f);
    CHECK(avatarClipRate(3.0f, kNaN) == 1.0f);
}

TEST_CASE("the yaw of a move matches the walk camera's yaw convention", "[exile][avatar]")
{
    CHECK(yawOfMotion(0.0, -1.0) == Approx(0.0f).margin(1e-6));
    CHECK(yawOfMotion(1.0, 0.0) == Approx(kPi / 2.0f));
    CHECK(yawOfMotion(-1.0, 0.0) == Approx(-kPi / 2.0f));
    CHECK(std::abs(yawOfMotion(0.0, 1.0)) == Approx(kPi));
    CHECK(yawOfMotion(std::numeric_limits<double>::quiet_NaN(), 1.0) == 0.0f);
}

TEST_CASE("turning goes the short way round, never overshoots and survives hostile numbers", "[exile][avatar]")
{
    CHECK(turnToward(0.0f, 1.0f, 0.25f) == Approx(0.25f));
    CHECK(turnToward(0.0f, 0.1f, 0.25f) == Approx(0.1f));
    CHECK(turnToward(3.0f, -3.0f, 0.1f) == Approx(3.1f));
    CHECK(turnToward(-3.0f, 3.0f, 0.1f) == Approx(-3.1f));
    float yaw = 3.0f;
    for (int i = 0; i < 100; ++i) {
        yaw = turnToward(yaw, -3.0f, 0.05f);
        CHECK(std::abs(yaw) <= kPi + 1e-5f);
    }
    CHECK(yaw == Approx(-3.0f));
    CHECK(turnToward(0.5f, kNaN, 0.1f) == 0.5f);
    CHECK(turnToward(kNaN, 1.0f, 0.1f) == Approx(0.1f));
    CHECK(turnToward(0.5f, 1.0f, kNaN) == 0.5f);
    CHECK(turnToward(0.0f, 1.0f, -1.0f) == 0.0f);
}

TEST_CASE("the feet follow the terrain on a slope and the body when it is clear of it", "[exile][avatar]")
{
    CHECK(avatarFeetHeight(10.2, 10.0) == 10.0);
    CHECK(avatarFeetHeight(9.7, 10.0) == 10.0);
    CHECK(avatarFeetHeight(11.0, 10.0) == 11.0);
    CHECK(avatarFeetHeight(8.0, 10.0) == 8.0);
    CHECK(avatarFeetHeight(10.2, std::numeric_limits<double>::quiet_NaN()) == 10.2);
    CHECK(avatarFeetHeight(std::numeric_limits<double>::infinity(), 10.0) == 0.0);
}

TEST_CASE("the avatar draws the body and both props with a full skinning palette", "[exile][avatar]")
{
    const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
    RenderView view;
    avatar.append({10.0, 4.0, -3.0}, 0.0f, 0.0f, 1.0 / 60.0, view);
    REQUIRE(view.instances.size() == 1 + exile().sockets.size());
    CHECK(view.instances[0].mesh == 7);
    CHECK(view.instances[0].position == glm::dvec3(10.0, 4.0, -3.0));
    CHECK(view.instances[0].jointCount == view.jointPalette.size());
    CHECK(view.instances[0].jointCount > 20);
    for (std::size_t i = 1; i < view.instances.size(); ++i) {
        CHECK(view.instances[i].mesh >= 7);
        CHECK(view.instances[i].jointCount == 0);
    }
}

TEST_CASE("an unprepared avatar draws nothing", "[exile][avatar]")
{
    ExileAvatar avatar;
    RenderView view;
    avatar.append({0.0, 0.0, 0.0}, 0.0f, 3.0f, 0.1, view);
    CHECK(view.instances.empty());
}

TEST_CASE("the stride moves with the speed and standing still holds a pose", "[exile][avatar]")
{
    // The metric is the mean distance every joint moves over a quarter second: it is near zero for a held pose
    // and large for a running stride, so it goes red if the clips stopped playing.
    const auto moved = [](float speed) {
        const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
        std::vector<glm::mat4> before;
        std::vector<glm::mat4> after;
        avatar.advance(speed, 0.0);
        REQUIRE(avatar.pose(chooseAvatarClip(speed), before));
        for (int i = 0; i < 15; ++i) avatar.advance(speed, 1.0 / 60.0);
        REQUIRE(avatar.pose(chooseAvatarClip(speed), after));
        return meanJointDistance(before, after);
    };
    const float run = moved(5.0f);
    const float walk = moved(1.0f);
    const float idle = moved(0.0f);
    CHECK(run > 0.05f);
    CHECK(walk > 0.01f);
    CHECK(run > 3.0f * idle);
    CHECK(walk > 2.0f * idle);
    CHECK(run > walk);
}

TEST_CASE("the walk and run clips are different poses at the same moment of the stride", "[exile][avatar]")
{
    const auto walkingHolder = readyAvatar();
    ExileAvatar& walking = *walkingHolder;
    const auto runningHolder = readyAvatar();
    ExileAvatar& running = *runningHolder;
    for (int i = 0; i < 20; ++i) {
        walking.advance(1.0f, 1.0 / 60.0);
        running.advance(5.0f, 1.0 / 60.0);
    }
    std::vector<glm::mat4> a;
    std::vector<glm::mat4> b;
    REQUIRE(walking.pose(AvatarClip::Walk, a));
    REQUIRE(running.pose(AvatarClip::Run, b));
    CHECK(meanJointDistance(a, b) > 0.02f);
}

TEST_CASE("the root's travel is taken out of the pose across a whole stride", "[exile][avatar]")
{
    const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
    std::vector<glm::mat4> skin;
    float farthest = 0.0f;
    for (int i = 0; i < 400; ++i) {
        avatar.advance(5.0f, 1.0 / 60.0);
        REQUIRE(avatar.pose(AvatarClip::Run, skin));
        // The skinning matrix of the root joint moves with the root; its ground travel must not accumulate.
        farthest = std::max(farthest, std::hypot(skin[0][3].x, skin[0][3].z));
    }
    CHECK(farthest < 0.05f);
}

TEST_CASE("the player is drawn on the terrain, faces the way it walks and keeps its facing at rest", "[exile][avatar]")
{
    const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
    avatar.face(0.0f);
    RenderView view;
    constexpr double kTick = 1.0 / 60.0;
    double clock = 0.0;
    // Walking toward +X for a second: the body swings to face +X and the drawn feet sit on the terrain, not the capsule.
    glm::dvec3 feet(0.0, 10.25, 0.0);
    glm::dvec3 drawn;
    for (int i = 0; i < 60; ++i) {
        const glm::dvec3 next = feet + glm::dvec3(3.0 * kTick, 0.0, 0.0);
        drawn = avatar.appendPlayer(feet, next, 1.0, kTick, clock, 10.0, view);
        feet = next;
        clock += kTick;
    }
    CHECK(drawn.y == 10.0);
    const glm::vec3 forward = view.instances[view.instances.size() - 1 - exile().sockets.size()].basis * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.x > 0.99f);

    // Standing still keeps that facing.
    for (int i = 0; i < 30; ++i) {
        drawn = avatar.appendPlayer(feet, feet, 1.0, kTick, clock, 10.0, view);
        clock += kTick;
    }
    const glm::vec3 resting = view.instances[view.instances.size() - 1 - exile().sockets.size()].basis * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(resting.x > 0.99f);
}

TEST_CASE("the player's stride follows the render clock and a hostile clock or tick moves nothing", "[exile][avatar]")
{
    const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
    RenderView view;
    const glm::dvec3 a(0.0, 0.0, 0.0);
    const glm::dvec3 b(0.05, 0.0, 0.0);
    avatar.appendPlayer(a, b, 0.5, 1.0 / 60.0, 5.0, 0.0, view);
    std::vector<glm::mat4> first;
    REQUIRE(avatar.pose(AvatarClip::Run, first));
    avatar.appendPlayer(a, b, 0.5, 1.0 / 60.0, 5.0, 0.0, view);
    std::vector<glm::mat4> sameClock;
    REQUIRE(avatar.pose(AvatarClip::Run, sameClock));
    CHECK(meanJointDistance(first, sameClock) == 0.0f);
    avatar.appendPlayer(a, b, 0.5, 1.0 / 60.0, 5.1, 0.0, view);
    std::vector<glm::mat4> later;
    REQUIRE(avatar.pose(AvatarClip::Run, later));
    CHECK(meanJointDistance(first, later) > 0.01f);
    // A clock that runs backwards or a huge jump advances at most a tenth of a second and never moves backwards.
    avatar.appendPlayer(a, b, 0.5, 1.0 / 60.0, 1.0, 0.0, view);
    avatar.appendPlayer(a, b, 0.5, 0.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), view);
    CHECK(view.instances.size() == 5 * (1 + exile().sockets.size()));
}

TEST_CASE("a non-finite or teleport-sized move never reaches the renderer as a stride or a turn", "[exile][avatar]")
{
    const auto holder = readyAvatar();
    ExileAvatar& avatar = *holder;
    avatar.face(0.5f);
    RenderView view;
    constexpr double kTick = 1.0 / 60.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const glm::dvec3 home(5.0, 2.0, 5.0);
    CHECK(avatar.appendPlayer(home, {nan, 2.0, 5.0}, 1.0, kTick, 0.0, 2.0, view) == glm::dvec3(0.0));
    CHECK(avatar.appendPlayer({5.0, nan, 5.0}, home, 1.0, kTick, 0.0, 2.0, view) == home);
    CHECK(view.instances.empty());

    // A respawn two kilometres away in one tick: the body stands, still facing as it was.
    avatar.appendPlayer(home, {2005.0, 2.0, 5.0}, 1.0, kTick, 0.0, 2.0, view);
    const glm::vec3 forward = view.instances[0].basis * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(forward.x == Approx(std::sin(0.5f)).margin(1e-4));
    CHECK(view.instances[0].jointCount > 0);
}
