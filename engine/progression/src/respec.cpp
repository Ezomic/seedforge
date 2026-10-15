#include "ichor/progression/respec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 256 * 1024;
constexpr std::size_t kMaxServices = 64;
constexpr std::size_t kMaxText = 120;
constexpr std::size_t kMaxTargets = 256;
constexpr int kMaxDepth = 8;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

bool isWord(std::string_view text)
{
    const auto lower = [](char c) { return c >= 'a' && c <= 'z'; };
    return !text.empty() && text.size() <= 64 && lower(text[0])
        && std::all_of(text.begin(), text.end(), [&](char c) { return lower(c) || (c >= '0' && c <= '9') || c == '_'; });
}

// Counted before parsing, so a hostile file never reaches a recursive walk.
int nestingDepth(std::string_view text)
{
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            escaped = !escaped && c == '\\';
            inString = escaped || c != '"';
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return deepest;
}

struct Reader {
    std::string_view label;
    std::vector<std::string>& problems;

    void add(const std::string& path, const std::string& message)
    {
        problems.push_back(std::string(label) + ": " + path + (path.empty() ? "" : ": ") + message);
    }

    bool text(const Json& item, const std::string& at, const char* key, std::string& out)
    {
        const std::string path = at + "." + key;
        const auto it = item.find(key);
        if (it == item.end()) {
            add(path, "missing");
        } else if (!it->is_string()) {
            add(path, "expected a string");
        } else {
            const std::string& value = it->get_ref<const std::string&>();
            if (value.empty() || value.size() > kMaxText) {
                add(path, "must be 1 to " + std::to_string(kMaxText) + " bytes");
            } else if (value.find(kEmDash) != std::string::npos) {
                add(path, "must not contain an em-dash");
            } else {
                out = value;
                return true;
            }
        }
        return false;
    }

    void amount(const Json& item, const std::string& at, const char* key, std::uint64_t& out)
    {
        const std::string path = at + "." + key;
        const auto it = item.find(key);
        if (it == item.end()) {
            add(path, "missing");
        } else if (!it->is_number_unsigned() && !(it->is_number_integer() && it->get<std::int64_t>() >= 0)) {
            add(path, "expected a non-negative integer");
        } else if (it->get<std::uint64_t>() > kMaxCurrencyCap) {
            add(path, "larger than " + std::to_string(kMaxCurrencyCap));
        } else {
            out = it->get<std::uint64_t>();
        }
    }
};

}

bool parseRespecServices(std::string_view label, std::string_view text, std::vector<RespecService>& out, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    Reader read{label, problems};
    if (text.size() > kMaxFileBytes) {
        read.add("", "file is larger than " + std::to_string(kMaxFileBytes) + " bytes");
        return false;
    }
    if (nestingDepth(text) > kMaxDepth) {
        read.add("", "nested deeper than " + std::to_string(kMaxDepth) + " levels");
        return false;
    }
    const Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        read.add("", "not a JSON array");
        return false;
    }
    if (json.size() > kMaxServices) {
        read.add("", "more than " + std::to_string(kMaxServices) + " services");
        return false;
    }
    std::vector<RespecService> parsed;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const Json& item = json[i];
        const std::string at = "[" + std::to_string(i) + "]";
        if (!item.is_object()) {
            read.add(at, "expected an object");
            continue;
        }
        RespecService service;
        if (read.text(item, at, "id", service.id)) {
            if (!isWord(service.id)) {
                read.add(at + ".id", "must be lowercase letters, digits and underscores, starting with a letter");
            } else if (!seen.insert(service.id).second) {
                read.add(at + ".id", "duplicate id");
            }
        }
        read.text(item, at, "name", service.name);
        std::string currency;
        if (read.text(item, at, "currency", currency)) {
            if (currency == "coin") {
                service.currency = CurrencySlot::Coin;
            } else if (currency == "hard") {
                service.currency = CurrencySlot::Hard;
            } else {
                read.add(at + ".currency", "must be \"coin\" or \"hard\"");
            }
        }
        read.text(item, at, "sink", service.sink);
        read.amount(item, at, "base_cost", service.baseCost);
        read.amount(item, at, "per_capability_cost", service.perCapabilityCost);
        read.amount(item, at, "refund_per_capability", service.refundPerCapability);
        read.amount(item, at, "min_travel", service.minTravel);
        parsed.push_back(std::move(service));
    }
    if (problems.size() != before) {
        return false;
    }
    out = std::move(parsed);
    return true;
}

