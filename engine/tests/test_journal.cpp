#include "ichor/data/data_set.h"
#include "ichor/data/journal.h"
#include "ichor/save/chronicle.h"
#include "ichor/script/chronicle_bindings.h"
#include "ichor/script/journal_bindings.h"
#include "ichor/script/script_host.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

std::filesystem::path uniqueRoot()
{
    static int counter = 0;
    const auto root = std::filesystem::temp_directory_path()
        / ("ichor_journal_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "journal");
    return root;
}

std::optional<JournalTemplates> templatesFromJson(const std::string& json, std::string& error)
{
    const auto root = uniqueRoot();
    std::ofstream(root / "journal" / "prose.json", std::ios::binary) << json;
    DataSet data;
    const bool loaded = data.loadDirectory(root);
    std::filesystem::remove_all(root);
    if (!loaded) {
        error = data.lastError();
        return std::nullopt;
    }
    const DataTable* table = data.table("journal");
    if (table == nullptr) {
        error = "no journal table";
        return std::nullopt;
    }
    return JournalTemplates::fromTable(*table, error);
}

JournalTemplates shippedTemplates()
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_REPO_DATA));
    const DataTable* table = data.table("journal");
    REQUIRE(table != nullptr);
    std::string error;
    auto templates = JournalTemplates::fromTable(*table, error);
    INFO(error);
    REQUIRE(templates.has_value());
    return *templates;
}

// One plain variant per record, so the output is exact.
JournalTemplates plainTemplates()
{
    std::string error;
    auto templates = templatesFromJson(R"([
        {"id": "asked", "variants": ["{who} at {where} wants you to {what}."]},
        {"id": "decided", "variants": ["{decided}."]},
        {"id": "undecided", "variants": ["Not decided about {who}."]},
        {"id": "fallback", "who": "a stranger", "what": "help them", "where": "a place I cannot name"}
    ])", error);
    INFO(error);
    REQUIRE(templates.has_value());
    return *templates;
}

JournalEntry ferryman()
{
    JournalEntry entry;
    entry.tick = 120;
    entry.who = "Kallias";
    entry.what = "carry the ash to the ferryman";
    entry.where = "the weir";
    entry.decided = "I agreed to go";
    return entry;
}

}

TEST_CASE("an entry reads as who, where, what and the decision")
{
    const JournalTemplates templates = plainTemplates();
    REQUIRE(composeJournal(ferryman(), templates) == "Kallias at the weir wants you to carry the ash to the ferryman. I agreed to go.");
}

TEST_CASE("the same entry always reads the same, and different entries can read differently")
{
    const JournalTemplates templates = shippedTemplates();
    const JournalEntry entry = ferryman();
    const std::string first = composeJournal(entry, templates);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(composeJournal(entry, templates) == first);
    }
    std::set<std::string> readings;
    for (std::uint64_t tick = 0; tick < 200; ++tick) {
        JournalEntry other = entry;
        other.tick = tick;
        readings.insert(composeJournal(other, templates));
    }
    REQUIRE(readings.size() > 4);
}

TEST_CASE("the pick is pinned, so it reads the same on every platform")
{
    const JournalTemplates templates = shippedTemplates();
    JournalEntry entry = ferryman();
    entry.tick = 7;
    // If the seeding or the pick changes this changes, and saved journals would read differently.
    const std::string prose = composeJournal(entry, templates);
    INFO(prose);
    REQUIRE(prose == "At the weir I heard Kallias out: they wished me to carry the ash to the ferryman. I thought it over, and I agreed to go.");
}

TEST_CASE("a missing who, what or where is replaced by words that still read as a sentence")
{
    const JournalTemplates templates = plainTemplates();
    JournalEntry entry = ferryman();
    entry.who.clear();
    REQUIRE(composeJournal(entry, templates) == "A stranger at the weir wants you to carry the ash to the ferryman. I agreed to go.");
    entry = ferryman();
    entry.what = "   ";
    REQUIRE(composeJournal(entry, templates) == "Kallias at the weir wants you to help them. I agreed to go.");
    entry = ferryman();
    entry.where = "";
    REQUIRE(composeJournal(entry, templates) == "Kallias at a place I cannot name wants you to carry the ash to the ferryman. I agreed to go.");
}

TEST_CASE("a missing decision says nothing was decided, and an empty entry still reads")
{
    const JournalTemplates templates = plainTemplates();
    JournalEntry entry = ferryman();
    entry.decided.clear();
    REQUIRE(composeJournal(entry, templates) == "Kallias at the weir wants you to carry the ash to the ferryman. Not decided about Kallias.");

    const std::string empty = composeJournal(JournalEntry{}, templates);
    REQUIRE(empty == "A stranger at a place I cannot name wants you to help them. Not decided about a stranger.");
}

