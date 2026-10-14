#pragma once

#include "ichor/data/axiom.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 02 firing rules, in resolution order. Triples are not ranked by the doc;
// they resolve first because they are the rarest and most specific.
enum class SynergyTier { Triple, CataclysmLaw, CataclysmPowers, LawPowers, Pressure, Anomaly };

std::string_view synergyTierName(SynergyTier tier);

// Doc 02: a world runs at most two synergies.
constexpr std::size_t kMaxActiveSynergies = 2;

struct Synergy {
    std::string id;
    SynergyTier tier = SynergyTier::CataclysmLaw;
    // Sorted, no repeats: two axioms, or three for a triple.
    std::vector<std::string> axioms;
};

// Every axiom id that exists, with its slot. Definitions are validated against it.
using AxiomPool = std::map<std::string, AxiomSlot, std::less<>>;
AxiomPool axiomPool(const AxiomSet& axioms);

// Parses one file of the form {"synergies": [{"id", "tier", "axioms"}, ...]}.
// Every problem is appended to `problems` as "<label>: <field path>: <what is
// wrong>". Returns true, and fills `out` sorted by id, only when there are no
// problems. Never throws on bad input.
bool parseSynergies(std::string_view label, std::string_view text, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems);

bool loadSynergyFile(const std::filesystem::path& file, const AxiomPool& pool, std::vector<Synergy>& out, std::vector<std::string>& problems);

// The ids of the synergies that fire for `axiomIds`, in firing order: tier
// priority, then id, cut off at kMaxActiveSynergies. Pure; ignores the order
// and any repeats in `axiomIds`, and the order of `definitions`. A triple does
// not suppress the pairs inside it.
std::vector<std::string> detectSynergies(const std::vector<std::string>& axiomIds, const std::vector<Synergy>& definitions);

}
