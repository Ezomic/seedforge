#pragma once

#include "ichor/ledger/consequence_ledger.h"
#include "ichor/save/chronicle.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Decides which ledger facts are notable enough for the chronicle and words
// each one. Pure and deterministic: the same ledger and rules always give the
// same entries. Nothing here changes the ledger, the chronicle or a save format.

struct NotableRule {
    std::string kind;     // the ledger fact kind this rule matches
    std::string category; // chronicle category: lowercase letters, digits, dash, underscore
    // Sentence with {subject} and {value} where the fact's text goes. No other braces.
    std::string phrase;
    // When true the fact's value must be a plain decimal number of at least minValue.
    bool numeric = false;
    std::uint64_t minValue = 0;
    // Facts of the same kind and subject closer than this many ticks to the last one kept are dropped.
    std::uint64_t cooldownTicks = 0;
    std::uint8_t weight = 0; // chronicle weight, 0 to 255
};

struct NotableEntry {
    std::uint64_t tick = 0;
    std::string category;
    std::string text;
    std::uint8_t weight = 0;
    bool operator==(const NotableEntry&) const = default;
};

constexpr std::size_t kMaxNotableRules = 256;
constexpr std::size_t kMaxNotableFieldBytes = 80;

// False for an empty or over-long kind, a category the chronicle would refuse,
// a phrase that is empty, over 300 bytes, has an unknown {placeholder} or a stray brace.
bool isValidNotableRule(const NotableRule& rule);

// The rules the game ships with.
std::vector<NotableRule> defaultNotableRules();

// One sentence for a fact under a rule. The fact's subject and value are made
// safe first: invalid UTF-8 becomes U+FFFD, control, line-separator and bidi
// characters become spaces, and each is cut to kMaxNotableFieldBytes on a
// character boundary. Text inside a value is never expanded as a placeholder.
std::string composeNotablePhrase(const NotableRule& rule, const Fact& fact);

// The notable facts in ledger order. A fact is skipped when no valid rule
// matches its kind (the first valid rule for a kind wins), a numeric value is
// below the threshold or not a number, it falls inside its rule's cooldown, or
// it words exactly like an entry already kept. Stops at maxEntries.
std::vector<NotableEntry> selectNotableEvents(const ConsequenceLedger& ledger, const std::vector<NotableRule>& rules,
    std::size_t maxEntries = Chronicle::kMaxEntries);

// Adds entries to the chronicle. Returns how many it accepted.
std::size_t appendNotableEvents(Chronicle& chronicle, const std::vector<NotableEntry>& entries);

}