TEST_CASE("fallback words are capitalised at the start of the prose and nowhere else")
{
    const JournalTemplates templates = shippedTemplates();
    for (std::uint64_t tick = 0; tick < 50; ++tick) {
        JournalEntry entry;
        entry.tick = tick;
        const std::string prose = composeJournal(entry, templates);
        REQUIRE_FALSE(prose.empty());
        REQUIRE(prose.front() >= 'A');
        REQUIRE(prose.front() <= 'Z');
        REQUIRE(prose.find('{') == std::string::npos);
        REQUIRE(prose.find("  ") == std::string::npos);
    }
}

TEST_CASE("names go in exactly as written, even when they look like placeholders or are not ASCII")
{
    const JournalTemplates templates = plainTemplates();
    JournalEntry entry = ferryman();
    entry.who = "{who}";
    entry.where = "{decided} {what} {";
    entry.what = "{where}}";
    entry.decided = "{who}";
    REQUIRE(composeJournal(entry, templates) == "{who} at {decided} {what} { wants you to {where}}. {who}.");

    entry = ferryman();
    entry.who = "caf\xC3\xA9 \xCE\x91\xCE\xB8\xCE\xB7\xCE\xBD\xE1\xBD\xB0 <b>&amp;</b> \xF0\x9F\x8F\xBA";
    const std::string prose = composeJournal(entry, templates);
    REQUIRE(prose.starts_with(entry.who + " at the weir"));
}

TEST_CASE("a lowercase name is not capitalised")
{
    const JournalTemplates templates = plainTemplates();
    JournalEntry entry = ferryman();
    entry.who = "kallias";
    REQUIRE(composeJournal(entry, templates).starts_with("kallias at"));
}

TEST_CASE("very long text is composed whole and does not break")
{
    const JournalTemplates templates = shippedTemplates();
    JournalEntry entry = ferryman();
    entry.who = std::string(100000, 'W');
    entry.what = std::string(50000, 'x');
    const std::string prose = composeJournal(entry, templates);
    REQUIRE(prose.size() > 150000);
    REQUIRE(prose.find(entry.who) != std::string::npos);
}

TEST_CASE("the shipped templates load, and every sentence they make is well formed")
{
    const JournalTemplates templates = shippedTemplates();
    REQUIRE(templates.asked().size() >= 3);
    REQUIRE(templates.decided().size() >= 3);
    REQUIRE(templates.undecided().size() >= 2);
    JournalEntry entry = ferryman();
    for (std::uint64_t tick = 0; tick < 300; ++tick) {
        entry.tick = tick;
        for (const bool decided : {true, false}) {
            entry.decided = decided ? "I agreed to go" : "";
            const std::string prose = composeJournal(entry, templates);
            REQUIRE(prose.back() == '.');
            REQUIRE(prose.find("--") == std::string::npos);
            REQUIRE(prose.find("\xE2\x80\x94") == std::string::npos);
        }
    }
}

TEST_CASE("templates that cannot be used are refused with a message")
{
    const std::string fallback = R"({"id": "fallback", "who": "a stranger", "what": "help them", "where": "a place"})";
    const std::string asked = R"({"id": "asked", "variants": ["{who} asked me to {what} at {where}."]})";
    const std::string decided = R"({"id": "decided", "variants": ["{decided}."]})";
    const std::string undecided = R"({"id": "undecided", "variants": ["Nothing yet."]})";
    const auto build = [&](const std::string& a, const std::string& d, const std::string& u, const std::string& f) {
        std::string error;
        const auto templates = templatesFromJson("[" + a + "," + d + "," + u + "," + f + "]", error);
        return std::pair{templates.has_value(), error};
    };
    REQUIRE(build(asked, decided, undecided, fallback).first);

    SECTION("an unknown placeholder")
    {
        const auto [ok, error] = build(R"({"id": "asked", "variants": ["{who} asked {whom}."]})", decided, undecided, fallback);
        REQUIRE_FALSE(ok);
        REQUIRE(error.find("whom") != std::string::npos);
    }
    SECTION("a placeholder that belongs to another sentence")
    {
        const auto [ok, error] = build(R"({"id": "asked", "variants": ["{who} asked, and I {decided}."]})", decided, undecided, fallback);
        REQUIRE_FALSE(ok);
        REQUIRE(error.find("decided") != std::string::npos);
    }
    SECTION("unbalanced braces")
    {
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": ["{who asked."]})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": ["who} asked."]})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": ["{{who}} asked."]})", decided, undecided, fallback).first);
    }
    SECTION("an em-dash or a stand-in for one")
    {
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": ["{who} asked — and waited."]})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": ["{who} asked -- and waited."]})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(asked, decided, undecided, R"({"id": "fallback", "who": "a stranger — unknown", "what": "help", "where": "a place"})").first);
    }
    SECTION("a record with no variants, or an empty one")
    {
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": []})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(R"({"id": "asked", "variants": [""]})", decided, undecided, fallback).first);
        REQUIRE_FALSE(build(R"({"id": "asked", "text": "no list"})", decided, undecided, fallback).first);
    }
    SECTION("a missing record, a missing fallback word, or a stray record")
    {
        std::string error;
        REQUIRE_FALSE(templatesFromJson("[" + asked + "," + decided + "," + fallback + "]", error).has_value());
        REQUIRE(error.find("undecided") != std::string::npos);
        REQUIRE_FALSE(build(asked, decided, undecided, R"({"id": "fallback", "who": "a stranger", "what": "help"})").first);
        REQUIRE_FALSE(build(asked, decided, undecided, R"({"id": "fallback", "who": "", "what": "help", "where": "a place"})").first);
        REQUIRE_FALSE(templatesFromJson("[" + asked + "," + decided + "," + undecided + "," + fallback + R"(,{"id": "extra", "variants": ["x"]}])", error).has_value());
    }
}

