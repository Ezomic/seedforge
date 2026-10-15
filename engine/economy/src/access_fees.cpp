#include "ichor/economy/access_fees.h"

#include "ichor/core/binary.h"
#include "ichor/core/hash.h"

#include <algorithm>
#include <limits>
#include <set>

namespace ichor {

namespace {

constexpr std::uint32_t kAccessFeesVersion = 1;
constexpr std::uint64_t kNever = 0;
// Saved ends above this are refused so a lease can always be extended by one period without wrapping.
constexpr std::uint64_t kMaxSavedEnd = std::numeric_limits<std::uint64_t>::max() - kMaxFeePeriod;

FeeStatus fromTx(TxStatus status)
{
    switch (status) {
    case TxStatus::Ok: return FeeStatus::Ok;
    case TxStatus::NotAllowed: return FeeStatus::NotAllowed;
    case TxStatus::Insufficient: return FeeStatus::Insufficient;
    }
    return FeeStatus::NotAllowed;
}

bool active(const std::map<std::string, std::uint64_t, std::less<>>& ends, std::string_view key, std::uint64_t now)
{
    const auto it = ends.find(key);
    return it != ends.end() && now < it->second;
}

}

std::optional<AccessFees> AccessFees::create(std::vector<FactionFee> fees, std::vector<StandingTier> tiers, std::vector<StandingGate> gates,
    const std::vector<Faction>& factions, std::string& error)
{
    const auto tierKnown = [&](const std::string& id) { return std::any_of(tiers.begin(), tiers.end(), [&](const StandingTier& t) { return t.id == id; }); };
    std::set<std::string> gateIds;
    for (const StandingGate& gate : gates) {
        if (!gateIds.insert(gate.id).second) {
            error = "gate '" + gate.id + "' listed twice";
            return std::nullopt;
        }
    }
    std::set<std::string> seen;
    for (const FactionFee& fee : fees) {
        const std::string where = "fee '" + fee.faction + "': ";
        if (std::none_of(factions.begin(), factions.end(), [&](const Faction& f) { return f.id == fee.faction; })) {
            error = where + "unknown faction";
            return std::nullopt;
        }
        if (!seen.insert(fee.faction).second) {
            error = where + "listed twice";
            return std::nullopt;
        }
        if (fee.duesAmount > kMaxCurrencyCap || fee.bribeBase > kMaxCurrencyCap || fee.bribePerTier > kMaxCurrencyCap) {
            error = where + "amount above the largest currency cap";
            return std::nullopt;
        }
        if (fee.duesAmount > 0 && (fee.duesPeriod == 0 || fee.duesPeriod > kMaxFeePeriod || !tierKnown(fee.duesMinTier))) {
            error = where + "dues need a period of 1 to " + std::to_string(kMaxFeePeriod) + " ticks and a known min tier";
            return std::nullopt;
        }
        if (fee.duesAmount == 0 && (fee.duesPeriod != 0 || !fee.duesMinTier.empty())) {
            error = where + "dues period or tier set without an amount";
            return std::nullopt;
        }
        if (fee.bribeDuration > 0 && (fee.bribeBase == 0 || fee.bribeDuration > kMaxFeePeriod || fee.bribeMaxTiers == 0 || fee.bribeMaxTiers > kMaxBribeTiers)) {
            error = where + "bribes need a positive base, a duration of 1 to " + std::to_string(kMaxFeePeriod) + " ticks and 1 to "
                + std::to_string(kMaxBribeTiers) + " tiers";
            return std::nullopt;
        }
        if (fee.bribeDuration == 0 && (fee.bribeBase != 0 || fee.bribePerTier != 0 || fee.bribeMaxTiers != 0)) {
            error = where + "bribe terms set without a duration";
            return std::nullopt;
        }
    }
    AccessFees out;
    out.fees_ = std::move(fees);
    std::sort(out.fees_.begin(), out.fees_.end(), [](const FactionFee& a, const FactionFee& b) { return a.faction < b.faction; });
    out.tiers_ = std::move(tiers);
    for (StandingGate& gate : gates) {
        out.gates_.emplace(gate.id, std::move(gate));
    }
    for (const FactionFee& fee : out.fees_) {
        if (fee.duesAmount > 0) {
            out.dues_.emplace(fee.faction, kNever);
        }
    }
    for (const auto& [id, gate] : out.gates_) {
        const FactionFee* fee = out.feeFor(gate.faction);
        if (fee != nullptr && fee->bribeDuration > 0) {
            out.bribes_.emplace(id, kNever);
        }
    }
    return out;
}

const FactionFee* AccessFees::feeFor(std::string_view faction) const
{
    const auto it = std::lower_bound(fees_.begin(), fees_.end(), faction, [](const FactionFee& f, std::string_view id) { return f.faction < id; });
    return it != fees_.end() && it->faction == faction ? &*it : nullptr;
}

std::optional<std::size_t> AccessFees::tierIndex(std::string_view tierId) const
{
    const auto it = std::find_if(tiers_.begin(), tiers_.end(), [&](const StandingTier& t) { return t.id == tierId; });
    if (it == tiers_.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(it - tiers_.begin());
}

std::optional<std::size_t> AccessFees::standingIndex(const FactionStanding& standing, std::string_view faction) const
{
    const StandingTier* current = standing.tier(faction);
    return current == nullptr ? std::nullopt : tierIndex(current->id);
}

bool AccessFees::hasAccess(const FactionStanding& standing, std::string_view gate, std::uint64_t now) const
{
    if (standing.allowed(gate)) {
        return true;
    }
    const auto found = gates_.find(gate);
    if (found == gates_.end()) {
        return false;
    }
    if (active(bribes_, gate, now)) {
        return true;
    }
    const FactionFee* fee = feeFor(found->second.faction);
    if (fee == nullptr || fee->duesAmount == 0 || !active(dues_, found->second.faction, now)) {
        return false;
    }
    const auto have = standingIndex(standing, found->second.faction);
    const auto need = tierIndex(fee->duesMinTier);
    return have && need && *have >= *need;
}

BribeQuote AccessFees::quoteBribe(const FactionStanding& standing, std::string_view gate, std::uint64_t now) const
{
    const auto found = gates_.find(gate);
    if (found == gates_.end()) {
        return {FeeStatus::UnknownGate, 0};
    }
    const FactionFee* fee = feeFor(found->second.faction);
    if (fee == nullptr || fee->bribeDuration == 0) {
        return {FeeStatus::NotOffered, 0};
    }
    if (hasAccess(standing, gate, now)) {
        return {FeeStatus::NotNeeded, 0};
    }
    const auto have = standingIndex(standing, found->second.faction);
    const auto need = tierIndex(found->second.minTier);
    if (!have || !need) {
        return {FeeStatus::UnknownGate, 0};
    }
    if (*need <= *have) {
        return {FeeStatus::NotNeeded, 0};
    }
    const std::size_t gap = *need - *have;
    if (gap > fee->bribeMaxTiers) {
        return {FeeStatus::StandingTooLow, 0};
    }
    return {FeeStatus::Ok, fee->bribeBase + fee->bribePerTier * gap};
}

FeeStatus AccessFees::payDues(Wallet& wallet, const CurrencyTable& table, const FactionStanding& standing, std::string_view faction, std::uint64_t now)
{
    const FactionFee* fee = feeFor(faction);
    if (fee == nullptr) {
        return standing.standing(faction) ? FeeStatus::NotOffered : FeeStatus::UnknownFaction;
    }
    if (fee->duesAmount == 0) {
        return FeeStatus::NotOffered;
    }
    const auto have = standingIndex(standing, faction);
    const auto need = tierIndex(fee->duesMinTier);
    if (!have || !need) {
        return FeeStatus::UnknownFaction;
    }
    if (*have < *need) {
        return FeeStatus::StandingTooLow;
    }
    if (now > std::numeric_limits<std::uint64_t>::max() - (kMaxPrepaidPeriods + 1) * fee->duesPeriod) {
        return FeeStatus::TimeOverflow;
    }
    std::uint64_t& until = dues_.find(faction)->second;
    const std::uint64_t next = std::max(until, now) + fee->duesPeriod;
    if (next - now > kMaxPrepaidPeriods * fee->duesPeriod) {
        return FeeStatus::AlreadyPaid;
    }
    const FeeStatus paid = fromTx(wallet.spend(table, CurrencySlot::Coin, fee->duesAmount, "dues"));
    if (paid == FeeStatus::Ok) {
        until = next;
    }
    return paid;
}

FeeStatus AccessFees::payBribe(Wallet& wallet, const CurrencyTable& table, const FactionStanding& standing, std::string_view gate, std::uint64_t now)
{
    const BribeQuote quote = quoteBribe(standing, gate, now);
    if (quote.status != FeeStatus::Ok) {
        return quote.status;
    }
    const FactionFee& fee = *feeFor(gates_.find(gate)->second.faction);
    if (now > std::numeric_limits<std::uint64_t>::max() - fee.bribeDuration) {
        return FeeStatus::TimeOverflow;
    }
    const FeeStatus paid = fromTx(wallet.spend(table, CurrencySlot::Coin, quote.price, "bribe"));
    if (paid == FeeStatus::Ok) {
        bribes_.find(gate)->second = now + fee.bribeDuration;
    }
    return paid;
}

std::optional<std::uint64_t> AccessFees::duesUntil(std::string_view faction) const
{
    const auto it = dues_.find(faction);
    return it == dues_.end() || it->second == kNever ? std::nullopt : std::optional(it->second);
}

std::optional<std::uint64_t> AccessFees::bribeUntil(std::string_view gate) const
{
    const auto it = bribes_.find(gate);
    return it == bribes_.end() || it->second == kNever ? std::nullopt : std::optional(it->second);
}

std::vector<std::uint8_t> AccessFees::serialise() const
{
    BinaryWriter writer;
    writer.write(kAccessFeesVersion);
    writer.write(static_cast<std::uint32_t>(dues_.size()));
    for (const auto& [id, until] : dues_) {
        writer.write(id);
        writer.write(until);
    }
    writer.write(static_cast<std::uint32_t>(bribes_.size()));
    for (const auto& [id, until] : bribes_) {
        writer.write(id);
        writer.write(until);
    }
    return writer.take();
}

std::uint64_t AccessFees::digest() const
{
    const std::vector<std::uint8_t> bytes = serialise();
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool AccessFees::deserialise(std::span<const std::uint8_t> bytes)
{
    BinaryReader reader(bytes);
    std::uint32_t version = 0;
    if (!reader.read(version) || version != kAccessFeesVersion) {
        return false;
    }
    auto dues = dues_;
    auto bribes = bribes_;
    for (auto* ends : {&dues, &bribes}) {
        std::uint32_t count = 0;
        if (!reader.read(count) || count != ends->size()) {
            return false;
        }
        for (auto& [id, until] : *ends) {
            std::string savedId;
            if (!reader.read(savedId) || savedId != id || !reader.read(until) || until > kMaxSavedEnd) {
                return false;
            }
        }
    }
    if (!reader.atEnd()) {
        return false;
    }
    dues_ = std::move(dues);
    bribes_ = std::move(bribes);
    return true;
}

}
