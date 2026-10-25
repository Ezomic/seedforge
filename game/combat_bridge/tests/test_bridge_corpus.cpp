#include "ichor/combat_bridge/bridge_corpus.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace ichor;
using namespace ichor::bridge_corpus;

namespace {

std::string slurp(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Fixtures are read-only inputs checked into the repo, so parallel ctest processes can share them.
std::vector<Entry> loadCorpus()
{
    std::vector<std::filesystem::path> paths;
    for (const auto& item : std::filesystem::directory_iterator(ICHOR_BRIDGE_CORPUS_DIR)) {
        if (item.path().extension() == ".replay") paths.push_back(item.path());
    }
    std::sort(paths.begin(), paths.end());
    std::vector<Entry> entries;
    for (const auto& path : paths) {
        const auto entry = decodeEntry(slurp(path));
        INFO(path.string());
        REQUIRE(entry.has_value());
        entries.push_back(*entry);
    }
    return entries;
}

const World& worldFor(std::uint64_t seed, int grid)
{
    static std::map<std::pair<std::uint64_t, int>, World> worlds;
    auto found = worlds.find({seed, grid});
    if (found == worlds.end()) {
        std::string error;
        World world = forgeWorld(seed, grid, error);
        INFO(error);
        REQUIRE(world.game != nullptr);
        found = worlds.emplace(std::make_pair(seed, grid), std::move(world)).first;
    }
    return found->second;
}

Entry sample()
{
    Entry entry;
    entry.skill = "novice";
    entry.worldSeed = 9;
    entry.grid = 64;
    entry.worldHash = 0xabcdef0123456789ULL;
    entry.sceneSeed = 4;
    entry.pick = 2;
    entry.frames.assign(3, InputFrame{});
    entry.frames.push_back({InputFrame::Attack | InputFrame::Block, 0.5f, -1.0f, 0.25f});
    entry.frames.push_back({InputFrame::Attack | InputFrame::Block, 0.5f, -1.0f, 0.25f});
    entry.expected = {true, true, stage0::DeathCause::Cornered, 5, 2, 1234, 0x0123456789abcdefULL};
    return entry;
}

}

TEST_CASE("every recorded forged-world fight replays to its recorded outcome", "[bridge_corpus]")
{
    const std::vector<Entry> corpus = loadCorpus();
    REQUIRE(corpus.size() == plan().size());
    for (const Entry& stored : corpus) {
        INFO(entryName(stored));
        const Resolved resolved = resolveEntry(stored, worldFor(stored.worldSeed, stored.grid));
        if (resolved.repicked) WARN(resolved.message);
        REQUIRE(resolved.entry.has_value());
        const Entry& entry = *resolved.entry;
        const Outcome actual = replayFight(worldFor(entry.worldSeed, entry.grid), entry.sceneSeed, entry.frames);
        CHECK(actual.finished);
        CHECK(actual.playerLost == entry.expected.playerLost);
        CHECK(actual.cause == entry.expected.cause);
        CHECK(actual.ticks == entry.expected.ticks);
        CHECK(actual.playerHits == entry.expected.playerHits);
        CHECK(actual.damageTakenTenths == entry.expected.damageTakenTenths);
        CHECK(actual.stateHash == entry.expected.stateHash);
    }
}

TEST_CASE("the corpus is the plan: skills, worlds and both winners", "[bridge_corpus]")
{
    const std::vector<Entry> corpus = loadCorpus();
    REQUIRE(corpus.size() == plan().size());
    REQUIRE(corpus.size() == 20);
    std::set<std::string> skills;
    std::set<std::string> seen;
    int wins = 0;
    int losses = 0;
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        INFO(entryName(corpus[i]));
        CHECK(corpus[i].skill == plan()[i].skill);
        CHECK(corpus[i].worldSeed == plan()[i].worldSeed);
        CHECK(corpus[i].pick == plan()[i].pick);
        CHECK(corpus[i].expected.playerLost == !plan()[i].playerWins);
        CHECK(seen.insert(entryName(corpus[i])).second);
        CHECK(corpus[i].grid == kGridSize);
        CHECK(corpus[i].worldHash != 0);
        CHECK(corpus[i].sceneSeed >= 1);
        CHECK(corpus[i].sceneSeed <= kSearchSeeds);
        CHECK(corpus[i].expected.finished);
        CHECK(corpus[i].expected.playerHits >= 0);
        skills.insert(corpus[i].skill);
        (corpus[i].expected.playerLost ? losses : wins) += 1;
    }
    CHECK(skills == std::set<std::string>{"novice", "regular", "expert"});
    CHECK(wins >= 5);
    CHECK(losses >= 5);
}

