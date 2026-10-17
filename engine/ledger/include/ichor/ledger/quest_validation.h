#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ichor {

// Pass 4 of Tier 1 quest generation (doc 04): validate a bound quest and discard
// failures. Pure and integer-only, so the same inputs give the same verdict on
// every platform. The expand and bind passes do not exist yet, so the input is a
// minimal bound shape of its own: nodes already resolved to entities, linked by
// "after" edges that point at earlier steps.

// What a node believes about the world when it runs. A contradiction is a ledger
// whose latest fact for subject and kind says something else; no fact at all is
// not a contradiction.
struct QuestAssumption {
    std::string subject;
    std::string kind;
    std::string value;
};

struct QuestNode {
    std::string id;
    std::string entity; // the persistent faction, NPC, site or road this node is bound to
    std::uint32_t cost = 0; // difficulty points the node adds to the quest
    bool refusable = false; // the player can walk away or side with the other party here
    std::vector<std::string> after; // node ids that must be done first
    std::vector<QuestAssumption> assumes;
};

struct BoundQuest {
    std::string id;
    std::vector<QuestNode> nodes;
};

enum class QuestIssueKind : std::uint8_t {
    TooLarge,          // more nodes, edges or assumptions than the limits allow
    Empty,             // no nodes
    DuplicateNode,     // two nodes share an id
    UnknownDependency, // after names a node that does not exist
    UnboundEntity,     // entity is empty or not in the known entity set
    Unreachable,       // never startable: on a cycle, or behind a node that is
    LedgerContradiction,
    OverBudget,
    NoRefusableNode,
};

struct QuestIssue {
    QuestIssueKind kind = QuestIssueKind::Empty;
    std::string node; // the node concerned, empty for quest-wide issues
    std::string detail;
};

struct QuestValidationInputs {
    // Not owned; null means an empty ledger.
    const ConsequenceLedger* ledger = nullptr;
    // Entities that exist in the world. A node bound to anything else fails.
    const std::set<std::string, std::less<>>* knownEntities = nullptr;
    std::uint64_t budget = 0;
};

constexpr std::size_t kMaxQuestNodes = 256;
constexpr std::size_t kMaxQuestEdges = 4096;
constexpr std::size_t kMaxQuestAssumptions = 4096;

struct QuestVerdict {
    bool valid = false;
    std::uint64_t totalCost = 0;
    std::vector<QuestIssue> issues; // in a fixed order: by check, then by node order
};

// A quest is valid only with no issues. Never throws; hostile input yields issues.
QuestVerdict validateQuest(const BoundQuest& quest, const QuestValidationInputs& inputs);

}
