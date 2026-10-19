#pragma once

#include "ichor/gfx/render_view.h"
#include "ichor/stage0/combat.h"
#include "ichor/stage0/grey_box_mesh.h"

#include <vector>

namespace ichor::stage0 {

// Adds the grey body, spear and shield of every combatant, posed from its state. `alpha` blends
// the previous and current position like the rest of the view. `skip` is left out, for a caller that draws it itself.
void appendCharacterInstances(const Scene& scene, double alpha, const Tuning& tuning, const GreyBoxMeshes& meshes, std::vector<RenderInstance>& out, entt::entity skip = entt::null);

}
