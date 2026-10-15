#pragma once

#include "ichor/factions/factions.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03: the Herald is read off the world's Pressure axiom. One entry per
// Pressure axiom; `faction` is always a Herald, built from the entry.
struct HeraldEntry {
    std::string pressure;
    Faction faction;
};

// Parses data/heralds/mapping.json: an array of {"id", "pressure", "name"}.
// Herald ids and pressure axiom ids must each be unique. Problems are appended as
// "<label>: <path>: <what is wrong>"; returns true, and fills `out` sorted by
// pressure id, only when there are none. Never throws on bad input.
bool parseHeraldMapping(std::string_view label, std::string_view text, std::vector<HeraldEntry>& out, std::vector<std::string>& problems);

// The Herald for a Pressure axiom id, or nullptr when the axiom has none. `mapping`
// must come from parseHeraldMapping.
const Faction* heraldFor(const std::vector<HeraldEntry>& mapping, std::string_view pressureAxiomId);

}
