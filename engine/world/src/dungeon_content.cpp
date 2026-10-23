#include "ichor/world/dungeon_content.h"

#include "ichor/core/rng.h"
#include "ichor/graph/mission.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <set>
#include <string_view>
#include <utility>

namespace ichor {

namespace {

namespace dc = dungeon_content;

struct Offset {
    double x;
    double z;
};

constexpr std::array<Offset, dc::kPosts> kPostOffsets = {{{-dc::kPostX, -dc::kPostZ}, {dc::kPostX, -dc::kPostZ}, {-dc::kPostX, dc::kPostZ}, {dc::kPostX, dc::kPostZ}}};

// Loot lies along the room's two axes, in the clear between the posts and the doorways.
constexpr std::size_t kLootSpots = 5;
constexpr std::array<Offset, kLootSpots> kLootOffsets = {{{-2.0, 0.0}, {2.0, 0.0}, {0.0, -1.5}, {0.0, 1.5}, {-1.5, 1.5}}};

constexpr std::size_t kLootTiers = 6;

bool finite(const glm::dvec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

std::size_t enemyCount(std::string_view type, Rng& rng)
{
    if (type == graph::mission::kChallenge) return 1 + rng.range(0, 1);
    if (type == graph::mission::kKey) return 1;
    if (type == graph::mission::kClimax) return 2;
    return 0;
}

// Where a room's loot is looked for first: the reward, then the key, the quiet rooms, the fights, the climax, and only
// last the room the player walks in through.
std::size_t lootTier(std::string_view type)
{
    if (type == graph::mission::kReward || type == graph::mission::kVault) return 0;
    if (type == graph::mission::kKey) return 1;
    if (type == graph::mission::kRest || type == graph::mission::kReveal || type == graph::mission::kJunction || type == graph::mission::kGate) return 2;
    if (type == graph::mission::kChallenge) return 3;
    if (type == graph::mission::kClimax) return 4;
    return 5;
}

std::string killName(const std::string& poiId, graph::NodeId room, std::uint32_t slot) { return std::format("dungeon_kill:{}:{}:{}", poiId, room, slot); }

}

const DungeonRoom* DungeonContent::room(graph::NodeId id) const
{
    const auto it = std::lower_bound(rooms.begin(), rooms.end(), id, [](const DungeonRoom& r, graph::NodeId v) { return r.id < v; });
    return it != rooms.end() && it->id == id ? &*it : nullptr;
}

const DungeonRoom* DungeonContent::roomAt(const glm::dvec3& point) const
{
    if (!finite(point)) return nullptr;
    for (const DungeonRoom& candidate : rooms) {
        const double dy = point.y - candidate.centre.y;
        if (std::abs(point.x - candidate.centre.x) <= dc::kHallHalfM && std::abs(point.z - candidate.centre.z) <= dc::kHallHalfM && dy >= -1.0 && dy < greybox::kStoreyM - 1.0) {
            return &candidate;
        }
    }
    return nullptr;
}

std::vector<std::size_t> DungeonContent::enemiesIn(graph::NodeId roomId) const
{
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < enemies.size(); ++i) {
        if (enemies[i].room == roomId) out.push_back(i);
    }
    return out;
}

DungeonContent buildDungeonContent(const BakedDungeon& dungeon, const DungeonGreyBox& box, const std::vector<PlacedLoot>& items)
{
    DungeonContent out;
    if (!box.ok() || box.rooms.empty()) return out;
    out.poiId = dungeon.poiId;
    const Rng seeded(dungeon.seed);

    for (const GreyBoxRoom& placed : box.rooms) {
        if (!finite(placed.centre)) return DungeonContent{};
        const graph::Node* node = dungeon.graph.node(placed.id);
        DungeonRoom room;
        room.id = placed.id;
        room.type = node != nullptr ? node->type : std::string();
        room.centre = placed.centre;
        out.rooms.push_back(room);
    }

    for (const DungeonRoom& room : out.rooms) {
        Rng rng = seeded.derive("dungeon_content:room:" + std::to_string(room.id));
        // Draw the shuffle first so the count never moves which posts a room's enemies take.
        std::array<std::uint32_t, dc::kPosts> posts = {0, 1, 2, 3};
        for (std::uint32_t i = static_cast<std::uint32_t>(dc::kPosts) - 1; i > 0; --i) std::swap(posts[i], posts[rng.range(0, i)]);
        const std::size_t count = enemyCount(room.type, rng);
        std::vector<std::uint32_t> chosen(posts.begin(), posts.begin() + static_cast<std::ptrdiff_t>(std::min(count, dc::kPosts)));
        std::sort(chosen.begin(), chosen.end());
        for (const std::uint32_t slot : chosen) {
            DungeonEnemy enemy;
            enemy.room = room.id;
            enemy.slot = slot;
            enemy.post = room.centre + glm::dvec3(kPostOffsets[slot].x, 0.0, kPostOffsets[slot].z);
            out.enemies.push_back(enemy);
        }
        if (out.bossRoom == 0 && room.type == graph::mission::kClimax) {
            out.bossRoom = room.id;
            out.bossMarker = room.centre;
        }
    }

    std::vector<const PlacedLoot*> ordered;
    std::set<std::string> seen;
    for (const PlacedLoot& item : items) {
        if (ordered.size() < dc::kMaxLoot && seen.insert(item.itemId).second) ordered.push_back(&item);
    }
    std::sort(ordered.begin(), ordered.end(), [](const PlacedLoot* a, const PlacedLoot* b) { return a->itemId < b->itemId; });

    std::vector<std::size_t> used(out.rooms.size(), 0);
    for (const PlacedLoot* item : ordered) {
        Rng rng = seeded.derive("dungeon_loot:" + item->itemId);
        std::size_t pick = out.rooms.size();
        for (std::size_t tier = 0; tier < kLootTiers && pick == out.rooms.size(); ++tier) {
            std::vector<std::size_t> open;
            for (std::size_t i = 0; i < out.rooms.size(); ++i) {
                if (lootTier(out.rooms[i].type) == tier && used[i] < kLootSpots) open.push_back(i);
            }
            if (!open.empty()) pick = open[rng.range(0, static_cast<std::uint32_t>(open.size() - 1))];
        }
        if (pick == out.rooms.size()) pick = rng.range(0, static_cast<std::uint32_t>(out.rooms.size() - 1)); // every spot is taken: share one
        const DungeonRoom& room = out.rooms[pick];
        const Offset spot = kLootOffsets[used[pick]++ % kLootSpots];
        PlacedLoot placed = *item;
        placed.x = room.centre.x + spot.x;
        placed.z = room.centre.z + spot.z;
        placed.groundY = static_cast<float>(room.centre.y);
        placed.cell = CellId{};
        out.loot.push_back(std::move(placed));
    }
    return out;
}

std::vector<PlacedLoot> lootAtPoi(const PlacedLootSet& loot, const std::string& poiId)
{
    std::vector<PlacedLoot> out;
    for (const PlacedLoot& item : loot.all()) {
        if (item.poiId == poiId) out.push_back(item);
    }
    return out;
}

std::shared_ptr<const PlacedLootSet> outdoorLoot(const ForgedGame& game)
{
    if (!game.loot || !game.pois || !game.dungeons) return game.loot;
    const std::vector<std::string> ids = game.dungeons->poiIds();
    const std::set<std::string> enterable(ids.begin(), ids.end());
    PlacedLootSet outdoors = game.loot->without([&](const PlacedLoot& item) {
        const PlacedPoi* poi = game.pois->find(item.poiId);
        return poi != nullptr && poi->kind == PlacedPoiKind::Dungeon && enterable.contains(item.poiId);
    });
    if (outdoors.all().size() == game.loot->all().size()) return game.loot;
    return std::make_shared<const PlacedLootSet>(std::move(outdoors));
}

bool enemyKilled(const Scene& scene, const std::string& poiId, graph::NodeId room, std::uint32_t slot)
{
    return scene.find(StableId::of(killName(poiId, room, slot))) != entt::null;
}

void recordKill(Scene& scene, const std::string& poiId, const DungeonEnemy& enemy)
{
    SpawnCommand marker;
    marker.id = StableId::of(killName(poiId, enemy.room, enemy.slot));
    marker.name.value = killName(poiId, enemy.room, enemy.slot);
    marker.transform.position = enemy.post;
    scene.enqueue(marker); // a spawn of an id that exists is ignored, so a second record changes nothing
}

bool roomCleared(const Scene& scene, const DungeonContent& content, graph::NodeId room)
{
    bool any = false;
    for (const DungeonEnemy& enemy : content.enemies) {
        if (enemy.room != room) continue;
        any = true;
        if (!enemyKilled(scene, content.poiId, enemy.room, enemy.slot)) return false;
    }
    return any;
}

std::size_t clearedRooms(const Scene& scene, const DungeonContent& content)
{
    std::size_t cleared = 0;
    for (const DungeonRoom& room : content.rooms) {
        if (roomCleared(scene, content, room.id)) ++cleared;
    }
    return cleared;
}

std::size_t roomsWithEnemies(const DungeonContent& content)
{
    std::set<graph::NodeId> rooms;
    for (const DungeonEnemy& enemy : content.enemies) rooms.insert(enemy.room);
    return rooms.size();
}

}
