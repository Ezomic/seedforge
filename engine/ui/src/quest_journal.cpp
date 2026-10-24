#include "ichor/ui/quest_journal.h"

#include "ichor/data/journal.h"

#include <limits>

namespace ichor {

namespace {

const char* standing(QuestStage stage)
{
    switch (stage) {
    case QuestStage::Active: return "in progress";
    case QuestStage::Found: return "ready to hand in";
    case QuestStage::Done: return "done";
    }
    return "";
}

}

std::vector<JournalModelEntry> questJournalEntries(const std::vector<QuestLogEntry>& log, std::uint64_t nowTick)
{
    std::vector<JournalModelEntry> lines;
    for (const QuestLogEntry& quest : log) {
        std::vector<JournalModelEntry> block;
        block.push_back({0, "Quest", std::string(kJournalCategory), quest.title + " (" + standing(quest.stage) + ")", 200});
        for (const QuestObjective& objective : quest.objectives) {
            block.push_back({0, "", std::string(kJournalCategory), std::string(objective.done ? "[x] " : "[ ] ") + objective.text, 150});
        }
        if (!quest.outcome.empty()) {
            block.push_back({0, "", std::string(kJournalCategory), quest.outcome, 150});
        }
        std::uint64_t tick = nowTick > std::numeric_limits<std::uint64_t>::max() - block.size() ? std::numeric_limits<std::uint64_t>::max() - block.size() : nowTick;
        tick += block.size();
        for (JournalModelEntry& line : block) {
            line.tick = tick--;
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

}
