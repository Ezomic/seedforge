#include "ichor/combat_bridge/terrain_trials.h"

#include "ichor/sim/scene.h"
#include "ichor/world/forged_walking.h"
#include "ichor/world/vegetation.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace ichor {

namespace {

constexpr double kEdgeMarginM = 40.0;
constexpr double kScanStepM = 8.0;
constexpr double kLineStepM = 2.0;
constexpr double kPatchStepM = 6.0;
constexpr double kPatchHalfM = 12.0;
constexpr double kWallOffsetM = 5.0;

constexpr float kLevelUpY = 0.97f;       // about 14 degrees
constexpr float kGentleUpY = 0.90f;      // about 26 degrees
constexpr float kSteepMinUpY = 0.72f;    // about 44 degrees: the top of what is walked
constexpr float kWallUpY = 0.80f;        // about 37 degrees: ground that is hostile to cross
constexpr float kClearingPatch = 0.25f;
constexpr float kForestPatch = 0.70f;
constexpr float kShallowMinM = 0.25f;
constexpr float kShallowMaxM = 1.0f;
constexpr double kMinSlopeRise = 0.25; // metres of rise per metre of line on a steep slope

// cos and sin of 0 to 180 degrees in 15 degree steps, as literals: trial positions are spawned into the simulation, so
// they are worked out with basic arithmetic and square roots only.
constexpr double kCos[13] = {1.0, 0.9659258262890683, 0.8660254037844387, 0.7071067811865476, 0.5, 0.25881904510252074, 0.0,
    -0.25881904510252074, -0.5, -0.7071067811865476, -0.8660254037844387, -0.9659258262890683, -1.0};
constexpr double kSin[13] = {0.0, 0.25881904510252074, 0.5, 0.7071067811865476, 0.8660254037844387, 0.9659258262890683, 1.0,
    0.9659258262890683, 0.8660254037844387, 0.7071067811865476, 0.5, 0.25881904510252074, 0.0};
constexpr int kDirections = 24;

glm::dvec2 direction(int index)
{
    return index <= 12 ? glm::dvec2(kCos[index], kSin[index]) : glm::dvec2(kCos[kDirections - index], -kSin[kDirections - index]);
}

struct Ground {
    const ForgedGame& game;
    ForgedWalking walking;
    float patchScale = VegetationParams{}.forestScale;

    explicit Ground(const ForgedGame& g)
        : game(g)
        , walking(g.heightfield, g.waterField.get(), CapabilitySet{})
    {
    }

