#include "ichor/ledger/quest_validation.h"

#include <algorithm>
#include <string_view>

namespace ichor {

namespace {

void add(QuestVerdict& verdict, QuestIssueKind kind, const std::string& node, std::string detail)
{
    verdict.issues.push_back({kind, node, std::move(detail)});
}

bool tooLarge(const BoundQuest& quest)
{
    if (quest.nodes.size() > kMaxQuestNodes) {
        return true;
    }
    std::size_t edges = 0;
    std::size_t assumptions = 0;
    for (const QuestNode& node : quest.nodes) {
        edges += node.after.size();
        assumptions += node.assumes.size();
        if (edges > kMaxQuestEdges || assumptions > kMaxQuestAssumptions) {
            return true;
        }
    }
    return false;
}

// A node can run once every node it after can. Nodes on a cycle, or behind
// an unknown or unreachable dependency, never become reachable.
std::set<std::string, std::less<>> reachableIds(const BoundQuest& quest)
{
    std::set<std::string, std::less<>> reachable;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const QuestNode& node : quest.nodes) {
            if (reachable.contains(node.id)) {
                continue;
            }
            const bool ready = std::all_of(node.after.begin(), node.after.end(),
                [&](const std::string& id) { return reachable.contains(id); });
            if (ready) {
                reachable.insert(node.id);
                changed = true;
            }
        }
    }
    return reachable;
}

bool contradicts(const ConsequenceLedger& ledger, const QuestAssumption& assumption)
{
    const std::vector<const Fact*> facts = ledger.bySubjectAndKind(assumption.subject, assumption.kind);
    return !facts.empty() && facts.back()->value != assumption.value;
}

}

QuestVerdict validateQuest(const BoundQuest& quest, const QuestValidationInputs& inputs)
{
    QuestVerdict verdict;

    if (tooLarge(quest)) {
        add(verdict, QuestIssueKind::TooLarge, "", "node, edge or assumption limit exceeded");
        return verdict;
    }
    if (quest.nodes.empty()) {
        add(verdict, QuestIssueKind::Empty, "", "quest has no nodes");
        return verdict;
    }

    std::set<std::string, std::less<>> ids;
    for (const QuestNode& node : quest.nodes) {
        if (!ids.insert(node.id).second) {
            add(verdict, QuestIssueKind::DuplicateNode, node.id, "node id used twice");
        }
    }

    for (const QuestNode& node : quest.nodes) {
        for (const std::string& dependency : node.after) {
            if (!ids.contains(dependency)) {
                add(verdict, QuestIssueKind::UnknownDependency, node.id, "after unknown node " + dependency);
            }
        }
    }

    for (const QuestNode& node : quest.nodes) {
        const bool known = inputs.knownEntities != nullptr && inputs.knownEntities->contains(node.entity);
        if (node.entity.empty() || !known) {
            add(verdict, QuestIssueKind::UnboundEntity, node.id, "entity is not a known world entity");
        }
    }

    const std::set<std::string, std::less<>> reachable = reachableIds(quest);
    for (const QuestNode& node : quest.nodes) {
        if (!reachable.contains(node.id)) {
            add(verdict, QuestIssueKind::Unreachable, node.id, "can never start");
        }
    }

    if (inputs.ledger != nullptr) {
        for (const QuestNode& node : quest.nodes) {
            for (const QuestAssumption& assumption : node.assumes) {
                if (contradicts(*inputs.ledger, assumption)) {
                    add(verdict, QuestIssueKind::LedgerContradiction, node.id,
                        "ledger says " + assumption.subject + " " + assumption.kind + " is not " + assumption.value);
                }
            }
        }
    }

    for (const QuestNode& node : quest.nodes) {
        verdict.totalCost += node.cost;
    }
    if (verdict.totalCost > inputs.budget) {
        add(verdict, QuestIssueKind::OverBudget, "", "total cost exceeds the budget");
    }

    const bool anyRefusable = std::any_of(quest.nodes.begin(), quest.nodes.end(), [](const QuestNode& n) { return n.refusable; });
    if (!anyRefusable) {
        add(verdict, QuestIssueKind::NoRefusableNode, "", "the player has no way to refuse");
    }

    verdict.valid = verdict.issues.empty();
    return verdict;
}

}
