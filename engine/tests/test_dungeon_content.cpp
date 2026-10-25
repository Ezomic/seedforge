#include "ichor/data/data_set.h"
#include "ichor/save/save_slots.h"
#include "ichor/world/carry.h"
#include "ichor/world/dungeon_content.h"
#include "ichor/world/dungeon_entry.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr int kGrid = 128;
constexpr glm::dvec3 kOrigin{1000.0, 50.0, -2000.0};

struct Hand {
    BakedDungeon dungeon;
    graph::NodeId add(const char* type, graph::embedding::Cell cell)
    {
        const graph::NodeId id = dungeon.graph.addNode(type);
        dungeon.cells[id] = cell;
        return id;
    }
    void link(graph::NodeId a, graph::NodeId b) { dungeon.graph.addEdge(a, b, "path"); }
};

bool solidAt(const DungeonGreyBox& box, const glm::dvec3& p)
{
    for (const GreyBoxPiece& piece : box.pieces) {
        const glm::dvec3 local = glm::dvec3(glm::inverse(piece.rotation) * glm::vec3(p - piece.centre));
        if (std::abs(local.x) <= piece.halfExtents.x && std::abs(local.y) <= piece.halfExtents.y && std::abs(local.z) <= piece.halfExtents.z) return true;
    }
    return false;
}

// entry - challenge - key - reward - climax in a row, so every role has a room.
Hand row(std::uint64_t seed)
{
    Hand h;
    h.dungeon.seed = seed;
    h.dungeon.poiId = "poi_0001";
    const auto entry = h.add("entry", {0, 0, 0});
    const auto challenge = h.add("challenge", {1, 0, 0});
    const auto key = h.add("key", {2, 0, 0});
    const auto reward = h.add("reward", {3, 0, 0});
    const auto climax = h.add("climax", {4, 0, 0});
    h.link(entry, challenge);
    h.link(challenge, key);
    h.link(key, reward);
    h.link(reward, climax);
    return h;
}

PlacedLoot item(const std::string& id)
{
    PlacedLoot out;
    out.itemId = id;
    out.poiId = "poi_0001";
    out.tier = "notable";
    out.itemClass = "gear";
    return out;
}

std::vector<PlacedLoot> items(std::size_t count)
{
    std::vector<PlacedLoot> out;
    for (std::size_t i = 0; i < count; ++i) out.push_back(item("item_" + std::to_string(1000 + i)));
    return out;
}

struct Built {
    DungeonGreyBox box;
    DungeonContent content;
};

Built build(const Hand& h, const std::vector<PlacedLoot>& loot = {})
{
    Built out;
    out.box = buildDungeonGreyBox(h.dungeon, kOrigin);
    REQUIRE(out.box.ok());
    out.content = buildDungeonContent(h.dungeon, out.box, loot);
    return out;
}

bool sameContent(const DungeonContent& a, const DungeonContent& b)
{
    if (a.enemies.size() != b.enemies.size() || a.loot.size() != b.loot.size() || a.bossRoom != b.bossRoom || a.bossMarker != b.bossMarker) return false;
    for (std::size_t i = 0; i < a.enemies.size(); ++i) {
        if (a.enemies[i].room != b.enemies[i].room || a.enemies[i].slot != b.enemies[i].slot || a.enemies[i].post != b.enemies[i].post) return false;
    }
    for (std::size_t i = 0; i < a.loot.size(); ++i) {
        if (a.loot[i].itemId != b.loot[i].itemId || a.loot[i].x != b.loot[i].x || a.loot[i].z != b.loot[i].z || a.loot[i].groundY != b.loot[i].groundY) return false;
    }
    return true;
}

glm::dvec3 doorwayCentre(const GreyBoxRoom& room, int side)
{
    constexpr double R = greybox::kRoomHalfM;
    const glm::dvec2 offsets[4] = {{0.0, -R}, {R, 0.0}, {0.0, R}, {-R, 0.0}};
    return room.centre + glm::dvec3(offsets[side].x, 0.0, offsets[side].y);
}

double horizontal(const glm::dvec3& a, const glm::dvec3& b) { return std::hypot(a.x - b.x, a.z - b.z); }

