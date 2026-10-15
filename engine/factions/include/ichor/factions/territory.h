#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03 "Placement": how a faction's flood-fill spreads from its homes.
enum class ClaimStrategy {
    ResourceFollowing,
    ContiguousDecay,
    NodesOnly,
    DefensiveIsolated,
    RoutesNotTerritory,
    ExpandingOverTime,
};

// Lowercase snake_case spelling: "resource_following", "contiguous_decay", ...
std::string_view claimStrategyName(ClaimStrategy strategy);
std::optional<ClaimStrategy> claimStrategyFromName(std::string_view name);

constexpr std::uint32_t kMaxClaimStrength = 1000;
constexpr std::size_t kMaxTerritoryCells = 1u << 20;
constexpr std::size_t kMaxTerritoryFactions = 16;

// Row-major layers, all width * height long. cost is the price of entering a
// cell (0 = impassable); resource is the cell's affinity for resource-following
// factions (0 = none); node marks harbours and junctions for nodes-only ones.
struct TerritoryGrid {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint16_t> cost;
    std::vector<std::uint8_t> resource;
    std::vector<std::uint8_t> node;
};

struct ClaimSpec {
    std::string id;
    ClaimStrategy strategy = ClaimStrategy::ContiguousDecay;
    // Home cell indices (y * width + x).
    std::vector<std::uint32_t> homes;
    // Total movement cost the fill may spend from a home.
    std::uint32_t reach = 0;
    // ExpandingOverTime: extra reach gained per unit of campaign time.
    std::uint32_t growthPerTime = 0;
    // ResourceFollowing: smallest resource value worth claiming.
    std::uint8_t minResource = 1;
    // DefensiveIsolated: cells within this Manhattan distance of another
    // faction's home are never entered.
    std::uint32_t avoidRadius = 0;
};

// strength[i] per faction in input order: 0 = unclaimed, up to kMaxClaimStrength.
// Overlaps between factions are kept as they are; resolving them is the
// contested-cell pass's job, not this one's.
struct TerritoryResult {
    std::vector<std::string> ids;
    std::vector<std::vector<std::uint16_t>> strength;
};

// Floods every faction's claim over the grid. `time` is campaign time for
// ExpandingOverTime. Every problem is appended to `problems` as
// "territory: <what is wrong>"; returns true, and fills `out`, only when there
// are none (on failure `out` is left empty). Never throws on bad input, and
// the result is identical on every platform: integer arithmetic, ties broken
// by cell index.
bool claimTerritory(const TerritoryGrid& grid, const std::vector<ClaimSpec>& specs, std::uint32_t time, TerritoryResult& out, std::vector<std::string>& problems);

std::uint64_t territoryDigest(const TerritoryResult& result);

}
