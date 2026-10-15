#include "ichor/save/autosave_policy.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <set>

namespace ichor {

namespace {

constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();

bool validName(const std::string& text)
{
    if (text.empty() || text.size() > ConsequenceLedger::kMaxKeyBytes) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':';
    });
}

std::uint64_t saturatingAdd(std::uint64_t a, std::uint64_t b)
{
    return b > kNever - a ? kNever : a + b;
}

// A missing key keeps `value`; a present one must be a whole number in range.
bool readCount(const nlohmann::json& record, const char* key, std::uint64_t maxValue, std::uint64_t& value)
{
    const auto it = record.find(key);
    if (it == record.end()) {
        return true;
    }
    if (!it->is_number_unsigned()) {
        return false;
    }
    const std::uint64_t read = it->get<std::uint64_t>();
    if (read > maxValue) {
        return false;
    }
    value = read;
    return true;
}

bool readString(const nlohmann::json& record, const char* key, std::string& out)
{
    const auto it = record.find(key);
    if (it == record.end() || !it->is_string()) {
        return false;
    }
    out = it->get<std::string>();
    return validName(out);
}

}

std::string AutosavePolicy::slotName(std::uint32_t index)
{
    return std::format("autosave_{:02}", index + 1);
}

std::optional<AutosavePolicy> AutosavePolicy::parse(std::string_view text)
{
    if (text.size() > kMaxBytes) {
        return std::nullopt;
    }
    const nlohmann::json json = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded() || !json.is_array() || json.size() > kMaxRules + 1) {
        return std::nullopt;
    }

    AutosavePolicy policy;
    std::set<std::string> ids;
    std::set<std::string> events;
    bool sawTiming = false;
    for (const nlohmann::json& record : json) {
        std::string id;
        std::string kind;
        if (!record.is_object() || !readString(record, "id", id) || !readString(record, "kind", kind) || !ids.insert(id).second) {
            return std::nullopt;
        }
        if (kind == "timing") {
            std::uint64_t slots = policy.slots;
            if (sawTiming || !readCount(record, "minGapTicks", kMaxTicks, policy.minGapTicks) || !readCount(record, "timerTicks", kMaxTicks, policy.timerTicks)
                || !readCount(record, "slots", kMaxSlots, slots) || slots == 0) {
                return std::nullopt;
            }
            policy.slots = static_cast<std::uint32_t>(slots);
            sawTiming = true;
        } else if (kind == "rule") {
            AutosaveRule rule;
            rule.id = id;
            if (!readString(record, "event", rule.event) || !readCount(record, "delayTicks", kMaxTicks, rule.delayTicks) || !events.insert(rule.event).second) {
                return std::nullopt;
            }
            policy.rules.push_back(std::move(rule));
        } else {
            return std::nullopt;
        }
    }
    return policy;
}

AutosaveDecision decideAutosave(const AutosavePolicy& policy, const AutosaveState& state, std::span<const AutosaveEvent> events, std::uint64_t now)
{
    AutosaveDecision decision;
    decision.state = state;
    AutosaveState& next = decision.state;
    if (next.pending.size() > AutosaveState::kMaxPending) {
        next.pending.resize(AutosaveState::kMaxPending);
    }

    // A tick before the last save means the clock was reset by a load: what was pending belongs to
    // the old timeline, and the timer starts over from here.
    const bool rewound = next.saved && now < next.lastSaveTick;
    if (rewound) {
        next.pending.clear();
        next.lastSaveTick = now;
    }

    for (const AutosaveEvent& event : events) {
        const auto rule = std::find_if(policy.rules.begin(), policy.rules.end(), [&](const AutosaveRule& r) { return r.event == event.kind; });
        if (rule == policy.rules.end()) {
            continue;
        }
        const PendingAutosave entry{event.kind, saturatingAdd(event.tick, rule->delayTicks)};
        if (next.pending.size() < AutosaveState::kMaxPending) {
            next.pending.push_back(entry);
            continue;
        }
        // Full: the entry furthest in the future makes way, so one corrupt tick cannot starve the rest.
        const auto latest = std::max_element(next.pending.begin(), next.pending.end(), [](const PendingAutosave& a, const PendingAutosave& b) { return a.dueTick < b.dueTick; });
        if (entry.dueTick < latest->dueTick) {
            *latest = entry;
        }
    }

    const bool gapOk = !next.saved || rewound || now - next.lastSaveTick >= policy.minGapTicks;
    if (!gapOk) {
        return decision;
    }

    const auto due = std::min_element(next.pending.begin(), next.pending.end(), [](const PendingAutosave& a, const PendingAutosave& b) { return a.dueTick < b.dueTick; });
    if (due != next.pending.end() && due->dueTick <= now) {
        decision.reason = due->reason;
        std::erase_if(next.pending, [&](const PendingAutosave& p) { return p.dueTick <= now; });
    } else {
        const std::uint64_t base = next.saved ? next.lastSaveTick : 0;
        if (policy.timerTicks == 0 || now - base < policy.timerTicks) {
            return decision;
        }
        decision.reason = "timer";
    }

    const std::uint32_t slots = std::max<std::uint32_t>(1, policy.slots);
    decision.save = true;
    decision.slot = AutosavePolicy::slotName(next.nextSlot % slots);
    next.nextSlot = (next.nextSlot % slots + 1) % slots;
    next.saved = true;
    next.lastSaveTick = now;
    return decision;
}

std::vector<AutosaveEvent> eventsFromLedger(const ConsequenceLedger& ledger, std::size_t fromIndex)
{
    std::vector<AutosaveEvent> events;
    const std::vector<Fact>& facts = ledger.facts();
    for (std::size_t i = fromIndex; i < facts.size(); ++i) {
        events.push_back({facts[i].kind, facts[i].tick});
    }
    return events;
}

}
