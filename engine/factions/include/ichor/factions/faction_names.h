#pragma once

#include "ichor/factions/factions.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03 naming: a role plus a noun the world's axioms produced ("the Salt
// League", "the Bronze Assize"). Pure and deterministic: the same seed, faction
// identity and noun pool always give the same name on every platform.

constexpr std::size_t kMaxNameNouns = 64;
constexpr std::size_t kMaxNameNounLength = 24;
constexpr std::size_t kMaxFactionNameLength = 96;

// Reduces a raw noun (an axiom economy id, a tag, a title word) to a short
// capitalised word: ASCII letters, digits, apostrophes and hyphens only, "_"
// and spaces become single spaces, everything else is dropped, at most
// kMaxNameNounLength characters. Returns "" when nothing usable is left.
std::string cleanNameNoun(std::string_view raw);

// One name. `nouns` is the world's pool, in the caller's order; unusable entries are
// skipped and only the first kMaxNameNouns entries are looked at. With no usable
// noun the name is the role's bare form ("The Assize"). Never longer than
// kMaxFactionNameLength.
std::string composeFactionName(std::uint64_t seed, std::string_view factionId, FactionRole role, const std::vector<std::string>& nouns);

// Names for a whole world: result[i] belongs to factions[i] (only id and role are
// read). Names are pairwise distinct; a clash is first re-rolled, then
// disambiguated with a numeral, so the work is bounded by factions.size().
std::vector<std::string> composeFactionNames(std::uint64_t seed, const std::vector<Faction>& factions, const std::vector<std::string>& nouns);

}
