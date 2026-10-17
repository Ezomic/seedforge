#pragma once

#include "ichor/economy/wallet.h"

#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace ichor {

// Doc 04 Tier 0: an honest job board. Every offer is a plain bounty or
// delivery with a flat coin price and no story. Nobody can grind it because
// supply is capped (a fixed number of offers per period, a limit on contracts
// held, a limit on payouts per period) and the price never scales with
// repetition. Time is a tick count the caller supplies. Whether a job was
// actually done is the caller's business; the board only pays out.

enum class ContractKind : std::uint8_t { Bounty, Delivery };

constexpr std::uint32_t kMaxBoardSlots = 64;
constexpr std::uint32_t kMaxBoardTemplates = 256;
constexpr std::uint32_t kMaxBoardIssuers = 64;
constexpr std::uint32_t kMaxContractQuantity = 1000000;
constexpr std::uint64_t kMaxUnitPrice = 1000000000;
constexpr std::uint64_t kMaxBoardPeriod = 1ull << 24; // ticks
constexpr std::uint64_t kMaxPeriodIndex = 1ull << 32;
constexpr std::uint32_t kMaxGracePeriods = 16;
constexpr std::uint32_t kMaxLiveContracts = 256;

struct ContractTemplate {
    ContractKind kind = ContractKind::Bounty;
    std::string target; // an enemy id for a bounty, an item id for a delivery
    std::uint32_t minQuantity = 1;
    std::uint32_t maxQuantity = 1;
    std::uint64_t unitPrice = 0; // coin per unit, flat
};

struct BoardConfig {
    std::uint64_t periodTicks = 0;           // how long one batch of offers lasts
    std::uint32_t slots = 0;                 // offers posted per period
    std::uint32_t maxActive = 0;             // contracts the player may hold at once
    std::uint32_t maxPayoutsPerPeriod = 0;   // paid completions allowed per period
    std::uint32_t gracePeriods = 0;          // extra periods an accepted contract stays valid
    std::vector<ContractTemplate> templates; // non-empty
    std::vector<std::string> issuers;        // faction ids, distinct and non-empty, non-empty list
};

struct Contract {
    std::uint64_t id = 0; // period in the high bits, slot in the low 16
    ContractKind kind = ContractKind::Bounty;
    std::string issuer;
    std::string target;
    std::uint32_t quantity = 0;
    std::uint64_t reward = 0; // coin
    std::uint64_t expiresAt = 0; // first tick at which it is no longer valid

    friend bool operator==(const Contract&, const Contract&) = default;
};

// One plain sentence stating the job and the price, nothing else.
std::string describe(const Contract& contract);

enum class BoardStatus {
    Ok,
    UnknownContract,
    AlreadyTaken,
    TooManyActive,
    WalletFull, // the coin cap is reached; the contract stays held and uses no payout
    PayoutCapReached, // the contract stays held and can be completed next period
    NotAllowed,       // the currency table forbids the "quest" source
    TimeInvalid, // the clock went back, or past the last period the ids can name
};

struct CompleteResult {
    BoardStatus status = BoardStatus::Ok;
    std::uint64_t credited = 0;
};

class ContractBoard {
public:
    // Fails with a message for a zero period, zero slots, an empty or oversized
    // list, duplicate issuers, a template with no target, an inverted or zero
    // quantity range, a zero price or an over-limit value.
    static std::optional<ContractBoard> create(BoardConfig config, std::uint64_t seed, std::string& error);

    // The offers for any period: a pure function of seed and period, so the
    // same on every machine and across save and load.
    std::vector<Contract> offersFor(std::uint64_t period) const;

    std::uint64_t period() const { return period_; }
    // What is still open to take this period, in slot order.
    std::vector<Contract> openOffers() const;
    const std::vector<Contract>& held() const { return held_; }
    std::uint32_t payoutsThisPeriod() const { return payouts_; }

    // Moves the clock. Time never goes back. A new period reopens the board
    // and resets the payout count; expired held contracts are dropped.
    [[nodiscard]] BoardStatus advanceTo(std::uint64_t tick);

    [[nodiscard]] BoardStatus accept(std::uint64_t id);
    [[nodiscard]] BoardStatus abandon(std::uint64_t id);
    // Pays the flat reward in coin from source "quest" (Tier 0 shares the quest income source).
    [[nodiscard]] CompleteResult complete(std::uint64_t id, Wallet& wallet, const CurrencyTable& table);

    // Little-endian bytes: a version, a fingerprint of config and seed, the clock, the payout count, the taken offers and the ids
    // of held contracts (each is rebuilt from its offer). Bytes saved under a different config or seed are refused.
    std::vector<std::uint8_t> serialise() const;
    // Nothing for truncated, trailing, wrong-version, over-limit or inconsistent bytes.
    [[nodiscard]] static std::optional<ContractBoard> deserialise(std::span<const std::uint8_t> bytes, BoardConfig config, std::uint64_t seed);

    std::uint64_t digest() const;

private:
    ContractBoard() = default;

    BoardConfig config_;
    std::uint64_t seed_ = 0;
    std::uint64_t tick_ = 0;
    std::uint64_t period_ = 0;
    std::uint32_t payouts_ = 0;
    std::set<std::uint64_t> takenThisPeriod_; // offers of the current period already accepted
    std::vector<Contract> held_;              // sorted by id
};

}
