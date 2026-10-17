#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// One chronicle entry as the journal sees it. The caller turns ticks into `time` text.
struct JournalModelEntry {
    std::uint64_t tick = 0;
    std::string time;
    std::string category;
    std::string text;
    // How much it mattered, 0 to 255.
    std::uint8_t weight = 0;
};

enum class JournalTab : std::uint8_t { Prose, Consequences };

struct JournalModelLine {
    std::string time;
    std::string text;
    std::uint8_t weight = 0;

    bool operator==(const JournalModelLine&) const = default;
};

// Prose has a single group with an empty title. Consequences has one group per category.
struct JournalModelGroup {
    std::string title;
    std::vector<JournalModelLine> lines;
    // Entries dropped by the line cap.
    std::size_t omitted = 0;

    bool operator==(const JournalModelGroup&) const = default;
};

struct JournalModelTab {
    JournalTab tab = JournalTab::Prose;
    std::string title;
    std::vector<JournalModelGroup> groups;

    bool operator==(const JournalModelTab&) const = default;
};

constexpr std::size_t kJournalModelMaxProseLines = 200;
constexpr std::size_t kJournalModelMaxGroups = 24;
constexpr std::size_t kJournalModelMaxGroupLines = 20;
constexpr std::size_t kJournalModelMaxTextBytes = 500;
constexpr std::size_t kJournalModelMaxTimeBytes = 16;
constexpr std::size_t kJournalModelMaxTitleBytes = 40;

// Always two tabs in fixed order: Prose, then Consequences. Prose lists the entries whose category is
// "journal" (kJournalCategory), newest first. Consequences lists every other category, "what I've done
// and what happened": groups are ordered by their newest entry (then by title), lines inside a group
// newest first, and entries of equal tick keep the later input first. A group title is its category
// with dashes and underscores as spaces and a capital first letter. An entry with no usable text is
// dropped; text, time and title are stripped of control characters and cut on a UTF-8 boundary. A
// tab with nothing to show has no groups. Pure and deterministic.
std::vector<JournalModelTab> buildJournalModel(const std::vector<JournalModelEntry>& entries);

}
