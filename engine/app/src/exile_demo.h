#pragma once

#include "ichor/assets/exile.h"
#include "ichor/sim/scene.h"

#include <glm/vec3.hpp>

#include <cstdint>

// The Exile placeholder character shown in the arena behind --exile: one standing in
// Idle and one in Walk, each carrying the spear and the shield. The mesh and clip bases
// are where the exile bundle landed once the renderer and the animation set took it.
struct ExileDemo {
    ichor::SceneBundle bundle;
    ichor::ExileKit kit;
    std::uint32_t meshBase = 0;
    std::uint32_t clipBase = 0;
};

void spawnExileDemo(ichor::Scene& scene, const ExileDemo& demo, glm::dvec3 floorPoint);
