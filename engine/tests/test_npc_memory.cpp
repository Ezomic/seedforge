#include "ichor/npc/npc_memory.h"
#include "ichor/save/chronicle.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

using namespace ichor;

namespace {

Chronicle sample()
{
    Chronicle story;
    story.add(100, "help", "Fed the ferryman.", {{7}, 0, 200});
    story.add(200, "harm", "Burned the mill.", {{9, 7}, 3, 255});
    story.add(300, "talk", "Said nothing of note.");
    story.add(400, "witness", "A brawl at the well.", {{}, 3, 50});
    return story;
}

}

TEST_CASE("an NPC remembers what names them or happened where they are")
{
    const Chronicle story = sample();
    const auto seven = memoryOf({7, 0}, story.entries(), 1000);
    REQUIRE(seven.size() == 2);
    REQUIRE(seven[0].index == 1); // heavier
    REQUIRE(seven[1].index == 0);

    const auto bystander = memoryOf({5, 3}, story.entries(), 1000);
    REQUIRE(bystander.size() == 2);
    REQUIRE(bystander[0].index == 1);
    REQUIRE(bystander[1].index == 3);

    REQUIRE(memoryOf({5, 0}, story.entries(), 1000).empty()); // never mentioned, place 0 matches nothing
}

TEST_CASE("memory of an empty ledger is empty")
{
    REQUIRE(memoryOf({1, 1}, {}, 5).empty());
}

TEST_CASE("salience never rises with age and cannot overflow")
{
    std::uint32_t last = memorySalience(255, 0, 0);
    REQUIRE(last == kMaxSalience);
    for (std::uint64_t age : {1ull, 10ull, 1000ull, 360000ull, 10000000ull, 1ull << 40, 1ull << 62, ~0ull}) {
        const std::uint32_t now = memorySalience(255, 0, age);
        REQUIRE(now <= last);
        last = now;
    }
    REQUIRE(memorySalience(255, 0, kMemoryHorizonTicks) == kMaxSalience / 2);
    REQUIRE(memorySalience(0, 5, 5) == 0);
}

TEST_CASE("an entry from the future counts as just happened")
{
    REQUIRE(memorySalience(100, 900, 10) == memorySalience(100, 10, 10));
}

TEST_CASE("ties break by newer tick, then by lower index")
{
    Chronicle story;
    story.add(10, "a", "x", {{1}, 0, 100});
    story.add(10, "a", "x", {{1}, 0, 100});
    story.add(10, "a", "x", {{1}, 0, 100}); // duplicates of one another
    story.add(20, "a", "x", {{1}, 0, 100});
    const auto got = memoryOf({1, 0}, story.entries(), 20);
    REQUIRE(got.size() == 4);
    // Older entries have lower salience, so the newest comes first, then the rest by index.
    REQUIRE(got[0].index == 3);
    REQUIRE(got[1].index == 0);
    REQUIRE(got[2].index == 1);
    REQUIRE(got[3].index == 2);

    Chronicle same;
    same.add(50, "a", "x", {{1}, 0, 100});
    same.add(50, "a", "y", {{1}, 0, 100});
    const auto equal = memoryOf({1, 0}, same.entries(), 50);
    REQUIRE(equal[0].index == 0);
    REQUIRE(equal[1].index == 1);
}

TEST_CASE("memory is capped and picks the strongest")
{
    Chronicle story;
    for (std::uint32_t i = 0; i < 100; ++i) {
        story.add(i, "a", "x", {{1}, 0, static_cast<std::uint8_t>(i + 1)});
    }
    const auto got = memoryOf({1, 0}, story.entries(), 100);
    REQUIRE(got.size() == kMaxRemembered);
    REQUIRE(got[0].index == 99);
    for (std::size_t i = 1; i < got.size(); ++i) {
        REQUIRE(got[i - 1].salience >= got[i].salience);
    }
}

TEST_CASE("a hundred thousand entries are answered and bounded")
{
    Chronicle story;
    for (std::uint32_t i = 0; i < Chronicle::kMaxEntries; ++i) {
        REQUIRE(story.add(i, "a", "x", {{i % 50}, 0, static_cast<std::uint8_t>(1 + i % 255)}));
    }
    const auto got = memoryOf({3, 0}, story.entries(), Chronicle::kMaxEntries);
    REQUIRE(got.size() == kMaxRemembered);
    REQUIRE(memoryOf({3, 0}, story.entries(), ~0ull).empty()); // everything has faded
}

TEST_CASE("memory matches after a save round trip and a custom rule is used")
{
    const Chronicle story = sample();
    Chronicle back;
    REQUIRE(Chronicle::parse(story.serialize(), back));
    REQUIRE(back == story);
    for (std::uint32_t npc : {0u, 5u, 7u, 9u}) {
        for (std::uint32_t place : {0u, 3u}) {
            const auto a = memoryOf({npc, place}, story.entries(), 777);
            const auto b = memoryOf({npc, place}, back.entries(), 777);
            REQUIRE(a.size() == b.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                REQUIRE(a[i].index == b[i].index);
                REQUIRE(a[i].salience == b[i].salience);
            }
        }
    }
    const auto all = memoryOf({0, 0}, story.entries(), 1000, [](const MemorySubject&, const ChronicleEntry& e) { return e.weight > 100; });
    REQUIRE(all.size() == 2);
}

TEST_CASE("chronicle tags are normalised, limited and saved only when present")
{
    Chronicle story;
    REQUIRE(story.add(1, "a", "x", {{9, 7, 9, 7}, 2, 255}));
    REQUIRE(story.entries()[0].npcs == std::vector<std::uint32_t>{7, 9});
    ChronicleTags many;
    for (std::uint32_t i = 0; i < Chronicle::kMaxTaggedNpcs + 1; ++i) {
        many.npcs.push_back(i);
    }
    REQUIRE_FALSE(story.add(2, "a", "x", many));

    Chronicle plain;
    plain.add(5, "quest", "done");
    REQUIRE(plain.serialize() == "ichor-chronicle 1\n5\tquest\tdone\n");

    Chronicle back;
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\n5\tquest\tdone\t7|x|3\n", back));
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\n5\tquest\tdone\t7|1|300\n", back));
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\n5\tquest\tdone\t7|1\n", back));
    REQUIRE(Chronicle::parse("ichor-chronicle 1\n5\tquest\tdone\t7,9|1|3\n", back));
    REQUIRE(back.entries()[0].npcs == std::vector<std::uint32_t>{7, 9});
}
