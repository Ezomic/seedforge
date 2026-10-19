#include "ichor/data/data_set.h"
#include "ichor/world/forged_heightfield.h"
#include "ichor/world/placed_loot.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>

using namespace ichor;

namespace {

constexpr int kGrid = 128;

const std::vector<LootItemInfo>& catalogue()
{
    static const std::vector<LootItemInfo> items = [] {
        DataSet data;
        REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
        std::vector<LootItemInfo> out;
        for (const char* tableName : {"notable_items", "catalogue"}) {
            const DataTable* table = data.table(tableName);
            REQUIRE(table != nullptr);
            for (const auto& [id, record] : table->records) {
                const auto text = [&](const char* key) {
                    const auto it = record.fields.find(key);
                    return it != record.fields.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : std::string{};
                };
                out.push_back({id, text("tier"), text("class")});
            }
        }
        return out;
    }();
    return items;
}

WorldDesc worldDesc(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 2048;
    desc.metresPerSample = 8;
    desc.cellMetres = 512;
    return forgedWorldDesc(desc);
}

struct Fixture {
    worldforge::ForgedWorld world;
    Heightfield field;
    PlacedPois pois;
    PlacedLootResult loot;
};

Fixture build(std::uint64_t seed)
{
    const WorldDesc desc = worldDesc(seed);
    worldforge::ForgeConfig config;
    config.dungeonArchetypes = {"crypt", "cave", "mine"};
    for (const LootItemInfo& item : catalogue()) config.itemIds.push_back(item.id);
    worldforge::ForgedWorld world = worldforge::forgeWorld(seed, kGrid, config);
    ForgedHeightfield adapted = heightfieldFromForgedWorld(world, desc);
    REQUIRE(adapted.ok());
    PlacedPois pois = placeForgedPois(world, adapted.field);
    PlacedLootResult loot = pois.ok() ? placeForgedLoot(world, pois.set, catalogue(), adapted.field) : PlacedLootResult{};
    return {std::move(world), std::move(adapted.field), std::move(pois), std::move(loot)};
}

const Fixture& fixture()
{
    static const Fixture f = [] {
        for (std::uint64_t seed = 1; seed < 40; ++seed) {
            Fixture candidate = build(seed);
            if (candidate.loot.ok() && candidate.world.manifest() && !candidate.world.manifest()->items.empty()) return candidate;
        }
        FAIL("no seed under 40 forged a world with loot");
        return build(1);
    }();
    return f;
}

worldforge::ContentManifest oneItem(const std::string& item, const std::string& poi)
{
    worldforge::ContentManifest m;
    m.items.push_back({item, poi});
    return m;
}

const PlacedPoi& firstNonSettlement(const Fixture& f)
{
    for (const PlacedPoi& poi : f.pois.set.all()) {
        if (poi.kind != PlacedPoiKind::Settlement) return poi;
    }
    FAIL("no dungeon or landmark");
    return f.pois.set.all().front();
}

}

TEST_CASE("every manifest item exists exactly once in the loaded cells", "[bake][loot]")
{
    const Fixture& f = fixture();
    const WorldDesc& desc = f.field.desc();
    const auto& items = f.world.manifest()->items;
    REQUIRE(items.size() > 100);
    REQUIRE(f.loot.set.all().size() == items.size());

    std::map<std::string, int> counted;
    for (std::int32_t z = 0; z < static_cast<std::int32_t>(desc.cellsPerSide()); ++z) {
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(desc.cellsPerSide()); ++x) {
            for (const PlacedLoot& item : f.loot.set.inCell({x, z})) {
                ++counted[item.itemId];
                REQUIRE(item.cell == CellId{x, z});
                REQUIRE(item.x >= f.field.cellOriginX({x, z}));
                REQUIRE(item.x <= f.field.cellOriginX({x, z}) + desc.cellMetres);
                REQUIRE(item.z >= f.field.cellOriginZ({x, z}));
                REQUIRE(item.z <= f.field.cellOriginZ({x, z}) + desc.cellMetres);
            }
        }
    }
    for (const worldforge::ItemPlacement& placement : items) {
        REQUIRE(counted[placement.itemId] == 1);
    }
    REQUIRE(f.loot.set.inCell({-1, 0}).empty());
    REQUIRE(f.loot.set.inCell({1000, 1000}).empty());
}

