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
    // A square heightfield of sampleCount^2 heights (metres), row major, whose
    // first sample sits at `origin` and whose samples are `spacing` apart.
    void addHeightField(StableId id, const float* heights, std::uint32_t sampleCount, glm::dvec3 origin, float spacing);
    void addCharacter(StableId id, const CharacterBody& body, const Transform& transform);
    void remove(StableId id);
    void clear();

    void setCharacterIntent(StableId id, glm::vec3 horizontalVelocity);
    void setCharacterPosition(StableId id, glm::dvec3 position);

    // Floating origin: Jolt works in floats, so every body sits relative to
    // this world position. Moving it recreates the statics and shifts the
    // characters; nothing in world coordinates changes.
    void setOrigin(glm::dvec3 origin);
    glm::dvec3 origin() const;
    bool hasBody(StableId id) const;

    // Re-evaluates every character's contacts against the bodies that exist
    // now. A restore calls this once after all bodies are back.
    void refreshCharacters();

    void step(double dt);

    struct CharacterState {
        glm::dvec3 position{0.0};
        glm::vec3 velocity{0.0f};
        float fallSpeed = 0.0f;
        bool onGround = false;
    };
    bool character(StableId id, CharacterState& out) const;

    void debugLines(std::vector<LineSegment>& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
