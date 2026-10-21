#include "ichor/combat_bridge/combat_site.h"

#include "ichor/world/placed_settlements.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace ichor {

namespace {

constexpr double kClearM = 4.0;     // from a building, as spawnAtSettlement keeps
constexpr float kMinUpY = 0.85f;    // surface normal y of ground to fight on (about 32 degrees)
constexpr float kMinAboveSeaM = 0.5f;
constexpr double kEdgeMarginM = 32.0;
constexpr double kEdgeSlackM = 40.0; // the settlement's own spawn point can lie this far past its nominal radius
constexpr double kDuelClearM = 6.0; // the duel start keeps this far from the enemy
// cos and sin of 0 to 180 degrees in 15 degree steps, as literals: the site is a position the simulation hashes, so
// it is worked out with basic arithmetic and square roots only, never libm trigonometry.
constexpr double kCos[13] = {1.0, 0.9659258262890683, 0.8660254037844387, 0.7071067811865476, 0.5, 0.25881904510252074, 0.0,
    -0.25881904510252074, -0.5, -0.7071067811865476, -0.8660254037844387, -0.9659258262890683, -1.0};
constexpr double kSin[13] = {0.0, 0.25881904510252074, 0.5, 0.7071067811865476, 0.8660254037844387, 0.9659258262890683, 1.0,
    0.9659258262890683, 0.8660254037844387, 0.7071067811865476, 0.5, 0.25881904510252074, 0.0};

double distance(double dx, double dz)
{
    return std::sqrt(dx * dx + dz * dz);
}

double radiusOf(const SettlementSite& site)
{
    const PlacedSettlementParams params;
    return params.tiers[std::min<std::size_t>(site.tier, kSettlementTierCount - 1)].radiusM;
}

// Dry, gentle ground clear of the settlement's buildings, away from the world's edge.
bool standable(const ForgedGame& game, const std::vector<PlacedBuilding>& buildings, double x, double z)
{
    const double size = game.heightfield.desc().sizeMetres;
    if (!(x >= kEdgeMarginM && z >= kEdgeMarginM && x <= size - kEdgeMarginM && z <= size - kEdgeMarginM)) {
        return false;
    }
    if (game.desc.hasWater() && !(game.heightfield.heightAt(x, z) >= game.desc.waterLevel() + kMinAboveSeaM)) {
        return false;
    }
    if (!(game.heightfield.normalAt(x, z).y >= kMinUpY) || (game.waterField && game.waterField->at(x, z).wet())) {
        return false;
    }
    for (const PlacedBuilding& b : buildings) {
        if (std::abs(x - b.x) < b.halfWidthM + kClearM && std::abs(z - b.z) < b.halfDepthM + kClearM) return false;
    }
    return true;
}

// The first standable spot on rings of the given radii around `centre`: the `first` direction, then swinging out
// either side of it in 15 degree steps. The order is fixed, so the answer is too.
bool searchAround(const ForgedGame& game, const std::vector<PlacedBuilding>& buildings, glm::dvec2 centre, glm::dvec2 first, const std::vector<double>& radii,
    glm::dvec2& found, glm::dvec2 avoid = {0.0, 0.0}, double avoidM = 0.0)
{
    const double length = distance(first.x, first.y);
    if (!(length > 1e-9)) {
        first = glm::dvec2(1.0, 0.0);
    } else {
        first /= length;
    }
    for (const double radius : radii) {
        for (int step = 0; step <= 12; ++step) {
            for (const int sign : {1, -1}) {
                if (step == 0 && sign == -1) continue;
                const double c = kCos[step];
                const double sn = sign * kSin[step];
                const glm::dvec2 direction(first.x * c - first.y * sn, first.x * sn + first.y * c);
                const glm::dvec2 at = centre + direction * radius;
                if (avoidM > 0.0 && distance(at.x - avoid.x, at.y - avoid.y) < avoidM) continue;
                if (standable(game, buildings, at.x, at.y)) {
                    found = at;
                    return true;
                }
            }
        }
    }
    return false;
}

}

bool settlementAt(const ForgedGame& game, double x, double z, std::size_t& index)
{
    double best = std::numeric_limits<double>::max();
    bool inside = false;
    for (std::size_t i = 0; i < game.sites.size(); ++i) {
        const SettlementSite& site = game.sites[i];
        const double distance = distance(x - site.x, z - site.z);
        if (distance <= radiusOf(site) + kEdgeSlackM && distance < best) {
            best = distance;
            index = i;
            inside = true;
        }
    }
    return inside;
}

glm::dvec3 respawnPoint(const ForgedGame& game, std::size_t index)
{
    const ForgedSpawn spawn = settlementSpawn(game, index);
    if (!spawn.found) {
        const SettlementSite& site = game.sites[index];
        return {site.x, static_cast<double>(game.heightfield.heightAt(site.x, site.z)), site.z};
    }
    return {spawn.x, static_cast<double>(spawn.groundY), spawn.z};
}

CombatSite findCombatSite(const ForgedGame& game)
{
    CombatSite site;
    if (game.sites.empty() || game.heightfield.empty()) {
        return site;
    }
    // The settlement nearest the spawn first; a settlement ringed by water or cliffs hands over to the next nearest.
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t i = 0; i < game.sites.size(); ++i) {
        order.emplace_back(distance(game.sites[i].x - game.spawn.x, game.sites[i].z - game.spawn.z), i);
    }
    std::sort(order.begin(), order.end());
    for (const auto& [distance, index] : order) {
        const SettlementSite& settlement = game.sites[index];
        const glm::dvec2 centre(settlement.x, settlement.z);
        const std::vector<PlacedBuilding> buildings = game.buildings ? game.buildings->ofSettlement(settlement.id) : std::vector<PlacedBuilding>{};

        // The enemy stands on the side of the settlement that faces away from the world's middle.
        glm::dvec2 outward = centre - glm::dvec2(game.heightfield.desc().sizeMetres * 0.5);
        outward = glm::length(outward) < 1.0 ? glm::dvec2(1.0, 0.0) : glm::normalize(outward);
        const double edge = radiusOf(settlement);
        glm::dvec2 post;
        glm::dvec2 duel;
        if (!searchAround(game, buildings, centre, outward, {edge + 10.0, edge + 18.0, edge + 28.0, edge + 40.0, edge + 60.0}, post)
            || !searchAround(game, buildings, post, centre - post, {9.0, 8.0, 10.0, 7.0, 11.0}, duel, post, kDuelClearM)) {
            continue;
        }
        site.found = true;
        site.settlement = index;
        site.enemyPost = {post.x, static_cast<double>(game.heightfield.heightAt(post.x, post.y)), post.y};
        site.duelStart = {duel.x, static_cast<double>(game.heightfield.heightAt(duel.x, duel.y)), duel.y};
        return site;
    }
    return site;
}

}