TEST_CASE("unique items are not duplicated across cells", "[bake][loot]")
{
    const Fixture& f = fixture();
    std::set<std::string> uniques;
    for (const PlacedLoot& item : f.loot.set.all()) {
        if (item.tier == "unique") REQUIRE(uniques.insert(item.itemId).second);
    }
    std::size_t catalogued = 0;
    for (const LootItemInfo& info : catalogue()) catalogued += info.tier == "unique" ? 1 : 0;
    REQUIRE(catalogued > 0);
    REQUIRE(uniques.size() == catalogued);
}

TEST_CASE("an item lies near the place the manifest gave it, on the ground, with its catalogue record", "[bake][loot]")
{
    const Fixture& f = fixture();
    const PlacedLootParams params;
    std::map<std::string, const LootItemInfo*> info;
    for (const LootItemInfo& i : catalogue()) info[i.id] = &i;
    for (const worldforge::ItemPlacement& placement : f.world.manifest()->items) {
        const PlacedLoot* item = f.loot.set.find(placement.itemId);
        REQUIRE(item != nullptr);
        REQUIRE(item->poiId == placement.poiId);
        const PlacedPoi* poi = f.pois.set.find(placement.poiId);
        REQUIRE(poi != nullptr);
        REQUIRE(poi->kind != PlacedPoiKind::Settlement);
        const double dx = item->x - poi->x;
        const double dz = item->z - poi->z;
        REQUIRE(dx * dx + dz * dz <= static_cast<double>(params.maxRadiusM) * params.maxRadiusM * 1.001);
        REQUIRE(item->groundY == f.field.heightAt(item->x, item->z));
        REQUIRE(item->tier == info[placement.itemId]->tier);
        REQUIRE(item->itemClass == info[placement.itemId]->itemClass);
    }
    REQUIRE(f.loot.set.find("no_such_item") == nullptr);
}

TEST_CASE("the placement does not depend on the order cells are asked for or items are listed", "[bake][loot]")
{
    const Fixture& f = fixture();
    const auto cells = static_cast<std::int32_t>(f.field.desc().cellsPerSide());
    std::vector<std::string> forward;
    std::vector<std::string> backward;
    for (std::int32_t i = 0; i < cells * cells; ++i) {
        for (const PlacedLoot& p : f.loot.set.inCell({i % cells, i / cells})) forward.push_back(p.itemId);
    }
    for (std::int32_t i = cells * cells - 1; i >= 0; --i) {
        for (const PlacedLoot& p : f.loot.set.inCell({i % cells, i / cells})) backward.push_back(p.itemId);
    }
    std::sort(forward.begin(), forward.end());
    std::sort(backward.begin(), backward.end());
    REQUIRE(forward == backward);

    worldforge::ContentManifest reversed = *f.world.manifest();
    std::reverse(reversed.items.begin(), reversed.items.end());
    std::vector<LootItemInfo> shuffled = catalogue();
    std::reverse(shuffled.begin(), shuffled.end());
    const PlacedLootResult again = placeManifestLoot(reversed, f.pois.set, shuffled, f.field);
    REQUIRE(again.ok());
    REQUIRE(again.set.all().size() == f.loot.set.all().size());
    for (std::size_t i = 0; i < again.set.all().size(); ++i) {
        REQUIRE(again.set.all()[i].itemId == f.loot.set.all()[i].itemId);
        REQUIRE(again.set.all()[i].x == f.loot.set.all()[i].x);
        REQUIRE(again.set.all()[i].z == f.loot.set.all()[i].z);
    }
}

TEST_CASE("items spread around their place rather than piling on its centre", "[bake][loot]")
{
    const Fixture& f = fixture();
    double total = 0.0;
    for (const PlacedLoot& item : f.loot.set.all()) {
        const PlacedPoi* poi = f.pois.set.find(item.poiId);
        total += std::hypot(item.x - poi->x, item.z - poi->z);
    }
    const double mean = total / static_cast<double>(f.loot.set.all().size());
    REQUIRE(mean > 3.0);
}

TEST_CASE("a streamed cell carries its loot, and none without a set", "[bake][loot]")
{
    const Fixture& f = fixture();
    StreamerConfig config;
    config.synchronous = true;
    config.lodCount = 1;
    config.loot = std::make_shared<const PlacedLootSet>(f.loot.set);

    std::size_t total = 0;
    const auto cells = static_cast<std::int32_t>(f.field.desc().cellsPerSide());
    for (std::int32_t z = 0; z < cells; ++z) {
        for (std::int32_t x = 0; x < cells; ++x) {
            CellPayload payload;
            WorldStreamer::buildPayload(f.field, {x, z}, config, payload);
            REQUIRE(payload.loot.size() == f.loot.set.inCell({x, z}).size());
            total += payload.loot.size();
        }
    }
    REQUIRE(total == f.world.manifest()->items.size());

    config.loot.reset();
    CellPayload bare;
    WorldStreamer::buildPayload(f.field, f.loot.set.all().front().cell, config, bare);
    REQUIRE(bare.loot.empty());
}

