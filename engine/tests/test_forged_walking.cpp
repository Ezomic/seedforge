#include "ichor/sim/scene.h"
#include "ichor/traversal/climb_rules.h"
#include "ichor/world/forged_walk_bot.h"
#include "ichor/world/forged_walking.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

using namespace ichor;

namespace {

// A square heightfield n samples to a side, one metre apart, one cell, heights in tenths of a metre.
Heightfield gridOf(std::uint32_t n, const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& tenths)
{
    WorldDesc desc;
    desc.sizeMetres = n - 1;
    desc.metresPerSample = 1;
    desc.cellMetres = n - 1;
    desc.heightRange = 6553.5f;
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(n) * n);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            samples[static_cast<std::size_t>(z) * n + x] = tenths(x, z);
        }
    }
    return Heightfield(desc, std::move(samples));
}

// Flat ground, then a wall rising `tenthsPerMetre` for 10 m from x = 20, then flat again.
Heightfield wallOf(std::uint16_t tenthsPerMetre)
{
    return gridOf(65, [tenthsPerMetre](std::uint32_t x, std::uint32_t) {
        const std::uint32_t risen = x < 20 ? 0 : (x > 30 ? 10 : x - 20);
        return static_cast<std::uint16_t>(10000 + risen * tenthsPerMetre);
    });
}

// A plateau with a bowl of the given floor, shored gently (0.5 m a metre), and a notch the water leaves by.
Heightfield basinOf(std::uint16_t floor)
{
    return gridOf(33, [floor](std::uint32_t x, std::uint32_t z) {
        if (x == 32 && z == 16) return std::uint16_t{5000};
        const auto dx = static_cast<int>(x) - 16;
        const auto dz = static_cast<int>(z) - 16;
        const int d = std::max(std::abs(dx), std::abs(dz));
        const int rise = d > 4 ? (d - 4) * 5 : 0;
        return static_cast<std::uint16_t>(std::min(10000, floor + rise));
    });
}

struct Lake {
    explicit Lake(std::uint16_t floor)
        : field(basinOf(floor))
        , hydrology(std::make_shared<const Hydrology>(computeHydrology(field)))
    {
        rivers.riverThreshold = 100000;
        water = std::make_unique<WaterField>(field, hydrology, rivers);
    }
    Heightfield field;
    std::shared_ptr<const Hydrology> hydrology;
    HydrologyParams rivers;
    std::unique_ptr<WaterField> water;
};

CapabilitySet with(Capability capability)
{
    CapabilitySet set;
    set.add(capability);
    return set;
}

}

TEST_CASE("the walk limit is the climb rules' own: gentler than minSlopeDegrees is walked, steeper is not", "[walking]")
{
    const float limit = ClimbLimits{}.minSlopeDegrees;
    // 11 tenths a metre is 47.7 degrees, 13 is 52.4: either side of the 50 degree limit.
    const Heightfield walkable = wallOf(11);
    const Heightfield steep = wallOf(13);
    const ForgedWalking onWalkable(walkable, nullptr, CapabilitySet{});
    const ForgedWalking onSteep(steep, nullptr, CapabilitySet{});

    REQUIRE(limit == kSteepSlopeDegrees);
    REQUIRE(onWalkable.params().maxSlopeDegrees == limit);
    REQUIRE(onWalkable.assess(25.0, 32.0).walkable());
    const WalkAssessment blocked = onSteep.assess(25.0, 32.0);
    REQUIRE(blocked.block == WalkBlock::TooSteep);
    REQUIRE(blocked.hazard > 0.0f);
    REQUIRE(blocked.speedFactor() == 0.0f);
}

TEST_CASE("a rise slows the walker before it stops them", "[walking]")
{
    const Heightfield flat = wallOf(0);
    const Heightfield rise = wallOf(10); // 45 degrees, walkable but past the ease slope
    const ForgedWalking walkingFlat(flat, nullptr, CapabilitySet{});
    const ForgedWalking walkingRise(rise, nullptr, CapabilitySet{});

    REQUIRE(walkingFlat.assess(25.0, 32.0).slopeFactor == 1.0f);
    const WalkAssessment eased = walkingRise.assess(25.0, 32.0);
    REQUIRE(eased.walkable());
    REQUIRE(eased.slopeFactor < 1.0f);
    REQUIRE(eased.slopeFactor >= walkingRise.params().minSlopeFactor);
}

