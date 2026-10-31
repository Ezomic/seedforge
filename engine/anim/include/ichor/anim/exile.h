#pragma once

#include "ichor/assets/bundle.h"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ichor {

// The Exile placeholder character from art/exile: one rig with nine clips, a spear and a
// round shield, each a GLB cooked by the ordinary glTF cooker. Props are untextured, there is
// one take per clip, and clips are not blended to loop. docs/exile-import.md has what was measured.

// A prop carried by a joint: with the joint's skinning matrix S, the prop's mesh is drawn
// at S * local, so `local` holds the bind pose of the joint and the grip.
struct JointSocket {
    std::string name;
    std::uint32_t joint = 0;
    std::uint32_t mesh = 0; // index across the kit's bundles in order: character, spear, shield
    glm::mat4 local{1.0f};
    // The shield: centred on the joint and turned each pose to face the way the hips face, `held` turning the
    // prop's own axes into that pose as at the start of idle. No fixed grip faces it forward in both idle and
    // block's guard, where the left forearm points at the threat.
    bool facesForward = false;
    glm::mat4 held{1.0f};
};

// The body's facing at the bind pose, carried by the hips and the spine. Rotations, not positions, so a crossfade
// (which turns joints spherically) turns the facing smoothly too.
struct ExileBody {
    std::uint32_t hips = 0;
    std::uint32_t spine = 0;
    glm::vec3 forward{0.0f, 0.0f, -1.0f}; // square to the up-legs' left-right and the spine's up
    glm::vec3 up{0.0f, 1.0f, 0.0f};       // from the hips to the spine
};

struct ExileKit {
    std::array<SceneBundle, 3> bundles; // SK_Exile.glb, SM_ExileSpear.glb, SM_ExileRoundShield.glb
    std::vector<JointSocket> sockets; // "spear", "shield"
    ExileBody body;
};

// Cooks the three GLBs in `folder` and works out where the props are held.
bool cookExile(const std::filesystem::path& folder, ExileKit& kit);

// Where a socket's prop is drawn, in the model space of `skin` (one pose's skinning matrices).
glm::mat4 heldMatrix(const ExileKit& kit, const JointSocket& socket, const std::vector<glm::mat4>& skin);

}
