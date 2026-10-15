#pragma once

#include "ichor/factions/factions.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 03: the Parasite is read off the Law axiom. One archetype, six faces.
struct ParasiteEntry {
    std::string axiom;
    std::string id;
    std::string name;
};

// Parses a JSON array of {"axiom", "id", "name"}. The axiom is a Law axiom id
// ("law.<word>"); axioms and faction ids are each unique across the file. Problems
// are appended as "<label>: <path>: <what is wrong>"; returns true, and fills
// `out`, only when there are none. Never throws on bad input.
bool parseParasiteMapping(std::string_view label, std::string_view text, std::vector<ParasiteEntry>& out, std::vector<std::string>& problems);

// The Parasite faction for a Law axiom, always with role Parasite, or nullopt
// when the axiom has no face.
std::optional<Faction> parasiteFor(const std::vector<ParasiteEntry>& mapping, std::string_view lawAxiomId);

// One problem for every id in `lawAxiomIds` that the mapping has no face for.
// Entries for axioms not listed are fine: the mapping may be ahead of the axioms.
std::vector<std::string> parasiteCoverageProblems(const std::vector<ParasiteEntry>& mapping, const std::vector<std::string>& lawAxiomIds);

}