TEST_CASE("the filter stops a walker at a wall too steep to walk, and lets one through a slope that is walked", "[walking]")
{
    const Heightfield steep = wallOf(25);
    const ForgedWalking walking(steep, nullptr, CapabilitySet{});
    const glm::dvec3 velocity{3.0, 0.0, 0.0};

    const glm::dvec3 before = walking.filterVelocity({10.0, 0.0, 32.0}, velocity);
    REQUIRE(before.x == 3.0);
    const glm::dvec3 atFoot = walking.filterVelocity({19.5, 0.0, 32.0}, velocity);
    REQUIRE(atFoot.x == 0.0);

    const Heightfield gentle = wallOf(8);
    const ForgedWalking walkingGentle(gentle, nullptr, CapabilitySet{});
    REQUIRE(walkingGentle.filterVelocity({19.5, 0.0, 32.0}, velocity).x > 0.0);
}

TEST_CASE("a walker slides along a wall instead of stopping dead", "[walking]")
{
    const Heightfield steep = wallOf(25);
    const ForgedWalking walking(steep, nullptr, CapabilitySet{});
    const glm::dvec3 slid = walking.filterVelocity({19.5, 0.0, 32.0}, {2.0, 0.0, 2.0});
    REQUIRE(slid.x == 0.0);
    REQUIRE(slid.z == 2.0);
}

TEST_CASE("a walker on blocked ground can always walk out of it", "[walking]")
{
    const Heightfield steep = wallOf(25);
    const ForgedWalking walking(steep, nullptr, CapabilitySet{});
    REQUIRE(walking.assess(25.0, 32.0).block == WalkBlock::TooSteep);

    const glm::dvec3 downhill = walking.filterVelocity({25.0, 0.0, 32.0}, {-3.0, 0.0, 0.0});
    REQUIRE(downhill.x < 0.0);
    const glm::dvec3 uphill = walking.filterVelocity({25.0, 0.0, 32.0}, {3.0, 0.0, 0.0});
    REQUIRE(uphill.x > 0.0); // the same slope all the way: no worse than here
    const glm::dvec3 steeperStill = walking.filterVelocity({19.5, 0.0, 32.0}, {3.0, 0.0, 0.0});
    REQUIRE(steeperStill.x == 0.0);
}

TEST_CASE("shallow water is waded at the wading pace and deep water needs swimming or a boat", "[walking]")
{
    const Lake shallow(9990); // 1 m under the rim: ankle to waist deep
    const ForgedWalking wading(shallow.field, shallow.water.get(), CapabilitySet{});
    const WalkAssessment waded = wading.assess(16.0, 16.0);
    REQUIRE(waded.walkable());
    REQUIRE(waded.waterDepth > 0.5f);
    REQUIRE(waded.wadeFactor < 1.0f);
    REQUIRE(waded.wadeFactor > 0.0f);
    REQUIRE(waded.wadeFactor == wadingSpeedFactor(waded.waterDepth));

    const Lake deep(9950); // 5 m
    const ForgedWalking onFoot(deep.field, deep.water.get(), CapabilitySet{});
    const WalkAssessment drowned = onFoot.assess(16.0, 16.0);
    REQUIRE(drowned.block == WalkBlock::DeepWater);
    REQUIRE(drowned.hazard > 0.0f);
    REQUIRE(onFoot.assess(2.0, 2.0).walkable());

    const ForgedWalking swimmer(deep.field, deep.water.get(), with(Capability::Swim));
    REQUIRE(swimmer.assess(16.0, 16.0).walkable());
    const ForgedWalking boater(deep.field, deep.water.get(), with(Capability::Boat));
    REQUIRE(boater.assess(16.0, 16.0).walkable());
    const ForgedWalking diver(deep.field, deep.water.get(), with(Capability::Dive));
    REQUIRE(diver.assess(16.0, 16.0).block == WalkBlock::DeepWater);
}

