#pragma once

#include "ichor/assets/bundle.h"

#include <glm/mat4x4.hpp>

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
};

struct ExileKit {
    std::array<SceneBundle, 3> bundles; // SK_Exile.glb, SM_ExileSpear.glb, SM_ExileRoundShield.glb
    std::vector<JointSocket> sockets; // "spear", "shield"
};

// Cooks the three GLBs in `folder` and works out where the props are held.
bool cookExile(const std::filesystem::path& folder, ExileKit& kit);

}
