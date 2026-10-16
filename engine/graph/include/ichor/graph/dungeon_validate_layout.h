#pragma once

#include "ichor/graph/graph.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor::graph::dungeon {

// A dungeon as plain data, so a hand-written or hostile description (duplicate
// ids, dangling links) can be judged without going through Graph, which makes
// such states impossible. Types use the names from mission.h.
struct Room {
    std::uint32_t id = 0;
    std::string type;
};

struct Link {
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    std::string type;
    bool oneWay = false; // a drop: path links are otherwise walkable both ways
};

struct Layout {
    std::vector<Room> rooms;
    std::vector<Link> links;
};

Layout fromGraph(const Graph& graph);

// Anything larger is rejected before any traversal, so hostile input costs
// bounded time.
inline constexpr std::size_t kMaxRooms = 512;
inline constexpr std::size_t kMaxLinks = 4096;
inline constexpr std::size_t kMaxKeys = 10;

enum class Code {
    Empty,
    TooLarge,
    DuplicateId,
    DanglingLink,
    UnknownType,
    BadUnlock,
    EntryCount,
    NoObjective,
    GateWithoutKey,
    Unreachable,
    KeyNotObtainable,
    GateBeforeKey,
    SoftLock,
};

struct Finding {
    Code code = Code::Empty;
    std::uint32_t room = 0; // the room concerned, 0 when none
    std::string message;
};

// Path links can be walked both ways (backtracking is allowed) unless marked
// oneWay, shortcut links are always one way; a gate can only be entered
// holding every key that has an "unlocks" link to it; keys are picked up on
// entry and never lost. An empty result means: every room reachable from the
// entry, every key obtainable before its gate is needed, the objective (every
// climax) reachable, and no reachable state (room plus keys held) from which
// a climax can no longer be reached. Search is exact over those states
// (at most kMaxRooms * 2^kMaxKeys). Never throws; structural problems stop the run early.
std::vector<Finding> validateLayout(const Layout& layout);

}
