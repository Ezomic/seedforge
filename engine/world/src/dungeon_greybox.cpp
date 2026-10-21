#include "ichor/world/dungeon_greybox.h"

#include "ichor/graph/mission.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace ichor {

namespace {

namespace gb = greybox;
using graph::NodeId;
using Cell = graph::embedding::Cell;

enum Side { North, East, South, West };

struct Room {
    NodeId id = 0;
    Cell cell;
    bool door[4] = {false, false, false, false};
    bool ramp = false; // a ramp up to the room above
    bool hole = false; // a hole in this room's floor, over the ramp of the room below
};

struct Builder {
    DungeonGreyBox out;
    glm::dvec3 origin{0.0};
    bool tooMany = false;

    void box(GreyBoxKind kind, glm::dvec3 centre, double x0, double x1, double y0, double y1, double z0, double z1)
    {
        if (out.pieces.size() >= gb::kMaxPieces) {
            tooMany = true;
            return;
        }
        GreyBoxPiece piece;
        piece.kind = kind;
        piece.centre = centre + glm::dvec3((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5);
        piece.halfExtents = glm::vec3(static_cast<float>((x1 - x0) * 0.5), static_cast<float>((y1 - y0) * 0.5), static_cast<float>((z1 - z0) * 0.5));
        out.pieces.push_back(piece);
    }
};

bool rampOnWest(std::int32_t lowerFloor) { return (lowerFloor & 1) == 0; }

void addWalls(Builder& b, const Room& room, glm::dvec3 c, double fy)
{
    constexpr double R = gb::kRoomHalfM;
    constexpr double W = R + gb::kWallM;
    constexpr double D = gb::kDoorWidthM * 0.5;
    const double bottom = fy - gb::kSlabM;
    const double top = fy + gb::kStoreyM;
    const double lintel = fy + gb::kDoorHeightM;
    // North and south run the full width, east and west fit between them.
    for (const Side side : {North, South}) {
        const double z0 = side == North ? -W : R;
        const double z1 = side == North ? -R : W;
        if (!room.door[side]) {
            b.box(GreyBoxKind::Wall, c, -W, W, bottom, top, z0, z1);
            continue;
        }
        b.box(GreyBoxKind::Wall, c, -W, -D, bottom, top, z0, z1);
        b.box(GreyBoxKind::Wall, c, D, W, bottom, top, z0, z1);
        b.box(GreyBoxKind::Wall, c, -D, D, lintel, top, z0, z1);
        b.box(GreyBoxKind::Floor, c, -D, D, bottom, fy, z0, z1);
    }
    for (const Side side : {West, East}) {
        const double x0 = side == West ? -W : R;
        const double x1 = side == West ? -R : W;
        if (!room.door[side]) {
            b.box(GreyBoxKind::Wall, c, x0, x1, bottom, top, -R, R);
            continue;
        }
        b.box(GreyBoxKind::Wall, c, x0, x1, bottom, top, -R, -D);
        b.box(GreyBoxKind::Wall, c, x0, x1, bottom, top, D, R);
        b.box(GreyBoxKind::Wall, c, x0, x1, lintel, top, -D, D);
        b.box(GreyBoxKind::Floor, c, x0, x1, bottom, fy, -D, D);
    }
}

void addFloor(Builder& b, const Room& room, glm::dvec3 c, double fy, bool holeWest)
{
    constexpr double R = gb::kRoomHalfM;
    const double bottom = fy - gb::kSlabM;
    if (!room.hole) {
        b.box(GreyBoxKind::Floor, c, -R, R, bottom, fy, -R, R);
        return;
    }
    const double edge = holeWest ? -R + gb::kRampWidthM : R - gb::kRampWidthM;
    if (holeWest) {
        b.box(GreyBoxKind::Floor, c, edge, R, bottom, fy, -R, R);
    } else {
        b.box(GreyBoxKind::Floor, c, -R, edge, bottom, fy, -R, R);
    }
}

void addRamp(Builder& b, glm::dvec3 c, double fy, bool west)
{
    constexpr double R = gb::kRoomHalfM;
    constexpr double thickness = 0.4;
    const double run = 2.0 * R;
    const double rise = gb::kStoreyM;
    const double length = std::sqrt(run * run + rise * rise);
    const double angle = std::atan2(rise, run);
    const double xs = west ? -R + gb::kRampWidthM * 0.5 : R - gb::kRampWidthM * 0.5;
    // The top surface runs from the floor at the north wall to the floor above at the south wall; the plank hangs below it.
    const glm::dvec3 normal(0.0, std::cos(angle), -std::sin(angle));
    GreyBoxPiece piece;
    piece.kind = GreyBoxKind::Floor;
    piece.centre = c + glm::dvec3(xs, fy + rise * 0.5, 0.0) - normal * (thickness * 0.5);
    piece.halfExtents = glm::vec3(static_cast<float>(gb::kRampWidthM * 0.5), static_cast<float>(thickness * 0.5), static_cast<float>(length * 0.5));
    piece.rotation = glm::angleAxis(static_cast<float>(-angle), glm::vec3(1.0f, 0.0f, 0.0f));
    if (b.out.pieces.size() >= gb::kMaxPieces) {
        b.tooMany = true;
        return;
    }
    b.out.pieces.push_back(piece);
}

}

DungeonGreyBox buildDungeonGreyBox(const BakedDungeon& dungeon, glm::dvec3 origin)
{
    Builder b;
    b.origin = origin;
    const auto fail = [&](std::string message) {
        b.out = DungeonGreyBox{};
        b.out.error = std::move(message);
        return std::move(b.out);
    };
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return fail("the dungeon origin is not a number");

    std::map<Cell, Room> byCell;
    std::map<NodeId, Cell> cellOf;
    NodeId entry = 0;
    for (const auto& [id, node] : dungeon.graph.nodes()) {
        const auto found = dungeon.cells.find(id);
        if (found == dungeon.cells.end()) continue;
        const Cell cell = found->second;
        if (std::abs(cell.x) > gb::kMaxCell || std::abs(cell.y) > gb::kMaxCell || std::abs(cell.floor) > gb::kMaxFloor) return fail("a room lies outside the supported grid");
        Room room;
        room.id = id;
        room.cell = cell;
        if (!byCell.emplace(cell, room).second) return fail("two rooms share one cell");
        cellOf[id] = cell;
        if (entry == 0 && node.type == graph::mission::kEntry) entry = id;
    }
    if (entry == 0) return fail("the layout has no entry room");

    // Doors and ramps come from the path links between rooms that touch.
    std::set<std::pair<Cell, Cell>> linked;
    for (const auto& [id, edge] : dungeon.graph.edges()) {
        if (edge.type != graph::mission::kPath) continue;
        const auto from = cellOf.find(edge.from);
        const auto to = cellOf.find(edge.to);
        if (from == cellOf.end() || to == cellOf.end()) continue;
        linked.insert({from->second, to->second});
        linked.insert({to->second, from->second});
    }
    for (auto& [cell, room] : byCell) {
        const std::array<std::pair<Side, Cell>, 4> around = {{
            {North, {cell.x, cell.y - 1, cell.floor}},
            {East, {cell.x + 1, cell.y, cell.floor}},
            {South, {cell.x, cell.y + 1, cell.floor}},
            {West, {cell.x - 1, cell.y, cell.floor}},
        }};
        for (const auto& [side, other] : around) {
            room.door[side] = linked.count({cell, other}) != 0 && byCell.count(other) != 0;
        }
        const Cell above{cell.x, cell.y, cell.floor + 1};
        room.ramp = linked.count({cell, above}) != 0 && byCell.count(above) != 0;
        const Cell below{cell.x, cell.y, cell.floor - 1};
        room.hole = linked.count({cell, below}) != 0 && byCell.count(below) != 0;
    }

    const Cell home = cellOf[entry];
    const auto centreOf = [&](const Cell& cell, double& fy) {
        fy = origin.y + static_cast<double>(cell.floor - home.floor) * gb::kStoreyM;
        return glm::dvec3(origin.x + static_cast<double>(cell.x - home.x) * gb::kPitchM, 0.0, origin.z + static_cast<double>(cell.y - home.y) * gb::kPitchM);
    };
    double lowest = origin.y;
    for (const auto& [cell, room] : byCell) {
        double fy = 0.0;
        const glm::dvec3 c = centreOf(cell, fy);
        lowest = std::min(lowest, fy);
        // The hole sits on the side the ramp below was built on, which depends on the floor below.
        addFloor(b, room, c, fy, rampOnWest(cell.floor - 1));
        addWalls(b, room, c, fy);
        if (room.ramp) addRamp(b, c, fy, rampOnWest(cell.floor));
        if (!byCell.count(Cell{cell.x, cell.y, cell.floor + 1})) {
            b.box(GreyBoxKind::Ceiling, c, -gb::kRoomHalfM, gb::kRoomHalfM, fy + gb::kStoreyM - gb::kSlabM, fy + gb::kStoreyM, -gb::kRoomHalfM, gb::kRoomHalfM);
        }
        if (b.tooMany) return fail("the dungeon needs more boxes than the grey-box limit");
    }

    double fy = 0.0;
    const glm::dvec3 c = centreOf(home, fy);
    const Room& entryRoom = byCell[home];
    constexpr double markerOffset = 3.5;
    b.box(GreyBoxKind::Marker, c, markerOffset - 0.5, markerOffset + 0.5, fy, fy + 2.6, markerOffset - 0.5, markerOffset + 0.5);
    b.out.exitPoint = c + glm::dvec3(markerOffset, fy, markerOffset);
    b.out.exitPoint.y = fy;
    b.out.spawn = glm::dvec3(c.x, fy + 0.5, c.z);
    // Face the first doorway, else the exit.
    glm::dvec2 facing(1.0, 1.0);
    const std::array<std::pair<Side, glm::dvec2>, 4> directions = {{{North, {0.0, -1.0}}, {East, {1.0, 0.0}}, {South, {0.0, 1.0}}, {West, {-1.0, 0.0}}}};
    for (const auto& [side, direction] : directions) {
        if (entryRoom.door[side]) {
            facing = direction;
            break;
        }
    }
    b.out.spawnYaw = std::atan2(facing.x, -facing.y);
    b.out.lowestY = lowest - 24.0;
    if (b.tooMany) return fail("the dungeon needs more boxes than the grey-box limit");
    return std::move(b.out);
}

namespace {

Mesh cubeMesh()
{
    Mesh mesh;
    mesh.name = "dungeon_cube";
    mesh.boundsMin = {-0.5f, -0.5f, -0.5f};
    mesh.boundsMax = {0.5f, 0.5f, 0.5f};
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };
    constexpr float h = 0.5f;
    const Face faces[6] = {
        {{0, 1, 0}, {{-h, h, -h}, {h, h, -h}, {h, h, h}, {-h, h, h}}},
        {{0, -1, 0}, {{-h, -h, h}, {h, -h, h}, {h, -h, -h}, {-h, -h, -h}}},
        {{0, 0, -1}, {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}}},
        {{1, 0, 0}, {{h, -h, -h}, {h, -h, h}, {h, h, h}, {h, h, -h}}},
        {{0, 0, 1}, {{h, -h, h}, {-h, -h, h}, {-h, h, h}, {h, h, h}}},
        {{-1, 0, 0}, {{-h, -h, h}, {-h, -h, -h}, {-h, h, -h}, {-h, h, h}}},
    };
    for (const Face& face : faces) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int v = 0; v < 4; ++v) {
            Vertex vertex{};
            vertex.position = face.corners[v];
            vertex.normal = face.normal;
            vertex.tangent = {-face.normal.z, 0.0f, face.normal.x, 1.0f};
            vertex.uv = {v == 1 || v == 2 ? 1.0f : 0.0f, v >= 2 ? 1.0f : 0.0f};
            vertex.weights[0] = 65535;
            mesh.vertices.push_back(vertex);
        }
        for (const std::uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(first + i);
    }
    mesh.submeshes.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

}

SceneBundle buildDungeonKit()
{
    SceneBundle bundle;
    const std::array<std::pair<const char*, glm::vec3>, kGreyBoxKindCount> looks = {{
        {"dungeon_wall", {0.46f, 0.43f, 0.40f}},
        {"dungeon_floor", {0.34f, 0.32f, 0.30f}},
        {"dungeon_ceiling", {0.26f, 0.25f, 0.26f}},
        {"dungeon_exit", {0.92f, 0.66f, 0.22f}},
    }};
    for (std::size_t i = 0; i < kGreyBoxKindCount; ++i) {
        Material material;
        material.name = looks[i].first;
        material.baseColorFactor = glm::vec4(looks[i].second, 1.0f);
        material.metallicFactor = 0.0f;
        material.roughnessFactor = 0.9f;
        bundle.materials.push_back(material);
        Mesh mesh = cubeMesh();
        mesh.submeshes.front().material = static_cast<std::uint32_t>(i);
        bundle.meshes.push_back(std::move(mesh));
    }
    return bundle;
}

}
