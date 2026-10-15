#pragma once

#include "ichor/ledger/consequence_ledger.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// When the game saves by itself (doc 14): after consequential moments, never
// before them, and never with a popup. Everything here is a pure function of
// the policy data, the events so far and the current tick; the caller does the
// actual SaveSlots::save and shows the player nothing.

// An event with a rule saves `delayTicks` after it happened, so the save lands
// once the moment has settled instead of on the frame of the choice.
struct AutosaveRule {
    std::string id;
    std::string event;
    std::uint64_t delayTicks = 0;
    bool operator==(const AutosaveRule&) const = default;
};

struct AutosavePolicy {
    static constexpr std::size_t kMaxBytes = 64 * 1024;
    static constexpr std::size_t kMaxRules = 256;
    static constexpr std::uint32_t kMaxSlots = 99;
    static constexpr std::uint64_t kMaxTicks = 1ULL << 53;

    std::vector<AutosaveRule> rules;
    // No two autosaves closer than this, so a burst of consequences is one save.
    std::uint64_t minGapTicks = 600;
    // A save when this long has passed with no other reason; 0 turns the timer off.
    std::uint64_t timerTicks = 18000;
    // Autosaves rotate through this many slots, autosave_01 onwards.
    std::uint32_t slots = 3;

    // The data/save/autosave_rules.json shape; all or nothing.
    static std::optional<AutosavePolicy> parse(std::string_view json);
    static std::string slotName(std::uint32_t index);
};

struct AutosaveEvent {
    std::string kind;
    std::uint64_t tick = 0;
};

struct PendingAutosave {
    std::string reason;
    std::uint64_t dueTick = 0;
    bool operator==(const PendingAutosave&) const = default;
};

struct AutosaveState {
    static constexpr std::size_t kMaxPending = 64;

    bool saved = false;
    std::uint64_t lastSaveTick = 0;
    std::uint32_t nextSlot = 0;
    std::vector<PendingAutosave> pending;
    bool operator==(const AutosaveState&) const = default;
};

struct AutosaveDecision {
    bool save = false;
    std::string reason; // the event kind that caused it, or "timer"
    std::string slot;
    AutosaveState state; // carry this into the next call
};

// `events` are the ones that happened since the last call, each given once.
AutosaveDecision decideAutosave(const AutosavePolicy& policy, const AutosaveState& state, std::span<const AutosaveEvent> events, std::uint64_t now);

// Each fact from `fromIndex` on becomes an event of its kind at its tick.
std::vector<AutosaveEvent> eventsFromLedger(const ConsequenceLedger& ledger, std::size_t fromIndex);

}
