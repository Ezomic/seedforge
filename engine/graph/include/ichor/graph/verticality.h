#pragma once

#include "ichor/graph/graph.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ichor::graph::verticality {

// How an edge is realised in 3D. Stairs span exactly one floor and are walkable
// both ways, shafts span any number of floors and give a sightline between them,
// drops are one-way and only descend along the edge direction.
enum class Connector : std::uint8_t { Flat, Stair, Shaft, Drop };

inline constexpr std::int32_t kMaxFloor = 1000000;
inline constexpr std::uint32_t kMaxRequired = 64;

// Floor index per room (node) and connector per edge. A room with no entry is
// a violation; an edge with no entry is Flat.
struct Layout {
    std::map<NodeId, std::int32_t> floors;
    std::map<EdgeId, Connector> connectors;
};

struct Requirements {
    std::uint32_t minFloors = 3;       // distinct floors used by the whole layout
    std::uint32_t minLevelChanges = 2; // floor changes along the entry to climax critical path
    std::uint32_t minShafts = 1;       // open shafts giving sightlines between floors
};

// The critical path is the shortest chain of "path" edges (see mission.h) from
// the entry to the climax, in edge IDs. Empty when there is no single entry and
// climax or no such chain.
std::vector<EdgeId> criticalPath(const Graph& graph);

// Violations of the requirements and of connector consistency; empty when the
// layout is acceptable. Never throws and tolerates any floor value.
std::vector<std::string> check(const Graph& graph, const Layout& layout, const Requirements& req);

struct EnforceResult {
    Layout layout;
    std::string error; // empty on success
};

// Builds a layout that satisfies the requirements: level changes (stairs) are
// spread along the critical path, the other rooms take the floor of their
// nearest path room, and shafts are placed on vertical edges. Fails (error set)
// when the graph cannot carry the requirements, for example a critical path
// shorter than the level changes asked for.
EnforceResult enforce(const Graph& graph, const Requirements& req);

}
