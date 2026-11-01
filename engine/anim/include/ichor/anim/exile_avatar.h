#pragma once

#include "ichor/anim/animation.h"
#include "ichor/anim/exile.h"
#include "ichor/gfx/render_view.h"

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace ichor {

// The Exile (art/exile) as the walking player of a forged world. Everything here is drawn, never
// simulated: it reads where the body is and how fast it moved, and writes only render instances,
// so sim hashes, replays and saves cannot change. Root motion stays out of the picture; the
// character controller owns where the body goes and the clip only supplies the legs.

enum class AvatarClip : std::uint8_t { Idle, Walk, Run, Count };

constexpr float kAvatarStandBelow = 0.3f; // m/s
constexpr float kAvatarWalkBelow = 2.2f;  // below this the Walk clip, above it Run
constexpr float kAvatarMinRate = 0.4f;
constexpr float kAvatarMaxRate = 2.2f;
constexpr double kAvatarSnapMetres = 0.4; // how far the body may stand off the terrain before the feet stop following it
constexpr double kAvatarTeleportAbove = 30.0; // m/s: a move this fast in one tick is a teleport or a respawn, not a stride
constexpr float kAvatarTurnPerSecond = 12.0f; // radians: the body swings to face the way it walks in a quarter second or so

// Not a number reads as standing.
AvatarClip chooseAvatarClip(float planarSpeed);

// Playback rate of a looping clip so its feet keep pace with the ground: `speed` over the clip's
// own root travel, held to a range where the stride still looks like one.
float avatarClipRate(float speed, float clipTravelSpeed);

// The yaw (0 faces -Z, a positive yaw turns toward +X) of a horizontal move. Not a number is 0.
float yawOfMotion(double dx, double dz);

// `current` turned toward `target` by at most `maxStep` radians the short way round, wrapped to
// (-pi, pi]. Not a number in either keeps `current` (0 if that is not a number either).
float turnToward(float current, float target, float maxStep);

// Where the drawn feet stand: on the terrain while the body is within kAvatarSnapMetres of it
// (a slope the capsule rides a little above or below the drawn ground), at the body's own height
// when it is further (off a ledge, on a roof). Not a number for the ground keeps the body's height.
double avatarFeetHeight(double bodyY, double groundY);

// The feet whose contact times lie in the stretch of a looping clip from `fromPhase` to `toPhase` (0..1 shares of the
// clip; `toPhase` below `fromPhase` means the stretch wrapped past the end), in the order they land. A contact exactly
// at `fromPhase` was counted by the stretch before.
std::vector<std::uint32_t> footfallsBetween(const std::vector<FootContact>& contacts, float duration, float fromPhase, float toPhase);

class ExileAvatar {
public:
    // Readies the clips of an already cooked kit whose meshes sit in the renderer from `meshBase`.
    bool prepare(const ExileKit& kit, std::uint32_t meshBase);
    bool loaded() const { return loaded_; }

    // Advances the stride by `seconds` of render time at `speed` m/s and returns the clip that plays.
    AvatarClip advance(float speed, double seconds);

    // The skinning matrices of a clip at the current stride, with the root's ground travel taken out.
    bool pose(AvatarClip clip, std::vector<glm::mat4>& skin) const;

    // Whether the walk and run clips carry foot contacts, so the stride can say when each foot lands.
    bool hasFootContacts() const { return hasContacts_; }

    // The feet that landed since the last call, in order, as the stride crossed their cooked contact times.
    std::vector<std::uint32_t> takeFootfalls();

    // Advances, then appends the body and props standing at `feet`, turned by `yaw`.
    void append(glm::dvec3 feet, float yaw, float speed, double seconds, RenderView& view);

    // The way the body faces before it first moves.
    void face(float yaw) { yaw_ = yaw; }

    // Draws the body that moved from `previous` to `current` over one tick of `tickSeconds`, `alpha` of the way
    // between them, on the terrain at `groundY`. The speed that picks the clip is that move over the tick, the body
    // turns to face the way it moves and keeps its facing while it stands, and the stride runs on `renderSeconds`
    // (any steadily rising clock). Returns where the feet were drawn.
    glm::dvec3 appendPlayer(glm::dvec3 previous, glm::dvec3 current, double alpha, double tickSeconds, double renderSeconds, double groundY, RenderView& view);

private:
    bool loaded_ = false;
    std::uint32_t meshBase_ = 0;
    ExileKit kit_;
    AnimationSet animations_;
    std::array<std::uint32_t, static_cast<std::size_t>(AvatarClip::Count)> clips_{};
    std::array<float, static_cast<std::size_t>(AvatarClip::Count)> travelSpeed_{};
    float phase_ = 0.0f;
    bool hasContacts_ = false;
    std::vector<std::uint32_t> footfalls_;
    float yaw_ = 0.0f;
    double lastSeconds_ = 0.0;
    bool hasSeconds_ = false;
};

}
