#include "ichor/anim/animation.h"
#include "ichor/anim/exile.h"
#include "ichor/anim/exile_avatar.h"
#include "ichor/assets/bundle_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <array>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace ichor;

namespace {

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

const AnimationClip& clipNamed(const ExileKit& kit, const std::string& name)
{
    const auto it = std::find_if(kit.bundles[0].clips.begin(), kit.bundles[0].clips.end(), [&](const AnimationClip& c) { return c.name == name; });
    REQUIRE(it != kit.bundles[0].clips.end());
    return *it;
}

// Footfalls of an avatar walking at `speed` for `seconds` on the 60 Hz fixed step, with the stride it covered.
struct Walked {
    std::vector<std::uint32_t> feet;
    double cycles = 0.0;
};

Walked walk(ExileAvatar& avatar, float speed, double seconds, float clipDuration, float clipTravelSpeed)
{
    Walked walked;
    const double dt = 1.0 / 60.0;
    for (double t = 0.0; t < seconds; t += dt) {
        avatar.advance(speed, dt);
        for (const std::uint32_t foot : avatar.takeFootfalls()) walked.feet.push_back(foot);
    }
    const float rate = avatarClipRate(speed, clipTravelSpeed);
    walked.cycles = seconds * rate / clipDuration;
    return walked;
}

}

TEST_CASE("the cooked walk and run clips land both feet equally often and a standing clip lands none", "[exile][footfall]")
{
    for (const char* name : {"walk", "run"}) {
        const AnimationClip& clip = clipNamed(exile(), name);
        INFO(name);
        std::array<int, 2> perFoot{};
        for (const FootContact& c : clip.footContacts) {
            REQUIRE(c.foot < 2);
            REQUIRE(c.time >= 0.0f);
            REQUIRE(c.time <= clip.duration);
            perFoot[c.foot] += 1;
        }
        CHECK(perFoot[0] >= 1);
        CHECK(perFoot[1] == perFoot[0]);
    }
    CHECK(clipNamed(exile(), "idle").footContacts.empty());
}

TEST_CASE("the cooked contacts fall while the foot is low", "[exile][footfall]")
{
    AnimationSet set;
    REQUIRE(set.load(exile().bundles[0]));
    const std::vector<std::string>& names = set.jointNames(0);
    for (const char* name : {"walk", "run"}) {
        const std::uint32_t clip = set.findClip(name);
        REQUIRE(clip != AnimationSet::kNoClip);
        const float duration = set.clipDuration(clip);
        for (const FootContact& c : set.footContacts(clip)) {
            const std::string joint = c.foot == 0 ? "mixamorig:LeftFoot" : "mixamorig:RightFoot";
            const auto it = std::find(names.begin(), names.end(), joint);
            REQUIRE(it != names.end());
            const auto index = static_cast<std::size_t>(it - names.begin());
            std::vector<glm::mat4> pose;
            float lowest = 1e9f;
            float highest = -1e9f;
            constexpr int kSamples = 60;
            for (int i = 0; i < kSamples; ++i) {
                REQUIRE(set.samplePose(clip, duration * static_cast<float>(i) / kSamples, pose));
                lowest = std::min(lowest, pose[index][3].y);
                highest = std::max(highest, pose[index][3].y);
            }
            REQUIRE(set.samplePose(clip, c.time, pose));
            INFO(name << " foot " << c.foot << " at " << c.time);
            CHECK(pose[index][3].y < 0.5f * (lowest + highest));
        }
    }
}

TEST_CASE("the contacts survive a bundle written and read back", "[exile][footfall]")
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "ichor_footfall_roundtrip.isc";
    REQUIRE(writeBundle(path, exile().bundles[0]));
    SceneBundle back;
    REQUIRE(readBundle(path, back));
    std::filesystem::remove(path);
    REQUIRE(back.clips.size() == exile().bundles[0].clips.size());
    for (std::size_t i = 0; i < back.clips.size(); ++i) {
        const auto& a = back.clips[i].footContacts;
        const auto& b = exile().bundles[0].clips[i].footContacts;
        REQUIRE(a.size() == b.size());
        for (std::size_t k = 0; k < a.size(); ++k) {
            CHECK(a[k].time == b[k].time);
            CHECK(a[k].foot == b[k].foot);
        }
    }
}