const GreyBoxRoom& boxRoom(const DungeonGreyBox& box, graph::NodeId id)
{
    for (const GreyBoxRoom& room : box.rooms) {
        if (room.id == id) return room;
    }
    FAIL("no such room");
    return box.rooms.front();
}

// A spot fit to stand on or to lie at: floor under it, air over it, and well away from every open doorway of its room.
void requireClearSpot(const DungeonGreyBox& box, graph::NodeId roomId, const glm::dvec3& feet)
{
    CHECK(solidAt(box, feet - glm::dvec3(0.0, 0.2, 0.0)));
    CHECK_FALSE(solidAt(box, feet + glm::dvec3(0.0, 0.3, 0.0)));
    CHECK_FALSE(solidAt(box, feet + glm::dvec3(0.0, 1.8, 0.0)));
    const GreyBoxRoom& room = boxRoom(box, roomId);
    for (int side = 0; side < 4; ++side) {
        if (room.doors[side]) CHECK(horizontal(feet, doorwayCentre(room, side)) >= 2.0);
    }
}

}

TEST_CASE("each room type holds the enemies its role says, and the climax room the boss marker", "[dungeon_content]")
{
    const Hand h = row(42);
    const Built built = build(h);
    REQUIRE_FALSE(built.content.empty());
    std::map<std::string, std::set<std::size_t>> counts;
    for (const DungeonRoom& room : built.content.rooms) counts[room.type].insert(built.content.enemiesIn(room.id).size());
    CHECK(counts["entry"] == std::set<std::size_t>{0});
    CHECK(counts["reward"] == std::set<std::size_t>{0});
    CHECK(counts["key"] == std::set<std::size_t>{1});
    CHECK(counts["climax"] == std::set<std::size_t>{2});
    CHECK((counts["challenge"] == std::set<std::size_t>{1} || counts["challenge"] == std::set<std::size_t>{2}));
    REQUIRE(built.content.bossRoom != 0);
    CHECK(built.content.room(built.content.bossRoom)->type == "climax");
    CHECK(built.content.bossMarker == built.content.room(built.content.bossRoom)->centre);
    CHECK(clearedRooms(Scene{1}, built.content) == 0);
    CHECK(roomsWithEnemies(built.content) == 3);
}

TEST_CASE("an enemy's role follows its room: a key guard is an anchor, a climax holds an anchor and an artillery, a challenge room an artillery second", "[dungeon_content]")
{
    std::set<DungeonEnemyRole> challengeSecond;
    for (std::uint64_t seed = 1; seed <= 64; ++seed) {
        const Built built = build(row(seed));
        for (const DungeonRoom& room : built.content.rooms) {
            const std::vector<std::size_t> in = built.content.enemiesIn(room.id);
            for (std::size_t ordinal = 0; ordinal < in.size(); ++ordinal) {
                INFO(room.type << " seed " << seed << " ordinal " << ordinal);
                const DungeonEnemyRole role = built.content.enemies[in[ordinal]].role;
                CHECK(role == roleForRoom(room.type, ordinal));
                if (room.type == "key") CHECK(role == DungeonEnemyRole::Anchor);
                if (room.type == "climax") CHECK(role == (ordinal == 0 ? DungeonEnemyRole::Anchor : DungeonEnemyRole::Artillery));
                if (room.type == "challenge" && ordinal == 0) CHECK(role == DungeonEnemyRole::Skirmisher);
                if (room.type == "challenge" && ordinal == 1) challengeSecond.insert(role);
            }
        }
    }
    CHECK(challengeSecond == std::set<DungeonEnemyRole>{DungeonEnemyRole::Artillery});
    CHECK(roleForRoom("entry", 0) == DungeonEnemyRole::Skirmisher);
    CHECK(roleForRoom("nonsense", 7) == DungeonEnemyRole::Skirmisher);
    CHECK(roleForRoom("", 0) == DungeonEnemyRole::Skirmisher);
}