TEST_CASE("the bridge corpus goes red when a damage value changes", "[bridge_corpus]")
{
    const std::vector<Entry> corpus = loadCorpus();
    REQUIRE(!corpus.empty());
    // Four fights spread over the file are enough to see the knob, and keep the test short.
    std::vector<const Entry*> some;
    for (std::size_t i = 0; i < corpus.size(); i += 5) some.push_back(&corpus[i]);
    const auto changed = [&](const stage0::Tuning& tuning) {
        int differing = 0;
        for (const Entry* entry : some) {
            const Outcome actual = replayFight(worldFor(entry->worldSeed, entry->grid), entry->sceneSeed, entry->frames, tuning);
            differing += actual != entry->expected ? 1 : 0;
        }
        return differing;
    };
    // Control: untouched tuning matches every entry, so the count below is caused by the change alone.
    CHECK(changed(stage0::Tuning{}) == 0);

    stage0::Tuning light;
    light.lightDamage += 1.0f;
    CHECK(changed(light) >= static_cast<int>(some.size()) / 2);
}

TEST_CASE("a changed world re-picks the same fight again, and says so", "[bridge_corpus]")
{
    const std::vector<Entry> corpus = loadCorpus();
    REQUIRE(!corpus.empty());
    const Entry& stored = corpus.front();
    const World& world = worldFor(stored.worldSeed, stored.grid);

    const Resolved same = resolveEntry(stored, world);
    CHECK_FALSE(same.repicked);
    CHECK(same.message.empty());
    REQUIRE(same.entry.has_value());
    CHECK(same.entry->frames == stored.frames);

    Entry stale = stored;
    stale.worldHash ^= 1;
    stale.sceneSeed += 1;
    stale.frames.clear();
    const Resolved first = resolveEntry(stale, world);
    const Resolved second = resolveEntry(stale, world);
    CHECK(first.repicked);
    REQUIRE(first.entry.has_value());
    REQUIRE(second.entry.has_value());
    CHECK(first.entry->worldHash == worldHashOf(world));
    CHECK(first.entry->skill == stored.skill);
    CHECK(first.entry->expected.playerLost == stored.expected.playerLost);
    CHECK(first.entry->sceneSeed == stored.sceneSeed);
    CHECK(first.entry->frames == stored.frames);
    CHECK(first.entry->expected == stored.expected);
    CHECK(first.entry->sceneSeed == second.entry->sceneSeed);
    CHECK(first.message == second.message);
    CHECK(first.message.find("changed") != std::string::npos);
    CHECK(first.message.find("re-picked") != std::string::npos);
    CHECK(first.message.find("bin/regen-combat-bridge-corpus") != std::string::npos);
}

TEST_CASE("a recording made now matches replaying it, and a fight with no site records nothing", "[bridge_corpus]")
{
    const World& world = worldFor(1, kGridSize);
    const auto entry = recordEntry(world, "regular", 3);
    REQUIRE(entry.has_value());
    CHECK(entry->worldHash == worldHashOf(world));
    CHECK(replayFight(world, entry->sceneSeed, entry->frames) == entry->expected);
    CHECK_FALSE(recordEntry(world, "grandmaster", 3).has_value());
    CHECK_FALSE(recordEntry(World{}, "regular", 3).has_value());
    CHECK_FALSE(replayFight(World{}, 3, entry->frames).finished);
}

