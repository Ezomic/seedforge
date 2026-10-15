#pragma once

#include "ichor/economy/wallet.h"
#include "ichor/progression/capabilities.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// One place the player can go to unlearn capabilities (a trainer, a faction
// favour, a ritual). Every number is data: the price is
// baseCost + perCapabilityCost * removed, less refundPerCapability * removed
// (never below zero), and the player must have travelled at least minTravel.
struct RespecService {
    std::string id;
    std::string name;
    CurrencySlot currency = CurrencySlot::Coin;
    std::string sink; // must be a spend sink of `currency`
    std::uint64_t baseCost = 0;
    std::uint64_t perCapabilityCost = 0;
    std::uint64_t refundPerCapability = 0;
    std::uint64_t minTravel = 0;
};

// Parses a JSON array of services, each {"id", "name", "currency": "coin"|"hard",
// "sink", "base_cost", "per_capability_cost", "refund_per_capability",
// "min_travel"}; the last four are non-negative integers up to kMaxCurrencyCap.
// Problems are appended as "<label>: <path>: <what is wrong>". Returns true, and
// fills `out`, only when there are none. Never throws on bad input.
bool parseRespecServices(std::string_view label, std::string_view text, std::vector<RespecService>& out, std::vector<std::string>& problems);

enum class RespecStatus {
    Done,
    UnknownCapability,
    NotUnlocked,
    NothingToRespec,
    TooClose,
    SinkNotAllowed,
    Insufficient,
};

struct RespecQuote {
    RespecStatus status = RespecStatus::Done;
    // Everything that would be lost: the requested ids plus every unlocked
    // capability that depends on one of them. Sorted by id.
    std::vector<std::string> removed;
    std::uint64_t cost = 0;
};

// What respeccing `targets` would remove and cost. Changes nothing. Status is
// Done when it could be bought now; otherwise it says why not.
RespecQuote quoteRespec(const RespecService& service, const std::vector<Capability>& table, const CapabilityUnlocks& unlocks, const std::vector<std::string>& targets, const Wallet& wallet,
    const CurrencyTable& currencies, std::uint64_t travelled);

// Charges the wallet and re-locks the quoted capabilities, or changes nothing
// and says why. Capabilities that were not named but depend on a removed one
// are removed too, so the unlocked set always stays prerequisite-closed.
[[nodiscard]] RespecQuote respec(const RespecService& service, const std::vector<Capability>& table, CapabilityUnlocks& unlocks, const std::vector<std::string>& targets, Wallet& wallet,
    const CurrencyTable& currencies, std::uint64_t travelled);

}
