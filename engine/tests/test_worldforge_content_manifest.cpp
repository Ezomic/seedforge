#include <worldforge/content_manifest.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

BindingInputs sampleInputs(std::uint64_t seed = kSeed)
{
    BindingInputs in;
    in.worldSeed = seed;
    for (int i = 0; i < 6; ++i) in.pois.push_back({ "town-" + std::to_string(i), PoiKind::Settlement, "" });
    for (int i = 0; i < 4; ++i) in.pois.push_back({ "crypt-" + std::to_string(i), PoiKind::Dungeon, i % 2 ? "crypt" : "mine" });
    in.pois.push_back({ "ruin-0", PoiKind::Ruin, "" });
    in.pois.push_back({ "tower-0", PoiKind::Landmark, "" });
    for (int i = 0; i < 20; ++i) in.itemIds.push_back("item-" + std::to_string(i));
    in.npcArchetypes = { "smith", "priest", "merchant" };
    return in;
}

ContentManifest bind(const BindingInputs& in, const BindingParams& p = {})
{
    auto m = bindContent(in, p);
    REQUIRE(m.has_value());
    return *m;
}

bool isSettlement(const BindingInputs& in, const std::string& id)
{
    return std::any_of(in.pois.begin(), in.pois.end(), [&](const PoiInput& p) { return p.id == id && p.kind == PoiKind::Settlement; });
}

}

TEST_CASE("manifest binding is deterministic and matches the golden hash", "[worldforge][manifest]")
{
    const BindingInputs in = sampleInputs();
    const ContentManifest a = bind(in);
    const ContentManifest b = bind(in);
    CHECK(a == b);
    CHECK(hashManifest(a) == hashManifest(b));
    CHECK(hashManifest(a) == 0x0d14ec6884cb3c9aull);
}

TEST_CASE("manifest binding does not depend on input order", "[worldforge][manifest]")
{
    BindingInputs in = sampleInputs();
    const ContentManifest base = bind(in);
    std::reverse(in.pois.begin(), in.pois.end());
    std::reverse(in.itemIds.begin(), in.itemIds.end());
    std::reverse(in.npcArchetypes.begin(), in.npcArchetypes.end());
    CHECK(bind(in) == base);
}

TEST_CASE("manifest binding responds to the seed", "[worldforge][manifest]")
{
    CHECK(hashManifest(bind(sampleInputs(1))) != hashManifest(bind(sampleInputs(2))));
}

TEST_CASE("bound content has the promised shape", "[worldforge][manifest]")
{
    const BindingInputs in = sampleInputs();
    const ContentManifest m = bind(in, { 3, 5 });
    CHECK(m.items.size() == 20);
    for (const ItemPlacement& i : m.items) {
        const auto poi = std::find_if(in.pois.begin(), in.pois.end(), [&](const PoiInput& p) { return p.id == i.poiId; });
        REQUIRE(poi != in.pois.end());
        CHECK((poi->kind == PoiKind::Dungeon || poi->kind == PoiKind::Ruin));
    }
    CHECK(m.cast.size() == 9);
    for (const CastMember& c : m.cast) CHECK(isSettlement(in, c.homePoiId));
    CHECK(m.tensions.size() == 5);
    std::set<std::pair<std::string, std::string>> pairs;
    for (const QuestTension& t : m.tensions) {
        CHECK(t.poiA < t.poiB);
        CHECK(isSettlement(in, t.poiA));
        CHECK(isSettlement(in, t.poiB));
        CHECK(pairs.insert({ t.poiA, t.poiB }).second);
    }
    REQUIRE(m.dungeons.size() == 4);
    CHECK(m.dungeons[0].poiId == "crypt-0");
    CHECK(m.dungeons[0].archetype == "mine");
    CHECK(m.dungeons[0].seed == dungeonSeed(kSeed, "crypt-0"));
}

TEST_CASE("tension count is capped by the available settlement pairs", "[worldforge][manifest]")
{
    BindingInputs in = sampleInputs();
    in.pois.erase(std::remove_if(in.pois.begin(), in.pois.end(), [](const PoiInput& p) { return p.id == "town-3" || p.id == "town-4" || p.id == "town-5"; }), in.pois.end());
    CHECK(bind(in, { 1, 100 }).tensions.size() == 3);
    BindingInputs one = in;
    one.pois.erase(std::remove_if(one.pois.begin(), one.pois.end(), [](const PoiInput& p) { return p.id == "town-1" || p.id == "town-2"; }), one.pois.end());
    CHECK(bind(one, { 1, 100 }).tensions.empty());
}

TEST_CASE("empty inputs bind to an empty manifest", "[worldforge][manifest]")
{
    const ContentManifest m = bind(BindingInputs{});
    CHECK(m.items.empty());
    CHECK(m.cast.empty());
    CHECK(m.tensions.empty());
    CHECK(m.dungeons.empty());
    CHECK(decodeManifest(encodeManifest(m)) == m);
}

