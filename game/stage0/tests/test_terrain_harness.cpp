#include "ichor/stage0/terrain_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <memory>

using namespace ichor::stage0;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

EnemyProbe scripted(ProbeIntent intent, float engageRange = 5.0f)
{
    EnemyProbe p;
    p.name = "scripted";
    p.engageRange = engageRange;
    p.make = [intent]() -> ProbeStep { return [intent](const ProbeSenses&) { return intent; }; };
    return p;
}

// Walks straight at the target and hits once within reach, like the real brains: no pathing.
EnemyProbe charger(float reach = 1.0f)
{
    EnemyProbe p;
    p.name = "charger";
    p.engageRange = 10.0f;
    p.make = [reach]() -> ProbeStep {
        return [reach](const ProbeSenses& s) {
            ProbeIntent out;
            const glm::vec2 d = s.target - s.self;
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len <= reach) {
                out.attack = true;
                out.hit = true;
            } else {
                out.move = d / len * 5.0f;
            }
            return out;
        };
    };
    return p;
}

const TerrainDescriptor& stock(TerrainCondition c)
{
    static const auto all = stockTerrain();
    return all[static_cast<std::size_t>(c)];
}

}

TEST_CASE("stock terrain covers the five conditions in order and is valid", "[terrain]")
{
    const auto all = stockTerrain();
    REQUIRE(all.size() == kTerrainConditionCount);
    for (std::size_t i = 0; i < all.size(); ++i) REQUIRE(static_cast<std::size_t>(all[i].condition) == i);
    for (const auto& t : all) REQUIRE_FALSE(runCondition(charger(), t).invalidInput);
}

TEST_CASE("ambusher and artillery pass all five stock conditions", "[terrain]")
{
    for (const EnemyProbe& probe : {ambusherProbe(), artilleryProbe()}) {
        const HarnessReport report = validateEnemy(probe);
        INFO(formatHarnessReport(report));
        REQUIRE(report.results.size() == kTerrainConditionCount);
        for (const ConditionResult& r : report.results) {
            REQUIRE(r.passed);
            REQUIRE(r.record.has_value());
            REQUIRE(r.summary.foeHits == 1);
        }
        REQUIRE(report.passed);
    }
}

TEST_CASE("the harness is deterministic", "[terrain]")
{
    const HarnessReport a = validateEnemy(artilleryProbe());
    const HarnessReport b = validateEnemy(artilleryProbe());
    REQUIRE(a.results.size() == b.results.size());
    for (std::size_t i = 0; i < a.results.size(); ++i) {
        REQUIRE(a.results[i].ticksToHit == b.results[i].ticksToHit);
        REQUIRE(a.results[i].record == b.results[i].record);
    }
}

TEST_CASE("slope drag costs time: the slope run is slower than the flat run", "[terrain]")
{
    const ConditionResult flat = runCondition(charger(), stock(TerrainCondition::Flat));
    const ConditionResult slope = runCondition(charger(), stock(TerrainCondition::Slope));
    REQUIRE(flat.passed);
    REQUIRE(slope.passed);
    REQUIRE(slope.ticksToHit > flat.ticksToHit * 3 / 2);

    TerrainDescriptor downhill = stock(TerrainCondition::Slope);
    downhill.uphill = {-1.0f, 0.0f};
    REQUIRE(runCondition(charger(), downhill).ticksToHit == flat.ticksToHit);
}

TEST_CASE("a behaviour that never lands a hit fails every condition", "[terrain]")
{
    const HarnessReport report = validateEnemy(scripted(ProbeIntent{}));
    REQUIRE_FALSE(report.passed);
    for (const ConditionResult& r : report.results) {
        REQUIRE_FALSE(r.hitLanded);
        REQUIRE_FALSE(r.passed);
    }
}

TEST_CASE("an attack that misses is not a hit", "[terrain]")
{
    ProbeIntent swing;
    swing.attack = true;
    const ConditionResult r = runCondition(scripted(swing), stock(TerrainCondition::Flat));
    REQUIRE_FALSE(r.passed);
    REQUIRE(r.summary.foeHits == 0);
}

TEST_CASE("a non-finite move fails the condition and stops the run", "[terrain]")
{
    for (const float bad : {kNaN, kInf, -kInf}) {
        ProbeIntent intent;
        intent.move = {bad, 0.0f};
        intent.attack = true;
        intent.hit = true;
        const ConditionResult r = runCondition(scripted(intent), stock(TerrainCondition::Flat));
        REQUIRE_FALSE(r.finite);
        REQUIRE_FALSE(r.passed);
        REQUIRE(r.ticks == 1);
    }
}

TEST_CASE("a pillar on the line sticks a straight-line mover, the flanked stock obstacles do not", "[terrain]")
{
    TerrainDescriptor blocked = stock(TerrainCondition::Flat);
    blocked.condition = TerrainCondition::Obstacle;
    blocked.obstacles.push_back({{5.0f, 0.0f}, 1.0f});
    const ConditionResult stuck = runCondition(charger(), blocked);
    REQUIRE(stuck.stuck);
    REQUIRE_FALSE(stuck.passed);
    REQUIRE(stuck.longestBlockedRun > kHarnessMaxBlockedTicks);

    REQUIRE(runCondition(charger(), stock(TerrainCondition::Obstacle)).passed);
}

