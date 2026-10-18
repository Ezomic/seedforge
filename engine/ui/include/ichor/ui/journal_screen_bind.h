#pragma once

#include "ichor/ui/journal_screen_model.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

class JournalScreen;

// Play time as hours:minutes:seconds, the way the chronicle shows it. A bad rate falls to zero.
std::string journalPlayTime(std::uint64_t tick, double secondsPerTick);

constexpr std::size_t kJournalBindMaxEntries = 2000;

// The newest entries of a chronicle (anything with tick, category, text and weight) as model entries.
template <class Entries>
std::vector<JournalModelEntry> journalEntries(const Entries& chronicle, double secondsPerTick)
{
    const std::size_t first = chronicle.size() > kJournalBindMaxEntries ? chronicle.size() - kJournalBindMaxEntries : 0;
    std::vector<JournalModelEntry> out;
    out.reserve(chronicle.size() - first);
    for (std::size_t i = first; i < chronicle.size(); ++i) {
        const auto& entry = chronicle[i];
        out.push_back({entry.tick, journalPlayTime(entry.tick, secondsPerTick), entry.category, entry.text, entry.weight});
    }
    return out;
}

// Builds the journal model from the entries and puts it on the screen.
void bindJournalModel(JournalScreen& screen, const std::vector<JournalModelEntry>& entries);

// RML for the tab buttons and for the active tab's groups. Every string goes through escapeRml.
std::string journalTabBarMarkup(const std::vector<JournalModelTab>& tabs, JournalTab active);
std::string journalTabBodyMarkup(const std::vector<JournalModelTab>& tabs, JournalTab active);

}