TEST_CASE("a manifest that does not fit its world is refused whole", "[bake][loot]")
{
    const Fixture& f = fixture();
    const PlacedPoi& place = firstNonSettlement(f);
    const std::vector<LootItemInfo> items = {{"sword", "unique", "gear"}, {"rope", "notable", "gear"}};

    REQUIRE(placeManifestLoot(oneItem("sword", place.id), f.pois.set, items, f.field).ok());
    REQUIRE_FALSE(placeManifestLoot(oneItem("ghost", place.id), f.pois.set, items, f.field).ok());
    REQUIRE_FALSE(placeManifestLoot(oneItem("sword", "poi_9999"), f.pois.set, items, f.field).ok());
    REQUIRE_FALSE(placeManifestLoot(oneItem("sword", ""), f.pois.set, items, f.field).ok());

    worldforge::ContentManifest twice = oneItem("sword", place.id);
    twice.items.push_back({"sword", place.id});
    REQUIRE_FALSE(placeManifestLoot(twice, f.pois.set, items, f.field).ok());

    worldforge::ContentManifest mixed = oneItem("sword", place.id);
    mixed.items.push_back({"rope", "poi_9999"});
    const PlacedLootResult refused = placeManifestLoot(mixed, f.pois.set, items, f.field);
    REQUIRE_FALSE(refused.ok());
    REQUIRE(refused.set.empty());

    for (const PlacedPoi& poi : f.pois.set.all()) {
        if (poi.kind == PlacedPoiKind::Settlement) {
            REQUIRE_FALSE(placeManifestLoot(oneItem("sword", poi.id), f.pois.set, items, f.field).ok());
            break;
        }
    }

    std::vector<LootItemInfo> dup = items;
    dup.push_back({"sword", "notable", "gear"});
    REQUIRE_FALSE(placeManifestLoot(oneItem("sword", place.id), f.pois.set, dup, f.field).ok());
}

TEST_CASE("bad inputs fail instead of crashing", "[bake][loot]")
{
    const Fixture& f = fixture();
    const PlacedPoi& place = firstNonSettlement(f);
    const std::vector<LootItemInfo> items = {{"sword", "unique", "gear"}};
    const worldforge::ContentManifest m = oneItem("sword", place.id);

    REQUIRE(placeManifestLoot(worldforge::ContentManifest{}, f.pois.set, items, f.field).ok());
    REQUIRE(placeManifestLoot(worldforge::ContentManifest{}, f.pois.set, items, f.field).set.empty());
    REQUIRE_FALSE(placeManifestLoot(m, f.pois.set, items, Heightfield{}).ok());

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const PlacedLootParams bad : {PlacedLootParams{nan, 5.0f}, PlacedLootParams{1.0f, nan}, PlacedLootParams{-1.0f, 5.0f}, PlacedLootParams{5.0f, 1.0f},
             PlacedLootParams{1.0f, inf}, PlacedLootParams{0.0f, 1.0e9f}}) {
        REQUIRE_FALSE(placeManifestLoot(m, f.pois.set, items, f.field, bad).ok());
    }
    PlacedLootParams zero;
    zero.minRadiusM = 0.0f;
    zero.maxRadiusM = 0.0f;
    REQUIRE(placeManifestLoot(m, f.pois.set, items, f.field, zero).ok());

    PlacedLootParams thin;
    thin.minRadiusM = 9.0f;
    thin.maxRadiusM = 9.0f;
    const PlacedLootResult thinResult = placeForgedLoot(f.world, f.pois.set, catalogue(), f.field, thin);
    REQUIRE(thinResult.ok());
    std::set<std::pair<double, double>> spots;
    for (const PlacedLoot& item : thinResult.set.all()) spots.insert({item.x, item.z});
    REQUIRE(spots.size() > thinResult.set.all().size() / 2);

    const worldforge::ForgedWorld early = worldforge::forgeWorld(f.world.seed(), kGrid, worldforge::ForgeConfig{}, worldforge::ForgePhase::Hydrology);
    REQUIRE_FALSE(placeForgedLoot(early, f.pois.set, items, f.field).ok());
}
