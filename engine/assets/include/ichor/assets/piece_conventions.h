#pragma once

#include "ichor/assets/piece_data.h"

#include <cstddef>
#include <string>
#include <vector>

struct cgltf_data;

namespace ichor {

// What the glTF node names say about sockets, LODs and collision (docs/art-pipeline.md).
// A node with a role other than Plain is not placed as an ordinary piece.
enum class NodeRole {
    Plain,
    Socket,
    Collision,
    LodExtra, // LOD1 or LOD2 of a valid group: cooked as a mesh, not placed as a node
};

struct ConventionScan {
    struct Lod {
        std::string piece;
        std::vector<std::size_t> nodes; // glTF node per level, LOD0 first
        std::vector<float> distances;
    };

    std::vector<NodeRole> roles; // one per glTF node
    std::vector<Socket> sockets;
    std::vector<Lod> lods;
    std::vector<CollisionShape> collision;
    std::vector<std::string> issues; // malformed names; each node an issue names keeps its old behaviour
};

constexpr std::size_t kMaxLodLevels = 3;

void scanConventions(const cgltf_data& data, ConventionScan& scan);

// One line per socket, LOD group, collision shape and issue, for the cooker's validator mode.
std::vector<std::string> describeConventions(const ConventionScan& scan);

}
