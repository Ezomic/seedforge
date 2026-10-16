#pragma once

#include "ichor/factions/factions.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Where a Refusenik is placed (doc 03, Placement): the Asebeis keep to marginal,
// isolated land; the Xenoi hold no land and follow routes.
enum class RefusenikPlacement { MarginalLand, Routes };

std::string_view refusenikPlacementName(RefusenikPlacement placement);

// Doc 03: the two Refusenik archetypes. `faction` is always a Refusenik.
struct RefusenikEntry {
    Faction faction;
    RefusenikPlacement placement = RefusenikPlacement::MarginalLand;
    bool claimsTerritory = false;
    // The Xenoi move under guest-right, so they can offer services in any region.
    bool offersServicesAnywhere = false;
    std::string summary;
};

// Parses data/factions/refuseniks.json: an array of {"id", "name", "placement",
// "claims_territory", "offers_services_anywhere", "summary"}. Placement is
// "marginal_land" or "routes". Ids are unique. Problems are appended as
// "<label>: <path>: <what is wrong>"; returns true, and fills `out` sorted by id,
// only when there are none. Never throws on bad input.
bool parseRefusenikMapping(std::string_view label, std::string_view text, std::vector<RefusenikEntry>& out, std::vector<std::string>& problems);

// The archetype with this id, or nullptr. `mapping` must come from parseRefusenikMapping.
const RefusenikEntry* refusenikFor(const std::vector<RefusenikEntry>& mapping, std::string_view id);

// The archetypes that can offer services in any region, in mapping order.
std::vector<const RefusenikEntry*> servicesAnywhereRefuseniks(const std::vector<RefusenikEntry>& mapping);

}
