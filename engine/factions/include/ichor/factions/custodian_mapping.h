#pragma once

#include "ichor/factions/factions.h"

#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// What a Custodian pays out for standing: the Mnemones trade what was never
// written down, the Periegetes trade the land (trading with them lights up POIs).
enum class CustodianReward { Secret, MapKnowledge };

std::string_view custodianRewardName(CustodianReward reward);

// Doc 03: the Custodian is read off the world's Cataclysm axiom. Two archetypes;
// each claims the Cataclysms it keeps a secret about. `faction` is always a Custodian.
struct CustodianEntry {
    Faction faction;
    CustodianReward reward = CustodianReward::Secret;
    std::vector<std::string> cataclysms;
};

// Parses data/factions/custodians.json: an array of {"id", "name", "reward",
// "cataclysms"}. Reward is "secret" or "map_knowledge"; cataclysms are Cataclysm
// axiom ids ("cataclysm.<word>"). Custodian ids are unique, and a Cataclysm belongs
// to one Custodian. Problems are appended as "<label>: <path>: <what is wrong>";
// returns true, and fills `out` sorted by id, only when there are none. Never
// throws on bad input.
bool parseCustodianMapping(std::string_view label, std::string_view text, std::vector<CustodianEntry>& out, std::vector<std::string>& problems);

// The Custodian entry for a Cataclysm axiom id, or nullptr when none claims it.
// `mapping` must come from parseCustodianMapping.
const CustodianEntry* custodianFor(const std::vector<CustodianEntry>& mapping, std::string_view cataclysmAxiomId);

// One problem for every id in `cataclysmAxiomIds` that no Custodian claims. Claims
// on Cataclysms not listed are fine: the mapping may be ahead of the axioms.
std::vector<std::string> custodianCoverageProblems(const std::vector<CustodianEntry>& mapping, const std::vector<std::string>& cataclysmAxiomIds);

}
