#pragma once

#include "ichor/progression/capabilities.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One catalogue item and the capability ids it grants. Items that grant nothing
// need not be listed.
struct GearGrant {
    std::string item;
    std::vector<std::string> grants;
};

// One part of the map and the capabilities it takes to get in. An empty list
// means open to everyone.
struct MapRegion {
    std::string id;
    std::vector<std::string> needs;
};

struct RegionAccess {
    std::string id;
    bool reachable = false;
    std::vector<std::string> missing; // sorted; needed capabilities not held, empty when reachable
};

constexpr std::size_t kMaxLootGrants = 4096;
constexpr std::size_t kMaxLootRegions = 4096;
constexpr std::size_t kMaxLootListed = 64; // grants per item, needs per region, items carried

// Checks the grant table against the capability table. Problems are appended as
// "capability loot: [i].field: <what is wrong>". Ids are lowercase words of at
// most 64 bytes, item ids are unique, an item grants at least one capability,
// each grant names a capability in `capabilities` and is not repeated, and the
// table is bounded in size. Returns true when there are none. Never throws.
bool validateGearGrants(const std::vector<GearGrant>& grants, const std::vector<Capability>& capabilities, std::vector<std::string>& problems);

// Same checks for a region table: unique ids, needs naming known capabilities.
bool validateMapRegions(const std::vector<MapRegion>& regions, const std::vector<Capability>& capabilities, std::vector<std::string>& problems);

// The capabilities a character holds: those in `baseline` (already unlocked) plus
// those granted by the items it carries, then cut down to the ones whose
// prerequisites are also held, repeatedly, so a Dive grant without Swim gives
// nothing. Items that are not in the table, repeated items and capability ids
// that are not in `capabilities` are ignored; carrying more than kMaxLootListed
// items reads only the first kMaxLootListed. Sorted, no repeats. Never throws.
std::vector<std::string> effectiveCapabilities(const std::vector<GearGrant>& grants, const std::vector<Capability>& capabilities, const std::vector<std::string>& baseline, const std::vector<std::string>& items);

// Which regions the held capabilities open, in region table order. A region
// naming a capability that is not held is locked and lists it as missing; a
// region needing an id the capability table does not know can never be entered.
std::vector<RegionAccess> assessRegions(const std::vector<MapRegion>& regions, const std::vector<std::string>& held);

// The ids of the reachable regions only, sorted.
std::vector<std::string> reachableRegionIds(const std::vector<MapRegion>& regions, const std::vector<std::string>& held);

}
