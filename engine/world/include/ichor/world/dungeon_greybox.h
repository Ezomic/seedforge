#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/world/placed_dungeons.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// A baked dungeon layout (doc 09) as boxes you can walk through: the grey-box stand-in until the dungeon kit exists.
// Every room is a square hall on its grid cell, a "path" link between touching rooms is a doorway through the shared
// wall, and a path link between two floors is a ramp in the lower room under a hole in the upper room's floor.
// "unlocks" and "shortcut" links are logical and get no geometry. A pure function of the layout, so one dungeon is the
// same boxes every time, and it holds only collision and look: nothing here is simulation state.

enum class GreyBoxKind : std::uint8_t { Wall, Floor, Ceiling, Marker, Loot, Boss };
inline constexpr std::size_t kGreyBoxKindCount = 6;

struct GreyBoxPiece {
    GreyBoxKind kind = GreyBoxKind::Wall;
    glm::dvec3 centre{0.0};
    glm::vec3 halfExtents{0.5f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

namespace greybox {
inline constexpr double kPitchM = 12.0;     // distance between room centres
inline constexpr double kRoomHalfM = 5.0;   // half the clear width of a room
inline constexpr double kWallM = 1.0;       // thickness of the wall each room owns
inline constexpr double kStoreyM = 4.5;     // floor to floor, slab included
inline constexpr double kSlabM = 0.5;
inline constexpr double kDoorWidthM = 3.0;
inline constexpr double kDoorHeightM = 3.0;
inline constexpr double kRampWidthM = 2.7;
inline constexpr std::int32_t kMaxCell = 64;   // |x| and |y| of a cell
inline constexpr std::int32_t kMaxFloor = 16;  // |floor| of a cell
inline constexpr std::size_t kMaxPieces = 8192;
}

// Where a room stands, for what is put in it. `centre` is the middle of the floor; `doors` are the open walls in the order
// north (-z), east (+x), south (+z), west (-x).
struct GreyBoxRoom {
    graph::NodeId id = 0;
    glm::dvec3 centre{0.0};
    bool doors[4] = {false, false, false, false};
    bool ramp = false; // a ramp up to the room above, along one side
    bool hole = false; // a hole in the floor, over the ramp of the room below
};

struct DungeonGreyBox {
    std::vector<GreyBoxPiece> pieces;
    std::vector<GreyBoxRoom> rooms; // by node id
    glm::dvec3 spawn{0.0};      // feet, in the entry room
    double spawnYaw = 0.0;      // radians, the walking view's yaw that faces into the dungeon
    glm::dvec3 exitPoint{0.0};  // foot of the exit marker
    double lowestY = 0.0;       // below this the player has left the dungeon
    std::string error;          // empty on success; the rest is empty otherwise

    bool ok() const { return error.empty(); }
};

// `origin` is where the entry room's floor centre lies. Refuses, with a message, a layout with no entry room, two rooms
// on one cell, a cell out of range, or more boxes than kMaxPieces. Never throws.
DungeonGreyBox buildDungeonGreyBox(const BakedDungeon& dungeon, glm::dvec3 origin);

// One unit cube centred on the origin per GreyBoxKind, in that order, with a material each. The cube has all six faces,
// so the underside of a ceiling is drawn. Pieces are drawn by scaling the cube by twice their half extents.
SceneBundle buildDungeonKit();

}
