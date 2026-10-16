#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstdint>
#include <string>

namespace ichor {

// Doc 15: low TTK plus co-op means frequent deaths, so a downed player can be
// revived, but never for free. Starting a revive spends the reviver's charges
// and writes the cargo lost and the world-clock time lost to the ledger as
// failure facts (failure_states.h). The cost is paid when the revive starts and
// is not refunded if it is cancelled or the downed player bleeds out first.
//
//   Alive -> Downed -> Reviving -> Alive
//               |          |
//               +----------+--> Dead
//
// The bleed-out timer keeps running while a revive is in progress. When the
// revive and the timer finish in the same tick, the revive wins.

enum class DownedStatus : std::uint8_t { Alive, Downed, Reviving, Dead };

struct ReviveCost {
    std::uint32_t charges = 1;
    std::string cargoSubject;
    std::uint32_t cargoAmount = 1;
    std::uint32_t timeTicks = 1;
};

struct DownedConfig {
    double bleedOutSeconds = 30.0;
    double reviveSeconds = 5.0;
    ReviveCost cost;
};

constexpr double kMaxDownedSeconds = 3600.0;

enum class [[nodiscard]] DownedResult {
    Ok,
    InvalidConfig,
    InvalidState,
    InvalidTick,
    NotEnoughCharges,
    LedgerRejected,
};

// The reviver's spendable charges. Untouched on any result but Ok.
struct ReviveResources {
    std::uint32_t charges = 0;
};

// Valid when both durations are finite and in (0, kMaxDownedSeconds], charges
// and both ledger amounts are at least 1, and the cargo subject is a valid
// failure key.
bool validConfig(const DownedConfig& config);

class DownedMachine {
public:
    // An invalid config makes every command return InvalidConfig.
    explicit DownedMachine(DownedConfig config);

    DownedStatus status() const { return status_; }
    double bleedRemaining() const { return bleedRemaining_; }
    double reviveRemaining() const { return reviveRemaining_; }

    // Alive to Downed. Starts the bleed-out timer.
    DownedResult down();

    // Downed to Reviving. Spends the charges and appends the cargo and time
    // failure facts at `worldTick`. All or nothing: on any other result the
    // resources, the ledger and the state are unchanged. A second revive while
    // one is running is InvalidState and costs nothing.
    DownedResult beginRevive(ReviveResources& resources, ConsequenceLedger& ledger, std::uint64_t worldTick);

    // Reviving to Downed. The cost stays spent.
    DownedResult cancelRevive();

    // Advances time by `seconds` (finite, not negative, at most
    // kMaxDownedSeconds; anything else is InvalidTick and changes nothing). A
    // no-op in Alive and Dead.
    DownedResult tick(double seconds);

private:
    DownedConfig config_;
    bool valid_;
    DownedStatus status_ = DownedStatus::Alive;
    double bleedRemaining_ = 0.0;
    double reviveRemaining_ = 0.0;
};

}
