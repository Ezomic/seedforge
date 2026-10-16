#include "ichor/traversal/info_loot.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <string>

using namespace ichor;

namespace {

constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();

InfoItemDef mapItem(const std::string& id, std::vector<VantageCell> cells, bool singleUse = false)
{
    InfoItemDef d;
    d.id = id;
    d.kind = InfoKind::Map;
    d.cells = std::move(cells);
    d.singleUse = singleUse;
    return d;
}

InfoItemDef chartItem(const std::string& id, std::vector<std::string> routes)
{
    InfoItemDef d;
    d.id = id;
    d.kind = InfoKind::Chart;
    d.routeIds = std::move(routes);
    return d;
}

InfoItemDef cipherItem(const std::string& id, std::vector<InfoCipherText> texts)
{
    InfoItemDef d;
    d.id = id;
    d.kind = InfoKind::Cipher;
    d.texts = std::move(texts);
    return d;
}

InfoItemDef keyItem(const std::string& id, const std::string& lock, bool singleUse = false)
{
    InfoItemDef d;
    d.id = id;
    d.kind = InfoKind::Key;
    d.lockId = lock;
    d.singleUse = singleUse;
    return d;
}

InfoLootRegistry sample()
{
    InfoLootRegistry r;
    REQUIRE(r.add(mapItem("fair_copy", {{2, 1}, {0, 0}, {1, 0}, {0, 0}})) == InfoDefStatus::Ok);
    REQUIRE(r.add(chartItem("periplus", {"route.b", "route.a", "route.b"})) == InfoDefStatus::Ok);
    REQUIRE(r.add(cipherItem("lexicon", {{"inscription.1", "the door is the wall"}, {"inscription.2", "dig"}}))
        == InfoDefStatus::Ok);
    REQUIRE(r.add(keyItem("key_ring", "vault.north")) == InfoDefStatus::Ok);
    REQUIRE(r.add(keyItem("thread", "maze.exit", true)) == InfoDefStatus::Ok);
    REQUIRE(r.add(mapItem("torn_map", {{5, 5}}, true)) == InfoDefStatus::Ok);
    return r;
}

bool unchanged(const InfoLootState& a, const InfoLootState& b)
{
    return a.spentItems == b.spentItems && a.revealedCells == b.revealedCells && a.markedRoutes == b.markedRoutes
        && a.decodedTexts == b.decodedTexts && a.openedLocks == b.openedLocks;
}

}

TEST_CASE("a map reveals its cells into a sorted unique set", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto r = useInfoItem(reg, st, "fair_copy");
    REQUIRE(r.status == InfoUseStatus::Ok);
    CHECK(r.newCells == 3);
    REQUIRE(st.revealedCells.size() == 3);
    CHECK(std::is_sorted(st.revealedCells.begin(), st.revealedCells.end(), vantageCellLess));
    CHECK(st.revealedCells[0] == VantageCell{0, 0});
    CHECK(st.revealedCells[2] == VantageCell{2, 1});
}

TEST_CASE("a map merges with cells already known and reuse adds nothing", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    st.revealedCells = {{1, 0}, {9, 9}};
    CHECK(useInfoItem(reg, st, "fair_copy").newCells == 2);
    CHECK(st.revealedCells.size() == 4);
    const auto again = useInfoItem(reg, st, "fair_copy");
    CHECK(again.status == InfoUseStatus::Ok);
    CHECK(again.newCells == 0);
    CHECK_FALSE(again.spent);
}

TEST_CASE("an empty map is rejected at registration", "[info_loot]")
{
    InfoLootRegistry r;
    CHECK(r.add(mapItem("blank", {})) == InfoDefStatus::Malformed);
    CHECK(r.size() == 0);
}