TEST_CASE("the same dungeon holds the same content every time, and another seed holds another", "[dungeon_content]")
{
    const std::vector<PlacedLoot> loot = items(6);
    const Built a = build(row(42), loot);
    const Built b = build(row(42), loot);
    CHECK(sameContent(a.content, b.content));

    std::set<std::vector<std::uint32_t>> patterns;
    std::set<std::size_t> challengeCounts;
    for (std::uint64_t seed = 1; seed <= 64; ++seed) {
        const Built built = build(row(seed), loot);
        std::vector<std::uint32_t> slots;
        for (const DungeonEnemy& enemy : built.content.enemies) slots.push_back(enemy.slot);
        patterns.insert(slots);
        for (const DungeonRoom& room : built.content.rooms) {
            if (room.type == "challenge") challengeCounts.insert(built.content.enemiesIn(room.id).size());
        }
    }
    CHECK(patterns.size() > 8);
    CHECK((challengeCounts == std::set<std::size_t>{1, 2}));
}

TEST_CASE("no enemy stands in a doorway, over a ramp or a hole, in the entry room or on the way out, in any shipped skeleton", "[dungeon_content]")
{
    const DungeonSkeletonCatalog catalog = DungeonSkeletonCatalog::loadDirectory(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons");
    REQUIRE(catalog.all().size() >= 30);
    std::size_t enemies = 0;
    std::size_t overRamps = 0;
    std::size_t climaxes = 0;
    for (const DungeonSkeleton& skeleton : catalog.all()) {
        for (const std::uint64_t seed : {3u, 11u, 29u}) {
            INFO(skeleton.id << " seed " << seed);
            BakedDungeon dungeon;
            dungeon.poiId = "poi_0001";
            dungeon.seed = seed;
            dungeon.graph = skeleton.graph;
            dungeon.cells = skeleton.cells;
            const DungeonGreyBox box = buildDungeonGreyBox(dungeon, kOrigin);
            REQUIRE(box.ok());
            const DungeonContent content = buildDungeonContent(dungeon, box, {});
            REQUIRE(content.rooms.size() == dungeon.graph.nodes().size());
            std::set<std::pair<graph::NodeId, std::uint32_t>> seen;
            for (const DungeonEnemy& enemy : content.enemies) {
                ++enemies;
                CHECK(seen.insert({enemy.room, enemy.slot}).second);
                CHECK(content.room(enemy.room)->type != "entry");
                requireClearSpot(box, enemy.room, enemy.post);
                CHECK(glm::length(enemy.post - box.spawn) >= 4.0);
                CHECK(glm::length(enemy.post - box.exitPoint) >= 4.0);
                CHECK(std::abs(enemy.post.x - content.room(enemy.room)->centre.x) <= 2.0);
                const GreyBoxRoom& room = boxRoom(box, enemy.room);
                if (room.ramp || room.hole) ++overRamps;
                const DungeonRoom* standingIn = content.roomAt(enemy.post);
                REQUIRE(standingIn != nullptr);
                CHECK(standingIn->id == enemy.room);
            }
            for (const DungeonRoom& room : content.rooms) {
                if (room.type == "challenge") CHECK_FALSE(content.enemiesIn(room.id).empty());
                if (room.type == "climax") ++climaxes;
            }
            CHECK((content.bossRoom != 0) == (climaxes > 0));
            climaxes = 0;
        }
    }
    CHECK(enemies > 500);
    CHECK(overRamps > 20);
}

TEST_CASE("every item the manifest placed lies in the dungeon once, in the reward room first, and never in a doorway", "[dungeon_content]")
{
    const Hand h = row(9);
    std::vector<PlacedLoot> loot = items(7);
    const Built built = build(h, loot);
    REQUIRE(built.content.loot.size() == 7);
    std::set<std::string> ids;
    std::map<graph::NodeId, int> perRoom;
    for (const PlacedLoot& placed : built.content.loot) {
        CHECK(ids.insert(placed.itemId).second);
        CHECK(placed.poiId == "poi_0001");
        const DungeonRoom* room = built.content.roomAt({placed.x, static_cast<double>(placed.groundY), placed.z});
        REQUIRE(room != nullptr);
        perRoom[room->id]++;
        CHECK(room->type != "entry");
        requireClearSpot(built.box, room->id, {placed.x, static_cast<double>(placed.groundY), placed.z});
    }
    // Five spots to a room: the reward room fills first, the key room takes the rest.
    for (const DungeonRoom& room : built.content.rooms) {
        if (room.type == "reward") CHECK(perRoom[room.id] == 5);
        if (room.type == "key") CHECK(perRoom[room.id] == 2);
        if (room.type == "climax" || room.type == "challenge") CHECK(perRoom[room.id] == 0);
    }
    std::set<std::pair<double, double>> spots;
    for (const PlacedLoot& placed : built.content.loot) spots.insert({placed.x, placed.z});
    CHECK(spots.size() == 7);
}

TEST_CASE("the order the manifest lists items in changes nothing, and a repeated item is placed once", "[dungeon_content]")
{
    std::vector<PlacedLoot> loot = items(9);
    const Built forward = build(row(5), loot);
    std::reverse(loot.begin(), loot.end());
    const Built backward = build(row(5), loot);
    CHECK(sameContent(forward.content, backward.content));

    loot.push_back(item("item_1003"));
    loot.push_back(item("item_1003"));
    CHECK(build(row(5), loot).content.loot.size() == 9);
}

TEST_CASE("a dungeon with no reward or key room still places every item, and in the entry room only when it has no other", "[dungeon_content]")
{
    Hand lone;
    lone.dungeon.seed = 1;
    lone.add("entry", {0, 0, 0});
    const Built alone = build(lone, items(4));
    REQUIRE(alone.content.loot.size() == 4);
    for (const PlacedLoot& placed : alone.content.loot) {
        const glm::dvec3 feet{placed.x, static_cast<double>(placed.groundY), placed.z};
        CHECK(horizontal(feet, alone.box.spawn) >= 1.5);
        CHECK(horizontal(feet, alone.box.exitPoint) >= 2.0);
        requireClearSpot(alone.box, alone.content.rooms.front().id, feet);
    }

    Hand pair;
    pair.dungeon.seed = 1;
    const auto entry = pair.add("entry", {0, 0, 0});
    const auto challenge = pair.add("challenge", {1, 0, 0});
    pair.link(entry, challenge);
    const Built two = build(pair, items(3));
    for (const PlacedLoot& placed : two.content.loot) {
        CHECK(two.content.roomAt({placed.x, static_cast<double>(placed.groundY), placed.z})->type == "challenge");
    }
}

TEST_CASE("a layout or grey-box that failed, a hostile origin or a flood of items gives empty or bounded content, never a crash", "[dungeon_content]")
{
    CHECK(buildDungeonContent(BakedDungeon{}, DungeonGreyBox{}, items(3)).empty());

    Hand noEntry;
    noEntry.add("challenge", {0, 0, 0});
    DungeonGreyBox failed = buildDungeonGreyBox(noEntry.dungeon, kOrigin);
    REQUIRE_FALSE(failed.ok());
    CHECK(buildDungeonContent(noEntry.dungeon, failed, items(3)).empty());

    const Hand h = row(2);
    DungeonGreyBox box = buildDungeonGreyBox(h.dungeon, kOrigin);
    REQUIRE(box.ok());
    box.rooms.front().centre.x = std::numeric_limits<double>::quiet_NaN();
    CHECK(buildDungeonContent(h.dungeon, box, items(3)).empty());

    const Built flood = build(row(2), items(dungeon_content::kMaxLoot + 500));
    CHECK(flood.content.loot.size() == dungeon_content::kMaxLoot);

    DungeonContent content = build(row(2)).content;
    CHECK(content.roomAt({std::nan(""), 0.0, 0.0}) == nullptr);
    CHECK(content.roomAt({std::numeric_limits<double>::infinity(), 0.0, 0.0}) == nullptr);
    CHECK(content.room(9999) == nullptr);
    CHECK(content.enemiesIn(9999).empty());
}

TEST_CASE("a point belongs to the room whose hall and storey hold it, walls included, and to none outside", "[dungeon_content]")
{
    Hand h;
    h.dungeon.seed = 1;
    const auto lower = h.add("entry", {0, 0, 0});
    const auto upper = h.add("challenge", {0, 0, 1});
    const auto east = h.add("key", {1, 0, 0});
    h.link(lower, upper);
    h.link(lower, east);
    const Built built = build(h);
    const DungeonRoom& a = *built.content.room(lower);
    const DungeonRoom& b = *built.content.room(upper);
    const DungeonRoom& c = *built.content.room(east);
    CHECK(built.content.roomAt(a.centre + glm::dvec3(0.0, 0.5, 0.0))->id == lower);
    CHECK(built.content.roomAt(a.centre + glm::dvec3(5.5, 0.5, 0.0))->id == lower);
    CHECK(built.content.roomAt(c.centre + glm::dvec3(-5.5, 0.5, 0.0))->id == east);
    CHECK(built.content.roomAt(b.centre + glm::dvec3(0.0, 0.5, 0.0))->id == upper);
    CHECK(built.content.roomAt(a.centre + glm::dvec3(0.0, greybox::kStoreyM + 0.5, 0.0))->id == upper);
    CHECK(built.content.roomAt(a.centre + glm::dvec3(0.0, 0.5, 6.5)) == nullptr);
    CHECK(built.content.roomAt(a.centre + glm::dvec3(0.0, -3.0, 0.0)) == nullptr);
}

// ---- what the player has done, in the scene -----------------------------------------------------------------------

namespace {

void spawnPlayer(Scene& scene, const glm::dvec3& at)
{
    SpawnCommand player;
    player.id = StableId::of("player");
    player.name.value = "player";
    player.transform.position = at;
    player.player = Player{};
    scene.enqueue(player);
}

}

TEST_CASE("a kill is a marker in the scene: it shows from the next tick, twice is once, and a room is cleared when all its enemies are", "[dungeon_content][save]")
{
    const Built built = build(row(42));
    const std::vector<std::size_t> climax = built.content.enemiesIn(built.content.bossRoom);
    REQUIRE(climax.size() == 2);
    Scene scene(7);
    scene.tick(kDt);
    const std::size_t before = scene.registry().view<StableId>().size();
    const DungeonEnemy& first = built.content.enemies[climax[0]];
    const DungeonEnemy& second = built.content.enemies[climax[1]];

    CHECK_FALSE(enemyKilled(scene, built.content.poiId, first.room, first.slot));
    recordKill(scene, built.content.poiId, first);
    recordKill(scene, built.content.poiId, first);
    scene.tick(kDt);
    CHECK(enemyKilled(scene, built.content.poiId, first.room, first.slot));
    CHECK_FALSE(enemyKilled(scene, built.content.poiId, second.room, second.slot));
    CHECK_FALSE(enemyKilled(scene, "poi_0002", first.room, first.slot));
    CHECK(scene.registry().view<StableId>().size() == before + 1);
    CHECK_FALSE(roomCleared(scene, built.content, built.content.bossRoom));
    CHECK(clearedRooms(scene, built.content) == 0);

    recordKill(scene, built.content.poiId, second);
    scene.tick(kDt);
    CHECK(roomCleared(scene, built.content, built.content.bossRoom));
    CHECK(clearedRooms(scene, built.content) == 1);
    CHECK_FALSE(roomCleared(scene, built.content, built.content.rooms.front().id)); // the entry room had none to kill
}

TEST_CASE("the kills survive a save and a load in a fresh scene, and a scene that killed nothing is untouched", "[dungeon_content][save]")
{
    const Built built = build(row(42));
    const std::filesystem::path dir = test::tempPath("ichor_dungeon_content_slot");
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    SaveSlots slots(dir, SavePolicy{});
    const SaveContext context{7, 1, 42};

    Scene quiet(7);
    spawnPlayer(quiet, {0.0, 0.0, 0.0});
    quiet.tick(kDt);
    quiet.tick(kDt);
    const std::uint64_t quietHash = quiet.stateHash();

    Scene played(7);
    spawnPlayer(played, {0.0, 0.0, 0.0});
    played.tick(kDt);
    for (const DungeonEnemy& enemy : built.content.enemies) {
        if (enemy.room == built.content.bossRoom) recordKill(played, built.content.poiId, enemy);
    }
    played.tick(kDt);
    REQUIRE(clearedRooms(played, built.content) == 1);
    CHECK(played.stateHash() != quietHash);
    REQUIRE(slots.save("quick", played, context));

    Scene restored(7);
    const LoadResult loaded = slots.load("quick", restored, context);
    REQUIRE(loaded.status == LoadStatus::Loaded);
    CHECK(restored.stateHash() == played.stateHash());
    CHECK(roomCleared(restored, built.content, built.content.bossRoom));
    CHECK(clearedRooms(restored, built.content) == 1);
    for (const DungeonEnemy& enemy : built.content.enemies) {
        CHECK(enemyKilled(restored, built.content.poiId, enemy.room, enemy.slot) == (enemy.room == built.content.bossRoom));
    }
    // The loaded game goes on exactly as the saved one would have.
    played.tick(kDt);
    restored.tick(kDt);
    CHECK(restored.stateHash() == played.stateHash());
    std::filesystem::remove_all(dir);
}

// ---- inside a forged world --------------------------------------------------------------------------------------------

namespace {

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

struct Forged {
    ForgedContentSetup setup;
    std::unique_ptr<ForgedGame> game;
    std::uint64_t seed = 0;
    const PlacedPoi* poi = nullptr; // a dungeon the manifest placed loot at, with a layout that bakes
};

bool forge(std::uint64_t seed, Forged& out)
{
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    out.setup = loadForgedContentSetup(data, ICHOR_GAME_DATA_DIR);
    ForgeJob job(descFor(seed), kGrid, {}, out.setup.config);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(job.error());
    REQUIRE(job.state() == ForgeJobState::Done);
    out.game = buildForgedGame(*job.result(), descFor(seed), out.setup.inputs);
    INFO(out.game->error);
    REQUIRE(out.game->ok());
    out.seed = seed;
    if (!out.game->loot || !out.game->pois || !out.game->dungeons) return false;
    for (const PlacedPoi& poi : out.game->pois->all()) {
        if (poi.kind == PlacedPoiKind::Dungeon && !lootAtPoi(*out.game->loot, poi.id).empty() && out.game->dungeons->layout(poi.id).ok()) {
            out.poi = &poi;
            return true;
        }
    }
    return false;
}

// The first world, by search, that has a dungeon with loot in it; no seed is assumed to have one.
Forged& forgedWithLoot()
{
    static Forged found = [] {
        Forged out;
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            out = Forged{};
            if (forge(seed, out)) return out;
        }
        FAIL("no seed under 13 forged a world with loot in a dungeon");
        return out;
    }();
    return found;
}

}

