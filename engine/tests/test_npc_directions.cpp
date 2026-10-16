#include "ichor/ui/npc_directions.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <map>
#include <string>

using namespace ichor;

namespace {

NpcKnowledge localNpc()
{
    NpcKnowledge npc;
    npc.npcId = "npc.mara";
    npc.role = DirectionRole::Local;
    npc.homeRegion = "salt_flats";
    npc.landmarks = {
        {"lm.tower", "the broken tower", {100.0f, 0.0f}},
        {"lm.well", "the dry well", {-2000.0f, 300.0f}},
    };
    npc.knownTargets = {"site.mine"};
    return npc;
}

DirectionTarget mine()
{
    return {"site.mine", "the old mine", "salt_flats", {100.0f, 400.0f}};
}

double distance(AnnotationPoint a, AnnotationPoint b)
{
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::map<DirectionQuality, int> tally(const NpcKnowledge& npc, const DirectionTarget& target, int seeds)
{
    std::map<DirectionQuality, int> counts;
    for (int s = 0; s < seeds; ++s) {
        ++counts[giveDirection(npc, target, static_cast<std::uint64_t>(s)).quality];
    }
    return counts;
}

} // namespace

TEST_CASE("accuracy depends on role and region", "[npc_directions]")
{
    CHECK(directionAccuracyPermille(DirectionRole::Local, true) > directionAccuracyPermille(DirectionRole::Traveller, true));
    CHECK(directionAccuracyPermille(DirectionRole::Traveller, true) > directionAccuracyPermille(DirectionRole::Hermit, true));
    for (auto role : {DirectionRole::Local, DirectionRole::Traveller, DirectionRole::Hermit}) {
        CHECK(directionAccuracyPermille(role, true) > directionAccuracyPermille(role, false));
        CHECK(directionAccuracyPermille(role, true) <= 1000);
        CHECK(directionAccuracyPermille(role, false) >= 0);
    }
}

TEST_CASE("roll boundaries pick the quality", "[npc_directions]")
{
    CHECK(directionQualityForRoll(1000, 0) == DirectionQuality::Exact);
    CHECK(directionQualityForRoll(1000, 999) == DirectionQuality::Exact);
    CHECK(directionQualityForRoll(0, 0) != DirectionQuality::Exact);
    CHECK(directionQualityForRoll(0, 999) == DirectionQuality::Wrong);
    CHECK(directionQualityForRoll(500, 499) == DirectionQuality::Exact);
    CHECK(directionQualityForRoll(500, 500) == DirectionQuality::Vague);
    CHECK(directionQualityForRoll(500, 999) == DirectionQuality::Wrong);
    CHECK(directionQualityForRoll(-50, 0) != DirectionQuality::Exact);
    CHECK(directionQualityForRoll(5000, 999) == DirectionQuality::Exact);
}

TEST_CASE("an NPC who knows the target describes it relative to a landmark", "[npc_directions]")
{
    const auto npc = localNpc();
    const auto target = mine();
    bool sawExact = false;
    for (std::uint64_t seed = 0; seed < 200 && !sawExact; ++seed) {
        const auto d = giveDirection(npc, target, seed);
        REQUIRE(d.status == DirectionStatus::Ok);
        REQUIRE(d.quality != DirectionQuality::Unknown);
        REQUIRE(d.annotation.has_value());
        CHECK_FALSE(d.landmarkId.empty());
        CHECK_FALSE(d.text.empty());
        CHECK(isCleanAnnotationText(d.text));
        CHECK(d.annotation->kind == AnnotationKind::Mark);
        CHECK(d.annotation->symbol == AnnotationSymbol::Direction);
        if (d.quality == DirectionQuality::Exact) {
            sawExact = true;
            CHECK(d.landmarkId == "lm.tower");
            CHECK(d.annotation->points.at(0).x == 100.0f);
            CHECK(d.annotation->points.at(0).y == 400.0f);
            CHECK(d.text.find("the broken tower") != std::string::npos);
            CHECK(d.text.find("north") != std::string::npos);
        }
    }
    CHECK(sawExact);
}

TEST_CASE("every annotation is accepted by the store", "[npc_directions]")
{
    MapAnnotationStore store;
    for (std::uint64_t seed = 0; seed < 250; ++seed) {
        auto npc = localNpc();
        npc.npcId = "npc." + std::to_string(seed);
        const auto d = giveDirection(npc, mine(), seed);
        REQUIRE(d.annotation.has_value());
        REQUIRE(store.add(*d.annotation) == AnnotationResult::Ok);
    }
}

TEST_CASE("wrong directions point away from the truth and vague ones stop short", "[npc_directions]")
{
    const auto npc = localNpc();
    const auto target = mine();
    int wrong = 0;
    int vague = 0;
    double wrongSum = 0.0;
    double vagueSum = 0.0;
    double exactSum = 0.0;
    int exact = 0;
    for (std::uint64_t seed = 0; seed < 3000; ++seed) {
        auto hermit = npc;
        hermit.role = DirectionRole::Hermit;
        hermit.homeRegion = "elsewhere";
        const auto d = giveDirection(hermit, target, seed);
        const double err = distance(d.annotation->points.at(0), target.position);
        switch (d.quality) {
        case DirectionQuality::Wrong: ++wrong; wrongSum += err; break;
        case DirectionQuality::Vague: ++vague; vagueSum += err; break;
        case DirectionQuality::Exact: ++exact; exactSum += err; break;
        case DirectionQuality::Unknown: FAIL("target is known"); break;
        }
    }
    REQUIRE(wrong > 0);
    REQUIRE(vague > 0);
    REQUIRE(exact > 0);
    CHECK(exactSum / exact == 0.0);
    CHECK(vagueSum / vague > 100.0);
    CHECK(wrongSum / wrong > vagueSum / vague);
    CHECK(wrongSum / wrong > 300.0);
}

TEST_CASE("role and region change how often the answer is right", "[npc_directions]")
{
    auto home = localNpc();
    auto stranger = localNpc();
    stranger.role = DirectionRole::Hermit;
    stranger.homeRegion = "far_away";
    const auto a = tally(home, mine(), 2000);
    const auto b = tally(stranger, mine(), 2000);
    CHECK(a.at(DirectionQuality::Exact) > b.at(DirectionQuality::Exact) * 3);
    CHECK(a.at(DirectionQuality::Exact) > 1500);
}

TEST_CASE("an NPC who does not know the target admits it", "[npc_directions]")
{
    auto npc = localNpc();
    npc.knownTargets = {"site.other"};
    const auto d = giveDirection(npc, mine(), 7);
    CHECK(d.status == DirectionStatus::Ok);
    CHECK(d.quality == DirectionQuality::Unknown);
    CHECK_FALSE(d.annotation.has_value());
    CHECK(d.landmarkId.empty());
    CHECK(d.text.find("the old mine") != std::string::npos);
    CHECK(isCleanAnnotationText(d.text));
}

TEST_CASE("empty knowledge gives no annotation", "[npc_directions]")
{
    NpcKnowledge empty;
    empty.npcId = "npc.blank";
    const auto d = giveDirection(empty, mine(), 1);
    CHECK(d.status == DirectionStatus::Ok);
    CHECK(d.quality == DirectionQuality::Unknown);
    CHECK_FALSE(d.annotation.has_value());

    auto noLandmarks = localNpc();
    noLandmarks.landmarks.clear();
    const auto e = giveDirection(noLandmarks, mine(), 1);
    CHECK(e.status == DirectionStatus::Ok);
    CHECK(e.quality == DirectionQuality::Unknown);
    CHECK_FALSE(e.annotation.has_value());
}

TEST_CASE("the target itself is never its own landmark", "[npc_directions]")
{
    auto npc = localNpc();
    npc.landmarks = {{"site.mine", "the old mine", {100.0f, 400.0f}}};
    const auto d = giveDirection(npc, mine(), 1);
    CHECK(d.quality == DirectionQuality::Unknown);
    CHECK_FALSE(d.annotation.has_value());
}

TEST_CASE("a landmark on top of the target reads as right here", "[npc_directions]")
{
    auto npc = localNpc();
    npc.landmarks = {{"lm.near", "the shrine", {100.0f, 400.0f}}};
    npc.role = DirectionRole::Hermit;
    npc.homeRegion = "far_away";
    int wrongSeen = 0;
    for (std::uint64_t seed = 0; seed < 300; ++seed) {
        const auto d = giveDirection(npc, mine(), seed);
        REQUIRE(d.annotation.has_value());
        CHECK(isCleanAnnotationText(d.text));
        if (d.quality == DirectionQuality::Wrong) {
            ++wrongSeen;
            CHECK(d.text.find("right by") == std::string::npos);
            CHECK(distance(d.annotation->points.at(0), mine().position) > 1.0);
        }
        for (const auto& p : d.annotation->points) {
            CHECK(std::isfinite(p.x));
            CHECK(std::isfinite(p.y));
        }
    }
    CHECK(wrongSeen > 0);
}

TEST_CASE("the same inputs always give the same answer", "[npc_directions]")
{
    const auto npc = localNpc();
    const auto target = mine();
    for (std::uint64_t seed = 0; seed < 100; ++seed) {
        const auto a = giveDirection(npc, target, seed);
        const auto b = giveDirection(npc, target, seed);
        CHECK(a.quality == b.quality);
        CHECK(a.text == b.text);
        CHECK(a.landmarkId == b.landmarkId);
        REQUIRE(a.annotation.has_value());
        CHECK(a.annotation->id == b.annotation->id);
        CHECK(a.annotation->text == b.annotation->text);
        CHECK(a.annotation->points.at(0).x == b.annotation->points.at(0).x);
        CHECK(a.annotation->points.at(0).y == b.annotation->points.at(0).y);
    }
}

TEST_CASE("different seeds and different NPCs can disagree", "[npc_directions]")
{
    auto npc = localNpc();
    npc.role = DirectionRole::Traveller;
    std::map<DirectionQuality, int> seen;
    for (std::uint64_t seed = 0; seed < 500; ++seed) {
        ++seen[giveDirection(npc, mine(), seed).quality];
    }
    CHECK(seen.size() >= 3);

    int differing = 0;
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        auto other = npc;
        other.npcId = "npc.other";
        if (giveDirection(npc, mine(), seed).quality != giveDirection(other, mine(), seed).quality) {
            ++differing;
        }
    }
    CHECK(differing > 0);
}