TEST_CASE("pushing into the corridor wall counts as stuck", "[terrain]")
{
    ProbeIntent intoWall;
    intoWall.move = {0.0f, 4.0f};
    const ConditionResult r = runCondition(scripted(intoWall), stock(TerrainCondition::Narrow));
    REQUIRE(r.stuck);
    REQUIRE_FALSE(r.passed);
}

TEST_CASE("alternating pushes into a wall with idle ticks still count as stuck", "[terrain]")
{
    EnemyProbe p;
    p.engageRange = 5.0f;
    p.make = []() -> ProbeStep {
        auto n = std::make_shared<int>(0);
        return [n](const ProbeSenses&) {
            ProbeIntent out;
            if ((*n)++ % 2 == 0) out.move = {0.0f, 4.0f};
            return out;
        };
    };
    REQUIRE(runCondition(p, stock(TerrainCondition::Narrow)).stuck);
}

TEST_CASE("line of sight is cut by an obstacle between enemy and target", "[terrain]")
{
    TerrainDescriptor t = stock(TerrainCondition::Flat);
    t.obstacles.push_back({{2.5f, 0.0f}, 0.5f});
    bool sawBlocked = false;
    bool sawClear = false;
    EnemyProbe p;
    p.engageRange = 5.0f;
    p.make = [&]() -> ProbeStep {
        return [&](const ProbeSenses& s) {
            (s.lineOfSight ? sawClear : sawBlocked) = true;
            return ProbeIntent{};
        };
    };
    runCondition(p, t, 3);
    REQUIRE(sawBlocked);
    REQUIRE_FALSE(sawClear);

    sawBlocked = false;
    runCondition(p, stock(TerrainCondition::Flat), 3);
    REQUIRE(sawClear);
    REQUIRE_FALSE(sawBlocked);
}

TEST_CASE("hostile descriptors and probes are rejected, not run", "[terrain]")
{
    const EnemyProbe good = charger();
    auto bad = [&](auto mutate) {
        TerrainDescriptor t = stock(TerrainCondition::Flat);
        mutate(t);
        const ConditionResult r = runCondition(good, t);
        return r.invalidInput && !r.passed && r.ticks == 0;
    };
    REQUIRE(bad([](TerrainDescriptor& t) { t.boundsMin.x = kNaN; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.boundsMax = {kInf, kInf}; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.boundsMin = t.boundsMax; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.approach = {0.0f, 0.0f}; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.approach = {kNaN, 0.0f}; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.slopeDrag = 1.0f; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.slopeDrag = kNaN; }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.push_back({{kNaN, 0.0f}, 1.0f}); }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.push_back({{1.0f, 0.0f}, 0.0f}); }));
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.push_back({{0.0f, 0.0f}, 1.0f}); })); // spawn inside
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.push_back({{10.0f, 0.0f}, 1.0f}); })); // target inside
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.push_back({{39.5f, 30.0f}, 1.0f}); })); // pokes through the bounds
    REQUIRE(bad([](TerrainDescriptor& t) { t.obstacles.assign(300, TerrainDisc{{20.0f, 20.0f}, 0.5f}); }));

    EnemyProbe p = good;
    p.engageRange = kNaN;
    REQUIRE(runCondition(p, stock(TerrainCondition::Flat)).invalidInput);
    p = good;
    p.engageRange = 1.0e9f;
    REQUIRE(runCondition(p, stock(TerrainCondition::Flat)).invalidInput);
    p = good;
    p.tickSeconds = 0.0f;
    REQUIRE(runCondition(p, stock(TerrainCondition::Flat)).invalidInput);
    p = good;
    p.make = nullptr;
    REQUIRE(runCondition(p, stock(TerrainCondition::Flat)).invalidInput);
    p = good;
    p.make = []() -> ProbeStep { return nullptr; };
    REQUIRE(runCondition(p, stock(TerrainCondition::Flat)).invalidInput);

    REQUIRE(runCondition(good, stock(TerrainCondition::Flat), 0).invalidInput);
    REQUIRE(runCondition(good, stock(TerrainCondition::Flat), -5).invalidInput);
    REQUIRE(runCondition(good, stock(TerrainCondition::Flat), 1 << 30).invalidInput);
}

TEST_CASE("an empty terrain list is not a pass", "[terrain]")
{
    REQUIRE_FALSE(validateEnemy(charger(), std::vector<TerrainDescriptor>{}).passed);
}

TEST_CASE("the report names the failing condition and reason", "[terrain]")
{
    const std::string text = formatHarnessReport(validateEnemy(scripted(ProbeIntent{})));
    REQUIRE(text.find("flat") != std::string::npos);
    REQUIRE(text.find("FAIL") != std::string::npos);
    REQUIRE(text.find("no hit") != std::string::npos);
    REQUIRE(text.find("verdict: FAIL") != std::string::npos);
    REQUIRE(formatHarnessReport(validateEnemy(charger())).find("verdict: pass") != std::string::npos);
}
