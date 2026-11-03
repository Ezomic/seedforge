#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/bull_warden.h"
#include "ichor/stage0/combat.h"

namespace ichor {

// Draws the foe of the stage as the role it plays (SEED-661). The Skirmisher and Controller are the plain Bull-Warden.
// The Anchor is the Bull-Warden at 1.3 times its size and the Artillery at 0.8, until those roles have bodies of their
// own; each stands in a ring of its own colour, and the slam and shell wind-ups play the attack clip. Render only: it
// reads the scene and writes nothing back.
void appendRoleFoe(const Scene& scene, const stage0::Stage0& stage, double alpha, const stage0::BullWardenKit& kit, RenderView& view);

// What a body under these effects is multiplied by: white with none, red-shifted while bleeding, blue-shifted while slowed.
glm::vec3 statusTint(const stage0::StatusSet& status);

// The scale the Bull-Warden is drawn at for a role, relative to the kit's own.
float roleScale(stage0::FoeRole role);

}