TEST_CASE("prose is stored in the chronicle and survives a save and load unchanged")
{
    const JournalTemplates templates = shippedTemplates();
    Chronicle chronicle;
    const JournalEntry entry = ferryman();
    const std::string prose = composeJournal(entry, templates);
    REQUIRE(chronicle.add(entry.tick, kJournalCategory, prose));

    Chronicle loaded;
    REQUIRE(Chronicle::parse(chronicle.serialize(), loaded));
    REQUIRE(loaded == chronicle);
    REQUIRE(loaded.entries().front().category == "journal");
    REQUIRE(loaded.entries().front().text == prose);
}

TEST_CASE("the journal lines are the journal entries only, newest first")
{
    Chronicle chronicle;
    chronicle.add(10, "quest", "not a journal entry");
    chronicle.add(20, "journal", "first");
    chronicle.add(30, "death", "also not");
    chronicle.add(40, "journal", "second");
    const auto lines = chronicle.recentIn(kJournalCategory, 1.0, 10);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].text == "second");
    REQUIRE(lines[0].time == "0:00:40");
    REQUIRE(lines[1].text == "first");
    REQUIRE(chronicle.recentIn(kJournalCategory, 1.0, 1).size() == 1);
    REQUIRE(chronicle.recentIn("nothing", 1.0, 5).empty());
}

TEST_CASE("ten thousand entries are stored and only the newest few are asked for")
{
    const JournalTemplates templates = shippedTemplates();
    Chronicle chronicle;
    JournalEntry entry = ferryman();
    for (std::uint64_t i = 0; i < 10000; ++i) {
        entry.tick = i;
        REQUIRE(chronicle.add(i, kJournalCategory, composeJournal(entry, templates)));
    }
    const auto lines = chronicle.recentIn(kJournalCategory, 1.0, 8);
    REQUIRE(lines.size() == 8);
    REQUIRE(lines.front().time == "2:46:39");
}

TEST_CASE("scripts record a journal entry at the current tick")
{
    Scene scene(1);
    Chronicle chronicle;
    std::optional<JournalTemplates> templates = plainTemplates();
    ScriptHost host(1);
    bindJournal(host, chronicle, scene, templates);
    scene.tick(1.0 / 60.0);
    REQUIRE(host.run("j", "ichor.journal('Kallias', 'carry the ash to the ferryman', 'the weir', 'I agreed to go')"));
    REQUIRE(chronicle.entries().size() == 1);
    REQUIRE(chronicle.entries()[0].tick == 1);
    REQUIRE(chronicle.entries()[0].category == "journal");
    REQUIRE(chronicle.entries()[0].text == "Kallias at the weir wants you to carry the ash to the ferryman. I agreed to go.");

    REQUIRE(host.run("short", "ichor.journal('Kallias')"));
    REQUIRE(chronicle.entries().size() == 2);
    REQUIRE(chronicle.entries()[1].text.starts_with("Kallias at a place I cannot name"));

    REQUIRE_FALSE(host.run("bad", "ichor.journal(1, 2, 3, 4)"));
    REQUIRE(chronicle.entries().size() == 2);

    templates.reset();
    REQUIRE_FALSE(host.run("none", "ichor.journal('a', 'b', 'c', 'd')"));
    REQUIRE(host.lastError().find("templates") != std::string::npos);
    REQUIRE(chronicle.entries().size() == 2);
}
