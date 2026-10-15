#include "ichor/factions/territory.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <utility>

namespace ichor {

namespace {

constexpr std::size_t kMaxHomes = 256;
constexpr std::size_t kMaxId = 64;
constexpr std::uint64_t kUnreached = std::numeric_limits<std::uint64_t>::max();
// Keeps reach * kMaxClaimStrength inside 64 bits whatever the caller passes.
constexpr std::uint64_t kReachCap = 1ull << 40;

struct StrategyName {
    ClaimStrategy strategy;
    std::string_view name;
};

constexpr std::array<StrategyName, 6> kNames = {{
    {ClaimStrategy::ResourceFollowing, "resource_following"},
    {ClaimStrategy::ContiguousDecay, "contiguous_decay"},
    {ClaimStrategy::NodesOnly, "nodes_only"},
    {ClaimStrategy::DefensiveIsolated, "defensive_isolated"},
    {ClaimStrategy::RoutesNotTerritory, "routes_not_territory"},
    {ClaimStrategy::ExpandingOverTime, "expanding_over_time"},
}};

void report(std::vector<std::string>& problems, const std::string& what)
{
    problems.push_back("territory: " + what);
}

bool validateGrid(const TerritoryGrid& grid, std::vector<std::string>& problems)
{
    const std::uint64_t cells = std::uint64_t{grid.width} * grid.height;
    if (grid.width == 0 || grid.height == 0 || cells > kMaxTerritoryCells) {
        report(problems, "grid " + std::to_string(grid.width) + "x" + std::to_string(grid.height) + " must be non-empty and at most " + std::to_string(kMaxTerritoryCells) + " cells");
        return false;
    }
    bool ok = true;
    const auto layer = [&](std::size_t size, const char* name) {
        if (size != cells) {
            report(problems, std::string("grid layer ") + name + " has " + std::to_string(size) + " entries, expected " + std::to_string(cells));
            ok = false;
        }
    };
    layer(grid.cost.size(), "cost");
    layer(grid.resource.size(), "resource");
    layer(grid.node.size(), "node");
    return ok;
}

bool validateSpecs(const TerritoryGrid& grid, const std::vector<ClaimSpec>& specs, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (specs.size() > kMaxTerritoryFactions) {
        report(problems, "at most " + std::to_string(kMaxTerritoryFactions) + " factions, got " + std::to_string(specs.size()));
        return false;
    }
    const std::uint64_t cells = std::uint64_t{grid.width} * grid.height;
    std::set<std::string> ids;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        const ClaimSpec& spec = specs[i];
        const std::string where = "faction [" + std::to_string(i) + "]: ";
        if (spec.id.empty() || spec.id.size() > kMaxId) {
            report(problems, where + "id must be 1 to " + std::to_string(kMaxId) + " characters");
        } else if (!ids.insert(spec.id).second) {
            report(problems, where + "duplicate faction id");
        }
        if (claimStrategyName(spec.strategy).empty()) {
            report(problems, where + "unknown claim strategy");
        }
        if (spec.homes.empty() || spec.homes.size() > kMaxHomes) {
            report(problems, where + "needs 1 to " + std::to_string(kMaxHomes) + " home cells, got " + std::to_string(spec.homes.size()));
            continue;
        }
        std::set<std::uint32_t> homes;
        for (std::uint32_t home : spec.homes) {
            if (home >= cells) {
                report(problems, where + "home cell " + std::to_string(home) + " is outside the grid");
            } else if (!homes.insert(home).second) {
                report(problems, where + "duplicate home cell " + std::to_string(home));
            }
        }
    }
    return problems.size() == before;
}

// Manhattan distance from the nearest seed, ignoring terrain.
std::vector<std::uint32_t> manhattanFrom(const TerritoryGrid& grid, const std::vector<std::uint32_t>& seeds)
{
    constexpr std::uint32_t kFar = std::numeric_limits<std::uint32_t>::max() / 2;
    const std::uint32_t w = grid.width;
    const std::uint32_t h = grid.height;
    std::vector<std::uint32_t> d(std::size_t{w} * h, kFar);
    for (std::uint32_t s : seeds) {
        d[s] = 0;
    }
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t i = std::size_t{y} * w + x;
            if (x > 0) {
                d[i] = std::min(d[i], d[i - 1] + 1);
            }
            if (y > 0) {
                d[i] = std::min(d[i], d[i - w] + 1);
            }
        }
    }
    for (std::uint32_t y = h; y-- > 0;) {
        for (std::uint32_t x = w; x-- > 0;) {
            const std::size_t i = std::size_t{y} * w + x;
            if (x + 1 < w) {
                d[i] = std::min(d[i], d[i + 1] + 1);
            }
            if (y + 1 < h) {
                d[i] = std::min(d[i], d[i + w] + 1);
            }
        }
    }
    return d;
}

