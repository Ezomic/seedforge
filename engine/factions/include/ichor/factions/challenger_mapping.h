#pragma once

#include "ichor/factions/factions.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03: the three Challenger archetypes. Each has a true diagnosis and a bad
// prescription. `faction` is always a Challenger, built from the entry.
struct ChallengerEntry {
    Faction faction;
    std::string diagnosis;
    std::string prescription;
    // The Diadochoi are a splinter of the Incumbent, so they need one to exist.
    bool requiresIncumbent = false;
};

// Parses data/factions/challengers.json: an array of {"id", "name", "diagnosis",
// "prescription", "requires_incumbent"}. Ids are unique. Problems are appended as
// "<label>: <path>: <what is wrong>"; returns true, and fills `out` sorted by id,
// only when there are none. Never throws on bad input.
bool parseChallengerMapping(std::string_view label, std::string_view text, std::vector<ChallengerEntry>& out, std::vector<std::string>& problems);

// The archetype with this id, or nullptr. `mapping` must come from parseChallengerMapping.
const ChallengerEntry* challengerFor(const std::vector<ChallengerEntry>& mapping, std::string_view id);

// The archetypes a world can field, in mapping order: all of them when it has an
// Incumbent, otherwise those that do not require one (The Dark Age has none).
std::vector<const ChallengerEntry*> availableChallengers(const std::vector<ChallengerEntry>& mapping, bool worldHasIncumbent);

}
