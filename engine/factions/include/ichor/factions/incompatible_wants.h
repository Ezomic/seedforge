#pragma once

#include "ichor/factions/factions.h"

#include <string>
#include <vector>

namespace ichor {

// What one faction wants on one contested axis, such as the Law resource
// (control, free_access, renounce) or the old order (restore, preserve,
// replace). Two stances on the same axis are strictly incompatible when they
// differ: the world can only end one way.
struct FactionWant {
    std::string faction;
    std::string axis;
    std::string stance;
};

struct IncompatiblePair {
    std::string axis;
    std::string factionA; // the lesser faction id
    std::string stanceA;
    std::string factionB;
    std::string stanceB;
};

// Every pair of distinct factions that want different stances on the same axis,
// sorted by axis, then factionA, then factionB. Wants naming a faction not in
// `factions` are ignored, as are repeats of the same (faction, axis, stance).
// A faction listing two stances on one axis pairs with each of the other
// side's stances, never with itself. Axis and stance are compared as written,
// so callers pass canonical lowercase words. The result holds one entry per
// pair, so keep the want list small (a world has a handful of factions).
std::vector<IncompatiblePair> incompatiblePairs(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants);

// Whether incompatiblePairs would be non-empty, without building the list
// (which grows quadratically with the factions sharing an axis).
bool hasIncompatiblePair(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants);

// Doc 03 rule 4: at least two factions want strictly incompatible things.
// Returns every violation, empty when the world has a real decision:
//  - a want names a faction that is not in the set, or has an empty axis or stance;
//  - no pair of factions is incompatible on any axis.
std::vector<std::string> validateIncompatibleWants(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants);

}
