#pragma once

#include "ichor/factions/factions.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

constexpr int kNormalExpansionPermille = 1000;
constexpr int kDarkAgeExpansionPermille = 1500;

// What one role does in a world. `expansionPermille` scales how hard the role's
// factions claim territory: 1000 is the normal claim, more is filling a vacuum.
struct RoleEcology {
    int minCount = 0;
    int maxCount = 0;
    int expansionPermille = kNormalExpansionPermille;
};

// Doc 03: how the world's Powers axiom changes the six-role ecology. The Dark
// Age has no Incumbent, so Parasites and Refuseniks expand into the vacuum.
struct EcologyPlan {
    bool postState = false;
    std::array<RoleEcology, kFactionRoleCount> roles{};

    const RoleEcology& role(FactionRole r) const { return roles[static_cast<std::size_t>(r)]; }
};

// Plans the ecology from the Powers and Law axiom ids ("powers.dark_age",
// "law.oath", ...). Nullopt when either id is empty, has the wrong prefix, or
// is not a plain axiom id (lowercase letters, digits, '_' after the dot, 64
// bytes at most). The Law id never changes the shape: the Parasite and
// Refusenik faces come from the Law mapping, not from here. Pure.
std::optional<EcologyPlan> planEcology(std::string_view powersAxiomId, std::string_view lawAxiomId);

// Every way `factions` breaks the plan: a role count outside its range, or an
// Incumbent present in a post-state world. Empty when the set fits.
std::vector<std::string> checkAgainstEcology(const EcologyPlan& plan, const std::vector<Faction>& factions);

// Territory claim weight for each faction (same order), in permille. A faction
// of a role that expands claims `expansionPermille`; other roles claim 1000.
std::vector<int> territoryWeightsPermille(const EcologyPlan& plan, const std::vector<Faction>& factions);

}
