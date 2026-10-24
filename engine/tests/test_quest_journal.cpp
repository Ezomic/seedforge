#include "ichor/ui/journal_screen_model.h"
#include "ichor/ui/quest_journal.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace ichor;

namespace {

QuestLogEntry errand(QuestStage stage)
{
    QuestLogEntry entry;
    entry.id = "quest.errand";
    entry.title = "The Merchant's Errand";
    entry.stage = stage;
    const int met = stage == QuestStage::Active ? 1 : stage == QuestStage::Found ? 2 : 3;
    entry.objectives = {{"Hear out Marta", met >= 1}, {"Recover the Tin Bell from the crypt", met >= 2}, {"Bring the Tin Bell back to Marta", met >= 3}};
    if (stage == QuestStage::Done) entry.outcome = "Marta paid 120 c";
    return entry;
}

std::vector<std::string> proseOf(const std::vector<JournalModelEntry>& entries)
{
    std::vector<std::string> out;
    const std::vector<JournalModelTab> tabs = buildJournalModel(entries);
    for (const JournalModelGroup& group : tabs[0].groups) {
        for (const JournalModelLine& line : group.lines) out.push_back(line.text);
    }
    return out;
}

}

TEST_CASE("a quest in the journal reads heading first, then its objectives in order, and marks the ones met", "[quest_journal]")
{
    const std::vector<std::string> prose = proseOf(questJournalEntries({errand(QuestStage::Active)}, 100));
    REQUIRE(prose.size() == 4);
    REQUIRE(prose[0] == "The Merchant's Errand (in progress)");
    REQUIRE(prose[1] == "[x] Hear out Marta");
    REQUIRE(prose[2] == "[ ] Recover the Tin Bell from the crypt");
    REQUIRE(prose[3] == "[ ] Bring the Tin Bell back to Marta");
}

TEST_CASE("the journal state follows the stage: found, then done with its outcome", "[quest_journal]")
{
    const std::vector<std::string> found = proseOf(questJournalEntries({errand(QuestStage::Found)}, 100));
    REQUIRE(found[0] == "The Merchant's Errand (ready to hand in)");
    REQUIRE(found[2] == "[x] Recover the Tin Bell from the crypt");
    REQUIRE(found[3] == "[ ] Bring the Tin Bell back to Marta");

    const std::vector<std::string> done = proseOf(questJournalEntries({errand(QuestStage::Done)}, 100));
    REQUIRE(done.size() == 5);
    REQUIRE(done[0] == "The Merchant's Errand (done)");
    REQUIRE(done[3] == "[x] Bring the Tin Bell back to Marta");
    REQUIRE(done[4] == "Marta paid 120 c");
}

TEST_CASE("quest lines sit above the chronicle's own prose, and no quest adds no lines", "[quest_journal]")
{
    std::vector<JournalModelEntry> entries = {{50, "0:00:50", "journal", "I crossed the river.", 90}, {99, "0:01:39", "journal", "I slept.", 20}};
    REQUIRE(questJournalEntries({}, 100).empty());
    for (const JournalModelEntry& line : questJournalEntries({errand(QuestStage::Active)}, 100)) entries.push_back(line);
    const std::vector<std::string> prose = proseOf(entries);
    REQUIRE(prose.size() == 6);
    REQUIRE(prose[0] == "The Merchant's Errand (in progress)");
    REQUIRE(prose[4] == "I slept.");
    REQUIRE(prose[5] == "I crossed the river.");
}

TEST_CASE("a tick at the top of the range does not wrap", "[quest_journal]")
{
    const std::vector<JournalModelEntry> lines = questJournalEntries({errand(QuestStage::Done)}, std::numeric_limits<std::uint64_t>::max());
    REQUIRE(lines.size() == 5);
    for (std::size_t i = 1; i < lines.size(); ++i) REQUIRE(lines[i - 1].tick > lines[i].tick);
}