TEST_CASE("a walker does not enter deep water on foot, and says so when it stands at the shore", "[walking]")
{
    const Lake deep(9950);
    const ForgedWalking walking(deep.field, deep.water.get(), CapabilitySet{});
    const WalkResult result = walkTo(walking, {2.0, 0.0, 16.0}, {30.0, 0.0, 17.0}, {});
    REQUIRE(result.outcome == WalkOutcome::Blocked);
    REQUIRE(result.blocker == WalkBlock::DeepWater);
    REQUIRE(walking.assess(result.end.x, result.end.z).waterDepth < walking.params().deepWaterDepth);
    REQUIRE_FALSE(result.report.empty());

    const ForgedWalking swimmer(deep.field, deep.water.get(), with(Capability::Swim));
    REQUIRE(walkTo(swimmer, {2.0, 0.0, 16.0}, {30.0, 0.0, 17.0}, {}).outcome == WalkOutcome::Arrived);
}

TEST_CASE("the walk bot arrives across open ground and is blocked, with the reason, by a wall", "[walking]")
{
    const Heightfield open = wallOf(0);
    const ForgedWalking walkingOpen(open, nullptr, CapabilitySet{});
    const WalkResult arrived = walkTo(walkingOpen, {5.0, 0.0, 32.0}, {55.0, 0.0, 32.0}, {});
    REQUIRE(arrived.outcome == WalkOutcome::Arrived);
    REQUIRE(arrived.metresLeft <= 4.0);

    const Heightfield steep = wallOf(25);
    const ForgedWalking walkingSteep(steep, nullptr, CapabilitySet{});
    const WalkResult blocked = walkTo(walkingSteep, {5.0, 0.0, 32.0}, {55.0, 0.0, 32.0}, {});
    REQUIRE(blocked.outcome == WalkOutcome::Blocked);
    REQUIRE(blocked.blocker == WalkBlock::TooSteep);
    REQUIRE(blocked.end.x < 21.0);
    REQUIRE(blocked.report.find("too steep") != std::string::npos);

    // The wall is the only thing in the way: with the limit lifted the same walk arrives.
    WalkParams lifted;
    lifted.maxSlopeDegrees = 85.0f;
    const ForgedWalking walkingLifted(steep, nullptr, CapabilitySet{}, lifted);
    REQUIRE(walkTo(walkingLifted, {5.0, 0.0, 32.0}, {55.0, 0.0, 32.0}, {}).outcome == WalkOutcome::Arrived);
}

TEST_CASE("hostile input never produces a NaN velocity or a crash", "[walking]")
{
    const Heightfield field = wallOf(0);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    REQUIRE(walking.assess(nan, 5.0).block == WalkBlock::OffMap);
    REQUIRE(walking.assess(5.0, inf).block == WalkBlock::OffMap);
    REQUIRE(walking.assess(-1.0, 5.0).block == WalkBlock::OffMap);
    REQUIRE(walking.assess(5.0, 1.0e300).block == WalkBlock::OffMap);
    for (const glm::dvec3& bad : {glm::dvec3(nan, 0.0, 0.0), glm::dvec3(inf, 0.0, 0.0), glm::dvec3(0.0, 0.0, -inf)}) {
        const glm::dvec3 a = walking.filterVelocity(bad, {1.0, 0.0, 0.0});
        const glm::dvec3 b = walking.filterVelocity({5.0, 0.0, 5.0}, bad);
        REQUIRE((a.x == 0.0 && a.z == 0.0));
        REQUIRE((b.x == 0.0 && b.z == 0.0));
    }
    REQUIRE(walking.filterVelocity({5.0, 0.0, 5.0}, {0.0, 0.0, 0.0}) == glm::dvec3(0.0));

    WalkParams junk;
    junk.maxSlopeDegrees = std::numeric_limits<float>::quiet_NaN();
    junk.easeSlopeDegrees = -4.0f;
    junk.minSlopeFactor = std::numeric_limits<float>::infinity();
    junk.deepWaterDepth = std::numeric_limits<float>::quiet_NaN();
    junk.probeMetres = -1.0f;
    const ForgedWalking sane(field, nullptr, CapabilitySet{}, junk);
    REQUIRE(sane.params().maxSlopeDegrees == ClimbLimits{}.minSlopeDegrees);
    REQUIRE(sane.assess(5.0, 5.0).walkable());

    const WalkResult nothing = walkTo(walking, {nan, 0.0, 0.0}, {5.0, 0.0, 5.0}, {});
    REQUIRE(nothing.outcome == WalkOutcome::Blocked);
}

