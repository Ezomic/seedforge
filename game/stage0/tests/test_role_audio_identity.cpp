#include "ichor/stage0/role_audio_identity.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <span>
#include <string_view>
#include <vector>

using namespace ichor::stage0;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

std::vector<RoleAudioIdentity> copyTable()
{
    auto t = roleAudioTable();
    return {t.begin(), t.end()};
}

bool has(const std::vector<RoleAudioProblem>& p, RoleAudioIssue issue)
{
    for (const auto& x : p) {
        if (x.issue == issue) return true;
    }
    return false;
}

} // namespace

TEST_CASE("the shipped table is valid", "[role_audio]")
{
    CHECK(validateRoleAudio(roleAudioTable()).empty());
}

TEST_CASE("every doc 05 role has an identity", "[role_audio]")
{
    for (const char* id : {"skirmisher", "anchor", "artillery", "controller", "swarm", "ambusher"}) {
        const RoleAudioIdentity* e = findRoleAudio(id);
        REQUIRE(e != nullptr);
        CHECK(e->role == id);
    }
    CHECK(roleAudioTable().size() == 6);
}

TEST_CASE("unknown roles are handled", "[role_audio]")
{
    CHECK(findRoleAudio("") == nullptr);
    CHECK(findRoleAudio("dragon") == nullptr);
    CHECK(findRoleAudio("Anchor") == nullptr);
    CHECK(findRoleAudio(std::string_view("anchor\0x", 8)) == nullptr);
    CHECK(findRoleAudio(std::span<const RoleAudioIdentity>{}, "anchor") == nullptr);
}

TEST_CASE("a colliding table goes red", "[role_audio]")
{
    SECTION("pitch overlap")
    {
        auto t = copyTable();
        t[1].pitchLowHz = t[0].pitchLowHz + 1.0f;
        t[1].pitchHighHz = t[0].pitchHighHz + 1.0f;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::PitchOverlap));
    }
    SECTION("touching bands overlap")
    {
        auto t = copyTable();
        t[1].pitchLowHz = t[0].pitchHighHz;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::PitchOverlap));
    }
    SECTION("identical rhythm")
    {
        auto t = copyTable();
        t[1].rhythm = t[0].rhythm;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::RhythmCollision));
    }
    SECTION("rotated rhythm")
    {
        auto t = copyTable();
        t[0].rhythm = 0x0005;
        t[1].rhythm = 0x0014;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::RhythmCollision));
    }
    SECTION("timbre")
    {
        auto t = copyTable();
        t[1].timbre = t[0].timbre;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::TimbreCollision));
    }
    SECTION("cue")
    {
        auto t = copyTable();
        t[1].cue = t[0].cue;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::CueCollision));
    }
    SECTION("duplicate role")
    {
        auto t = copyTable();
        t[1].role = t[0].role;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::DuplicateRole));
    }
}

TEST_CASE("the report names the colliding pair", "[role_audio]")
{
    auto t = copyTable();
    t[3].timbre = t[4].timbre;
    const auto p = validateRoleAudio(t);
    REQUIRE(p.size() == 1);
    CHECK(p[0].roleA == "ambusher");
    CHECK(p[0].roleB == "skirmisher");
}

TEST_CASE("hostile entries are rejected without crashing", "[role_audio]")
{
    for (float bad : {kNaN, kInf, -kInf, 0.0f, -5.0f}) {
        auto t = copyTable();
        t[2].pitchLowHz = bad;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::BadPitchBand));
        t = copyTable();
        t[2].pitchHighHz = bad;
        CHECK(has(validateRoleAudio(t), RoleAudioIssue::BadPitchBand));
    }
    auto t = copyTable();
    t[2].pitchHighHz = t[2].pitchLowHz;
    CHECK(has(validateRoleAudio(t), RoleAudioIssue::BadPitchBand));

    t = copyTable();
    t[0].rhythm = 0;
    CHECK(has(validateRoleAudio(t), RoleAudioIssue::EmptyRhythm));

    t = copyTable();
    t[0].role = {};
    CHECK(has(validateRoleAudio(t), RoleAudioIssue::EmptyRole));

    CHECK(validateRoleAudio({}).empty());
}

TEST_CASE("rhythm equivalence is rotation only", "[role_audio]")
{
    CHECK(rhythmsEquivalent(0x0001, 0x8000));
    CHECK(rhythmsEquivalent(0x0005, 0x0005));
    CHECK_FALSE(rhythmsEquivalent(0x0005, 0x0101));
    CHECK_FALSE(rhythmsEquivalent(0x0001, 0x0003));
}
