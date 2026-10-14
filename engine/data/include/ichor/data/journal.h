#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// The chronicle category journal prose is stored under.
constexpr std::string_view kJournalCategory = "journal";

// What the player should be able to remember: who asked, what they asked, where,
// and what was decided. Every text is optional; an empty one reads as unknown.
struct JournalEntry {
    std::uint64_t tick = 0;
    std::string who;     // a name, as written
    std::string what;    // the request as a thing to do, e.g. "carry the ash to the ferryman"
    std::string where;   // a place name, as written
    std::string decided; // the player's answer as a first-person clause, e.g. "I agreed to go"; empty while undecided
};

// The validated prose templates (data/journal/prose.json). Records:
//   asked      variants for the opening sentence; {who} {what} {where}
//   decided    variants for the closing sentence when something was decided; also {decided}
//   undecided  variants for the closing sentence when nothing was; {who} {what} {where}
//   fallback   the words that stand in for a missing who, what or where (strings)
// A set only exists if every placeholder is known for its record, braces are
// balanced, no text holds a dash used as punctuation (em-dash or double hyphen),
// and every record has at least one variant.
class JournalTemplates {
public:
    static std::optional<JournalTemplates> fromTable(const DataTable& table, std::string& error);

    const std::vector<std::string>& asked() const { return asked_; }
    const std::vector<std::string>& decided() const { return decided_; }
    const std::vector<std::string>& undecided() const { return undecided_; }
    const std::string& fallbackWho() const { return fallbackWho_; }
    const std::string& fallbackWhat() const { return fallbackWhat_; }
    const std::string& fallbackWhere() const { return fallbackWhere_; }

private:
    JournalTemplates() = default;

    std::vector<std::string> asked_;
    std::vector<std::string> decided_;
    std::vector<std::string> undecided_;
    std::string fallbackWho_;
    std::string fallbackWhat_;
    std::string fallbackWhere_;
};

// Pure: one or two sentences. The variants are picked by a generator seeded from
// the entry alone, so the same entry always reads the same and nothing else is
// drawn. Names and places go in exactly as written, and text that looks like a
// placeholder is not expanded. A missing who, what or where is replaced by the
// template set's fallback words, and a missing decision by an "undecided" sentence.
// `decided` must be a clause without its final full stop, as the templates add it. The
// chronicle keeps at most Chronicle::kMaxTextBytes of the result, so very long fields cost
// the end of the prose (the decision) when stored.
std::string composeJournal(const JournalEntry& entry, const JournalTemplates& templates);

}