TEST_CASE("the world edge holds the walker in", "[walking]")
{
    const Heightfield field = wallOf(0);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const WalkResult result = walkTo(walking, {50.0, 0.0, 32.0}, {200.0, 0.0, 32.0}, {});
    REQUIRE(result.outcome == WalkOutcome::Blocked);
    REQUIRE(result.blocker == WalkBlock::OffMap);
    REQUIRE(result.end.x <= 64.0);
}

TEST_CASE("a spawn is found on dry walkable ground near the asked point", "[walking]")
{
    const Lake deep(9950);
    const ForgedWalking walking(deep.field, deep.water.get(), CapabilitySet{});
    glm::dvec3 spawn;
    REQUIRE(findWalkableSpawn(walking, 16.0, 16.0, 64.0, spawn));
    const WalkAssessment there = walking.assess(spawn.x, spawn.z);
    REQUIRE(there.walkable());
    REQUIRE(there.waterDepth == 0.0f);
    REQUIRE_FALSE(findWalkableSpawn(walking, 16.0, 16.0, 2.0, spawn)); // the lake fills the first rings
    REQUIRE_FALSE(findWalkableSpawn(walking, std::numeric_limits<double>::quiet_NaN(), 0.0, 64.0, spawn));
}

TEST_CASE("a walker that tries to move and does not is reported, with the cause and a place to recover to", "[walking][stuck]")
{
    StuckDetector detector;
    const WalkAssessment fine;
    glm::dvec3 position{0.0, 0.0, 0.0};
    const double dt = 0.1;
    for (int i = 0; i < 100; ++i) {
        position.x += 0.3;
        REQUIRE_FALSE(detector.step(position, true, fine, WalkBlock::None, dt).stuck);
    }
    StuckReport report;
    for (int i = 0; i < 80 && !report.stuck; ++i) {
        report = detector.step(position, true, fine, WalkBlock::TooSteep, dt);
    }
    REQUIRE(report.stuck);
    REQUIRE(report.cause == WalkBlock::TooSteep);
    REQUIRE(report.seconds >= 3.0);
    REQUIRE(report.canRecover);
    REQUIRE(report.recoverTo.x < position.x - 4.0);
    REQUIRE(describeStuck(report).find("too steep") != std::string::npos);
}

TEST_CASE("stuck with clear ground ahead is wedged, and standing still on purpose is never stuck", "[walking][stuck]")
{
    StuckDetector detector;
    const WalkAssessment fine;
    StuckReport report;
    for (int i = 0; i < 60 && !report.stuck; ++i) {
        report = detector.step({10.0, 0.0, 10.0}, true, fine, WalkBlock::None, 0.1);
    }
    REQUIRE(report.stuck);
    REQUIRE(report.cause == WalkBlock::Wedged);
    REQUIRE(report.recoverTo == report.where); // the only footing it knew is where it stands

    StuckDetector idle;
    for (int i = 0; i < 1000; ++i) {
        REQUIRE_FALSE(idle.step({10.0, 0.0, 10.0}, false, fine, WalkBlock::TooSteep, 0.1).stuck);
    }
}

TEST_CASE("a walker making slow but steady progress is not stuck", "[walking][stuck]")
{
    StuckDetector detector;
    const WalkAssessment fine;
    glm::dvec3 position{0.0};
    for (int i = 0; i < 1000; ++i) {
        position.x += 0.1 * 0.4; // 0.4 m/s, well past 0.75 m in the 3 s window
        REQUIRE_FALSE(detector.step(position, true, fine, WalkBlock::None, 0.1).stuck);
    }
}

