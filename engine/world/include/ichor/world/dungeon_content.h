#pragma once

#include "ichor/graph/graph.h"
#include "ichor/sim/scene.h"
#include "ichor/world/dungeon_greybox.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/placed_loot.h"

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ichor {

// What stands in a baked dungeon (doc 09, "instantiate": encounters and loot placement), as data. A pure function of
// the baked layout, its grey-box and the manifest's items for the dungeon, so one dungeon holds the same enemies, the
// same loot and the same boss marker every time, on every machine, whatever order things load in.
//
// Enemies are placed by the room's role in the mission graph: a challenge room holds one or two, the key room one
// guard and the climax room two, with the boss marker standing at its centre. The entry room, rests, junctions,
// reveals, gates and rewards hold none. Only skirmishers are placed: the other stage0 roles need systems the combat
// bridge does not register yet, so a room is never given an enemy the game cannot fight.
//
// Every enemy stands on one of four posts inside the hall, well clear of the walls, so never in a doorway, never over
// a ramp or a hole in the floor, and never in the entry room. Loot is the manifest's own: the items placed at this
// dungeon, which lie in the dungeon instead of outside its door (see outdoorLoot), in reward rooms first.
//
// What the player has done to it is not here: kills are recorded in the scene (recordKill), one marker entity per
// kill, so they are saved and loaded with the campaign and a scene that kills nothing hashes as it always did.

enum class DungeonEnemyRole : std::uint8_t { Skirmisher };

struct DungeonRoom {
    graph::NodeId id = 0;
    std::string type;          // the mission graph's room type: entry, challenge, key, climax, ...
    glm::dvec3 centre{0.0};    // the middle of the floor
    bool reachable = true;     // the player can walk here from the entry room through doorways and ramps
};

struct DungeonEnemy {
    graph::NodeId room = 0;
    std::uint32_t slot = 0;    // which of the room's posts, 0 to 3; with the room it names the enemy for good
    DungeonEnemyRole role = DungeonEnemyRole::Skirmisher;
    glm::dvec3 post{0.0};      // the feet
};

namespace dungeon_content {
inline constexpr double kPostX = 1.8;   // metres from the room's centre, each way
inline constexpr double kPostZ = 2.5;
inline constexpr std::size_t kPosts = 4;
inline constexpr std::size_t kMaxLoot = 4096;
inline constexpr double kHallHalfM = greybox::kRoomHalfM + greybox::kWallM; // a room owns its hall and the wall around it
}

// How the player gets from one room to the next: through a doorway on the same floor, or up or down a ramp.
enum class RoomLink : std::uint8_t { Door, RampUp, RampDown };

struct RoomStep {
    graph::NodeId room = 0; // the room the step arrives in
    RoomLink link = RoomLink::Door;
};

// The shortest walk, by rooms, from one room to another over the grey-box's doorways and ramps (what the player's body can
// pass, not the mission graph's logical links). A doorway in the wall a ramp or its hole runs along is not passable: the ramp
// fills the strip in front of it in the lower room and the hole does in the upper one. Empty for the same room, nullopt when
// `to` cannot be reached. Pure; of equal routes the one that tries north, east, south, west, up, down in that order first wins.
std::optional<std::vector<RoomStep>> routeBetweenRooms(const DungeonGreyBox& box, graph::NodeId from, graph::NodeId to);

struct DungeonContent {
    std::string poiId;
    std::vector<DungeonRoom> rooms;     // by id
    std::vector<DungeonEnemy> enemies;  // by (room, slot)
    std::vector<PlacedLoot> loot;       // by item id; x, z and groundY are in the dungeon's own space
    graph::NodeId bossRoom = 0;         // 0 when the dungeon has no climax room
    glm::dvec3 bossMarker{0.0};

    bool empty() const { return rooms.empty(); }
    const DungeonRoom* room(graph::NodeId id) const;
    // The room whose hall (walls included) and storey hold the point; null outside every room.
    const DungeonRoom* roomAt(const glm::dvec3& point) const;
    std::vector<std::size_t> enemiesIn(graph::NodeId room) const;
};

// `items` are the loot placed at this dungeon (their coordinates are replaced). Never throws; a layout or grey-box
// that failed, or a non-finite coordinate, gives empty content.
DungeonContent buildDungeonContent(const BakedDungeon& dungeon, const DungeonGreyBox& box, const std::vector<PlacedLoot>& items);

// The loot the manifest places at a dungeon POI, in the world set's order.
std::vector<PlacedLoot> lootAtPoi(const PlacedLootSet& loot, const std::string& poiId);

// Where one placed item ends up, for a system that wants to send the player to it (the errand). `inside` is false for an item
// that stays outdoors at the dungeon's mouth (a dungeon that cannot be baked) or that the dungeon does not hold.
struct ItemSite {
    bool inside = false;
    bool reachable = false;  // the room can be walked to from the entry room
    bool guarded = false;    // the room holds enemies
    bool crowded = false;    // another item lies on the very same spot (a room holds more items than it has spots), so F takes one per press
    graph::NodeId room = 0;
    std::string roomType;
};
ItemSite itemSite(const ForgedGame& game, const std::string& poiId, const std::string& itemId);

// The world's loot without the items that now lie inside a dungeon that can be entered (one with a usable slot): an
// item is never in two places. The same set when there is nothing to move, null for a game without loot.
std::shared_ptr<const PlacedLootSet> outdoorLoot(const ForgedGame& game);

// The campaign's record of what was killed, one marker entity per enemy named "dungeon_kill:<poi>:<room>:<slot>".
// recordKill is a command and shows from the next tick; recording the same kill twice is harmless.
bool enemyKilled(const Scene& scene, const std::string& poiId, graph::NodeId room, std::uint32_t slot);
void recordKill(Scene& scene, const std::string& poiId, const DungeonEnemy& enemy);
// Every enemy of a room is dead, and the room had at least one.
bool roomCleared(const Scene& scene, const DungeonContent& content, graph::NodeId room);
std::size_t clearedRooms(const Scene& scene, const DungeonContent& content);
std::size_t roomsWithEnemies(const DungeonContent& content);

}
