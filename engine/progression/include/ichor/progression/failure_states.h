#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 07: losing a fight can cost cargo, standing, a companion, a route or
// world-clock time instead of the player's life. Each loss is one ledger fact,
// so a loaded ledger gives the same history and quests can read it.
//
//   failure-<kind>  subject = what was lost, value = "<cause>;<amount>"
//
// Subject is the cargo item, faction, companion or route id, and "world-clock"
// by convention for time. Amount is units of cargo, points of standing or
// ticks of time, and is 0 for a companion or a route (they are lost whole).

enum class FailureKind : std::uint8_t { Cargo, Standing, Companion, Route, Time };

constexpr std::size_t kMaxFailureKeyBytes = 40;
constexpr std::uint32_t kMaxFailureAmount = 1000000000;

struct FailureEvent {
    FailureKind kind = FailureKind::Cargo;
    std::string subject;
    std::string cause;
    std::uint32_t amount = 0;
    std::uint64_t tick = 0;
};

enum class [[nodiscard]] RecordResult { Ok, InvalidKey, InvalidAmount, DuplicateId, LedgerFull };

// Subject and cause use lowercase letters, digits and - _ . : (at most
// kMaxFailureKeyBytes). Cargo, standing and time need an amount of 1 to
// kMaxFailureAmount; a companion or route needs 0. Two identical events are
// one duplicate. On any result but Ok the ledger is unchanged.
RecordResult recordFailure(ConsequenceLedger& ledger, const FailureEvent& event);

struct RecordedFailure {
    FailureKind kind = FailureKind::Cargo;
    std::string subject;
    std::string cause;
    std::uint32_t amount = 0;
    std::uint64_t tick = 0;
};

// In ledger order. Facts of these kinds that are malformed (a hand edited or
// foreign ledger) are skipped.
std::vector<RecordedFailure> failures(const ConsequenceLedger& ledger);
std::vector<RecordedFailure> failures(const ConsequenceLedger& ledger, FailureKind kind);
std::vector<RecordedFailure> failuresFor(const ConsequenceLedger& ledger, std::string_view subject);
std::vector<RecordedFailure> failuresFor(const ConsequenceLedger& ledger, std::string_view subject, FailureKind kind);

// The sum of amounts. 64 bits, so it cannot overflow within the ledger's size limit.
std::uint64_t totalLost(const ConsequenceLedger& ledger, FailureKind kind, std::string_view subject);

}