RespecQuote quoteRespec(const RespecService& service, const std::vector<Capability>& table, const CapabilityUnlocks& unlocks, const std::vector<std::string>& targets, const Wallet& wallet,
    const CurrencyTable& currencies, std::uint64_t travelled)
{
    RespecQuote quote;
    if (targets.empty() || targets.size() > kMaxTargets) {
        quote.status = RespecStatus::NothingToRespec;
        return quote;
    }
    std::set<std::string, std::less<>> removed;
    for (const std::string& id : targets) {
        const bool known = std::any_of(table.begin(), table.end(), [&](const Capability& c) { return c.id == id; });
        if (!known) {
            quote.status = RespecStatus::UnknownCapability;
            return quote;
        }
        if (!unlocks.can(id)) {
            quote.status = RespecStatus::NotUnlocked;
            return quote;
        }
        removed.insert(id);
    }
    for (bool grew = true; grew;) {
        grew = false;
        for (const Capability& c : table) {
            if (unlocks.can(c.id) && !removed.contains(c.id)
                && std::any_of(c.prerequisites.begin(), c.prerequisites.end(), [&](const std::string& p) { return removed.contains(p); })) {
                removed.insert(c.id);
                grew = true;
            }
        }
    }
    quote.removed.assign(removed.begin(), removed.end());
    // At most one entry per capability and each amount at most 2^53, so none of this can wrap.
    const std::uint64_t count = quote.removed.size();
    const std::uint64_t gross = service.baseCost + service.perCapabilityCost * count;
    const std::uint64_t refund = service.refundPerCapability * count;
    quote.cost = gross > refund ? gross - refund : 0;
    if (travelled < service.minTravel) {
        quote.status = RespecStatus::TooClose;
    } else if (!currencies.canSpendOn(service.currency, service.sink)) {
        quote.status = RespecStatus::SinkNotAllowed;
    } else if (!wallet.canAfford(service.currency, quote.cost)) {
        quote.status = RespecStatus::Insufficient;
    }
    return quote;
}

RespecQuote respec(const RespecService& service, const std::vector<Capability>& table, CapabilityUnlocks& unlocks, const std::vector<std::string>& targets, Wallet& wallet,
    const CurrencyTable& currencies, std::uint64_t travelled)
{
    RespecQuote quote = quoteRespec(service, table, unlocks, targets, wallet, currencies, travelled);
    if (quote.status != RespecStatus::Done) {
        return quote;
    }
    CapabilityUnlocks next(table);
    std::vector<std::string> keep = unlocks.unlocked();
    std::erase_if(keep, [&](const std::string& id) { return std::binary_search(quote.removed.begin(), quote.removed.end(), id); });
    for (bool progressed = true; progressed && !keep.empty();) {
        progressed = false;
        for (auto it = keep.begin(); it != keep.end();) {
            if (next.unlock(*it) == UnlockResult::Unlocked) {
                it = keep.erase(it);
                progressed = true;
            } else {
                ++it;
            }
        }
    }
    if (!keep.empty()) {
        quote.status = RespecStatus::NotUnlocked;
        return quote;
    }
    if (wallet.spend(currencies, service.currency, quote.cost, service.sink) != TxStatus::Ok) {
        quote.status = RespecStatus::Insufficient;
        return quote;
    }
    unlocks = std::move(next);
    return quote;
}

}