TEST_CASE("binding rejects bad inputs", "[worldforge][manifest]")
{
    BindingInputs dupPoi = sampleInputs();
    dupPoi.pois.push_back(dupPoi.pois.front());
    CHECK_FALSE(bindContent(dupPoi).has_value());

    BindingInputs dupItem = sampleInputs();
    dupItem.itemIds.push_back("item-0");
    CHECK_FALSE(bindContent(dupItem).has_value());

    BindingInputs emptyId = sampleInputs();
    emptyId.itemIds.push_back("");
    CHECK_FALSE(bindContent(emptyId).has_value());

    BindingInputs longId = sampleInputs();
    longId.pois.push_back({ std::string(kManifestMaxIdLength + 1, 'x'), PoiKind::Ruin, "" });
    CHECK_FALSE(bindContent(longId).has_value());

    BindingInputs noArchetype = sampleInputs();
    noArchetype.pois.push_back({ "crypt-x", PoiKind::Dungeon, "" });
    CHECK_FALSE(bindContent(noArchetype).has_value());

    BindingInputs badKind = sampleInputs();
    badKind.pois.push_back({ "weird", static_cast<PoiKind>(200), "" });
    CHECK_FALSE(bindContent(badKind).has_value());

    BindingInputs longArchetype = sampleInputs();
    longArchetype.npcArchetypes.push_back(std::string(kManifestMaxIdLength, 'a'));
    CHECK_FALSE(bindContent(longArchetype).has_value());
    longArchetype.npcArchetypes.back().resize(kManifestMaxIdLength - 3);
    const auto fits = bindContent(longArchetype, { kManifestMaxPerArchetype, 1 });
    REQUIRE(fits.has_value());
    CHECK(decodeManifest(encodeManifest(*fits)) == *fits);

    CHECK_FALSE(bindContent(sampleInputs(), { -1, 1 }).has_value());
    CHECK_FALSE(bindContent(sampleInputs(), { kManifestMaxPerArchetype + 1, 1 }).has_value());
    CHECK_FALSE(bindContent(sampleInputs(), { 1, -1 }).has_value());
    CHECK_FALSE(bindContent(sampleInputs(), { 1, 1 << 30 }).has_value());
}

TEST_CASE("manifest codec round trips and is canonical", "[worldforge][manifest][codec]")
{
    const ContentManifest m = bind(sampleInputs());
    const std::vector<std::uint8_t> bytes = encodeManifest(m);
    const auto back = decodeManifest(bytes);
    REQUIRE(back.has_value());
    CHECK(*back == m);
    CHECK(encodeManifest(*back) == bytes);
}

TEST_CASE("manifest decode rejects hostile bytes", "[worldforge][manifest][codec]")
{
    const std::vector<std::uint8_t> good = encodeManifest(bind(sampleInputs()));

    SECTION("every truncation fails")
    {
        for (std::size_t n = 0; n < good.size(); ++n) {
            CHECK_FALSE(decodeManifest(std::span<const std::uint8_t>(good.data(), n)).has_value());
        }
    }
    SECTION("trailing bytes fail")
    {
        std::vector<std::uint8_t> extra = good;
        extra.push_back(0);
        CHECK_FALSE(decodeManifest(extra).has_value());
    }
    SECTION("bad magic and version fail")
    {
        std::vector<std::uint8_t> bad = good;
        bad[0] ^= 1;
        CHECK_FALSE(decodeManifest(bad).has_value());
        bad = good;
        bad[4] = 2;
        CHECK_FALSE(decodeManifest(bad).has_value());
        bad = good;
        bad[4] = 0;
        CHECK_FALSE(decodeManifest(bad).has_value());
    }
    SECTION("huge counts fail without allocating")
    {
        // An empty manifest is magic, version, seed and five counts; each count in turn is made hostile.
        const std::vector<std::uint8_t> empty = encodeManifest(ContentManifest{});
        REQUIRE(empty.size() == 36);
        for (std::size_t field = 16; field < 36; field += 4) {
            std::vector<std::uint8_t> hostile = empty;
            for (std::size_t i = field; i < field + 4; ++i) hostile[i] = 0xFF;
            CHECK_FALSE(decodeManifest(hostile).has_value());
            hostile = empty;
            hostile[field] = 1;
            CHECK_FALSE(decodeManifest(hostile).has_value());
        }
    }
    SECTION("single byte flips never crash and a surviving decode re-encodes to the same bytes")
    {
        std::size_t rejected = 0;
        for (std::size_t i = 0; i < good.size(); ++i) {
            std::vector<std::uint8_t> bad = good;
            bad[i] ^= 0x55;
            const auto d = decodeManifest(bad);
            if (d) CHECK(encodeManifest(*d) == bad);
            else ++rejected;
        }
        CHECK(rejected > good.size() / 4);
    }
    SECTION("empty and garbage input fail")
    {
        CHECK_FALSE(decodeManifest({}).has_value());
        const std::vector<std::uint8_t> garbage(256, 0xAB);
        CHECK_FALSE(decodeManifest(garbage).has_value());
    }
}

TEST_CASE("manifest decode rejects dangling references and unsorted ids", "[worldforge][manifest][codec]")
{
    ContentManifest m = bind(sampleInputs());

    ContentManifest dangling = m;
    dangling.items.front().poiId = "nowhere";
    CHECK_FALSE(decodeManifest(encodeManifest(dangling)).has_value());

    ContentManifest unsortedPois = m;
    std::swap(unsortedPois.poiIds.front(), unsortedPois.poiIds.back());
    CHECK_FALSE(decodeManifest(encodeManifest(unsortedPois)).has_value());

    ContentManifest dupNpc = m;
    dupNpc.cast.push_back(dupNpc.cast.front());
    CHECK_FALSE(decodeManifest(encodeManifest(dupNpc)).has_value());

    ContentManifest backwardsTension = m;
    REQUIRE_FALSE(backwardsTension.tensions.empty());
    std::swap(backwardsTension.tensions.front().poiA, backwardsTension.tensions.front().poiB);
    CHECK_FALSE(decodeManifest(encodeManifest(backwardsTension)).has_value());

    ContentManifest wrongSeed = m;
    REQUIRE_FALSE(wrongSeed.dungeons.empty());
    wrongSeed.dungeons.front().seed ^= 1;
    CHECK_FALSE(decodeManifest(encodeManifest(wrongSeed)).has_value());

    ContentManifest emptyId = m;
    emptyId.cast.front().npcId.clear();
    CHECK_FALSE(decodeManifest(encodeManifest(emptyId)).has_value());
}
