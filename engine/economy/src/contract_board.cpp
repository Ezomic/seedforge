#include "ichor/economy/contract_board.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <algorithm>

namespace ichor {

namespace {

constexpr std::uint32_t kBoardVersion = 1;
constexpr unsigned kSlotBits = 16;

std::uint64_t contractId(std::uint64_t period, std::uint32_t slot)
{
    return (period << kSlotBits) | slot;
}

std::uint64_t periodOf(std::uint64_t id)
{
    return id >> kSlotBits;
}

std::uint32_t slotOf(std::uint64_t id)
{
    return static_cast<std::uint32_t>(id & ((1u << kSlotBits) - 1));
}

bool validate(const BoardConfig& config, std::string& error)
{
    if (config.periodTicks == 0 || config.periodTicks > kMaxBoardPeriod) {
        error = "periodTicks must be 1 to kMaxBoardPeriod";
    } else if (config.slots == 0 || config.slots > kMaxBoardSlots) {
        error = "slots must be 1 to kMaxBoardSlots";
    } else if (config.maxActive == 0 || config.maxActive > kMaxLiveContracts) {
        error = "maxActive must be 1 to kMaxLiveContracts";
    } else if (config.maxPayoutsPerPeriod == 0 || config.maxPayoutsPerPeriod > kMaxLiveContracts) {
        error = "maxPayoutsPerPeriod must be 1 to kMaxLiveContracts";
    } else if (config.gracePeriods > kMaxGracePeriods) {
        error = "gracePeriods is over the limit";
    } else if (config.templates.empty() || config.templates.size() > kMaxBoardTemplates) {
        error = "templates must hold 1 to kMaxBoardTemplates entries";
    } else if (config.issuers.empty() || config.issuers.size() > kMaxBoardIssuers) {
        error = "issuers must hold 1 to kMaxBoardIssuers entries";
    }
    if (!error.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < config.issuers.size(); ++i) {
        const auto first = config.issuers.begin();
        const auto self = first + static_cast<std::ptrdiff_t>(i);
        if (config.issuers[i].empty() || std::find(first, self, config.issuers[i]) != self) {
            error = "issuers must be distinct and non-empty";
            return false;
        }
    }
    for (const ContractTemplate& t : config.templates) {
        if (t.target.empty()) {
            error = "a template needs a target";
        } else if (t.minQuantity == 0 || t.minQuantity > t.maxQuantity || t.maxQuantity > kMaxContractQuantity) {
            error = "a template quantity range must be 1 to kMaxContractQuantity, low to high";
        } else if (t.unitPrice == 0 || t.unitPrice > kMaxUnitPrice) {
            error = "a template unitPrice must be 1 to kMaxUnitPrice";
        } else if (t.kind != ContractKind::Bounty && t.kind != ContractKind::Delivery) {
            error = "a template has an unknown kind";
        }
        if (!error.empty()) {
            return false;
        }
    }
    return true;
}

std::uint64_t fingerprint(const BoardConfig& config, std::uint64_t seed)
{
    BinaryWriter writer;
    writer.write(seed);
    writer.write(config.periodTicks);
    writer.write(config.slots);
    writer.write(config.maxActive);
    writer.write(config.maxPayoutsPerPeriod);
    writer.write(config.gracePeriods);
    for (const ContractTemplate& t : config.templates) {
        writer.write(static_cast<std::uint8_t>(t.kind));
        writer.write(t.target);
        writer.write(t.minQuantity);
        writer.write(t.maxQuantity);
        writer.write(t.unitPrice);
    }
    for (const std::string& issuer : config.issuers) {
        writer.write(issuer);
    }
    const std::vector<std::uint8_t>& bytes = writer.bytes();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}

std::string describe(const Contract& contract)
{
    const std::string count = std::to_string(contract.quantity);
    const std::string job = (contract.kind == ContractKind::Bounty ? "Defeat " : "Deliver ") + count + " " + contract.target;
    return job + " for " + contract.issuer + ". Pays " + std::to_string(contract.reward) + " coin.";
}

std::optional<ContractBoard> ContractBoard::create(BoardConfig config, std::uint64_t seed, std::string& error)
{
    error.clear();
    if (!validate(config, error)) {
        return std::nullopt;
    }
    ContractBoard board;
    board.config_ = std::move(config);
    board.seed_ = seed;
    return board;
}

std::vector<Contract> ContractBoard::offersFor(std::uint64_t period) const
{
    std::vector<Contract> offers;
    if (period >= kMaxPeriodIndex) {
        return offers;
    }
    Rng rng(mix64(hashCombine(seed_, period)));
    offers.reserve(config_.slots);
    for (std::uint32_t slot = 0; slot < config_.slots; ++slot) {
        const auto templateIndex = rng.range(0, static_cast<std::uint32_t>(config_.templates.size() - 1));
        const auto issuerIndex = rng.range(0, static_cast<std::uint32_t>(config_.issuers.size() - 1));
        const ContractTemplate& t = config_.templates[templateIndex];
        Contract contract;
        contract.id = contractId(period, slot);
        contract.kind = t.kind;
        contract.issuer = config_.issuers[issuerIndex];
        contract.target = t.target;
        contract.quantity = rng.range(t.minQuantity, t.maxQuantity);
        contract.reward = static_cast<std::uint64_t>(contract.quantity) * t.unitPrice;
        contract.expiresAt = (period + 1 + config_.gracePeriods) * config_.periodTicks;
        offers.push_back(std::move(contract));
    }
    return offers;
}

std::vector<Contract> ContractBoard::openOffers() const
{
    std::vector<Contract> offers = offersFor(period_);
    std::erase_if(offers, [&](const Contract& c) { return takenThisPeriod_.contains(c.id); });
    return offers;
}

BoardStatus ContractBoard::advanceTo(std::uint64_t tick)
{
    if (tick < tick_ || tick / config_.periodTicks >= kMaxPeriodIndex) {
        return BoardStatus::TimeInvalid;
    }
    tick_ = tick;
    const std::uint64_t period = tick / config_.periodTicks;
    if (period != period_) {
        period_ = period;
        payouts_ = 0;
        takenThisPeriod_.clear();
    }
    std::erase_if(held_, [&](const Contract& c) { return tick_ >= c.expiresAt; });
    return BoardStatus::Ok;
}

BoardStatus ContractBoard::accept(std::uint64_t id)
{
    if (takenThisPeriod_.contains(id)) {
        return BoardStatus::AlreadyTaken;
    }
    if (periodOf(id) != period_ || slotOf(id) >= config_.slots) {
        return BoardStatus::UnknownContract;
    }
    if (held_.size() >= config_.maxActive) {
        return BoardStatus::TooManyActive;
    }
    std::vector<Contract> offers = offersFor(period_);
    Contract contract = std::move(offers[slotOf(id)]);
    takenThisPeriod_.insert(id);
    const auto at = std::lower_bound(held_.begin(), held_.end(), id, [](const Contract& c, std::uint64_t v) { return c.id < v; });
    held_.insert(at, std::move(contract));
    return BoardStatus::Ok;
}

BoardStatus ContractBoard::abandon(std::uint64_t id)
{
    const auto it = std::find_if(held_.begin(), held_.end(), [&](const Contract& c) { return c.id == id; });
    if (it == held_.end()) {
        return BoardStatus::UnknownContract;
    }
    held_.erase(it);
    return BoardStatus::Ok;
}

CompleteResult ContractBoard::complete(std::uint64_t id, Wallet& wallet, const CurrencyTable& table)
{
    const auto it = std::find_if(held_.begin(), held_.end(), [&](const Contract& c) { return c.id == id; });
    if (it == held_.end()) {
        return {BoardStatus::UnknownContract, 0};
    }
    if (payouts_ >= config_.maxPayoutsPerPeriod) {
        return {BoardStatus::PayoutCapReached, 0};
    }
    if (!table.canEarnFrom(CurrencySlot::Coin, "quest")) {
        return {BoardStatus::NotAllowed, 0};
    }
    if (wallet.balance(CurrencySlot::Coin) >= table.def(CurrencySlot::Coin).cap) {
        return {BoardStatus::WalletFull, 0};
    }
    const EarnResult earned = wallet.earn(table, CurrencySlot::Coin, it->reward, "quest");
    ++payouts_;
    held_.erase(it);
    return {BoardStatus::Ok, earned.credited};
}

std::vector<std::uint8_t> ContractBoard::serialise() const
{
    BinaryWriter writer;
    writer.write(kBoardVersion);
    writer.write(fingerprint(config_, seed_));
    writer.write(tick_);
    writer.write(payouts_);
    writer.write(static_cast<std::uint32_t>(takenThisPeriod_.size()));
    for (const std::uint64_t id : takenThisPeriod_) {
        writer.write(id);
    }
    writer.write(static_cast<std::uint32_t>(held_.size()));
    for (const Contract& c : held_) {
        writer.write(c.id);
    }
    return writer.take();
}

std::optional<ContractBoard> ContractBoard::deserialise(std::span<const std::uint8_t> bytes, BoardConfig config, std::uint64_t seed)
{
    std::string error;
    std::optional<ContractBoard> board = create(std::move(config), seed, error);
    if (!board) {
        return std::nullopt;
    }
    BinaryReader reader(bytes);
    std::uint32_t version = 0;
    std::uint64_t print = 0;
    std::uint64_t tick = 0;
    std::uint32_t payouts = 0;
    std::vector<std::uint64_t> taken;
    std::vector<std::uint64_t> heldIds;
    if (!reader.read(version) || version != kBoardVersion || !reader.read(print) || print != fingerprint(board->config_, seed)
        || !reader.read(tick) || !reader.read(payouts) || !reader.read(taken)
        || !reader.read(heldIds) || !reader.atEnd()) {
        return std::nullopt;
    }
    if (board->advanceTo(tick) != BoardStatus::Ok || payouts > board->config_.maxPayoutsPerPeriod
        || taken.size() > board->config_.slots || heldIds.size() > board->config_.maxActive) {
        return std::nullopt;
    }
    board->payouts_ = payouts;
    for (std::size_t i = 0; i < taken.size(); ++i) {
        if (periodOf(taken[i]) != board->period_ || slotOf(taken[i]) >= board->config_.slots || (i > 0 && taken[i] <= taken[i - 1])) {
            return std::nullopt;
        }
        board->takenThisPeriod_.insert(taken[i]);
    }
    for (std::size_t i = 0; i < heldIds.size(); ++i) {
        const std::uint64_t id = heldIds[i];
        const std::uint64_t period = periodOf(id);
        if (period > board->period_ || slotOf(id) >= board->config_.slots || (i > 0 && id <= heldIds[i - 1])
            || (period == board->period_ && !board->takenThisPeriod_.contains(id))) {
            return std::nullopt;
        }
        std::vector<Contract> offers = board->offersFor(period);
        Contract& contract = offers[slotOf(id)];
        if (board->tick_ >= contract.expiresAt) {
            return std::nullopt;
        }
        board->held_.push_back(std::move(contract));
    }
    return board;
}

std::uint64_t ContractBoard::digest() const
{
    const std::vector<std::uint8_t> bytes = serialise();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}
