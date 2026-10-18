#include "ichor/stage0/audio_telegraph.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>
#include <vector>

using namespace ichor::stage0;

namespace {

std::vector<AttackCueProblem> validate(const std::vector<AttackAudioCue>& t)
{
    return validateAttackCues(t, audioTelegraphs(), roleAudioTable(), visualTelegraphTable());
}

bool has(const std::vector<AttackCueProblem>& p, AttackCueIssue issue)
{
    for (const auto& x : p)
        if (x.issue == issue) return true;
    return false;
}

} // namespace

TEST_CASE("every attack wind-up has a valid cue", "[audio_telegraph]")
{
    CHECK(validate(attackAudioCues()).empty());
    for (const AudioTelegraph& a : audioTelegraphs()) {
        const AttackAudioCue* c = findAttackCue(attackAudioCues(), a.role, a.attack);
        REQUIRE(c != nullptr);
        CHECK(c->leadTicks >= kMinTelegraphLeadTicks);
    }
}

TEST_CASE("a missing cue for any single attack goes red", "[audio_telegraph]")
{
    const auto& full = attackAudioCues();
    for (std::size_t skip = 0; skip < full.size(); ++skip) {
        std::vector<AttackAudioCue> t;
        for (std::size_t i = 0; i < full.size(); ++i)
            if (i != skip) t.push_back(full[i]);
        const auto problems = validate(t);
        REQUIRE(has(problems, AttackCueIssue::Missing));
        CHECK(problems.front().attack == full[skip].attack);
    }
}

TEST_CASE("cue ids are unique and name their attack", "[audio_telegraph]")
{
    const auto& t = attackAudioCues();
    for (std::size_t i = 0; i < t.size(); ++i) {
        CHECK(t[i].cueId == std::string(name(t[i].role)) + "." + t[i].attack);
        for (std::size_t j = 0; j < i; ++j) CHECK(t[i].cueId != t[j].cueId);
    }
}

TEST_CASE("cue family comes from the role audio identity", "[audio_telegraph]")
{
    for (const AttackAudioCue& c : attackAudioCues()) {
        const RoleAudioIdentity* id = findRoleAudio(name(c.role));
        REQUIRE(id != nullptr);
        CHECK(id->cue == c.cue);
    }
}

TEST_CASE("attacks of one role are told apart by pitch step", "[audio_telegraph]")
{
    auto t = attackAudioCues();
    for (auto& c : t)
        if (c.role == TelegraphRole::Skirmisher) c.pitchStep = 0;
    CHECK(has(validate(t), AttackCueIssue::PitchStepClash));
}

TEST_CASE("lead matches the visual wind-up length", "[audio_telegraph]")
{
    for (const AttackAudioCue& c : attackAudioCues()) {
        const VisualTelegraph* v = findVisualTelegraph(visualTelegraphTable(), c.role, c.attack);
        REQUIRE(v != nullptr);
        CHECK(v->durationTicks == c.leadTicks);
    }
    auto t = attackAudioCues();
    t[0].leadTicks += 1;
    CHECK(has(validate(t), AttackCueIssue::LeadMismatch));
}

TEST_CASE("hostile tables are reported, not crashed on", "[audio_telegraph]")
{
    SECTION("empty table misses every attack")
    {
        CHECK(validate({}).size() == audioTelegraphs().size());
    }
    SECTION("extreme and negative values")
    {
        auto t = attackAudioCues();
        t[0].leadTicks = std::numeric_limits<int>::min();
        t[1].pitchStep = std::numeric_limits<int>::max();
        t[2].cueId.clear();
        t[3].role = static_cast<TelegraphRole>(250);
        t[4] = t[5];
        const auto p = validate(t);
        CHECK(has(p, AttackCueIssue::BadLead));
        CHECK(has(p, AttackCueIssue::BadPitchStep));
        CHECK(has(p, AttackCueIssue::EmptyCueId));
        CHECK(has(p, AttackCueIssue::UnknownRole));
        CHECK(has(p, AttackCueIssue::Duplicate));
        CHECK(has(p, AttackCueIssue::DuplicateCueId));
    }
    SECTION("wrong cue family")
    {
        auto t = attackAudioCues();
        t[0].cue = TelegraphCue::StalkHush;
        CHECK(has(validate(t), AttackCueIssue::CueFamilyMismatch));
    }
}

TEST_CASE("cue start offset is clamped", "[audio_telegraph]")
{
    const AttackAudioCue* c = findAttackCue(attackAudioCues(), TelegraphRole::Anchor, "slam");
    REQUIRE(c != nullptr);
    CHECK(cueStartOffset(*c, 60) == 0);
    CHECK(cueStartOffset(*c, 90) == 30);
    CHECK(cueStartOffset(*c, 10) == 0);
    CHECK(cueStartOffset(*c, -5) == 0);
    CHECK(cueStartOffset(*c, std::numeric_limits<int>::max()) == std::numeric_limits<int>::max() - 60);
}

TEST_CASE("unknown attack has no cue", "[audio_telegraph]")
{
    CHECK(findAttackCue(attackAudioCues(), TelegraphRole::Anchor, "") == nullptr);
    CHECK(findAttackCue(attackAudioCues(), TelegraphRole::Anchor, "nope") == nullptr);
}
