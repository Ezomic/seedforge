#pragma once

#include "ichor/ledger/consequence_ledger.h"
#include "ichor/npc/relationships.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 10: roles are the durable thing, people are instances. Who holds a role,
// and how it became vacant, lives in the consequence ledger as facts, so a
// loaded ledger gives the same holders and the same memories.
//
//   role-vacated    subject = role, value = "<deceased>;<cause>"
//   role-filled     subject = role, value = "<holder>"
//   inherited-role  subject = successor, value = "<role>;<predecessor>;<cause>"

constexpr std::size_t kMaxSuccessionKeyBytes = 40;

// Ids and causes use lowercase letters, digits and - _ . : (the ledger's keys),
// at most kMaxSuccessionKeyBytes.
bool validSuccessionKey(std::string_view text);

// The holder after replaying the role's facts in order, or none when it is
// vacant or was never filled. This is what the quest binder asks.
std::optional<std::string> roleHolder(const ConsequenceLedger& ledger, std::string_view role);

// Role to holder for every filled role.
std::map<std::string, std::string, std::less<>> roleHolders(const ConsequenceLedger& ledger);

// Puts a holder into a vacant role. One NPC may hold several roles; each vacancy is succeeded on its own.
enum class [[nodiscard]] FillResult { Ok, InvalidKey, RoleTaken, DuplicateId, LedgerFull };
FillResult fillRole(ConsequenceLedger& ledger, std::string_view role, std::string_view npc, std::uint64_t tick);

// The best living candidate from the deceased's relationship web, or none.
// Family beats rival beats lover beats creditor and grudge; a stronger tie
// beats a weaker one; the smaller id breaks a remaining tie. Only ids in
// `eligible` can win (the deceased never can). Input order does not matter.
std::optional<std::string> chooseSuccessor(
    std::string_view deceased, const std::vector<RelationshipEdge>& edges, const std::vector<std::string>& eligible);

enum class [[nodiscard]] SuccessionStatus { Succeeded, Vacant, InvalidKey, NotHolder, DuplicateId, LedgerFull };

struct SuccessionOutcome {
    SuccessionStatus status = SuccessionStatus::InvalidKey;
    std::string successor; // set only when Succeeded
};

// The holder of `role` has died of `cause` at `tick`. Records the vacancy, and
// when a successor exists (a living Regular in the web who holds no role) the
// new holder and what they remember. `living` lists every NPC still alive;
// anyone already holding a role is skipped. All or nothing: on any status
// but Succeeded or Vacant the ledger is unchanged.
SuccessionOutcome succeedRole(ConsequenceLedger& ledger, std::string_view role, std::string_view deceased, std::string_view cause,
    std::uint64_t tick, const std::vector<RelationshipEdge>& edges, const std::vector<std::string>& living);

struct InheritedRole {
    std::string role;
    std::string predecessor;
    std::string cause;
    std::uint64_t tick = 0;
};

// What `npc` remembers about how they came by their roles, in ledger order.
std::vector<InheritedRole> inheritedRoles(const ConsequenceLedger& ledger, std::string_view npc);

}