TEST_CASE("footfalls land once per planted foot per cycle at walk, run and dodge speeds, left and right in turn", "[exile][footfall]")
{
    ExileAvatar avatar;
    REQUIRE(avatar.prepare(exile(), 0));
    REQUIRE(avatar.hasFootContacts());
    AnimationSet set;
    REQUIRE(set.load(exile().bundles[0]));
    struct Case {
        const char* clip;
        float speed;
    };
    // 6 m/s is a dodge's pace (CombatConfig::dodgeSpeed); the run clip carries it at its fastest playback.
    for (const Case c : {Case{"walk", 1.4f}, Case{"run", 4.0f}, Case{"run", 6.0f}}) {
        const std::uint32_t id = set.findClip(c.clip);
        glm::vec3 start;
        glm::vec3 end;
        REQUIRE(set.rootTranslation(id, 0.0f, start));
        REQUIRE(set.rootTranslation(id, set.clipDuration(id), end));
        const float travel = std::hypot(end.x - start.x, end.z - start.z) / set.clipDuration(id);
        INFO(c.clip << " at " << c.speed);
        avatar.advance(0.0f, 0.0);
        const Walked walked = walk(avatar, c.speed, 12.0, set.clipDuration(id), travel);
        const auto left = std::count(walked.feet.begin(), walked.feet.end(), 0u);
        const auto right = std::count(walked.feet.begin(), walked.feet.end(), 1u);
        const auto& contacts = set.footContacts(id);
        const auto perClip = static_cast<double>(std::count_if(contacts.begin(), contacts.end(), [](const FootContact& f) { return f.foot == 0; }));
        const double expected = walked.cycles * perClip;
        INFO("left " << left << " right " << right << " expected " << expected);
        CHECK(std::abs(static_cast<double>(left) - expected) <= 1.0);
        CHECK(std::abs(static_cast<double>(right) - expected) <= 1.0);
        REQUIRE(walked.feet.size() > 4);
        for (std::size_t i = 1; i < walked.feet.size(); ++i) {
            CHECK(walked.feet[i] != walked.feet[i - 1]);
        }
    }
}

TEST_CASE("a clip without contacts falls back to the distance pacer", "[exile][footfall]")
{
    ExileKit bare = exile();
    for (AnimationClip& clip : bare.bundles[0].clips) clip.footContacts.clear();
    ExileAvatar avatar;
    REQUIRE(avatar.prepare(bare, 0));
    CHECK_FALSE(avatar.hasFootContacts());
    for (int i = 0; i < 600; ++i) avatar.advance(4.0f, 1.0 / 60.0);
    CHECK(avatar.takeFootfalls().empty());
}

TEST_CASE("a stretch of stride that wraps past the end of the clip counts the contacts on both sides", "[exile][footfall]")
{
    const std::vector<FootContact> contacts{{0.1f, 0}, {0.6f, 1}};
    CHECK(footfallsBetween(contacts, 1.0f, 0.0f, 0.2f) == std::vector<std::uint32_t>{0});
    CHECK(footfallsBetween(contacts, 1.0f, 0.5f, 0.7f) == std::vector<std::uint32_t>{1});
    CHECK(footfallsBetween(contacts, 1.0f, 0.9f, 0.2f) == std::vector<std::uint32_t>{0});
    CHECK(footfallsBetween(contacts, 1.0f, 0.5f, 0.2f) == std::vector<std::uint32_t>{1, 0});
    CHECK(footfallsBetween(contacts, 1.0f, 0.1f, 0.5f).empty());
    CHECK(footfallsBetween(contacts, 0.0f, 0.0f, 0.5f).empty());
}