TEST_CASE("loot that lies inside a dungeon is not also lying outside its door, and nothing is lost or doubled", "[dungeon_content][forged]")
{
    const ForgedGame& game = *forgedWithLoot().game;
    const std::shared_ptr<const PlacedLootSet> outdoors = outdoorLoot(game);
    REQUIRE(outdoors != nullptr);
    CHECK(outdoors->all().size() < game.loot->all().size());

    std::set<std::string> inside;
    for (const std::string& poiId : game.dungeons->poiIds()) {
        const DungeonLayoutResult layout = game.dungeons->layout(poiId);
        if (!layout.ok()) continue;
        const DungeonGreyBox box = buildDungeonGreyBox(*layout.dungeon, kOrigin);
        REQUIRE(box.ok());
        const DungeonContent content = buildDungeonContent(*layout.dungeon, box, lootAtPoi(*game.loot, poiId));
        CHECK(sameContent(content, buildDungeonContent(*layout.dungeon, box, lootAtPoi(*game.loot, poiId))));
        for (const PlacedLoot& placed : content.loot) {
            CHECK(inside.insert(placed.itemId).second);
            CHECK(outdoors->find(placed.itemId) == nullptr);
            REQUIRE(game.loot->find(placed.itemId) != nullptr);
        }
    }
    CHECK(inside.size() + outdoors->all().size() == game.loot->all().size());
    for (const PlacedLoot& placed : outdoors->all()) CHECK(inside.count(placed.itemId) == 0);

    const std::size_t cellCount = [&] {
        std::size_t n = 0;
        for (const PlacedLoot& placed : outdoors->all()) n += outdoors->inCell(placed.cell).empty() ? 0 : 1;
        return n;
    }();
    CHECK(cellCount == outdoors->all().size());
}

