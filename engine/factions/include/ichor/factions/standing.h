#pragma once

#include "ichor/factions/factions.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Standing is the one progression axis that can go negative.
constexpr std::int32_t kStandingMin = -1000;
constexpr std::int32_t kStandingMax = 1000;

struct StandingTier {
    std::string id;
    std::string name;
    std::int32_t threshold = 0;
};

struct StandingGate {
    std::string id;
    std::string faction;
    std::string minTier;
};

struct StandingChange {
    std::string faction;
    std::int32_t oldValue = 0;
    std::int32_t newValue = 0;
};

// Parses a JSON array of tiers, each {"id", "name", "threshold"}. Thresholds must
// rise strictly and the first must be kStandingMin, so every standing has a tier.
// A tier holds from its threshold up to the next one. Problems are appended as
// "<label>: <path>: <what is wrong>"; returns true, and fills `out`, only when
// there are none. Never throws on bad input.
bool parseStandingTiers(std::string_view label, std::string_view text, std::vector<StandingTier>& out, std::vector<std::string>& problems);

// Parses a JSON array of gates, each {"id", "faction", "min_tier"}. The faction
// must be in `factions` and the tier in `tiers`. Same reporting as the tiers.
bool parseStandingGates(std::string_view label, std::string_view text, const std::vector<StandingTier>& tiers, const std::vector<Faction>& factions,
    std::vector<StandingGate>& out, std::vector<std::string>& problems);

// The player's signed standing with each faction, plus the tiers and gates it is
// read through. Values stay within [kStandingMin, kStandingMax]; changes saturate.
class FactionStanding {
public:
    // `tiers` must be valid per parseStandingTiers and `gates` per parseStandingGates.
    FactionStanding(const std::vector<Faction>& factions, std::vector<StandingTier> tiers, std::vector<StandingGate> gates);

    std::optional<std::int32_t> standing(std::string_view faction) const;
    const StandingTier* tier(std::string_view faction) const;

    // The only way standing moves. Returns the old and new value, or nullopt for an
    // unknown faction (nothing changes).
    std::optional<StandingChange> change(std::string_view faction, std::int32_t delta);

    // True when the gate's faction is at or above the gate's minimum tier. An
    // unknown gate is closed.
    bool allowed(std::string_view gate) const;

    // Faction ids ascending, then each value: the same bytes whatever order the
    // factions were given in.
    std::vector<std::uint8_t> serialise() const;
    std::uint64_t digest() const;
    // Replaces the standing from `bytes`, or returns false and changes nothing when
    // the bytes are malformed, out of range, or name a different set of factions.
    bool deserialise(std::span<const std::uint8_t> bytes);

private:
    std::map<std::string, std::int32_t, std::less<>> values_;
    std::vector<StandingTier> tiers_;
    std::map<std::string, StandingGate, std::less<>> gates_;
};

}
