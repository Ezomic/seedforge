#include "ichor/stage0/terrain_harness.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace ichor::stage0 {

namespace {

constexpr float kStuckProgress = 0.25f; // moving less than this fraction of what was asked counts as blocked
constexpr float kMinAsk = 1e-4f;
constexpr float kMaxCoord = 1.0e5f;
constexpr double kHitDamage = 1.0;
constexpr int kResolveIterations = 4;
constexpr std::size_t kMaxObstacles = 256;
constexpr int kMaxRunTicks = 10 * kHarnessMaxTicks;

bool finite(glm::vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool sane(glm::vec2 v)
{
    return finite(v) && std::fabs(v.x) <= kMaxCoord && std::fabs(v.y) <= kMaxCoord;
}

bool validDescriptor(const TerrainDescriptor& t)
{
    if (!sane(t.boundsMin) || !sane(t.boundsMax) || t.boundsMin.x >= t.boundsMax.x || t.boundsMin.y >= t.boundsMax.y) return false;
    if (!sane(t.approach) || !sane(t.uphill)) return false;
    if (std::fabs(glm::length(t.approach) - 1.0f) > 1e-3f) return false;
    if (!std::isfinite(t.slopeDrag) || t.slopeDrag < 0.0f || t.slopeDrag >= 1.0f) return false;
    if (t.slopeDrag > 0.0f && std::fabs(glm::length(t.uphill) - 1.0f) > 1e-3f) return false;
    if (t.obstacles.size() > kMaxObstacles) return false;
    for (const TerrainDisc& d : t.obstacles) {
        if (!sane(d.centre) || !std::isfinite(d.radius) || d.radius <= 0.0f || d.radius > kMaxCoord) return false;
        if (d.centre.x - d.radius < t.boundsMin.x || d.centre.y - d.radius < t.boundsMin.y || d.centre.x + d.radius > t.boundsMax.x ||
            d.centre.y + d.radius > t.boundsMax.y)
            return false;
    }
    return true;
}

bool validProbe(const EnemyProbe& p)
{
    return p.make && std::isfinite(p.engageRange) && p.engageRange > 0.0f && p.engageRange <= kMaxCoord && std::isfinite(p.tickSeconds) &&
        p.tickSeconds > 0.0f && p.tickSeconds <= 1.0f;
}

bool segmentBlocked(glm::vec2 a, glm::vec2 b, const TerrainDisc& d)
{
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    float t = len2 > 0.0f ? glm::dot(d.centre - a, ab) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::length(a + ab * t - d.centre) < d.radius;
}

bool lineOfSight(glm::vec2 a, glm::vec2 b, const TerrainDescriptor& t)
{
    return std::none_of(t.obstacles.begin(), t.obstacles.end(), [&](const TerrainDisc& d) { return segmentBlocked(a, b, d); });
}

glm::vec2 resolve(glm::vec2 p, const TerrainDescriptor& t)
{
    for (int i = 0; i < kResolveIterations; ++i) {
        for (const TerrainDisc& d : t.obstacles) {
            const glm::vec2 off = p - d.centre;
            const float len = glm::length(off);
            if (len >= d.radius) continue;
            const glm::vec2 dir = len > 1e-6f ? off / len : glm::vec2(1.0f, 0.0f);
            p = d.centre + dir * d.radius;
        }
        p = glm::clamp(p, t.boundsMin, t.boundsMax);
    }
    return p;
}

float slopeScale(glm::vec2 move, const TerrainDescriptor& t)
{
    if (t.slopeDrag <= 0.0f) return 1.0f;
    const float len = glm::length(move);
    if (len <= 0.0f) return 1.0f;
    return 1.0f - t.slopeDrag * std::max(0.0f, glm::dot(move / len, t.uphill));
}

}

const char* name(TerrainCondition condition)
{
    switch (condition) {
    case TerrainCondition::Flat: return "flat";
    case TerrainCondition::Slope: return "slope";
    case TerrainCondition::Obstacle: return "obstacle";
    case TerrainCondition::Narrow: return "narrow";
    case TerrainCondition::Open: return "open";
    }
    return "unknown";
}

std::array<TerrainDescriptor, kTerrainConditionCount> stockTerrain()
{
    std::array<TerrainDescriptor, kTerrainConditionCount> out;

    out[0].condition = TerrainCondition::Flat;

    out[1].condition = TerrainCondition::Slope;
    out[1].uphill = {1.0f, 0.0f};
    out[1].slopeDrag = 0.5f;

    // Pillars flank the path with clearance: the behaviours steer straight at the target and do no pathing,
    // so a pillar on the line would fail every one of them and say nothing about the behaviour.
    out[2].condition = TerrainCondition::Obstacle;
    for (const float x : {2.0f, 7.0f, 12.0f, 17.0f}) {
        out[2].obstacles.push_back({{x, 1.2f}, 0.8f});
        out[2].obstacles.push_back({{x, -1.2f}, 0.8f});
    }

    out[3].condition = TerrainCondition::Narrow;
    out[3].boundsMin = {-40.0f, -1.5f};
    out[3].boundsMax = {40.0f, 1.5f};

    out[4].condition = TerrainCondition::Open;
    out[4].boundsMin = {-200.0f, -200.0f};
    out[4].boundsMax = {200.0f, 200.0f};
    out[4].approach = {0.6f, 0.8f};

    return out;
}

EnemyProbe ambusherProbe(const AmbusherTuning& tuning)
{
    EnemyProbe probe;
    probe.name = "ambusher";
    probe.engageRange = tuning.triggerRange * 0.85f;
    probe.tickSeconds = tuning.tickSeconds;
    probe.make = [tuning]() -> ProbeStep {
        auto brain = std::make_shared<AmbusherBrain>();
        return [brain, tuning](const ProbeSenses& in) {
            AmbusherSenses senses;
            senses.self = in.self;
            senses.cover = {0.0f, 0.0f};
            senses.target = in.target;
            senses.targetFacing = senses.cover - in.target;
            const AmbusherIntent intent = ambusherStep(*brain, senses, tuning);
            ProbeIntent out;
            out.move = intent.move;
            out.attack = intent.strike;
            out.hit = intent.strike && glm::length(in.target - in.self) <= tuning.strikeRange;
            return out;
        };
    };
    return probe;
}

EnemyProbe artilleryProbe(const ArtilleryTuning& tuning)
{
    EnemyProbe probe;
    probe.name = "artillery";
    probe.engageRange = tuning.maxRange + 6.0f; // inside aggro, outside the firing band: it has to advance
    probe.tickSeconds = tuning.tickSeconds;
    probe.make = [tuning]() -> ProbeStep {
        auto brain = std::make_shared<ArtilleryBrain>();
        return [brain, tuning](const ProbeSenses& in) {
            ArtillerySenses senses;
            senses.self = in.self;
            senses.target = in.target;
            senses.lineOfSight = in.lineOfSight;
            const ArtilleryIntent intent = artilleryStep(*brain, senses, tuning);
            ProbeIntent out;
            out.move = intent.move;
            out.attack = intent.fire;
            out.hit = intent.fire && intent.hits;
            return out;
        };
    };
    return probe;
}

ConditionResult runCondition(const EnemyProbe& probe, const TerrainDescriptor& terrain, int maxTicks)
{
    ConditionResult result;
    result.condition = terrain.condition;
    if (!validDescriptor(terrain) || !validProbe(probe) || maxTicks <= 0 || maxTicks > kMaxRunTicks) {
        result.invalidInput = true;
        return result;
    }

    const glm::vec2 target = terrain.approach * probe.engageRange;
    if (!sane(target) || resolve(target, terrain) != target || resolve(glm::vec2(0.0f), terrain) != glm::vec2(0.0f)) {
        result.invalidInput = true; // spawn or target would sit inside a wall or obstacle
        return result;
    }

    ProbeStep step = probe.make();
    if (!step) {
        result.invalidInput = true;
        return result;
    }

    FightRecordBuffer buffer;
    glm::vec2 self{0.0f};
    int run = 0;
    double now = 0.0;

    for (int tick = 0; tick < maxTicks; ++tick) {
        now = static_cast<double>(tick) * probe.tickSeconds;
        const ProbeSenses senses{self, target, lineOfSight(self, target, terrain)};
        const ProbeIntent intent = step(senses);
        result.ticks = tick + 1;

        if (!finite(intent.move)) {
            result.finite = false;
            break;
        }

        const glm::vec2 ask = intent.move * probe.tickSeconds * slopeScale(intent.move, terrain);
        const glm::vec2 next = resolve(self + ask, terrain);
        const float asked = glm::length(ask);
        const float moved = glm::length(next - self);
        if (asked > kMinAsk && moved < asked * kStuckProgress) {
            ++result.blockedTicks;
            result.longestBlockedRun = std::max(result.longestBlockedRun, ++run);
        } else if (asked > kMinAsk) {
            run = 0;
        }
        self = next;

        if (intent.attack) buffer.noteEvent(FightEventKind::Attack, FightSide::Foe, now);
        if (intent.attack && intent.hit) {
            buffer.noteEvent(FightEventKind::Hit, FightSide::Foe, now, kHitDamage);
            result.hitLanded = true;
            result.ticksToHit = tick + 1;
            break;
        }
    }

    if (buffer.end(result.hitLanded ? FightSide::Player : FightSide::Foe, DeathCause::Unknown, now) && !buffer.fights().empty()) {
        result.record = buffer.fights().back();
        result.summary = summarize(*result.record);
    }
    result.stuck = result.longestBlockedRun > kHarnessMaxBlockedTicks;
    result.passed = result.finite && result.hitLanded && !result.stuck;
    return result;
}

HarnessReport validateEnemy(const EnemyProbe& probe, const std::vector<TerrainDescriptor>& terrain, int maxTicks)
{
    HarnessReport report;
    report.probe = probe.name;
    for (const TerrainDescriptor& t : terrain) report.results.push_back(runCondition(probe, t, maxTicks));
    report.passed = !report.results.empty() &&
        std::all_of(report.results.begin(), report.results.end(), [](const ConditionResult& r) { return r.passed; });
    return report;
}

HarnessReport validateEnemy(const EnemyProbe& probe, int maxTicks)
{
    const auto stock = stockTerrain();
    return validateEnemy(probe, std::vector<TerrainDescriptor>(stock.begin(), stock.end()), maxTicks);
}

std::string formatHarnessReport(const HarnessReport& report)
{
    std::string out = "enemy " + report.probe + "\n";
    char line[160];
    for (const ConditionResult& r : report.results) {
        const char* why = r.invalidInput ? "invalid input" : !r.finite ? "non-finite move" : r.stuck ? "stuck" : !r.hitLanded ? "no hit" : "";
        std::snprintf(line, sizeof line, "  %-8s %s  hit at tick %d, blocked run %d%s%s\n", name(r.condition), r.passed ? "PASS" : "FAIL",
            r.ticksToHit, r.longestBlockedRun, *why ? ", " : "", why);
        out += line;
    }
    out += report.passed ? "verdict: pass\n" : "verdict: FAIL\n";
    return out;
}

}
