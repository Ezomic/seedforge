#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ichor {

// Sockets, LOD groups and collision shapes of a cooked scene (docs/art-pipeline.md). They live
// beside SceneBundle rather than in it so that adding them did not touch the widely included bundle.h.
// A named attachment point on a piece, in the piece's own space (docs/art-pipeline.md).
// `piece` is empty for a socket that hangs off no mesh; its transform is then file space.
struct Socket {
    std::string piece;
    std::string name; // without the SOCKET_ prefix
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct LodLevel {
    std::uint32_t mesh = 0;
    float distance = 0.0f; // the level is used from this camera distance on
};

// levels[n] is LODn; distances never decrease and levels[0].distance is 0.
struct LodGroup {
    std::string piece;
    std::vector<LodLevel> levels;
};

enum class CollisionKind : std::uint32_t {
    Box = 1, // UBX_<piece>: boundsMin/boundsMax are the box, points is empty
    Convex = 2, // UCX_<piece>: points are the hull candidates, bounds enclose them
};

struct CollisionShape {
    std::string piece;
    std::string name; // the full node name, UBX_/UCX_ included
    CollisionKind kind = CollisionKind::Box;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<glm::vec3> points; // piece space
};

struct PieceData {
    std::vector<Socket> sockets;
    std::vector<LodGroup> lodGroups;
    std::vector<CollisionShape> collision;

    bool empty() const { return sockets.empty() && lodGroups.empty() && collision.empty(); }
};

// A socket of `piece`, or of no piece when the piece has none by that name.
inline const Socket* findSocket(const PieceData& pieces, std::string_view piece, std::string_view name)
{
    const Socket* fileLevel = nullptr;
    for (const Socket& socket : pieces.sockets) {
        if (socket.name != name) continue;
        if (socket.piece == piece) return &socket;
        if (socket.piece.empty()) fileLevel = &socket;
    }
    return fileLevel;
}

inline const LodGroup* findLodGroup(const PieceData& pieces, std::string_view piece)
{
    for (const LodGroup& group : pieces.lodGroups) {
        if (group.piece == piece) return &group;
    }
    return nullptr;
}

// The mesh to draw `group` with at `distance`: the last level whose distance has been reached.
inline std::uint32_t lodMeshAt(const LodGroup& group, float distance)
{
    std::uint32_t mesh = group.levels.front().mesh;
    for (const LodLevel& level : group.levels) {
        if (distance >= level.distance) mesh = level.mesh;
    }
    return mesh;
}

}
