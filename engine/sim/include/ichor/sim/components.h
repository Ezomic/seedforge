#pragma once

#include "ichor/core/hash.h"
#include "ichor/core/reflect.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace ichor {

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

}