TEST_CASE("walking into a dungeon fills it, a pickup takes loot out of the hall, and the way out stays open", "[dungeon_content][forged]")
{
    ForgedGame& game = *forgedWithLoot().game;
    const PlacedPoi& poi = *forgedWithLoot().poi;
    DataSet data;
    REQUIRE(data.loadDirectory(ICHOR_GAME_DATA_DIR));
    std::vector<std::string> problems;

    Scene scene(7);
    Transform ground;
    ground.position = {poi.x, static_cast<double>(poi.groundY) - 1.0, poi.z};
    scene.physics().addStaticBox(StableId::of("test_ground"), StaticBox{{400.0f, 1.0f, 400.0f}}, ground);
    DungeonEntry entry(game, 0, DungeonEntryParams{.autoEnter = true});
    entry.install(scene);
    CarrySystem carry(outdoorLoot(game), CarryCatalogue::fromData(data, problems));
    carry.setInteriorLoot([&] { return &entry.content().loot; });
    carry.install(scene);
    SpawnCommand player;
    player.id = StableId::of("player");
    player.name.value = "player";
    player.transform.position = {poi.x - 4.0, static_cast<double>(poi.groundY) + 0.5, poi.z};
    player.player = Player{};
    player.character = CharacterBody{};
    scene.enqueue(player);
    for (int i = 0; i < 30; ++i) scene.tick(kDt);
    for (int i = 0; i < 200 && !(entry.inside(scene) && !entry.content().empty()); ++i) scene.tick(kDt);
    REQUIRE(entry.inside(scene));
    const DungeonContent& content = entry.content();
    REQUIRE_FALSE(content.empty());
    CHECK(content.poiId == poi.id);
    CHECK(content.loot.size() == lootAtPoi(*game.loot, poi.id).size());
    CHECK_FALSE(content.enemies.empty());

    const auto drawn = [&] {
        RenderView view;
        entry.appendContentInstances(scene, view);
        return view.instances.size();
    };
    CHECK(drawn() == content.loot.size() + (content.bossRoom != 0 ? 1 : 0));

    const PlacedLoot target = content.loot.front();
    SetTransformCommand move;
    move.id = StableId::of("player");
    move.transform.position = {target.x, static_cast<double>(target.groundY) + 0.05, target.z};
    scene.enqueue(move);
    for (int i = 0; i < 5; ++i) scene.tick(kDt);
    CHECK(carry.prompt(scene).find("pick up") != std::string::npos);
    scene.tick(kDt, InputFrame{.buttons = InputFrame::Interact});
    for (int i = 0; i < 4; ++i) scene.tick(kDt);
    REQUIRE(carry.carried(scene) != nullptr);
    CHECK(CarrySystem::isTaken(carry.carried(scene), target.itemId));
    CHECK(drawn() == content.loot.size() - 1 + (content.bossRoom != 0 ? 1 : 0));

    // The loot on another storey is out of reach from here.
    for (const PlacedLoot& other : content.loot) {
        if (std::abs(static_cast<double>(other.groundY) - target.groundY) > 1.0 && std::hypot(other.x - target.x, other.z - target.z) < 1.0) {
            SetTransformCommand over;
            over.id = StableId::of("player");
            over.transform.position = {other.x, static_cast<double>(target.groundY) + 0.05, other.z};
            scene.enqueue(over);
            for (int i = 0; i < 3; ++i) scene.tick(kDt);
            CHECK(carry.prompt(scene).find("pick up") == std::string::npos);
        }
    }

    // Enemies stand in the hall and every one of them is alive; the exit still works.
    const DungeonGreyBox hall = entry.greyBox();
    SetTransformCommand toExit;
    toExit.id = StableId::of("player");
    toExit.transform.position = {hall.exitPoint.x + 0.5, hall.exitPoint.y + 0.1, hall.exitPoint.z};
    scene.enqueue(toExit);
    for (int i = 0; i < 20; ++i) scene.tick(kDt);
    CHECK(entry.prompt(scene).find("leave") != std::string::npos);
    scene.tick(kDt, InputFrame{.buttons = InputFrame::Confirm});
    for (int i = 0; i < 150 && entry.inside(scene); ++i) scene.tick(kDt);
    CHECK_FALSE(entry.inside(scene));
    CHECK(entry.content().empty());
}
