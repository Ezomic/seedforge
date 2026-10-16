#pragma once

#include "ichor/factions/hostility.h"
#include "ichor/factions/territory.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 03 "Placement": overlapping claims mark content sites.
enum class ContestedKind : std::uint8_t {
    // Claimed by two or more factions.
    Overlap = 0,
    // Claimed by one faction, with a 4-neighbour claimed by a faction hostile to it.
    Border = 1,
};

constexpr std::size_t kDefaultMaxContested = 4096;

struct ContestedCell {
    std::uint32_t cell = 0;  // y * width + x
    ContestedKind kind = ContestedKind::Overlap;
    // Bit i set = the faction at index i of TerritoryResult::ids claims the cell.
    std::uint16_t claimants = 0;
    std::uint32_t claimantCount = 0;
    // Overlap: highest hostility between any two claimants. Border: highest
    // hostility between the claimant and a hostile neighbouring claimant.
    // Pairs missing from the matrix count as 0.
    std::int32_t hostility = 0;
    // The caller's value layer at this cell (0 when none is given).
    std::uint32_t value = 0;
    // Sum of the claimants' strengths.
    std::uint32_t strength = 0;
};

struct ContestedResult {
    // Best first: hostility, then value, then claimant count, then total
    // strength (all descending), then Overlap before Border, then cell index.
    // Truncated to maxResults.
    std::vector<ContestedCell> cells;
    // Contested cells found before truncation.
    std::size_t total = 0;
    std::size_t overlapTotal = 0;
    std::size_t borderTotal = 0;
};

// Finds contested cells in a territory fill. `value` is an optional per-cell
// layer (width * height long, or empty), e.g. TerritoryGrid::resource, used to
// rank cells. Matrix entries naming factions not in `territory` are ignored.
// At most `maxResults` cells are kept, so the output stays bounded however
// large the map. Every problem is appended to `problems` as
// "contested: <what is wrong>"; returns true, and fills `out`, only when there
// are none (on failure `out` is left empty). Integer only, identical on every
// platform.
bool findContestedCells(std::uint32_t width, std::uint32_t height, const TerritoryResult& territory, const std::vector<HostilityEntry>& matrix,
    const std::vector<std::uint8_t>& value, std::size_t maxResults, ContestedResult& out, std::vector<std::string>& problems);

std::uint64_t contestedDigest(const ContestedResult& result);

}
