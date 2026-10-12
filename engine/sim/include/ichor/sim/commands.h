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
    std::optional<StaticBox> staticBox;
    std::optional<CharacterBody> character;
    std::optional<Animator> animator;
};

struct DestroyCommand {
    StableId id;
};

// Teleports. Input-driven movement goes through the character controller
// and is recorded by the input stream, not by commands.
struct MoveCommand {
    StableId id;
    glm::dvec3 delta{0.0};
};

// Starts a montage on an entity that has an Animator; ignored while one is
// already playing, which is what makes attacks commit.
struct PlayMontageCommand {
    StableId id;
    std::uint32_t clip = 0;
    float activeStart = 0.0f;
    float activeEnd = 0.0f;
    float recoveryEnd = 0.0f;
    std::uint32_t windupSound = kNoSound;
    std::uint32_t activeSound = kNoSound;
};

using Command = std::variant<SpawnCommand, DestroyCommand, MoveCommand, PlayMontageCommand>;

struct CommandLog {
    struct Entry {
        std::uint64_t tick = 0;
        Command command;
    };
    std::vector<Entry> entries;
};

}
