#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace ichor {

// Discovery XP (docs/07-progression.md): the world generator already scores sites,
// so the reward is derived from that score. Integer math only after the score is
// validated, so the result is identical on every platform.
enum class DiscoveryKind : std::uint8_t { Poi, Landmark, Vista };

constexpr std::size_t kDiscoveryKindCount = 3;
constexpr std::int64_t kPoiBaseXp = 100;
constexpr std::int64_t kLandmarkBaseXp = 40;
constexpr std::int64_t kVistaBaseXp = 20;

// Scores at or above this saturate; the generator's remoteness/difficulty score is nominally 0 to 1.
constexpr double kMaxDiscoveryScore = 1.0;
// Each earlier discovery of the same kind scales the next one by 970/1000, never below the floor.
constexpr std::int64_t kRepeatKeepPermille = 970;
constexpr std::int64_t kRepeatFloorPermille = 100;
constexpr std::int64_t kMaxTotalXp = 1'000'000'000;
constexpr std::size_t kMaxDiscoveryIdBytes = 128;
constexpr std::size_t kMaxDiscoveries = 100'000;

enum class DiscoveryStatus : std::uint8_t { Awarded, Duplicate, InvalidId, InvalidKind, InvalidScore, LedgerFull };

struct Discovery {
    std::string id;
    DiscoveryKind kind = DiscoveryKind::Poi;
    double score = 0.0;
};

struct DiscoveryResult {
    DiscoveryStatus status = DiscoveryStatus::InvalidId;
    std::int64_t xp = 0;
};

// Base XP of a kind at score 1.0. Zero for an out-of-range kind.
std::int64_t discoveryBaseXp(DiscoveryKind kind);

// Multiplier in permille for a discovery of a kind after `priorOfKind` earlier ones: 1000 for the first, falling geometrically to the floor.
std::int64_t discoveryRepeatPermille(std::size_t priorOfKind);

// XP for one discovery, rounded down. Zero for NaN, infinite or negative scores and
// unknown kinds; scores above the maximum saturate. Never overflows.
std::int64_t discoveryXp(DiscoveryKind kind, double score, std::size_t priorOfKind);

// Tracks which discoveries were already counted (by stable id) and the running XP total.
class DiscoveryLedger {
public:
    // Counts a discovery once. Invalid input is rejected without changing the ledger,
    // so a corrected report of the same id can still count later. A valid score of
    // zero is recorded as discovered and awards nothing.
    DiscoveryResult record(const Discovery& discovery);

    bool has(std::string_view id) const;
    std::size_t count() const { return seen_.size(); }
    std::size_t countOf(DiscoveryKind kind) const;
    std::int64_t totalXp() const { return totalXp_; }

private:
    std::set<std::string, std::less<>> seen_;
    std::size_t perKind_[kDiscoveryKindCount] = {};
    std::int64_t totalXp_ = 0;
};

}
