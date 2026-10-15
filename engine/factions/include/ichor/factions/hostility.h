#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03 "Relationships": the old-order axis and the Law axis, each three steps.
enum class OrderStance : std::uint8_t { Restore = 0, Preserve = 1, Replace = 2 };
enum class LawStance : std::uint8_t { Control = 0, Free = 1, Renounce = 2 };

struct FactionStance {
    std::string faction;
    OrderStance order = OrderStance::Preserve;
    LawStance law = LawStance::Free;
};

// The cells and resource nodes a faction claims, as plain ids. Order and
// duplicates do not matter. Supplied by territory fill, not owned here.
struct FactionClaim {
    std::string faction;
    std::vector<std::uint32_t> cells;
    std::vector<std::uint32_t> resources;
};

// All scores are integers: no floats, so the matrix is identical on every platform.
constexpr std::int32_t kStepDistance = 250;  // one step on one axis
constexpr std::int32_t kMaxIdeology = 1000;  // two axes, two steps each
constexpr std::int32_t kMaxOverlap = 1000;   // complete overlap of the smaller claim
constexpr std::int32_t kMaxHostility = kMaxIdeology + kMaxOverlap;
constexpr std::int32_t kAllyAtMost = 250;
constexpr std::int32_t kHostileAtLeast = 1000;

enum class Relation : std::uint8_t { Allied, Neutral, Hostile };

struct HostilityEntry {
    std::string a;  // a < b by id
    std::string b;
    std::int32_t ideology = 0;
    std::int32_t overlap = 0;
    std::int32_t hostility = 0;  // ideology + overlap
    Relation relation = Relation::Neutral;
    // Opposite ends of an axis, yet not hostile: the pairs a player can exploit.
    bool oddBedfellows = false;
};

// 0..kMaxIdeology: kStepDistance per step apart on each axis, summed.
std::int32_t ideologicalDistance(const FactionStance& a, const FactionStance& b);

// 0..kMaxOverlap: shared cells and resources over the smaller claim's total, in
// thousandths. Two empty claims overlap by 0.
std::int32_t resourceOverlap(const FactionClaim& a, const FactionClaim& b);

// Symmetric pairwise matrix, one entry per unordered pair, sorted by (a, b).
// `claims` may omit a faction (no territory: overlap 0). Fails, appending to
// `problems` and leaving `out` empty, on duplicate faction ids in either input,
// a stance out of range, or a claim naming a faction with no stance.
bool computeHostilityMatrix(const std::vector<FactionStance>& stances, const std::vector<FactionClaim>& claims, std::vector<HostilityEntry>& out,
    std::vector<std::string>& problems);

// Entry for the unordered pair, or nullptr when absent.
const HostilityEntry* findHostility(const std::vector<HostilityEntry>& matrix, std::string_view a, std::string_view b);

std::uint64_t hostilityDigest(const std::vector<HostilityEntry>& matrix);

}
