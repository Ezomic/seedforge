#pragma once

#include "ichor/data/synergy.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The two-tier content rules of doc 02, applied on top of parseSynergies.
//   content tier 1 (flagship): faction, dungeon_type, mechanic and world_name are required.
//   content tier 2 (text only): world_name and a non-empty flavour are required; faction,
//   dungeon_type and mechanic are not allowed.
// A record with no content_tier is a problem here, although parseSynergies accepts it.
// Problems are appended as "<label>: <record id>.<field>: <what is wrong>". Returns true
// when nothing was appended. Faction ids are checked against the factions table by
// checkReferences (xref.h), not here.
bool validateSynergyContent(std::string_view label, const std::vector<Synergy>& synergies, std::vector<std::string>& problems);

bool loadSynergyContentFile(const std::filesystem::path& file, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems);

// Loads every *.json in `directory` (at most 256, in name order), parses and validates each,
// and rejects an id or an axiom set used by two records. Fills `out` sorted by id, and only
// when there are no problems. Never throws on bad input.
bool loadSynergyContentDirectory(const std::filesystem::path& directory, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems);

}
