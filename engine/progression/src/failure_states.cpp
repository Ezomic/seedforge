#include "ichor/progression/failure_states.h"

#include <array>
#include <optional>

namespace ichor {

namespace {

constexpr std::array<FailureKind, 5> kKinds = {
    FailureKind::Cargo, FailureKind::Standing, FailureKind::Companion, FailureKind::Route, FailureKind::Time};

std::optional<std::string_view> kindName(FailureKind kind)
{
    switch (kind) {
    case FailureKind::Cargo: return "failure-cargo";
    case FailureKind::Standing: return "failure-standing";
    case FailureKind::Companion: return "failure-companion";
    case FailureKind::Route: return "failure-route";
    case FailureKind::Time: return "failure-time";
    }
    return std::nullopt;
}

std::optional<FailureKind> kindFromName(std::string_view name)
{
    for (FailureKind kind : kKinds) {
        if (kindName(kind) == name) return kind;
    }
    return std::nullopt;
}

bool wholeLoss(FailureKind kind) { return kind == FailureKind::Companion || kind == FailureKind::Route; }

bool validKey(std::string_view text)
{
    if (text.empty() || text.size() > kMaxFailureKeyBytes) return false;
    for (char c : text) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
        if (!ok) return false;
    }
    return true;
}

bool validAmount(FailureKind kind, std::uint32_t amount)
{
    return wholeLoss(kind) ? amount == 0 : amount >= 1 && amount <= kMaxFailureAmount;
}

std::optional<std::uint32_t> parseAmount(std::string_view text)
{
    if (text.empty() || text.size() > 10 || (text.size() > 1 && text[0] == '0')) return std::nullopt;
    std::uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value > kMaxFailureAmount) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

std::optional<RecordedFailure> decode(const Fact& fact)
{
    const auto kind = kindFromName(fact.kind);
    if (!kind || !validKey(fact.subject)) return std::nullopt;
    const std::size_t split = fact.value.find(';');
    if (split == std::string::npos) return std::nullopt;
    const std::string_view value = fact.value;
    const std::string_view cause = value.substr(0, split);
    const auto amount = parseAmount(value.substr(split + 1));
    if (!validKey(cause) || !amount || !validAmount(*kind, *amount)) return std::nullopt;
    return RecordedFailure{*kind, fact.subject, std::string(cause), *amount, fact.tick};
}

void collect(std::vector<RecordedFailure>& out, const Fact& fact, const FailureKind* only)
{
    if (auto failure = decode(fact); failure && (only == nullptr || failure->kind == *only)) out.push_back(std::move(*failure));
}

}

RecordResult recordFailure(ConsequenceLedger& ledger, const FailureEvent& event)
{
    const auto name = kindName(event.kind);
    if (!name || !validKey(event.subject) || !validKey(event.cause)) return RecordResult::InvalidKey;
    if (!validAmount(event.kind, event.amount)) return RecordResult::InvalidAmount;
    const std::string id = std::string(*name) + ":" + std::to_string(event.tick) + ":" + event.subject + ":" + event.cause + ":"
        + std::to_string(event.amount);
    if (ledger.find(id) != nullptr) return RecordResult::DuplicateId;
    if (ledger.size() >= ConsequenceLedger::kMaxFacts) return RecordResult::LedgerFull;
    const AppendResult result
        = ledger.append({id, event.subject, std::string(*name), event.cause + ";" + std::to_string(event.amount), event.tick});
    return result == AppendResult::Ok ? RecordResult::Ok : RecordResult::InvalidKey;
}

std::vector<RecordedFailure> failures(const ConsequenceLedger& ledger)
{
    std::vector<RecordedFailure> out;
    for (const Fact& fact : ledger.facts()) collect(out, fact, nullptr);
    return out;
}

std::vector<RecordedFailure> failures(const ConsequenceLedger& ledger, FailureKind kind)
{
    std::vector<RecordedFailure> out;
    const auto name = kindName(kind);
    if (!name) return out;
    for (const Fact* fact : ledger.byKind(*name)) collect(out, *fact, &kind);
    return out;
}

std::vector<RecordedFailure> failuresFor(const ConsequenceLedger& ledger, std::string_view subject)
{
    std::vector<RecordedFailure> out;
    for (const Fact* fact : ledger.bySubject(subject)) collect(out, *fact, nullptr);
    return out;
}

std::vector<RecordedFailure> failuresFor(const ConsequenceLedger& ledger, std::string_view subject, FailureKind kind)
{
    std::vector<RecordedFailure> out;
    const auto name = kindName(kind);
    if (!name) return out;
    for (const Fact* fact : ledger.bySubjectAndKind(subject, *name)) collect(out, *fact, &kind);
    return out;
}

std::uint64_t totalLost(const ConsequenceLedger& ledger, FailureKind kind, std::string_view subject)
{
    std::uint64_t total = 0;
    for (const RecordedFailure& failure : failuresFor(ledger, subject, kind)) total += failure.amount;
    return total;
}

}
