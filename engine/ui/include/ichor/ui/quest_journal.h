#pragma once

#include "ichor/ui/journal_screen_model.h"
#include "ichor/world/parley.h"

#include <cstdint>
#include <vector>

namespace ichor {

// The accepted quests as lines the journal's prose tab lists: a heading with where the quest stands, then one line per
// objective ("[x]" met, "[ ]" still to do) and, once done, how it ended. The journal lists newest first, so the lines
// are given ticks just past `nowTick` in reverse and read top to bottom in this order, above the chronicle's own prose.
// Pure and deterministic.
std::vector<JournalModelEntry> questJournalEntries(const std::vector<QuestLogEntry>& log, std::uint64_t nowTick);

}
