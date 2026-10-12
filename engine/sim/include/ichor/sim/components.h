#pragma once

#include "ichor/core/hash.h"
#include "ichor/core/reflect.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <glm/mat4x4.hpp>

namespace ichor {

constexpr std::uint32_t kNoSound = 0xffffffffu;

// Every entity carries a StableId derived from world structure, never from
// spawn order (doc 14). Saves, commands and replays refer to entities by it.
struct StableId {
    std::uint64_t value = 0;

    static StableId of(std::string_view name) { return {hash64(name)}; }
    static StableId of(std::string_view name, std::uint64_t index) { return {hashCombine(hash64(name), index)}; }
    bool operator==(const StableId&) const = default;
    auto operator<=>(const StableId&) const = default;
};

struct Name {
    std::string value;
};

struct Transform {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// Where the entity was at the start of the tick. Rendering interpolates
// between this and Transform; it is never saved.
struct PreviousTransform {
    glm::dvec3 position{0.0};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct MeshRef {
    std::uint32_t mesh = 0;
};

// Rotates its entity every tick; the simplest system that makes ticks matter.
struct Spin {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radiansPerSecond = 1.0f;
};

// Moves under player input. Only the speed is data; the input arrives per tick.
struct Player {
    float metresPerSecond = 3.0f;
};

// A static collider; the transform gives its centre and rotation.
struct StaticBox {
    glm::vec3 halfExtents{1.0f};
};

// A capsule character standing on Transform::position. The horizontal
// intent is set from input each tick; the rest is written back by physics.
struct CharacterBody {
    float radius = 0.3f;
    float halfHeight = 0.6f;
    glm::vec3 intent{0.0f};
    glm::vec3 velocity{0.0f}; // what the controller reported after the step
    float fallSpeed = 0.0f;   // the controller's own accumulated vertical velocity
    bool onGround = false;
};

// Plays a clip from the AnimationSet the scene was given. With rootMotion
// the root joint's travel becomes the character's intent instead of input.
struct Animator {
    std::uint32_t clip = 0;
    float time = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool rootMotion = false;
};

// A committed action in progress: the clip and its windows, and where it is.
struct MontagePlayer {
    std::uint32_t clip = 0;
    float activeStart = 0.0f;
    float activeEnd = 0.0f;
    float recoveryEnd = 0.0f;
    float time = 0.0f;
    std::uint8_t phase = 0; // MontagePhase
    std::uint32_t windupSound = kNoSound; // raised when the wind-up begins
    std::uint32_t activeSound = kNoSound; // raised when the active window opens
};

// Skinning matrices for rendering, rebuilt every tick; never saved.
struct SkinPose {
    std::vector<glm::mat4> matrices;
};

template <>
struct Reflect<StableId> {
    static constexpr auto fields() { return std::make_tuple(field("value", &StableId::value)); }
};
template <>
struct Reflect<Name> {
    static constexpr auto fields() { return std::make_tuple(field("value", &Name::value)); }
};
template <>
struct Reflect<Transform> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("position", &Transform::position), field("rotation", &Transform::rotation), field("scale", &Transform::scale));
    }
};
template <>
struct Reflect<MeshRef> {
    static constexpr auto fields() { return std::make_tuple(field("mesh", &MeshRef::mesh)); }
};
template <>
struct Reflect<Spin> {
    static constexpr auto fields() { return std::make_tuple(field("axis", &Spin::axis), field("radiansPerSecond", &Spin::radiansPerSecond)); }
};
template <>
struct Reflect<Player> {
    static constexpr auto fields() { return std::make_tuple(field("metresPerSecond", &Player::metresPerSecond)); }
};
template <>
struct Reflect<Animator> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("clip", &Animator::clip), field("time", &Animator::time), field("speed", &Animator::speed),
            field("loop", &Animator::loop), field("rootMotion", &Animator::rootMotion));
    }
};
template <>
struct Reflect<MontagePlayer> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("clip", &MontagePlayer::clip), field("activeStart", &MontagePlayer::activeStart),
            field("activeEnd", &MontagePlayer::activeEnd), field("recoveryEnd", &MontagePlayer::recoveryEnd),
            field("time", &MontagePlayer::time), field("phase", &MontagePlayer::phase),
            field("windupSound", &MontagePlayer::windupSound), field("activeSound", &MontagePlayer::activeSound));
    }
};
template <>
struct Reflect<StaticBox> {
    static constexpr auto fields() { return std::make_tuple(field("halfExtents", &StaticBox::halfExtents)); }
};
template <>
struct Reflect<CharacterBody> {
    static constexpr auto fields()
    {
        return std::make_tuple(field("radius", &CharacterBody::radius), field("halfHeight", &CharacterBody::halfHeight),
            field("intent", &CharacterBody::intent), field("velocity", &CharacterBody::velocity), field("fallSpeed", &CharacterBody::fallSpeed),
            field("onGround", &CharacterBody::onGround));
    }
};

}
