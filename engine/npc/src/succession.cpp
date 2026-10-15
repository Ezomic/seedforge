#include "ichor/npc/succession.h"

#include <algorithm>
#include <set>

namespace ichor {

namespace {

constexpr std::string_view kVacated = "role-vacated";
constexpr std::string_view kFilled = "role-filled";
constexpr std::string_view kInherited = "inherited-role";

int kindWeight(RelationKind kind)
{
    switch (kind) {
    case RelationKind::Family: return 4;
    case RelationKind::Rival: return 3;
    case RelationKind::Lover: return 2;
    case RelationKind::Creditor:
    case RelationKind::Grudge: return 1;
    }
    return 0;
}

std::string factId(std::string_view prefix, std::uint64_t tick, std::string_view role, std::string_view npc)
{
    return std::string(prefix) + ":" + std::to_string(tick) + ":" + std::string(role) + ":" + std::string(npc);
}

std::vector<std::string> splitFields(std::string_view text)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(';', start);
        if (at == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
}

}

bool validSuccessionKey(std::string_view text)
{
    if (text.empty() || text.size() > kMaxSuccessionKeyBytes) return false;
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
        if (!ok) return false;
    }
    return true;
}

std::optional<std::string> roleHolder(const ConsequenceLedger& ledger, std::string_view role)
{
    std::optional<std::string> holder;
    for (const Fact* fact : ledger.bySubject(role)) {
        if (fact->kind == kFilled) holder = fact->value;
        else if (fact->kind == kVacated) holder.reset();
    }
    return holder;
}

std::map<std::string, std::string, std::less<>> roleHolders(const ConsequenceLedger& ledger)
{
    std::map<std::string, std::string, std::less<>> holders;
    for (const Fact& fact : ledger.facts()) {
        if (fact.kind == kFilled) holders[fact.subject] = fact.value;
        else if (fact.kind == kVacated) holders.erase(fact.subject);
    }
    return holders;
}

FillResult fillRole(ConsequenceLedger& ledger, std::string_view role, std::string_view npc, std::uint64_t tick)
{
    if (!validSuccessionKey(role) || !validSuccessionKey(npc)) return FillResult::InvalidKey;
    if (roleHolder(ledger, role)) return FillResult::RoleTaken;
    const std::string id = factId("filled", tick, role, npc);
    if (ledger.find(id) != nullptr) return FillResult::DuplicateId;
    if (ledger.size() >= ConsequenceLedger::kMaxFacts) return FillResult::LedgerFull;
    (void)ledger.append({id, std::string(role), std::string(kFilled), std::string(npc), tick});
    return FillResult::Ok;
}

std::optional<std::string> chooseSuccessor(
    std::string_view deceased, const std::vector<RelationshipEdge>& edges, const std::vector<std::string>& eligible)
{
    const std::set<std::string, std::less<>> pool(eligible.begin(), eligible.end());
    std::map<std::string, int, std::less<>> score;
    for (const RelationshipEdge& edge : edges) {
        const std::string* other = nullptr;
        if (edge.a == deceased) other = &edge.b;
        else if (edge.b == deceased) other = &edge.a;
        if (other == nullptr || *other == deceased || !pool.contains(*other)) continue;
        const int strength = std::clamp(edge.strength, kMinStrength, kMaxStrength);
        int& best = score[*other];
        best = std::max(best, kindWeight(edge.kind) * 10 + strength);
    }
    std::optional<std::string> winner;
    int winnerScore = 0;
    for (const auto& [id, value] : score) { // id order, so the first of equal scores is the smallest id
        if (value > winnerScore) {
            winner = id;
            winnerScore = value;
        }
    }
    return winner;
}

SuccessionOutcome succeedRole(ConsequenceLedger& ledger, std::string_view role, std::string_view deceased, std::string_view cause,
    std::uint64_t tick, const std::vector<RelationshipEdge>& edges, const std::vector<std::string>& living)
{
    if (!validSuccessionKey(role) || !validSuccessionKey(deceased) || !validSuccessionKey(cause)) {
        return {SuccessionStatus::InvalidKey, {}};
    }
    const auto holder = roleHolder(ledger, role);
    if (!holder || *holder != deceased) return {SuccessionStatus::NotHolder, {}};

    std::set<std::string, std::less<>> busy;
    for (const auto& entry : roleHolders(ledger)) busy.insert(entry.second);
    std::vector<std::string> eligible;
    for (const std::string& id : living) {
        if (id != deceased && !busy.contains(id) && validSuccessionKey(id)) eligible.push_back(id);
    }
    const auto successor = chooseSuccessor(deceased, edges, eligible);

    const std::string vacatedId = factId("vacated", tick, role, deceased);
    const std::string filledId = factId("filled", tick, role, successor.value_or(""));
    const std::string inheritedId = factId("inherited", tick, role, successor.value_or(""));
    if (ledger.find(vacatedId) != nullptr || (successor && (ledger.find(filledId) != nullptr || ledger.find(inheritedId) != nullptr))) {
        return {SuccessionStatus::DuplicateId, {}};
    }
    if (ledger.size() + (successor ? 3 : 1) > ConsequenceLedger::kMaxFacts) return {SuccessionStatus::LedgerFull, {}};

    const std::string roleKey(role);
    (void)ledger.append({vacatedId, roleKey, std::string(kVacated), std::string(deceased) + ";" + std::string(cause), tick});
    if (!successor) return {SuccessionStatus::Vacant, {}};
    (void)ledger.append({filledId, roleKey, std::string(kFilled), *successor, tick});
    (void)ledger.append(
        {inheritedId, *successor, std::string(kInherited), roleKey + ";" + std::string(deceased) + ";" + std::string(cause), tick});
    return {SuccessionStatus::Succeeded, *successor};
}

std::vector<InheritedRole> inheritedRoles(const ConsequenceLedger& ledger, std::string_view npc)
{
    std::vector<InheritedRole> out;
    for (const Fact* fact : ledger.bySubjectAndKind(npc, kInherited)) {
        const auto parts = splitFields(fact->value);
        if (parts.size() != 3 || !validSuccessionKey(parts[0]) || !validSuccessionKey(parts[1]) || !validSuccessionKey(parts[2])) continue;
        out.push_back({parts[0], parts[1], parts[2], fact->tick});
    }
    return out;
}

}
