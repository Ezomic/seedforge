#pragma once

#include "ichor/sim/components.h"

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace ichor {

// State changes only through commands (doc 15, rule 2). The scene applies
// them at the start of a tick and records every one in the log, so a log
// replayed into a fresh scene reproduces the same state hash.

struct SpawnCommand {
    StableId id;
    Name name;
    Transform transform;
    std::optional<MeshRef> mesh;
    std::optional<Spin> spin;
    std::optional<Player> player;
};

struct DestroyCommand {
    StableId id;
};

struct MoveCommand {
    StableId id;
    glm::dvec3 delta{0.0};
};

using Command = std::variant<SpawnCommand, DestroyCommand, MoveCommand>;

struct CommandLog {
    struct Entry {
        std::uint64_t tick = 0;
        Command command;
    };
    std::vector<Entry> entries;
};

}
