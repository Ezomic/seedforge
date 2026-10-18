#include "ichor/stage0/replay_corpus.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::stage0;

namespace {

std::string slurp(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Fixtures are read-only inputs checked into the repo, so parallel ctest processes can share them.
std::vector<CorpusEntry> loadCorpus()
{
    std::vector<std::filesystem::path> paths;
    for (const auto& item : std::filesystem::directory_iterator(ICHOR_CORPUS_DIR)) {
        if (item.path().extension() == ".replay") paths.push_back(item.path());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<CorpusEntry> entries;
    for (const auto& path : paths) {
        const auto entry = decodeCorpusEntry(slurp(path));
        INFO(path.string());
        REQUIRE(entry.has_value());
        entries.push_back(*entry);
    }
    return entries;
}

CorpusEntry sample()
{
    CorpusEntry entry;
    entry.name = "novice-seed-9";
    entry.seed = 9;
    entry.frames.assign(3, InputFrame{});
    entry.frames.push_back({InputFrame::Attack | InputFrame::Block, 0.5f, -1.0f, 0.25f});
    entry.frames.push_back({InputFrame::Attack | InputFrame::Block, 0.5f, -1.0f, 0.25f});
    entry.expected = {true, true, DeathCause::Cornered, 5, 2, 1234, 0xabcdef0123456789ULL};
    return entry;
}

}

TEST_CASE("every recorded fight replays to its recorded outcome", "[corpus][replay]")
{
    const std::vector<CorpusEntry> corpus = loadCorpus();
    REQUIRE(corpus.size() == corpusPlan().size());
    for (const CorpusEntry& entry : corpus) {
        INFO(entry.name);
        const CorpusOutcome actual = replayFight(entry.seed, entry.frames);
        CHECK(actual.finished);
        CHECK(actual.playerLost == entry.expected.playerLost);
        CHECK(actual.cause == entry.expected.cause);
        CHECK(actual.ticks == entry.expected.ticks);
        CHECK(actual.playerHits == entry.expected.playerHits);
        CHECK(actual.damageTakenTenths == entry.expected.damageTakenTenths);
        CHECK(actual.stateHash == entry.expected.stateHash);
    }
}

TEST_CASE("the corpus holds both wins and losses", "[corpus][replay]")
{
    const std::vector<CorpusEntry> corpus = loadCorpus();
    int wins = 0;
    int losses = 0;
    for (const CorpusEntry& entry : corpus) (entry.expected.playerLost ? losses : wins) += 1;
    CHECK(wins >= 3);
    CHECK(losses >= 3);
}

TEST_CASE("the corpus goes red when a damage value changes", "[corpus][replay]")
{
    const std::vector<CorpusEntry> corpus = loadCorpus();
    REQUIRE(!corpus.empty());
    const auto changed = [&](const Tuning& tuning) {
        int differing = 0;
        for (const CorpusEntry& entry : corpus) differing += replayFight(entry.seed, entry.frames, tuning) != entry.expected ? 1 : 0;
        return differing;
    };
    // Control: untouched tuning matches every entry, so the counts below are caused by the change alone.
    CHECK(changed(Tuning{}) == 0);

    Tuning light;
    light.lightDamage += 1.0f;
    CHECK(changed(light) >= static_cast<int>(corpus.size()) / 2);

    Tuning heavy;
    heavy.heavyDamage += 1.0f;
    CHECK(changed(heavy) >= 1);

    Tuning block;
    block.blockDamageRatio += 0.05f;
    CHECK(changed(block) >= 1);
}

TEST_CASE("an entry survives an encode and decode round trip", "[corpus][replay]")
{
    const CorpusEntry entry = sample();
    const auto back = decodeCorpusEntry(encodeCorpusEntry(entry));
    REQUIRE(back.has_value());
    CHECK(back->name == entry.name);
    CHECK(back->seed == entry.seed);
    CHECK(back->frames == entry.frames);
    CHECK(back->expected == entry.expected);
}

TEST_CASE("a recording made now matches replaying it", "[corpus][replay]")
{
    const auto entry = recordCorpusEntry("regular", 3);
    REQUIRE(entry.has_value());
    CHECK(replayFight(entry->seed, entry->frames) == entry->expected);
    CHECK_FALSE(recordCorpusEntry("grandmaster", 3).has_value());
}

TEST_CASE("a replay stops at the end of the fight and an unfinished one says so", "[corpus][replay]")
{
    const std::vector<InputFrame> idle(10, InputFrame{});
    const CorpusOutcome outcome = replayFight(1, idle);
    CHECK_FALSE(outcome.finished);
    CHECK(outcome.ticks == 10);
}

TEST_CASE("malformed corpus text is refused", "[corpus][replay]")
{
    const std::string good = encodeCorpusEntry(sample());
    REQUIRE(decodeCorpusEntry(good).has_value());
    CHECK_FALSE(decodeCorpusEntry("").has_value());
    CHECK_FALSE(decodeCorpusEntry("garbage\n").has_value());
    CHECK_FALSE(decodeCorpusEntry(good.substr(0, good.size() / 2)).has_value());

    const auto replaced = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };
    CHECK_FALSE(decodeCorpusEntry(replaced("ichor-stage0-corpus 1", "ichor-stage0-corpus 2")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("winner foe", "winner nobody")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("cause cornered", "cause mystery")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("ticks 5", "ticks 6")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("runs 2", "runs 3")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("seed 9", "seed -1")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("hash abcdef0123456789", "hash zz")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("2 40 0.5 -1 0.25", "0 40 0.5 -1 0.25")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("2 40 0.5 -1 0.25", "2 40 nan -1 0.25")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("2 40 0.5 -1 0.25", "2 40 inf -1 0.25")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("2 40 0.5 -1 0.25", "2 40 0.5 -1")).has_value());
    CHECK_FALSE(decodeCorpusEntry(replaced("2 40 0.5 -1 0.25", "99999999999 40 0.5 -1 0.25")).has_value());
}
