#pragma once

#include "ichor/progression/capabilities.h"

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

constexpr int kMaxMilestoneRank = 1000;

struct RankCondition {
    std::string faction;
    int rank = 0; // at least this rank, 1 to kMaxMilestoneRank
};

// A named unlock earned by finishing things, not by spending points. It holds
// when every listed quest is done, every set piece cleared and every faction
// rank reached. It grants exactly one of a capability or a stat unlock id.
struct Milestone {
    std::string id;
    std::vector<std::string> quests;
    std::vector<std::string> setPieces;
    std::vector<RankCondition> ranks;
    std::string capability;
    std::string stat;
};

// What the player has done so far. The caller fills it from the consequence
// ledger and the standing data; this module only reads it.
struct MilestoneFacts {
    std::set<std::string, std::less<>> quests;
    std::set<std::string, std::less<>> setPieces;
    std::map<std::string, int, std::less<>> ranks;
};

// Checks a table against the capability table. Problems are appended as
// "milestones: [i].field: <what is wrong>". Ids are lowercase words of at most
// 64 bytes, milestone ids are unique, a milestone has at least one condition
// and exactly one grant, a capability grant names a capability in
// `capabilities`, and the table is bounded in size. Returns true when there are
// none. Never throws.
bool validateMilestones(const std::vector<Milestone>& table, const std::vector<Capability>& capabilities, std::vector<std::string>& problems);

// Which milestones have been granted. The granted set is the only state.
class MilestoneTracker {
public:
    // The table should have passed validateMilestones; one that did not is
    // tolerated, its bad entries simply never grant.
    explicit MilestoneTracker(std::vector<Milestone> table);

    // Grants every ungranted milestone whose conditions hold in `facts`, in id
    // order, and returns their ids. A capability grant goes through `unlocks`;
    // if the capability is already unlocked (a trainer got there first) the
    // milestone still counts as granted, and if its prerequisites are unmet or
    // it is unknown the milestone stays ungranted and is retried next time.
    std::vector<std::string> evaluate(const MilestoneFacts& facts, CapabilityUnlocks& unlocks);

    bool isGranted(std::string_view id) const;
    bool hasStat(std::string_view stat) const;

    // Sorted.
    std::vector<std::string> granted() const;
    std::vector<std::string> statUnlocks() const;

private:
    std::vector<Milestone> table_; // sorted by id
    std::set<std::string, std::less<>> granted_;
    std::set<std::string, std::less<>> stats_;
};

}