TEST_CASE("annotation ids are stable, valid and distinct per pair", "[npc_directions]")
{
    const auto a = directionAnnotationId("npc.mara", "site.mine");
    CHECK(a == directionAnnotationId("npc.mara", "site.mine"));
    CHECK(a != directionAnnotationId("npc.mara", "site.well"));
    CHECK(a != directionAnnotationId("npc.joss", "site.mine"));
    CHECK(a != directionAnnotationId("npc.mara.site", ".mine"));
    CHECK(isValidAnnotationId(a, AnnotationLimits{}.maxIdBytes));
}

TEST_CASE("hostile input is rejected without crashing", "[npc_directions]")
{
    const auto target = mine();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    SECTION("bad npc id")
    {
        for (const std::string id : {std::string(), std::string("has space"), std::string(200, 'a'), std::string("a\0b", 3)}) {
            auto npc = localNpc();
            npc.npcId = id;
            const auto d = giveDirection(npc, target, 1);
            CHECK(d.status == DirectionStatus::InvalidNpc);
            CHECK_FALSE(d.annotation.has_value());
        }
    }
    SECTION("bad target")
    {
        for (int i = 0; i < 5; ++i) {
            auto t = target;
            if (i == 0) t.id = "";
            if (i == 1) t.id = "bad id!";
            if (i == 2) t.name = "";
            if (i == 3) t.name = "line\nbreak";
            if (i == 4) t.position.x = nan;
            const auto d = giveDirection(localNpc(), t, 1);
            CHECK(d.status == DirectionStatus::InvalidTarget);
            CHECK_FALSE(d.annotation.has_value());
        }
        auto t = target;
        t.position.y = inf;
        CHECK(giveDirection(localNpc(), t, 1).status == DirectionStatus::InvalidTarget);
        t.position = {2.0e6f, 0.0f};
        CHECK(giveDirection(localNpc(), t, 1).status == DirectionStatus::InvalidTarget);
        t = target;
        t.name = std::string(65, 'x');
        CHECK(giveDirection(localNpc(), t, 1).status == DirectionStatus::InvalidTarget);
        t.name = "bad \xC0\xAF utf8";
        CHECK(giveDirection(localNpc(), t, 1).status == DirectionStatus::InvalidTarget);
    }
    SECTION("bad landmarks")
    {
        auto npc = localNpc();
        npc.landmarks.push_back({"lm.nan", "x", {nan, 0.0f}});
        CHECK(giveDirection(npc, target, 1).status == DirectionStatus::InvalidLandmark);

        npc = localNpc();
        npc.landmarks.push_back({"lm.ctl", "bell\x07", {0.0f, 0.0f}});
        CHECK(giveDirection(npc, target, 1).status == DirectionStatus::InvalidLandmark);

        npc = localNpc();
        npc.landmarks.push_back({"bad id", "ok", {0.0f, 0.0f}});
        CHECK(giveDirection(npc, target, 1).status == DirectionStatus::InvalidLandmark);

        npc = localNpc();
        npc.landmarks.push_back({"lm.tower", "again", {5.0f, 5.0f}});
        CHECK(giveDirection(npc, target, 1).status == DirectionStatus::DuplicateLandmark);
    }
    SECTION("too many landmarks")
    {
        auto npc = localNpc();
        npc.landmarks.clear();
        for (int i = 0; i < 300; ++i) {
            npc.landmarks.push_back({"lm." + std::to_string(i), "mark", {static_cast<float>(i), 0.0f}});
        }
        const auto d = giveDirection(npc, target, 1);
        CHECK(d.status == DirectionStatus::TooMuchKnowledge);
        CHECK_FALSE(d.annotation.has_value());
    }
    SECTION("too many known targets")
    {
        auto npc = localNpc();
        for (int i = 0; i < 300; ++i) {
            npc.knownTargets.push_back("t" + std::to_string(i));
        }
        CHECK(giveDirection(npc, target, 1).status == DirectionStatus::TooMuchKnowledge);
    }
    SECTION("extreme but valid coordinates stay in range")
    {
        auto npc = localNpc();
        npc.landmarks = {{"lm.far", "the far pillar", {-1.0e6f, -1.0e6f}}};
        auto t = target;
        t.position = {1.0e6f, 1.0e6f};
        MapAnnotationStore store;
        for (std::uint64_t seed = 0; seed < 250; ++seed) {
            const auto d = giveDirection(npc, t, seed);
            REQUIRE(d.status == DirectionStatus::Ok);
            REQUIRE(d.annotation.has_value());
            auto a = *d.annotation;
            a.id = "x" + std::to_string(seed);
            CHECK(store.add(a) == AnnotationResult::Ok);
        }
    }
}
