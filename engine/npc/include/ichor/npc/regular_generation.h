#pragma once

#include "ichor/data/npc_tiers.h"
#include "ichor/data/npc_traits.h"
#include "ichor/data/wants_fears.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

constexpr std::size_t kMinRegulars = 60; // doc 10: 60 to 100 Regulars in a world
constexpr std::size_t kMaxRegulars = 100;
constexpr int kMaxAbundance = 100;
constexpr std::uint32_t kMaxPopulation = 1'000'000;

// How much of a good a settlement has, as the economy layer reports it:
// positive is abundant, zero or negative is short. Clamped to +-kMaxAbundance.
struct EconomyGood {
    std::string good;
    int abundance = 0;
};

// The slice of a settlement the generator reads. Plain data on purpose, so
// worldgen can fill it from whatever it ends up producing. `world` is the
// settlement's live state (the same tags the wants and fears test).
struct SettlementEconomy {
    std::string id;
    std::uint32_t population = 1; // weights the share of Regulars; 0 counts as 1
    std::vector<EconomyGood> goods;
    WorldSnapshot world;
};

// A job a Regular can hold. A role with a good only exists where that good is
// at least `minAbundance` (the salt-panner needs abundant salt); a role with
// no good is available everywhere (a porter, a ferryman).
struct EconomicRole {
    std::string id;
    std::string good;
    int minAbundance = 1; // below 1 is raised to 1: a short good never supports a role
};

struct RegularInputs {
    std::vector<SettlementEconomy> settlements;
    std::vector<EconomicRole> roles;
    std::vector<std::string> names;
    std::vector<Desire> desires;
    const TraitPool* traits = nullptr;
};

// Pure and deterministic in (worldSeed, inputs), independent of the order of
// every input list. Draws kMinRegulars to kMaxRegulars Regulars, spread over
// settlements by population, each with an economic role the settlement's
// goods support, traits, relationships (generateRelationships), a want, a
// fear and a ledger memory key. `want` and `fear` hold the id of the chosen
// desire, preferring ones live in the settlement's world and falling back to
// any of that kind.
//
// A Regular's edges are written on both ends with the same kind, because a
// Regular record needs three to five entries and has no inverse kind for
// creditor or grudge; the pair's direction is not kept in the record.
//
// Entries that are not usable are dropped: ids that are not lowercase words
// (settlement ids at most 49 bytes), ids that appear twice with different
// content, blank or overlong names. Returns nothing when no settlement
// supports any role, when names, wants, fears or traits are missing, or when
// any input list or a settlement's goods exceed 10000 entries. A Regular
// whose traits cannot be placed (a pool whose conflicts leave too few) is
// skipped, so the count can then fall short of kMinRegulars. Sorted by id.
std::vector<Regular> generateRegulars(std::uint64_t worldSeed, const RegularInputs& inputs);

}