TEST_CASE("maps hold extreme coordinates", "[info_loot]")
{
    InfoLootRegistry r;
    REQUIRE(r.add(mapItem("edge", {{kMax, kMax}, {kMin, kMin}, {kMax, kMin}, {kMin, kMax}})) == InfoDefStatus::Ok);
    InfoLootState st;
    const auto res = useInfoItem(r, st, "edge");
    CHECK(res.newCells == 4);
    CHECK(std::is_sorted(st.revealedCells.begin(), st.revealedCells.end(), vantageCellLess));
    CHECK(st.revealedCells.front() == VantageCell{kMin, kMin});
    CHECK(st.revealedCells.back() == VantageCell{kMax, kMax});
}

TEST_CASE("a single-use map is spent by its first use", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto first = useInfoItem(reg, st, "torn_map");
    CHECK(first.status == InfoUseStatus::Ok);
    CHECK(first.spent);
    CHECK(st.spentItems == std::vector<std::string>{"torn_map"});
    const InfoLootState before = st;
    CHECK(useInfoItem(reg, st, "torn_map").status == InfoUseStatus::Spent);
    CHECK(unchanged(before, st));
}

TEST_CASE("a chart marks its routes sorted and unique and reports only new ones", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    st.markedRoutes = {"route.a"};
    const auto r = useInfoItem(reg, st, "periplus");
    REQUIRE(r.status == InfoUseStatus::Ok);
    CHECK(r.newRoutes == std::vector<std::string>{"route.b"});
    CHECK(st.markedRoutes == std::vector<std::string>{"route.a", "route.b"});
    CHECK(useInfoItem(reg, st, "periplus").newRoutes.empty());
}

TEST_CASE("a cipher decodes a hidden text once", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto r = useInfoItem(reg, st, "lexicon", "inscription.1");
    REQUIRE(r.status == InfoUseStatus::Ok);
    CHECK(r.plain == "the door is the wall");
    CHECK(st.decodedTexts == std::vector<std::string>{"inscription.1"});
    CHECK_FALSE(r.spent);

    const InfoLootState before = st;
    const auto again = useInfoItem(reg, st, "lexicon", "inscription.1");
    CHECK(again.status == InfoUseStatus::AlreadyDone);
    CHECK(again.plain.empty());
    CHECK(unchanged(before, st));

    CHECK(useInfoItem(reg, st, "lexicon", "inscription.2").plain == "dig");
}

TEST_CASE("a cipher with a wrong or missing text id changes nothing", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    CHECK(useInfoItem(reg, st, "lexicon", "inscription.9").status == InfoUseStatus::WrongTarget);
    CHECK(useInfoItem(reg, st, "lexicon").status == InfoUseStatus::MissingTarget);
    CHECK(unchanged(st, InfoLootState{}));
}

TEST_CASE("a key opens the lock it is cut for", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto r = useInfoItem(reg, st, "key_ring", "vault.north");
    REQUIRE(r.status == InfoUseStatus::Ok);
    CHECK(r.openedLock == "vault.north");
    CHECK(st.openedLocks == std::vector<std::string>{"vault.north"});
    CHECK_FALSE(r.spent);

    const InfoLootState before = st;
    CHECK(useInfoItem(reg, st, "key_ring", "vault.north").status == InfoUseStatus::AlreadyDone);
    CHECK(unchanged(before, st));
}

TEST_CASE("a wrong key opens nothing and is not spent", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto r = useInfoItem(reg, st, "thread", "vault.north");
    CHECK(r.status == InfoUseStatus::WrongTarget);
    CHECK_FALSE(r.spent);
    CHECK(r.openedLock.empty());
    CHECK(unchanged(st, InfoLootState{}));
    CHECK(useInfoItem(reg, st, "thread", "maze.exit").status == InfoUseStatus::Ok);
}

TEST_CASE("a single-use key is spent by the lock it opens", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    const auto r = useInfoItem(reg, st, "thread", "maze.exit");
    CHECK(r.spent);
    CHECK(useInfoItem(reg, st, "thread", "maze.exit").status == InfoUseStatus::Spent);
    CHECK(st.openedLocks.size() == 1);
}