// Cheapest cost to each cell below `reach`, kUnreached elsewhere. Homes are
// always at distance 0. Equal costs pop in cell order, so the walk is fixed.
std::vector<std::uint64_t> flood(const TerritoryGrid& grid, const std::vector<std::uint32_t>& homes, std::uint64_t reach, const std::vector<char>* forbidden)
{
    const std::uint32_t w = grid.width;
    const std::uint32_t h = grid.height;
    std::vector<std::uint64_t> dist(std::size_t{w} * h, kUnreached);
    using Entry = std::pair<std::uint64_t, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    for (std::uint32_t home : homes) {
        dist[home] = 0;
        open.emplace(0, home);
    }
    while (!open.empty()) {
        const auto [d, i] = open.top();
        open.pop();
        if (d != dist[i]) {
            continue;
        }
        const std::uint32_t x = i % w;
        const std::uint32_t y = i / w;
        std::array<std::uint32_t, 4> next{};
        std::size_t count = 0;
        if (y > 0) {
            next[count++] = i - w;
        }
        if (x > 0) {
            next[count++] = i - 1;
        }
        if (x + 1 < w) {
            next[count++] = i + 1;
        }
        if (y + 1 < h) {
            next[count++] = i + w;
        }
        for (std::size_t n = 0; n < count; ++n) {
            const std::uint32_t j = next[n];
            const std::uint16_t step = grid.cost[j];
            if (step == 0 || (forbidden != nullptr && (*forbidden)[j] != 0)) {
                continue;
            }
            const std::uint64_t nd = d + step;
            if (nd >= reach || nd >= dist[j]) {
                continue;
            }
            dist[j] = nd;
            open.emplace(nd, j);
        }
    }
    return dist;
}

std::uint16_t decay(std::uint64_t dist, std::uint64_t reach)
{
    if (reach == 0 || dist >= reach) {
        return dist == 0 ? static_cast<std::uint16_t>(kMaxClaimStrength) : 0;
    }
    const std::uint64_t value = kMaxClaimStrength * (reach - dist) / reach;
    return static_cast<std::uint16_t>(std::max<std::uint64_t>(value, 1));
}

std::uint64_t effectiveReach(const ClaimSpec& spec, std::uint32_t time)
{
    std::uint64_t reach = spec.reach;
    if (spec.strategy == ClaimStrategy::ExpandingOverTime) {
        reach += std::uint64_t{spec.growthPerTime} * time;
    }
    return std::min(reach, kReachCap);
}

std::vector<std::uint16_t> claimOne(const TerritoryGrid& grid, const ClaimSpec& spec, std::uint32_t time, const std::vector<char>* forbidden)
{
    const std::size_t cells = std::size_t{grid.width} * grid.height;
    std::vector<std::uint16_t> strength(cells, 0);
    if (spec.strategy == ClaimStrategy::RoutesNotTerritory) {
        return strength;
    }

    const std::uint64_t reach = effectiveReach(spec, time);
    const std::vector<std::uint64_t> dist = flood(grid, spec.homes, reach, forbidden);
    for (std::size_t i = 0; i < cells; ++i) {
        if (dist[i] == kUnreached) {
            continue;
        }
        switch (spec.strategy) {
        case ClaimStrategy::ResourceFollowing: {
            const std::uint8_t resource = grid.resource[i];
            if (resource > 0 && resource >= spec.minResource) {
                strength[i] = static_cast<std::uint16_t>(std::max<std::uint32_t>(resource * kMaxClaimStrength / 255, 1));
            }
            break;
        }
        case ClaimStrategy::NodesOnly:
            if (grid.node[i] != 0 || dist[i] == 0) {
                strength[i] = decay(dist[i], reach);
            }
            break;
        case ClaimStrategy::ContiguousDecay:
        case ClaimStrategy::DefensiveIsolated:
        case ClaimStrategy::ExpandingOverTime:
            strength[i] = decay(dist[i], reach);
            break;
        case ClaimStrategy::RoutesNotTerritory:
            break;
        }
    }
    return strength;
}

std::vector<char> avoidMask(const TerritoryGrid& grid, const std::vector<ClaimSpec>& specs, std::size_t self)
{
    std::vector<std::uint32_t> others;
    for (std::size_t j = 0; j < specs.size(); ++j) {
        if (j != self) {
            others.insert(others.end(), specs[j].homes.begin(), specs[j].homes.end());
        }
    }
    std::vector<char> mask(std::size_t{grid.width} * grid.height, 0);
    if (others.empty()) {
        return mask;
    }
    const std::vector<std::uint32_t> d = manhattanFrom(grid, others);
    for (std::size_t i = 0; i < mask.size(); ++i) {
        mask[i] = d[i] <= specs[self].avoidRadius ? 1 : 0;
    }
    return mask;
}

}

std::string_view claimStrategyName(ClaimStrategy strategy)
{
    for (const StrategyName& entry : kNames) {
        if (entry.strategy == strategy) {
            return entry.name;
        }
    }
    return {};
}

std::optional<ClaimStrategy> claimStrategyFromName(std::string_view name)
{
    for (const StrategyName& entry : kNames) {
        if (entry.name == name) {
            return entry.strategy;
        }
    }
    return std::nullopt;
}

bool claimTerritory(const TerritoryGrid& grid, const std::vector<ClaimSpec>& specs, std::uint32_t time, TerritoryResult& out, std::vector<std::string>& problems)
{
    out = {};
    if (!validateGrid(grid, problems) || !validateSpecs(grid, specs, problems)) {
        return false;
    }
    TerritoryResult result;
    for (std::size_t i = 0; i < specs.size(); ++i) {
        std::vector<char> mask;
        const bool isolated = specs[i].strategy == ClaimStrategy::DefensiveIsolated;
        if (isolated) {
            mask = avoidMask(grid, specs, i);
        }
        result.ids.push_back(specs[i].id);
        result.strength.push_back(claimOne(grid, specs[i], time, isolated ? &mask : nullptr));
    }
    out = std::move(result);
    return true;
}

std::uint64_t territoryDigest(const TerritoryResult& result)
{
    std::uint64_t h = hash64("territory");
    for (std::size_t i = 0; i < result.strength.size(); ++i) {
        if (i < result.ids.size()) {
            h = hash64(result.ids[i], h);
        }
        for (std::uint16_t v : result.strength[i]) {
            const std::array<char, 2> bytes = {static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)};
            h = hash64(std::string_view(bytes.data(), bytes.size()), h);
        }
    }
    return h;
}

}