TEST_CASE("the stuck detector survives hostile input", "[walking][stuck]")
{
    StuckParams junk;
    junk.windowSeconds = -1.0;
    junk.minProgressMetres = std::numeric_limits<double>::quiet_NaN();
    junk.safeSpacingMetres = 0.0;
    junk.safeHistory = 0;
    junk.recoverBack = 99;
    StuckDetector detector(junk);
    const WalkAssessment fine;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_FALSE(detector.step({nan, 0.0, 0.0}, true, fine, WalkBlock::None, 0.1).stuck);
    REQUIRE_FALSE(detector.step({0.0, 0.0, 0.0}, true, fine, WalkBlock::None, nan).stuck);
    REQUIRE_FALSE(detector.step({0.0, 0.0, 0.0}, true, fine, WalkBlock::None, -1.0).stuck);
    StuckReport report;
    for (int i = 0; i < 100 && !report.stuck; ++i) {
        report = detector.step({1.0, 0.0, 1.0}, true, fine, WalkBlock::None, 0.1);
    }
    REQUIRE(report.stuck);
    detector.reset({1.0, 0.0, 1.0});
}

TEST_CASE("the scene's velocity filter keeps the player off ground that cannot be walked", "[walking]")
{
    const Heightfield steep = wallOf(25);
    const ForgedWalking walking(steep, nullptr, CapabilitySet{});
    const auto runInto = [&](bool filtered) {
        Scene scene(3);
        SpawnCommand spawn;
        spawn.id = StableId::of("player");
        spawn.name.value = "player";
        spawn.player = Player{4.0f};
        spawn.transform.position = {10.0, 0.0, 32.0};
        scene.enqueue(spawn);
        if (filtered) {
            scene.setVelocityFilter([&](const glm::dvec3& p, const glm::dvec3& v) { return walking.filterVelocity(p, v); });
        }
        InputFrame east;
        east.moveX = 1.0f;
        for (int i = 0; i < 600; ++i) {
            scene.tick(1.0 / 60.0, east);
        }
        return scene.registry().get<Transform>(scene.find(StableId::of("player"))).position.x;
    };
    REQUIRE(runInto(false) > 30.0);
    REQUIRE(runInto(true) < 20.5);
}

TEST_CASE("strafing back and forth in place is not stuck, and the slowest legitimate pace clears the threshold", "[walking][stuck]")
{
    const WalkAssessment fine;
    StuckDetector strafing;
    glm::dvec3 position{10.0, 0.0, 10.0};
    for (int i = 0; i < 600; ++i) {
        position.x += (i / 10) % 2 == 0 ? 0.3 : -0.3; // 3 m/s, turning every second
        REQUIRE_FALSE(strafing.step(position, true, fine, WalkBlock::None, 0.1).stuck);
    }

    // Wading at its slowest on a steep rise: walking pace 3 m/s, wading 0.35, rise 0.3.
    const double slowest = 3.0 * 0.35 * 0.3;
    StuckDetector crawling;
    glm::dvec3 where{0.0};
    for (int i = 0; i < 1000; ++i) {
        where.x += slowest * 0.1;
        REQUIRE_FALSE(crawling.step(where, true, fine, WalkBlock::None, 0.1).stuck);
    }
}

TEST_CASE("a walker on a barely moving stick, or asking for a huge radius, is handled", "[walking]")
{
    const Heightfield field = wallOf(0);
    const ForgedWalking walking(field, nullptr, CapabilitySet{});
    const glm::dvec3 tiny = walking.filterVelocity({5.0, 0.0, 5.0}, {1.0e-300, 0.0, 0.0});
    REQUIRE(std::isfinite(tiny.x));
    REQUIRE(std::isfinite(tiny.z));
    glm::dvec3 spawn;
    REQUIRE(findWalkableSpawn(walking, 5.0, 5.0, 1.0e12, spawn));
    const Lake deep(9950);
    const ForgedWalking drowned(deep.field, deep.water.get(), CapabilitySet{});
    REQUIRE(findWalkableSpawn(drowned, 16.0, 16.0, 1.0e12, spawn));
}