TEST_CASE("unknown items and hostile ids are refused without touching state", "[info_loot]")
{
    const auto reg = sample();
    InfoLootState st;
    CHECK(useInfoItem(reg, st, "nothing").status == InfoUseStatus::UnknownItem);
    CHECK(useInfoItem(reg, st, "").status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, "FAIR_COPY").status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, std::string("fair_copy\0x", 11)).status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, std::string(100000, 'a')).status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, "../../etc/passwd").status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, "key_ring", std::string(100000, 'b')).status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, "key_ring", "vault north").status == InfoUseStatus::InvalidId);
    CHECK(useInfoItem(reg, st, "key_ring", "\xff\xfe").status == InfoUseStatus::InvalidId);
    CHECK(unchanged(st, InfoLootState{}));
}

TEST_CASE("registration rejects bad, duplicate and oversized definitions", "[info_loot]")
{
    InfoLootRegistry r;
    CHECK(r.add(keyItem("", "lock")) == InfoDefStatus::InvalidId);
    CHECK(r.add(keyItem("Bad Id", "lock")) == InfoDefStatus::InvalidId);
    CHECK(r.add(keyItem("k", "")) == InfoDefStatus::Malformed);
    CHECK(r.add(keyItem("k", "bad lock")) == InfoDefStatus::InvalidId);
    REQUIRE(r.add(keyItem("k", "lock")) == InfoDefStatus::Ok);
    CHECK(r.add(keyItem("k", "other")) == InfoDefStatus::Duplicate);
    CHECK(r.add(chartItem("c", {})) == InfoDefStatus::Malformed);
    CHECK(r.add(chartItem("c", {"bad route"})) == InfoDefStatus::InvalidId);
    CHECK(r.add(cipherItem("z", {})) == InfoDefStatus::Malformed);
    CHECK(r.add(cipherItem("z", {{"t", std::string(kInfoMaxTextLength + 1, 'x')}})) == InfoDefStatus::TooLarge);
    CHECK(r.add(chartItem("c", std::vector<std::string>(kInfoMaxEntries + 1, "r"))) == InfoDefStatus::TooLarge);
    CHECK(r.add(mapItem("m", std::vector<VantageCell>(kInfoMaxMapCells + 1))) == InfoDefStatus::TooLarge);
    CHECK(r.size() == 1);
    CHECK(r.find("k") != nullptr);
    CHECK(r.find("missing") == nullptr);
}

TEST_CASE("a full state refuses a map and spends nothing", "[info_loot]")
{
    InfoLootRegistry r;
    REQUIRE(r.add(mapItem("m", {{0, 0}}, true)) == InfoDefStatus::Ok);
    InfoLootState st;
    st.revealedCells.reserve(kInfoMaxStateRevealedCells);
    for (std::size_t i = 0; i < kInfoMaxStateRevealedCells; ++i) {
        st.revealedCells.push_back({1, static_cast<std::int32_t>(i)});
    }
    const InfoLootState before = st;
    CHECK(useInfoItem(r, st, "m").status == InfoUseStatus::StateFull);
    CHECK(st.revealedCells == before.revealedCells);
    CHECK(st.spentItems.empty());
}

TEST_CASE("results do not depend on registration order", "[info_loot]")
{
    InfoLootRegistry a;
    InfoLootRegistry b;
    a.add(keyItem("a", "l1"));
    a.add(keyItem("b", "l2"));
    b.add(keyItem("b", "l2"));
    b.add(keyItem("a", "l1"));
    InfoLootState sa;
    InfoLootState sb;
    CHECK(useInfoItem(a, sa, "b", "l2").status == useInfoItem(b, sb, "b", "l2").status);
    CHECK(unchanged(sa, sb));
}

TEST_CASE("a failed use of a single-use cipher does not spend it", "[info_loot]")
{
    InfoLootRegistry r;
    InfoItemDef d = cipherItem("once", {{"t.1", "alpha"}});
    d.singleUse = true;
    REQUIRE(r.add(d) == InfoDefStatus::Ok);
    InfoLootState st;
    CHECK(useInfoItem(r, st, "once", "t.9").status == InfoUseStatus::WrongTarget);
    CHECK(useInfoItem(r, st, "once").status == InfoUseStatus::MissingTarget);
    CHECK(st.spentItems.empty());
    CHECK(useInfoItem(r, st, "once", "t.1").spent);
    CHECK(useInfoItem(r, st, "once", "t.1").status == InfoUseStatus::Spent);
}

