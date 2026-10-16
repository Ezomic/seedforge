#pragma once

#include "ichor/graph/graph.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ichor::graph::hubshortcut {

inline constexpr std::uint32_t kMaxHubs = 8;
inline constexpr std::uint32_t kMaxShortcutsPerHub = 4;
inline constexpr std::size_t kMaxNodes = 2048;

struct HubParams {
    std::uint64_t seed = 0;
    std::uint32_t maxHubs = 2;          // clamped to kMaxHubs
    std::uint32_t shortcutsPerHub = 2;  // clamped to kMaxShortcutsPerHub
};

// A shortcut edge (type "shortcut") from `farSide` back to `hub`. It starts
// locked: it can only be opened by standing at `farSide`, and once open it
// only runs from the far side to the hub, never the other way.
struct HubShortcut {
    EdgeId edge = 0;
    NodeId farSide = 0;
    NodeId hub = 0;
};

struct Plan {
    Graph graph;                        // the input mission plus the new shortcut edges
    std::vector<NodeId> hubs;           // ascending ids
    std::vector<HubShortcut> shortcuts; // the edges added by this plan, in creation order
    std::string error;                  // empty on success; graph and lists are empty otherwise
};

// Designates hub rooms (entry or junction nodes the mission routes through
// more than once) and adds locked shortcuts from later rooms back to them.
// The input must pass mission::validateMission and have at most kMaxNodes
// nodes. The same graph and params always give the same plan; the input is
// not modified. The result is re-validated, so a plan never contains a
// soft-lock or a way past a gate.
Plan addHubShortcuts(const Graph& mission, const HubParams& params);

// Empty when a shortcut farSide -> hub may be added to the mission: both
// nodes exist and differ, hub is a path ancestor of farSide, the link is not
// already present, and it would not make anything reachable with every gate
// shut that was not reachable before. Otherwise the reason it is refused.
std::string refuseShortcut(const Graph& mission, NodeId farSide, NodeId hub);

struct UnlockState {
    std::set<EdgeId> open;
};

// Opens the shortcut only when the actor stands at its far side. False when
// the edge is not a shortcut of the plan, the actor is elsewhere, or it is
// already open.
bool unlock(const Plan& plan, UnlockState& state, EdgeId edge, NodeId actorAt);

// True only for far side -> hub along a shortcut of the plan that is open.
bool canTraverse(const Plan& plan, const UnlockState& state, NodeId from, NodeId to);

}
