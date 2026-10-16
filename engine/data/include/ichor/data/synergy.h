#pragma once

#include "ichor/data/axiom.h"

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 02 firing rules, in resolution order. Triples are not ranked by the doc;
// they resolve first because they are the rarest and most specific.
enum class SynergyTier { Triple, CataclysmLaw, CataclysmPowers, LawPowers, Pressure, Anomaly };

std::string_view synergyTierName(SynergyTier tier);

// Doc 02 "Content tiers": 1 is a flagship (faction + dungeon type + mechanic + prose),
// 2 is text only. Unset for files that do not say; synergy_content.h requires it.
enum class ContentTier { Unset, Flagship, TextOnly };

// Doc 02: a world runs at most two synergies.
constexpr std::size_t kMaxActiveSynergies = 2;

struct Synergy {
    std::string id;
    SynergyTier tier = SynergyTier::CataclysmLaw;
    // Sorted, no repeats: two axioms, or three for a triple.
    std::vector<std::string> axioms;
    // Optional overrides, empty when the file leaves them out. Ids are carried
    // as strings; nothing here checks them against a faction or dungeon table.
    std::string faction;
    std::string dungeonType;
    std::string worldName;
    // Flavour slot id -> text that replaces the default for that slot.
    std::map<std::string, std::string> flavour;
    ContentTier contentTier = ContentTier::Unset;
    // Id of the mechanical change a flagship introduces, empty when the file leaves it out.
    std::string mechanic;
};

// What the fired synergies inject, ready to be copied into WorldConfig.
struct SynergyOverride {
    std::vector<std::string> factions;
    std::vector<std::string> dungeonTypes;
    std::string worldName;
    std::map<std::string, std::string> flavour;
};

// Every axiom id that exists, with its slot. Definitions are validated against it.
using AxiomPool = std::map<std::string, AxiomSlot, std::less<>>;
AxiomPool axiomPool(const AxiomSet& axioms);

// Parses one file of the form {"synergies": [{"id", "tier", "axioms"}, ...]}.
// Each entry may also carry "faction", "dungeon_type" (lowercase ids),
// "world_name" (text), "flavour" (an object of slot id -> text), "mechanic" (a
// lowercase id) and "content_tier" (1 or 2).
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

// Applies the synergies named by `fired` (as returned by detectSynergies, in
// firing order) to `defaultFlavour`. The first fired synergy with a world name
// names the world, and for a flavour slot the first fired synergy that has one
// wins. Factions and dungeon types are listed in firing order without repeats.
// Ids in `fired` that no definition has are skipped. Pure and deterministic.
SynergyOverride applyOverrides(const std::vector<std::string>& fired, const std::vector<Synergy>& definitions, const std::map<std::string, std::string>& defaultFlavour);

}