TEST_CASE("a single-use chart is spent by its first use", "[info_loot]")
{
    InfoLootRegistry r;
    InfoItemDef d = chartItem("one_chart", {"route.a"});
    d.singleUse = true;
    REQUIRE(r.add(d) == InfoDefStatus::Ok);
    InfoLootState st;
    CHECK(useInfoItem(r, st, "one_chart").spent);
    CHECK(useInfoItem(r, st, "one_chart").status == InfoUseStatus::Spent);
}

TEST_CASE("cipher definitions are validated and deduplicated", "[info_loot]")
{
    InfoLootRegistry r;
    CHECK(r.add(cipherItem("c", {{"bad id", "x"}})) == InfoDefStatus::InvalidId);
    CHECK(r.add(cipherItem("c", {{"t", ""}})) == InfoDefStatus::Malformed);
    CHECK(r.add(cipherItem("c", std::vector<InfoCipherText>(kInfoMaxEntries + 1, {"t", "x"})))
        == InfoDefStatus::TooLarge);
    REQUIRE(r.add(cipherItem("c", {{"t", "first"}, {"t", "second"}})) == InfoDefStatus::Ok);
    InfoLootState st;
    CHECK(useInfoItem(r, st, "c", "t").plain == "first");
}

TEST_CASE("full state lists refuse and spend nothing", "[info_loot]")
{
    InfoLootRegistry r;
    InfoItemDef k = keyItem("k", "lock.new", true);
    REQUIRE(r.add(k) == InfoDefStatus::Ok);
    REQUIRE(r.add(chartItem("c", {"route.new"})) == InfoDefStatus::Ok);
    REQUIRE(r.add(cipherItem("z", {{"t.new", "x"}})) == InfoDefStatus::Ok);
    std::vector<std::string> full;
    for (std::size_t i = 0; i < kInfoMaxStateEntries; ++i) {
        full.push_back("a" + std::to_string(i));
    }
    std::sort(full.begin(), full.end());

    InfoLootState st;
    st.openedLocks = full;
    st.markedRoutes = full;
    st.decodedTexts = full;
    CHECK(useInfoItem(r, st, "k", "lock.new").status == InfoUseStatus::StateFull);
    CHECK(useInfoItem(r, st, "c").status == InfoUseStatus::StateFull);
    CHECK(useInfoItem(r, st, "z", "t.new").status == InfoUseStatus::StateFull);
    CHECK(st.openedLocks.size() == kInfoMaxStateEntries);
    CHECK(st.markedRoutes.size() == kInfoMaxStateEntries);
    CHECK(st.decodedTexts.size() == kInfoMaxStateEntries);

    InfoLootState spentFull;
    spentFull.spentItems = full;
    CHECK(useInfoItem(r, spentFull, "k", "lock.new").status == InfoUseStatus::StateFull);
    CHECK(spentFull.openedLocks.empty());
}

TEST_CASE("lookup works whatever order items were registered in", "[info_loot]")
{
    InfoLootRegistry a;
    InfoLootRegistry b;
    const char* ids[] = {"d", "b", "a", "c"};
    for (const char* id : ids) {
        a.add(keyItem(id, std::string("lock.") + id));
    }
    for (int i = 3; i >= 0; --i) {
        b.add(keyItem(ids[i], std::string("lock.") + ids[i]));
    }
    for (const char* id : ids) {
        REQUIRE(a.find(id) != nullptr);
        REQUIRE(b.find(id) != nullptr);
        InfoLootState sa;
        InfoLootState sb;
        const auto ra = useInfoItem(a, sa, id, std::string("lock.") + id);
        const auto rb = useInfoItem(b, sb, id, std::string("lock.") + id);
        CHECK(ra.status == InfoUseStatus::Ok);
        CHECK(ra.openedLock == rb.openedLock);
        CHECK(unchanged(sa, sb));
    }
}
