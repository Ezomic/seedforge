#pragma once

#include "ichor/assets/bundle.h"
#include "ichor/assets/fbx_import.h"

#include <filesystem>
#include <vector>

namespace ichor {

// The Exile placeholder character from art/exile: one rig, nine clips, a spear and a
// round shield. Untextured, one take per clip, clips not blended to loop.
struct ExileKit {
    FbxRig rig;
    std::vector<JointSocket> sockets; // "spear" and "shield"
};

constexpr const char* kExileClipNames[] = {"Idle", "Walk", "Run", "Thrust", "Hit", "Swing", "Block", "Dodge", "Death"};

// Cooks the whole kit from the art/exile folder into `bundle`: skeleton and mesh first,
// then the clips in kExileClipNames order, then the two props.
bool cookExile(const std::filesystem::path& folder, const FbxImportOptions& options, SceneBundle& bundle, ExileKit& kit);

}
