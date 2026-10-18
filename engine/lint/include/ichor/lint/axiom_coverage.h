#pragma once

#include "ichor/data/axiom.h"
#include "ichor/data/synergy.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ichor::lint {

// Markdown report of which axiom pairs have a flagship synergy, which have a text-only one and
// which have none, plus every triple. Pure and deterministic: the same inputs give the same text
// (LF line endings, no timestamps), whatever the order of `synergies`.
// A pair is "excluded" when the axioms share a slot or one lists the other in `conflicts`, since a
// world never rolls them together; a synergy on such a pair still counts as covered.
std::string renderAxiomCoverage(const AxiomSet& axioms, const std::vector<Synergy>& synergies);

// Loads data/axioms and data/synergies under `dataRoot` and renders the report. Returns false and
// appends to `problems` when either fails to load. Never throws on bad data.
bool generateAxiomCoverage(const std::filesystem::path& dataRoot, std::string& markdown, std::vector<std::string>& problems);

}
