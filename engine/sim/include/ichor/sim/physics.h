#pragma once

#include "ichor/sim/components.h"

#include <glm/vec3.hpp>

#include <memory>
#include <vector>

namespace ichor {

struct LineSegment {
    glm::dvec3 a{0.0};
    glm::dvec3 b{0.0};
    glm::vec3 color{1.0f};
};

// Jolt behind a small door. Bodies are addressed by StableId, the world
// steps on the fixed tick with a single-threaded job system so a given
// input stream always produces the same result on a given platform, and
// characters are Jolt virtual characters driven by an intent velocity.
class PhysicsWorld {
public:
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void addStaticBox(StableId id, const StaticBox& box, const Transform& transform);
    void addCharacter(StableId id, const CharacterBody& body, const Transform& transform);
    void remove(StableId id);
    void clear();

    void setCharacterIntent(StableId id, glm::vec3 horizontalVelocity);
    void setCharacterPosition(StableId id, glm::dvec3 position);

    void step(double dt);

    struct CharacterState {
        glm::dvec3 position{0.0};
        glm::vec3 velocity{0.0f};
        bool onGround = false;
    };
    bool character(StableId id, CharacterState& out) const;

    void debugLines(std::vector<LineSegment>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
