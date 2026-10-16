#include "ichor/progression/downed_revive.h"

#include "ichor/progression/failure_states.h"

#include <cmath>

namespace ichor {

namespace {

constexpr const char* kCause = "revive";
constexpr const char* kClockSubject = "world-clock";

bool validSeconds(double seconds) { return std::isfinite(seconds) && seconds > 0.0 && seconds <= kMaxDownedSeconds; }

}

bool validConfig(const DownedConfig& config)
{
    const ReviveCost& cost = config.cost;
    if (!validSeconds(config.bleedOutSeconds) || !validSeconds(config.reviveSeconds)) return false;
    if (cost.charges < 1 || cost.timeTicks < 1 || cost.timeTicks > kMaxFailureAmount) return false;
    if (cost.cargoAmount < 1 || cost.cargoAmount > kMaxFailureAmount) return false;
    ConsequenceLedger probe;
    return recordFailure(probe, FailureEvent{FailureKind::Cargo, cost.cargoSubject, kCause, cost.cargoAmount, 0}) ==
        RecordResult::Ok;
}

DownedMachine::DownedMachine(DownedConfig config)
    : config_(std::move(config))
    , valid_(validConfig(config_))
{
}

DownedResult DownedMachine::down()
{
    if (!valid_) return DownedResult::InvalidConfig;
    if (status_ != DownedStatus::Alive) return DownedResult::InvalidState;
    status_ = DownedStatus::Downed;
    bleedRemaining_ = config_.bleedOutSeconds;
    reviveRemaining_ = 0.0;
    return DownedResult::Ok;
}

DownedResult DownedMachine::beginRevive(ReviveResources& resources, ConsequenceLedger& ledger, std::uint64_t worldTick)
{
    if (!valid_) return DownedResult::InvalidConfig;
    if (status_ != DownedStatus::Downed) return DownedResult::InvalidState;
    if (resources.charges < config_.cost.charges) return DownedResult::NotEnoughCharges;

    ConsequenceLedger staged = ledger;
    const FailureEvent cargo{FailureKind::Cargo, config_.cost.cargoSubject, kCause, config_.cost.cargoAmount, worldTick};
    const FailureEvent time{FailureKind::Time, kClockSubject, kCause, config_.cost.timeTicks, worldTick};
    if (recordFailure(staged, cargo) != RecordResult::Ok) return DownedResult::LedgerRejected;
    if (recordFailure(staged, time) != RecordResult::Ok) return DownedResult::LedgerRejected;

    ledger = std::move(staged);
    resources.charges -= config_.cost.charges;
    status_ = DownedStatus::Reviving;
    reviveRemaining_ = config_.reviveSeconds;
    return DownedResult::Ok;
}

DownedResult DownedMachine::cancelRevive()
{
    if (!valid_) return DownedResult::InvalidConfig;
    if (status_ != DownedStatus::Reviving) return DownedResult::InvalidState;
    status_ = DownedStatus::Downed;
    reviveRemaining_ = 0.0;
    return DownedResult::Ok;
}

DownedResult DownedMachine::tick(double seconds)
{
    if (!valid_) return DownedResult::InvalidConfig;
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > kMaxDownedSeconds) return DownedResult::InvalidTick;
    if (status_ == DownedStatus::Reviving && reviveRemaining_ <= seconds && reviveRemaining_ <= bleedRemaining_) {
        status_ = DownedStatus::Alive;
        reviveRemaining_ = 0.0;
        bleedRemaining_ = 0.0;
        return DownedResult::Ok;
    }
    if (status_ == DownedStatus::Downed || status_ == DownedStatus::Reviving) {
        bleedRemaining_ -= seconds;
        if (status_ == DownedStatus::Reviving) reviveRemaining_ -= seconds;
        if (bleedRemaining_ <= 0.0) {
            status_ = DownedStatus::Dead;
            bleedRemaining_ = 0.0;
            reviveRemaining_ = 0.0;
        }
    }
    return DownedResult::Ok;
}

}