TEST_CASE("a replay that stops short says the fight did not finish", "[bridge_corpus]")
{
    const World& world = worldFor(1, kGridSize);
    const std::vector<InputFrame> idle(10, InputFrame{});
    const Outcome outcome = replayFight(world, 1, idle);
    CHECK_FALSE(outcome.finished);
    CHECK(outcome.ticks == 10);
    CHECK_FALSE(replayFight(world, 1, {}).finished);
}

TEST_CASE("an entry survives an encode and decode round trip", "[bridge_corpus]")
{
    const Entry entry = sample();
    const auto back = decodeEntry(encodeEntry(entry));
    REQUIRE(back.has_value());
    CHECK(back->skill == entry.skill);
    CHECK(back->worldSeed == entry.worldSeed);
    CHECK(back->grid == entry.grid);
    CHECK(back->worldHash == entry.worldHash);
    CHECK(back->sceneSeed == entry.sceneSeed);
    CHECK(back->pick == entry.pick);
    CHECK(back->frames == entry.frames);
    CHECK(back->expected == entry.expected);
}

TEST_CASE("malformed corpus text is refused", "[bridge_corpus]")
{
    const std::string good = encodeEntry(sample());
    REQUIRE(decodeEntry(good).has_value());
    CHECK_FALSE(decodeEntry("").has_value());
    CHECK_FALSE(decodeEntry("garbage\n").has_value());
    CHECK_FALSE(decodeEntry(good.substr(0, good.size() / 2)).has_value());
    CHECK_FALSE(decodeEntry(good.substr(0, good.find('\n') + 1)).has_value());

    const auto replaced = [&](const std::string& from, const std::string& to) {
        std::string text = good;
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), to);
        return text;
    };
    CHECK_FALSE(decodeEntry(replaced("ichor-combat-bridge-corpus 1", "ichor-combat-bridge-corpus 2")).has_value());
    CHECK_FALSE(decodeEntry(replaced("skill novice", "skill grandmaster")).has_value());
    CHECK_FALSE(decodeEntry(replaced("skill novice", "skill ")).has_value());
    CHECK_FALSE(decodeEntry(replaced("world_seed 9", "world_seed -1")).has_value());
    CHECK_FALSE(decodeEntry(replaced("grid 64", "grid 0")).has_value());
    CHECK_FALSE(decodeEntry(replaced("grid 64", "grid 4096")).has_value());
    CHECK_FALSE(decodeEntry(replaced("grid 64", "grid 128")).has_value());
    CHECK_FALSE(decodeEntry(replaced("world_hash abcdef0123456789", "world_hash 0000000000000000")).has_value());
    CHECK_FALSE(decodeEntry(replaced("pick 2", "pick -1")).has_value());
    CHECK_FALSE(decodeEntry(replaced("pick 2", "pick 64")).has_value());
    CHECK_FALSE(decodeEntry(replaced("grid 64", "grid 99999999")).has_value());
    CHECK_FALSE(decodeEntry(replaced("world_hash abcdef0123456789", "world_hash zz")).has_value());
    CHECK_FALSE(decodeEntry(replaced("ichor-stage0-corpus 1", "ichor-stage0-corpus 2")).has_value());
    CHECK_FALSE(decodeEntry(replaced("winner foe", "winner nobody")).has_value());
    CHECK_FALSE(decodeEntry(replaced("ticks 5", "ticks 6")).has_value());
    CHECK_FALSE(decodeEntry(replaced("2 40 0.5 -1 0.25", "2 40 nan -1 0.25")).has_value());
    CHECK_FALSE(decodeEntry(replaced("2 40 0.5 -1 0.25", "99999999999 40 0.5 -1 0.25")).has_value());
}

TEST_CASE("a forge that cannot run reports why", "[bridge_corpus]")
{
    std::string error;
    const World bad = forgeWorld(1, 0, error);
    CHECK(bad.game == nullptr);
    CHECK_FALSE(error.empty());
}
