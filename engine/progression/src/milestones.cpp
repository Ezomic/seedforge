#include "ichor/progression/milestones.h"

#include <algorithm>
#include <string>
#include <utility>

namespace ichor {

namespace {

constexpr std::size_t kMaxId = 64;
constexpr std::size_t kMaxMilestones = 1024;
constexpr std::size_t kMaxConditions = 32;

bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && text.size() <= kMaxId && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

bool conditionsHold(const Milestone& m, const MilestoneFacts& facts)
{
    if (m.quests.empty() && m.setPieces.empty() && m.ranks.empty()) {
        return false;
    }
    const bool quests = std::all_of(m.quests.begin(), m.quests.end(), [&](const std::string& q) { return facts.quests.find(q) != facts.quests.end(); });
    const bool pieces = std::all_of(m.setPieces.begin(), m.setPieces.end(), [&](const std::string& s) { return facts.setPieces.find(s) != facts.setPieces.end(); });
    const bool ranks = std::all_of(m.ranks.begin(), m.ranks.end(), [&](const RankCondition& r) {
        const auto it = facts.ranks.find(r.faction);
        return r.rank >= 1 && it != facts.ranks.end() && it->second >= r.rank;
    });
    return quests && pieces && ranks;
}

}

bool validateMilestones(const std::vector<Milestone>& table, const std::vector<Capability>& capabilities, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (table.size() > kMaxMilestones) {
        problems.push_back("milestones: more than " + std::to_string(kMaxMilestones) + " milestones");
        return false;
    }
    std::set<std::string, std::less<>> known;
    for (const Capability& c : capabilities) {
        known.insert(c.id);
    }
    std::set<std::string, std::less<>> seen;
    const auto add = [&](std::size_t i, const std::string& path, const std::string& what) {
        problems.push_back("milestones: [" + std::to_string(i) + "]" + path + ": " + what);
    };
    const auto checkIds = [&](std::size_t i, const std::string& field, const std::vector<std::string>& ids) {
        if (ids.size() > kMaxConditions) {
            add(i, "." + field, "too many entries");
            return;
        }
        std::set<std::string_view> once;
        for (const std::string& id : ids) {
            if (!isWord(id)) {
                add(i, "." + field, "invalid " + field + " id");
            } else if (!once.insert(id).second) {
                add(i, "." + field, "duplicate entry");
            }
        }
    };
    for (std::size_t i = 0; i < table.size(); ++i) {
        const Milestone& m = table[i];
        if (!isWord(m.id)) {
            add(i, ".id", "invalid id, use lowercase letters, digits and underscores, up to 64 bytes");
        } else if (!seen.insert(m.id).second) {
            add(i, ".id", "duplicate id");
        }
        checkIds(i, "quests", m.quests);
        checkIds(i, "setPieces", m.setPieces);
        if (m.ranks.size() > kMaxConditions) {
            add(i, ".ranks", "too many entries");
        }
        std::set<std::string_view> factions;
        for (const RankCondition& r : m.ranks) {
            if (!isWord(r.faction)) {
                add(i, ".ranks", "invalid faction id");
            } else if (!factions.insert(r.faction).second) {
                add(i, ".ranks", "duplicate faction");
            }
            if (r.rank < 1 || r.rank > kMaxMilestoneRank) {
                add(i, ".ranks", "rank must be 1 to " + std::to_string(kMaxMilestoneRank));
            }
        }
        if (m.quests.empty() && m.setPieces.empty() && m.ranks.empty()) {
            add(i, "", "needs at least one condition");
        }
        if (m.capability.empty() == m.stat.empty()) {
            add(i, "", "must grant exactly one of capability or stat");
        } else if (!m.capability.empty() && known.find(m.capability) == known.end()) {
            add(i, ".capability", "unknown capability");
        } else if (!m.stat.empty() && !isWord(m.stat)) {
            add(i, ".stat", "invalid stat id");
        }
    }
    return problems.size() == before;
}

MilestoneTracker::MilestoneTracker(std::vector<Milestone> table)
    : table_(std::move(table))
{
    std::stable_sort(table_.begin(), table_.end(), [](const Milestone& a, const Milestone& b) { return a.id < b.id; });
}

std::vector<std::string> MilestoneTracker::evaluate(const MilestoneFacts& facts, CapabilityUnlocks& unlocks)
{
    std::vector<std::string> newly;
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (const Milestone& m : table_) {
            if (granted_.find(m.id) != granted_.end() || !conditionsHold(m, facts)) {
                continue;
            }
            if (!m.capability.empty()) {
                const UnlockResult result = unlocks.unlock(m.capability);
                if (result != UnlockResult::Unlocked && result != UnlockResult::AlreadyUnlocked) {
                    continue;
                }
            } else if (!m.stat.empty()) {
                stats_.insert(m.stat);
            } else {
                continue;
            }
            granted_.insert(m.id);
            newly.push_back(m.id);
            progressed = true;
        }
    }
    return newly;
}

bool MilestoneTracker::isGranted(std::string_view id) const
{
    return granted_.find(id) != granted_.end();
}

bool MilestoneTracker::hasStat(std::string_view stat) const
{
    return stats_.find(stat) != stats_.end();
}

std::vector<std::string> MilestoneTracker::granted() const
{
    return {granted_.begin(), granted_.end()};
}

std::vector<std::string> MilestoneTracker::statUnlocks() const
{
    return {stats_.begin(), stats_.end()};
}

}
