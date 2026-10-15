#pragma once

#include "ichor/economy/wallet.h"
#include "ichor/progression/capabilities.h"

#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One thing a trainer teaches: the capability, its price and the days it takes.
struct TrainerOffer {
    std::string capability;
    std::uint64_t cost = 0;
    std::uint64_t days = 0;
};

// A person who sells capabilities. The player must have found them, and the
// standing gate (a FactionStanding gate id) must be open. Gate ids are not
// checked here: the caller owns the standing data.
struct Trainer {
    std::string id;
    std::string name;
    std::string gate;
    CurrencySlot currency = CurrencySlot::Coin;
    std::string sink; // must be a spend sink of `currency`
    std::vector<TrainerOffer> offers;
};

// Parses a JSON array with one record per offer (data tables hold flat records):
// {"id" (unique per record), "trainer", "name", "gate", "currency": "coin"|"hard",
// "sink", "capability", "cost", "days"}. Records with the same "trainer" form one
// Trainer and must agree on name, gate, currency and sink. Every capability must
// be in `table`, no trainer repeats one, and cost and days are non-negative
// integers up to kMaxCurrencyCap. `out` is sorted by trainer id. Problems are
// appended as "<label>: <path>: <what is wrong>". Returns true, and fills `out`,
// only when there are none. Never throws on bad input.
bool parseTrainers(std::string_view label, std::string_view text, const std::vector<Capability>& table, std::vector<Trainer>& out, std::vector<std::string>& problems);

enum class TrainStatus {
    Done,
    NotFound,
    GateClosed,
    NotOffered,
    AlreadyUnlocked,
    PrerequisiteUnmet,
    SinkNotAllowed,
    Insufficient,
};

struct TrainQuote {
    TrainStatus status = TrainStatus::Done;
    std::uint64_t cost = 0;
    // Time the lesson takes; the caller advances the world clock by it.
    std::uint64_t days = 0;
};

using TrainerGate = std::function<bool(std::string_view)>;
using FoundTrainers = std::set<std::string, std::less<>>;

// What learning `capability` from `trainer` would cost. Changes nothing.
// `found` holds the ids of trainers the player has met; `gateOpen` answers
// whether a standing gate id is open (FactionStanding::allowed). Checks run in
// the order of TrainStatus, so the first failing one is reported.
TrainQuote quoteTraining(const Trainer& trainer, std::string_view capability, const FoundTrainers& found, const TrainerGate& gateOpen, const CapabilityUnlocks& unlocks,
    const Wallet& wallet, const CurrencyTable& currencies);

// Charges the wallet and unlocks the capability, or changes nothing and says why.
[[nodiscard]] TrainQuote train(const Trainer& trainer, std::string_view capability, const FoundTrainers& found, const TrainerGate& gateOpen, CapabilityUnlocks& unlocks, Wallet& wallet,
    const CurrencyTable& currencies);

}