    float depth(double x, double z) const { return game.waterField ? game.waterField->at(x, z).depth : 0.0f; }
    float upY(double x, double z) const { return game.heightfield.normalAt(x, z).y; }
    float height(double x, double z) const { return game.heightfield.heightAt(x, z); }
    bool walkable(double x, double z) const { return walking.assess(x, z).walkable(); }
    bool dry(double x, double z) const { return depth(x, z) <= 0.0f; }
    float forest(double x, double z) const { return forestPatch(x, z, patchScale); }
};

template <class Test>
bool allAround(glm::dvec2 centre, const Test& test)
{
    for (double dz = -kPatchHalfM; dz <= kPatchHalfM; dz += kPatchStepM) {
        for (double dx = -kPatchHalfM; dx <= kPatchHalfM; dx += kPatchStepM) {
            if (!test(centre.x + dx, centre.y + dz)) return false;
        }
    }
    return true;
}

template <class Test>
bool alongLine(glm::dvec2 from, glm::dvec2 to, double step, const Test& test)
{
    const double length = glm::length(to - from);
    const int count = std::max(1, static_cast<int>(length / step));
    for (int i = 0; i <= count; ++i) {
        const glm::dvec2 at = from + (to - from) * (static_cast<double>(i) / count);
        if (!test(at)) return false;
    }
    return true;
}

bool fits(const Ground& ground, glm::dvec2 at)
{
    const double size = ground.game.heightfield.desc().sizeMetres;
    return at.x >= kEdgeMarginM && at.y >= kEdgeMarginM && at.x <= size - kEdgeMarginM && at.y <= size - kEdgeMarginM;
}

bool matches(const Ground& g, FieldCondition condition, bool foeHigher, glm::dvec2 foe, glm::dvec2 player)
{
    const auto standable = [&](glm::dvec2 at) { return g.walkable(at.x, at.y); };
    switch (condition) {
    case FieldCondition::OpenField: {
        const auto level = [&](double x, double z) { return g.dry(x, z) && g.upY(x, z) >= kLevelUpY && g.forest(x, z) < kClearingPatch; };
        return allAround(foe, level) && allAround(player, level);
    }
    case FieldCondition::DenseForest: {
        const auto thick = [&](double x, double z) { return g.dry(x, z) && g.upY(x, z) >= kGentleUpY && g.forest(x, z) >= kForestPatch; };
        return allAround(foe, thick) && allAround(player, thick);
    }
    case FieldCondition::NarrowDefile: {
        const glm::dvec2 along = glm::normalize(player - foe);
        const glm::dvec2 across(-along.y, along.x);
        return alongLine(foe, player, kLineStepM, [&](glm::dvec2 at) {
            if (!g.walkable(at.x, at.y) || !g.dry(at.x, at.y) || g.upY(at.x, at.y) < kGentleUpY) return false;
            const glm::dvec2 left = at + across * kWallOffsetM;
            const glm::dvec2 right = at - across * kWallOffsetM;
            return g.upY(left.x, left.y) < kWallUpY && g.upY(right.x, right.y) < kWallUpY;
        });
    }
    case FieldCondition::SteepSlope: {
        const double rise = static_cast<double>(g.height(player.x, player.y)) - static_cast<double>(g.height(foe.x, foe.y));
        const double wanted = kMinSlopeRise * glm::length(player - foe);
        if (foeHigher ? rise > -wanted : rise < wanted) return false;
        return alongLine(foe, player, kLineStepM, [&](glm::dvec2 at) {
            const float up = g.upY(at.x, at.y);
            return g.walkable(at.x, at.y) && g.dry(at.x, at.y) && up >= kSteepMinUpY && up <= kGentleUpY;
        });
    }
    case FieldCondition::ShallowWater: {
        const glm::dvec2 bank = foeHigher ? foe : player;
        const glm::dvec2 wading = foeHigher ? player : foe;
        if (!g.dry(bank.x, bank.y)) return false;
        const float depth = g.depth(wading.x, wading.y);
        if (depth < kShallowMinM || depth > kShallowMaxM) return false;
        bool sawWater = false;
        const bool walkable = alongLine(foe, player, 1.0, [&](glm::dvec2 at) {
            const float d = g.depth(at.x, at.y);
            sawWater = sawWater || d > 0.0f;
            return d <= kShallowMaxM && standable(at);
        });
        return walkable && sawWater;
    }
    }
    return false;
}

}

const char* name(FieldCondition condition)
{
    switch (condition) {
    case FieldCondition::OpenField: return "open field";
    case FieldCondition::DenseForest: return "dense forest";
    case FieldCondition::NarrowDefile: return "narrow defile";
    case FieldCondition::SteepSlope: return "steep slope";
    case FieldCondition::ShallowWater: return "shallow water and bank";
    }
    return "unknown";
}

std::optional<TerrainTrial> findTrial(const ForgedGame& game, FieldCondition condition, bool foeHigher, const TrialSearch& search)
{
    if (game.heightfield.empty() || !std::isfinite(search.separationM) || search.separationM < 2.0 || search.separationM > 200.0) {
        return std::nullopt;
    }
    const Ground ground(game);
    const double size = game.heightfield.desc().sizeMetres;
    for (double z = kEdgeMarginM; z <= size - kEdgeMarginM; z += kScanStepM) {
        for (double x = kEdgeMarginM; x <= size - kEdgeMarginM; x += kScanStepM) {
            const glm::dvec2 foe(x, z);
            if (!ground.walkable(x, z)) continue;
            for (int d = 0; d < kDirections; ++d) {
                const glm::dvec2 player = foe + direction(d) * search.separationM;
                if (!fits(ground, player) || !ground.walkable(player.x, player.y) || !matches(ground, condition, foeHigher, foe, player)) continue;
                TerrainTrial trial;
                trial.condition = condition;
                trial.foeHigher = foeHigher;
                trial.foe = {foe.x, static_cast<double>(ground.height(foe.x, foe.y)), foe.y};
                trial.player = {player.x, static_cast<double>(ground.height(player.x, player.y)), player.y};
                return trial;
            }
        }
    }
    return std::nullopt;
}

void installFieldRules(Scene& scene, const ForgedGame& game)
{
    if (game.heightfield.empty()) return;
    auto walking = std::make_shared<ForgedWalking>(game.heightfield, game.waterField.get(), CapabilitySet{});
    const ForgedGame* world = &game;
    if (game.waterField) {
        scene.setSpeedModifier([world](const glm::dvec3& at) { return static_cast<double>(wadingSpeedFactor(world->waterField->at(at.x, at.z).depth)); });
    }
    scene.setVelocityFilter([walking](const glm::dvec3& at, const glm::dvec3& velocity) { return walking->filterVelocity(at, velocity); });
}

}
