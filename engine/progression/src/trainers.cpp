#include "ichor/progression/trainers.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>

namespace ichor {

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxFileBytes = 256 * 1024;
constexpr std::size_t kMaxRecords = 1024;
constexpr std::size_t kMaxOffers = 64;
constexpr std::size_t kMaxText = 120;
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
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
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

bool parseTrainers(std::string_view label, std::string_view text, const std::vector<Capability>& table, std::vector<Trainer>& out, std::vector<std::string>& problems)
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
    if (json.size() > kMaxRecords) {
        read.add("", "more than " + std::to_string(kMaxRecords) + " records");
        return false;
    }
    std::map<std::string, Trainer, std::less<>> parsed;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < json.size(); ++i) {
        const Json& item = json[i];
        const std::string at = "[" + std::to_string(i) + "]";
        if (!item.is_object()) {
            read.add(at, "expected an object");
            continue;
        }
        std::string id;
        if (read.text(item, at, "id", id)) {
            if (!isWord(id)) {
                read.add(at + ".id", "must be lowercase letters, digits and underscores, starting with a letter");
            } else if (!seen.insert(id).second) {
                read.add(at + ".id", "duplicate id");
            }
        }
        Trainer trainer;
        TrainerOffer offer;
        std::string currency;
        read.text(item, at, "trainer", trainer.id);
        read.text(item, at, "name", trainer.name);
        read.text(item, at, "gate", trainer.gate);
        read.text(item, at, "sink", trainer.sink);
        if (read.text(item, at, "currency", currency)) {
            if (currency == "coin") {
                trainer.currency = CurrencySlot::Coin;
            } else if (currency == "hard") {
                trainer.currency = CurrencySlot::Hard;
            } else {
                read.add(at + ".currency", "must be \"coin\" or \"hard\"");
            }
        }
        if (read.text(item, at, "capability", offer.capability)
            && !std::any_of(table.begin(), table.end(), [&](const Capability& c) { return c.id == offer.capability; })) {
            read.add(at + ".capability", "names no capability");
        }
        read.amount(item, at, "cost", offer.cost);
        read.amount(item, at, "days", offer.days);
        if (trainer.id.empty()) {
            continue;
        }
        if (!isWord(trainer.id)) {
            read.add(at + ".trainer", "must be lowercase letters, digits and underscores, starting with a letter");
            continue;
        }
        const auto [it, added] = parsed.try_emplace(trainer.id, trainer);
        Trainer& merged = it->second;
        if (!added && (merged.name != trainer.name || merged.gate != trainer.gate || merged.currency != trainer.currency || merged.sink != trainer.sink)) {
            read.add(at, "disagrees with the trainer's earlier records on name, gate, currency or sink");
        }
        const bool repeated = std::any_of(merged.offers.begin(), merged.offers.end(), [&](const TrainerOffer& o) { return o.capability == offer.capability; });
        if (repeated) {
            read.add(at + ".capability", "offered twice by " + merged.id);
        } else if (merged.offers.size() >= kMaxOffers) {
            read.add(at, "trainer has more than " + std::to_string(kMaxOffers) + " offers");
        } else {
            merged.offers.push_back(std::move(offer));
        }
    }
    if (problems.size() != before) {
        return false;
    }
    out.clear();
    for (auto& [id, trainer] : parsed) {
        out.push_back(std::move(trainer));
    }
    return true;
}

TrainQuote quoteTraining(const Trainer& trainer, std::string_view capability, const FoundTrainers& found, const TrainerGate& gateOpen, const CapabilityUnlocks& unlocks,
    const Wallet& wallet, const CurrencyTable& currencies)
{
    TrainQuote quote;
    const auto offer = std::find_if(trainer.offers.begin(), trainer.offers.end(), [&](const TrainerOffer& o) { return o.capability == capability; });
    if (!found.contains(trainer.id)) {
        quote.status = TrainStatus::NotFound;
    } else if (!gateOpen || !gateOpen(trainer.gate)) {
        quote.status = TrainStatus::GateClosed;
    } else if (offer == trainer.offers.end()) {
        quote.status = TrainStatus::NotOffered;
    } else {
        quote.cost = offer->cost;
        quote.days = offer->days;
        // Probe on a copy so quoting never changes the live unlocks.
        CapabilityUnlocks probe = unlocks;
        const UnlockResult result = probe.unlock(capability);
        if (result == UnlockResult::AlreadyUnlocked) {
            quote.status = TrainStatus::AlreadyUnlocked;
        } else if (result != UnlockResult::Unlocked) {
            quote.status = TrainStatus::PrerequisiteUnmet;
        } else if (!currencies.canSpendOn(trainer.currency, trainer.sink)) {
            quote.status = TrainStatus::SinkNotAllowed;
        } else if (!wallet.canAfford(trainer.currency, quote.cost)) {
            quote.status = TrainStatus::Insufficient;
        }
    }
    return quote;
}

TrainQuote train(const Trainer& trainer, std::string_view capability, const FoundTrainers& found, const TrainerGate& gateOpen, CapabilityUnlocks& unlocks, Wallet& wallet,
    const CurrencyTable& currencies)
{
    TrainQuote quote = quoteTraining(trainer, capability, found, gateOpen, unlocks, wallet, currencies);
    if (quote.status != TrainStatus::Done) {
        return quote;
    }
    if (wallet.spend(currencies, trainer.currency, quote.cost, trainer.sink) != TxStatus::Ok) {
        quote.status = TrainStatus::Insufficient;
        return quote;
    }
    if (unlocks.unlock(capability) != UnlockResult::Unlocked) {
        quote.status = TrainStatus::PrerequisiteUnmet;
    }
    return quote;
}

}
