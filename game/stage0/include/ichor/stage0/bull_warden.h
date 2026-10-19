#pragma once

#include "ichor/anim/animation.h"
#include "ichor/assets/bundle.h"
#include "ichor/gfx/render_view.h"
#include "ichor/stage0/combat.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace ichor::stage0 {

// The Bull-Warden from art/monsters/bull_warden as the Stage 0 enemy, behind --bull-warden. Everything here is
// render-only: the pose is read from the combat state and never written back, so hashes and replays do not move.

struct BullWardenPose {
    std::uint32_t clip = AnimationSet::kNoClip;
    float time = 0.0f; // seconds into the clip
};

struct BullWardenClips {
    std::uint32_t idle = AnimationSet::kNoClip;
    std::uint32_t walk = AnimationSet::kNoClip;
    std::uint32_t attack = AnimationSet::kNoClip;
    std::uint32_t stagger = AnimationSet::kNoClip;
    std::uint32_t death = AnimationSet::kNoClip;
    float attackDuration = 0.0f;
    float staggerDuration = 0.0f;
    float deathDuration = 0.0f;
    float walkDuration = 0.0f;
    float idleDuration = 0.0f;
    bool complete() const;
};

// Where in the attack clip the swing starts and ends, as fractions of its length. The wind-up of the combat state
// is stretched over the first part, the active window over the swing and the recovery over the rest, so the blade
// is moving through the target exactly while the combat state is Active.
struct BullWardenSwing {
    float strikeStart = 0.34f; // the axe leaves its overhead hold at about 1.0 s of the 2.96 s clip
    float strikeEnd = 0.43f;   // and has struck the ground by about 1.27 s
};

struct BullWardenKit {
    SceneBundle bundle;
    AnimationSet animations;
    BullWardenClips clips;
    BullWardenSwing swing;
    std::vector<std::uint32_t> bodyJoints; // joints that decide where the ground is: everything but the hands, axe and shield
    float groundClearance = 0.0f;          // height of the lowest of them in the bind pose
    std::uint32_t mesh = 0; // renderer mesh index, set by the caller after uploadBundle
    float scale = 1.0f;
};

// Cooks SK_BullWarden.glb in `folder`.
bool cookBullWarden(const std::filesystem::path& folder, BullWardenKit& kit);

// Which clip and time show a fighter in this state. `moving` is whether it travelled since the previous tick,
// `clock` is a free-running time in seconds for the looping clips.
BullWardenPose bullWardenPose(const Combatant& combatant, const Tuning& tuning, const BullWardenClips& clips, const BullWardenSwing& swing, bool moving, double clock);

// Skinning matrices for a pose, and how far to lift the model so it stands on the floor. The clips put the hips at
// the origin instead of at the rest height (0.96 m), which sinks the model by about a metre, so the lift is taken
// from the lowest body joint of each pose rather than from a constant.
struct BullWardenFrame {
    std::vector<glm::mat4> skin;
    float lift = 0.0f; // metres, before the kit's scale
};
bool bullWardenFrame(const BullWardenKit& kit, const BullWardenPose& pose, BullWardenFrame& out);

// Adds the Bull-Warden for the entity with `foe` as its id, posed from its combat state.
void appendBullWarden(const Scene& scene, StableId foe, double alpha, const Tuning& tuning, const BullWardenKit& kit, RenderView& view);

}
