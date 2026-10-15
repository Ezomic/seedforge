#pragma once

#include "ichor/economy/wallet.h"
#include "ichor/factions/standing.h"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Doc 06 sink: access is rented, not bought. A faction may charge dues (a lease
// that opens its gates for a while) and may take bribes (a dearer, shorter pass
// through one gate the player's standing does not yet open). Both are paid in
// coin under the "dues" and "bribe" sinks, which the currency table must allow.
// Time is a tick count the caller supplies; nothing here reads a clock.

constexpr std::uint64_t kMaxFeePeriod = 1ull << 40;
constexpr std::uint32_t kMaxBribeTiers = 64;
// A lease cannot be stacked further ahead than this many periods from now.
constexpr std::uint64_t kMaxPrepaidPeriods = 3;

struct FactionFee {
    std::string faction;
    // Dues: zero amount means the faction charges none.
    std::uint64_t duesAmount = 0;
    std::uint64_t duesPeriod = 0; // ticks one payment lasts
    std::string duesMinTier;      // below this tier the faction refuses dues
    // Bribes: zero duration means the faction takes none.
    std::uint64_t bribeBase = 0;
    std::uint64_t bribePerTier = 0; // added per tier the standing is short of the gate
    std::uint64_t bribeDuration = 0;
    std::uint32_t bribeMaxTiers = 0; // further short than this and nobody can be bought
};

enum class FeeStatus {
    Ok,
    UnknownFaction,
    UnknownGate,
    NotOffered, // the faction takes no dues, or no bribes
    StandingTooLow,
    NotNeeded,   // the gate is already open
    AlreadyPaid, // the lease is as far ahead as it may go
    TimeOverflow,
    NotAllowed, // the currency table forbids the sink
    Insufficient,
};

struct BribeQuote {
    FeeStatus status = FeeStatus::Ok;
    std::uint64_t price = 0;
};

class AccessFees {
public:
    // Every fee names a known faction once; dues need amount and period (both
    // positive, period within kMaxFeePeriod) and a known min tier; bribes need
    // a positive base, duration within kMaxFeePeriod and 1 to kMaxBribeTiers
    // tiers; no amount exceeds kMaxCurrencyCap. Nothing for any violation.
    static std::optional<AccessFees> create(std::vector<FactionFee> fees, std::vector<StandingTier> tiers, std::vector<StandingGate> gates,
        const std::vector<Faction>& factions, std::string& error);

    // Open by standing, by an unexpired lease the standing still qualifies for,
    // or by an unexpired bribe.
    bool hasAccess(const FactionStanding& standing, std::string_view gate, std::uint64_t now) const;

    // What a bribe through `gate` costs now, or why none can be bought.
    BribeQuote quoteBribe(const FactionStanding& standing, std::string_view gate, std::uint64_t now) const;

    // All or nothing: a refused payment changes neither wallet nor lease.
    [[nodiscard]] FeeStatus payDues(Wallet& wallet, const CurrencyTable& table, const FactionStanding& standing, std::string_view faction, std::uint64_t now);
    [[nodiscard]] FeeStatus payBribe(Wallet& wallet, const CurrencyTable& table, const FactionStanding& standing, std::string_view gate, std::uint64_t now);

    // Tick a lease or pass runs until (exclusive); nullopt when none was ever bought.
    std::optional<std::uint64_t> duesUntil(std::string_view faction) const;
    std::optional<std::uint64_t> bribeUntil(std::string_view gate) const;

    // Version, then each lease (faction id, end) in id order, then each pass (gate id, end).
    std::vector<std::uint8_t> serialise() const;
    std::uint64_t digest() const;
    // Replaces the leases and passes, or returns false and changes nothing for
    // truncated, trailing, wrong-version bytes, ids that differ from this table, or ends too near the u64 limit.
    [[nodiscard]] bool deserialise(std::span<const std::uint8_t> bytes);

private:
    AccessFees() = default;

    const FactionFee* feeFor(std::string_view faction) const;
    std::optional<std::size_t> tierIndex(std::string_view tierId) const;
    std::optional<std::size_t> standingIndex(const FactionStanding& standing, std::string_view faction) const;

    std::vector<FactionFee> fees_;
    std::vector<StandingTier> tiers_;
    std::map<std::string, StandingGate, std::less<>> gates_;
    std::map<std::string, std::uint64_t, std::less<>> dues_;
    std::map<std::string, std::uint64_t, std::less<>> bribes_;
};

}
