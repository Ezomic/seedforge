#pragma once

#include "ichor/factions/factions.h"

#include <optional>
#include <string_view>
#include <vector>

namespace ichor {

constexpr int kMinPoleisIncumbents = 3;
constexpr int kMaxPoleisIncumbents = 4;

// How a Powers axiom shapes the Incumbent slot of doc 03.
enum class IncumbentShape { Single, Poleis, None };

struct IncumbentPlan {
    IncumbentShape shape = IncumbentShape::Single;
    // Every entry has role Incumbent. One for Single, three or four for Poleis, none for None.
    std::vector<Faction> incumbents;
};

// Reads the Incumbent off a Powers axiom id ("powers.hegemony", ...). Returns
// nullopt for an id that is not a known Powers axiom, and for a Poleis world
// whose `poleisCount` is outside 3 to 4. `poleisCount` is ignored for every
// other axiom. Pure and deterministic.
std::optional<IncumbentPlan> planIncumbents(std::string_view powersAxiomId, int poleisCount = kMinPoleisIncumbents);

}
