#pragma once

#include "ichor/anim/animation.h"
#include "ichor/anim/exile.h"
#include "ichor/gfx/render_view.h"
#include "ichor/gfx/renderer.h"
#include "ichor/stage0/combat.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace ichor::stage0 {

// The Exile (art/exile) as the Stage 0 player, behind --exile-player. Everything here is render
// side: it reads the combat components and never writes one, so hashes, replays and the corpus
// cannot change. Root motion stays out of the picture; the controller owns where the body goes.

enum class ExileClip : std::uint8_t { Idle, Walk, Run, Thrust, Hit, Swing, Block, Dodge, Death, Count };
constexpr std::size_t kExileClipCount = static_cast<std::size_t>(ExileClip::Count);

const char* name(ExileClip clip); // the clip's name in SK_Exile.glb

struct ExileClipChoice {
    ExileClip clip = ExileClip::Idle;
    bool locomotion = false; // loops on a render clock at a rate that follows the ground speed
    float progress = 0.0f;   // one-shots only: 0 to 1 across the clip, clamped
};

constexpr float kExileStandBelow = 0.3f; // m/s
constexpr float kExileWalkBelow = 1.75f; // below this the Walk clip, above it Run
constexpr float kExileMinRate = 0.25f;   // Walk covers 0.3 to 1.05 * 1.8 m/s and Run from 5.2 * 0.25 m/s, so the two overlap at the switch and neither slides
constexpr float kExileMaxRate = 1.8f;
constexpr float kExileDeathSeconds = 1.2f;
constexpr float kExileGuardRaiseSeconds = 0.25f;
// The block clip raises the shield and then lowers it again. This is how far through it the guard is
// square in front of the body (the left hand is at its highest and furthest forward), and it holds there.
constexpr float kExileGuardPhase = 0.38f;
constexpr float kExileFadeSeconds = 0.12f; // render time over which one clip's pose gives way to the next
constexpr float kExileTurnPerSecond = 12.0f;       // radians: how fast the body swings to face the way it travels
constexpr float kExileActionTurnPerSecond = 30.0f; // and back to the aim when it stops travelling

// Which clip plays for a combatant and how far into it. Swings and dodges are stretched to the
// state's length, so the strike lands in the Active window whatever the clip's own duration.
ExileClipChoice chooseExileClip(const Combatant& combatant, const Tuning& tuning, float planarSpeed, float fixedDt);

// Playback rate of a looping clip so its feet keep pace with the ground: `speed` over the clip's
// own root travel, held to a range where the stride still looks like one.
float exileLocomotionRate(float speed, float clipTravelSpeed);

// Skinning matrices `from` to `to` at `t` in [0, 1]: each joint's translation and scale mixed linearly and its
// rotation spherically, so a crossfade keeps limb lengths and the props on the hands stay attached.
void blendSkin(const std::vector<glm::mat4>& from, const std::vector<glm::mat4>& to, float t, std::vector<glm::mat4>& out);

class ExilePlayer {
public:
    // Cooks the folder's three GLBs and uploads them; meshes land at meshCount() as it was before the call.
    bool load(const std::filesystem::path& folder, Renderer& renderer);

    // The same without a renderer: cooks and readies the clips, for a caller that uploads kit().bundles
    // itself, in order, once `meshBase` meshes are already resident.
    bool prepare(const std::filesystem::path& folder, std::uint32_t meshBase);
    const ExileKit& kit() const { return kit_; }
    bool loaded() const { return loaded_; }

    // Adds the Exile (body, spear, shield) for every player-side combatant. `frameSeconds` advances the
    // locomotion clock and is render time only.
    void append(const Scene& scene, double alpha, double frameSeconds, float fixedDt, const Tuning& tuning, RenderView& view);

    // The model-space skinning matrices of a choice, with the root's ground travel taken out.
    bool pose(const ExileClipChoice& choice, float locomotionPhase, std::vector<glm::mat4>& skin) const;

    // Appends the body and props for a skinned pose standing at `feet`, turned by `yaw`.
    void appendPosed(const std::vector<glm::mat4>& skin, glm::dvec3 feet, float yaw, RenderView& view) const;

private:
    // The yaw the body is drawn at: the aim while it fights, the way it travels while it moves, the dodge's own way in a dodge.
    float bodyYaw(const Combatant& combatant, const ExileClipChoice& choice, glm::dvec3 step, float speed, float seconds);

    bool loaded_ = false;
    std::uint32_t meshBase_ = 0;
    ExileKit kit_;
    AnimationSet animations_;
    std::array<std::uint32_t, kExileClipCount> clips_{};
    std::array<float, kExileClipCount> travelSpeed_{};
    float locomotionPhase_ = 0.0f;
    float dodgeTravelYaw_ = 0.0f; // the way the Dodge clip's root travels, as a yaw from the way the body faces
    float bodyYaw_ = 0.0f;
    bool hasBodyYaw_ = false;
    ExileClip lastClip_ = ExileClip::Idle;
    std::vector<glm::mat4> lastSkin_;
    std::vector<glm::mat4> fadeFrom_;
    float fadeSeconds_ = kExileFadeSeconds;
};

}
