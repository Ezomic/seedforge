#include "ichor/world/forged_water_view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ichor {

namespace {

constexpr double kEdgeMarginM = 64.0;

bool dryBank(const ForgedGame& game, double x, double z)
{
    const double size = game.heightfield.desc().sizeMetres;
    if (x < kEdgeMarginM || z < kEdgeMarginM || x > size - kEdgeMarginM || z > size - kEdgeMarginM) {
        return false;
    }
    return !game.waterField->at(x, z).wet();
}

bool nearLake(const Hydrology& hydrology, std::uint32_t x, std::uint32_t z)
{
    constexpr std::int64_t kReach = 4;
    for (std::int64_t dz = static_cast<std::int64_t>(z) - kReach; dz <= static_cast<std::int64_t>(z) + kReach; ++dz) {
        for (std::int64_t dx = static_cast<std::int64_t>(x) - kReach; dx <= static_cast<std::int64_t>(x) + kReach; ++dx) {
            if (dx >= 0 && dz >= 0 && dx < hydrology.side && dz < hydrology.side && hydrology.lake[hydrology.index(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dz))] != 0) return true;
        }
    }
    return false;
}

void place(ForgedGame& game, double x, double z, double faceX, double faceZ)
{
    game.spawn.found = true;
    game.spawn.x = x;
    game.spawn.z = z;
    game.spawn.groundY = game.heightfield.heightAt(x, z);
    game.spawn.faceX = faceX;
    game.spawn.faceZ = faceZ;
}

bool viewRiver(ForgedGame& game)
{
    const Hydrology& hydrology = *game.water.hydrology;
    const double spacing = game.heightfield.desc().metresPerSample;
    const auto margin = static_cast<std::uint32_t>(kEdgeMarginM / spacing) + 1;
    std::vector<std::pair<std::uint32_t, std::size_t>> candidates;
    for (std::uint32_t z = margin; z + margin < hydrology.side; ++z) {
        for (std::uint32_t x = margin; x + margin < hydrology.side; ++x) {
            const std::size_t at = hydrology.index(x, z);
            if (hydrology.lake[at] != 0 || !isRiver(hydrology, at, game.water.rivers) || hydrology.isOutlet(at) || nearLake(hydrology, x, z)) {
                continue;
            }
            candidates.emplace_back(hydrology.accumulation[at], at);
        }
    }
    if (candidates.empty()) return false;
    std::sort(candidates.begin(), candidates.end());
    const std::size_t best = candidates[candidates.size() * 3 / 4].second;
    const double cx = static_cast<double>(best % hydrology.side) * spacing;
    const double cz = static_cast<double>(best / hydrology.side) * spacing;
    const std::size_t next = hydrology.downstream(best);
    double fx = static_cast<double>(next % hydrology.side) * spacing - cx;
    double fz = static_cast<double>(next / hydrology.side) * spacing - cz;
    const double length = std::hypot(fx, fz);
    if (length <= 0.0) return false;
    fx /= length;
    fz /= length;
    for (double distance = 14.0; distance <= 48.0; distance += 2.0) {
        for (const double side : {1.0, -1.0}) {
            const double x = cx - fz * side * distance;
            const double z = cz + fx * side * distance;
            if (dryBank(game, x, z) && dryBank(game, x - fz * side * 6.0, z + fx * side * 6.0)) {
                place(game, x, z, cx, cz);
                return true;
            }
        }
    }
    return false;
}

bool viewLake(ForgedGame& game)
{
    const Hydrology& hydrology = *game.water.hydrology;
    const double spacing = game.heightfield.desc().metresPerSample;
    const std::size_t lakes = hydrology.lakeDeepest.size();
    std::vector<double> area(lakes, 0.0);
    std::vector<double> sumX(lakes, 0.0);
    std::vector<double> sumZ(lakes, 0.0);
    for (std::uint32_t z = 0; z < hydrology.side; ++z) {
        for (std::uint32_t x = 0; x < hydrology.side; ++x) {
            const std::uint32_t id = hydrology.lake[hydrology.index(x, z)];
            if (id == 0 || id > lakes || hydrology.lakeDeepest[id - 1] < 2.0f || hydrology.filled[hydrology.index(x, z)] <= game.desc.waterLevel() + 2.0f) continue;
            area[id - 1] += 1.0;
            sumX[id - 1] += x * spacing;
            sumZ[id - 1] += z * spacing;
        }
    }
    std::vector<std::size_t> order(lakes);
    for (std::size_t i = 0; i < lakes; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return area[a] > area[b]; });
    for (const std::size_t lake : order) {
        if (area[lake] < 1.0) break;
        const double cx = sumX[lake] / area[lake];
        const double cz = sumZ[lake] / area[lake];
        double bestReach = 0.0;
        double bx = 0.0;
        double bz = 0.0;
        constexpr int kRays = 16;
        for (int ray = 0; ray < kRays; ++ray) {
            const double angle = 6.283185307179586 * ray / kRays;
            const double dx = std::cos(angle);
            const double dz = std::sin(angle);
            double reach = 0.0;
            while (reach < 600.0 && game.waterField->at(cx + dx * reach, cz + dz * reach).wet()) reach += 2.0;
            for (double extra = 4.0; extra <= 30.0 && reach < 600.0; extra += 2.0) {
                if (dryBank(game, cx + dx * (reach + extra), cz + dz * (reach + extra))) {
                    if (reach > bestReach) {
                        bestReach = reach;
                        bx = cx + dx * (reach + extra);
                        bz = cz + dz * (reach + extra);
                    }
                    break;
                }
            }
        }
        if (bestReach > 0.0) {
            place(game, bx, bz, cx, cz);
            return true;
        }
    }
    return false;
}

}

bool spawnAtWater(ForgedGame& game, WaterViewKind kind)
{
    if (!game.waterField || !game.water.ok() || !game.water.hydrology || game.water.hydrology->empty()) return false;
    return kind == WaterViewKind::River ? viewRiver(game) : viewLake(game);
}

}
