#pragma once

#include "ichor/anim/animation.h"
#include "ichor/anim/exile.h"
#include "ichor/gfx/renderer.h"
#include "ichor/sim/scene.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>

// The Exile placeholder character shown in the arena behind --exile: one standing in Idle and
// one in Walk, each carrying the spear and the shield. The mesh and clip bases are where the
// kit landed once the renderer and the animation set took it.
struct ExileDemo {
    ichor::ExileKit kit;
    std::uint32_t meshBase = 0;
    std::uint32_t idleClip = 0;
    std::uint32_t walkClip = 0;
};

bool loadExileDemo(const std::filesystem::path& folder, ichor::Renderer& renderer, ichor::AnimationSet& animations, ExileDemo& demo);
void spawnExileDemo(ichor::Scene& scene, const ExileDemo& demo, glm::dvec3 floorPoint);
