#include "ichor/anim/exile_avatar.h"

#include "ichor/core/log.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace ichor {

namespace {

constexpr float kPi = 3.14159265358979323846f;

constexpr const char* kClipNames[] = {"idle", "walk", "run"};

float wrapAngle(float angle)
{
    float wrapped = std::fmod(angle, 2.0f * kPi);
    if (wrapped > kPi) wrapped -= 2.0f * kPi;
    if (wrapped <= -kPi) wrapped += 2.0f * kPi;
    return wrapped;
}

}

AvatarClip chooseAvatarClip(float planarSpeed)
{
    if (!std::isfinite(planarSpeed) || planarSpeed < kAvatarStandBelow) return AvatarClip::Idle;
    return planarSpeed < kAvatarWalkBelow ? AvatarClip::Walk : AvatarClip::Run;
}

float avatarClipRate(float speed, float clipTravelSpeed)
{
    if (!std::isfinite(speed) || !std::isfinite(clipTravelSpeed) || clipTravelSpeed < 0.05f) return 1.0f;
    return std::clamp(speed / clipTravelSpeed, kAvatarMinRate, kAvatarMaxRate);
}

float yawOfMotion(double dx, double dz)
{
    if (!std::isfinite(dx) || !std::isfinite(dz)) return 0.0f;
    return static_cast<float>(std::atan2(dx, -dz));
}

float turnToward(float current, float target, float maxStep)
{
    if (!std::isfinite(current)) current = 0.0f;
    if (!std::isfinite(target) || !std::isfinite(maxStep)) return wrapAngle(current);
    const float difference = wrapAngle(target - current);
    const float step = std::max(maxStep, 0.0f);
    if (std::abs(difference) <= step) return wrapAngle(target);
    return wrapAngle(current + (difference > 0.0f ? step : -step));
}

double avatarFeetHeight(double bodyY, double groundY)
{
    if (!std::isfinite(bodyY)) return 0.0;
    if (!std::isfinite(groundY)) return bodyY;
    return std::abs(bodyY - groundY) <= kAvatarSnapMetres ? groundY : bodyY;
}

bool ExileAvatar::prepare(const ExileKit& kit, std::uint32_t meshBase)
{
    loaded_ = false;
    meshBase_ = meshBase;
    kit_ = kit;
    animations_.clear();
    if (!animations_.load(kit_.bundles[0])) return false;
    for (std::size_t i = 0; i < clips_.size(); ++i) {
        clips_[i] = animations_.findClip(kClipNames[i]);
        if (clips_[i] == AnimationSet::kNoClip) {
            logf(LogLevel::Error, "the Exile has no {} clip", kClipNames[i]);
            return false;
        }
        const float duration = animations_.clipDuration(clips_[i]);
        travelSpeed_[i] = 0.0f;
        glm::vec3 start;
        glm::vec3 end;
        if (duration > 0.0f && animations_.rootTranslation(clips_[i], 0.0f, start) && animations_.rootTranslation(clips_[i], duration, end)) {
            travelSpeed_[i] = std::hypot(end.x - start.x, end.z - start.z) / duration;
        }
    }
    phase_ = 0.0f;
    loaded_ = true;
    return true;
}

AvatarClip ExileAvatar::advance(float speed, double seconds)
{
    const AvatarClip clip = chooseAvatarClip(speed);
    const std::size_t index = static_cast<std::size_t>(clip);
    const float duration = animations_.clipDuration(clips_[index]);
    if (duration > 0.0f && std::isfinite(seconds) && seconds > 0.0) {
        const float rate = clip == AvatarClip::Idle ? 1.0f : avatarClipRate(speed, travelSpeed_[index]);
        phase_ += static_cast<float>(seconds) * rate / duration;
        phase_ -= std::floor(phase_);
    }
    return clip;
}

bool ExileAvatar::pose(AvatarClip clip, std::vector<glm::mat4>& skin) const
{
    const std::uint32_t id = clips_[static_cast<std::size_t>(clip)];
    glm::vec3 root(0.0f);
    if (!animations_.sample(id, phase_ * animations_.clipDuration(id), skin, root)) return false;
    for (glm::mat4& m : skin) {
        m[3].x -= root.x;
        m[3].z -= root.z;
    }
    return true;
}

void ExileAvatar::append(glm::dvec3 feet, float yaw, float speed, double seconds, RenderView& view)
{
    if (!loaded_) return;
    const AvatarClip clip = advance(speed, seconds);
    std::vector<glm::mat4> skin;
    if (!pose(clip, skin)) return;
    const float safeYaw = std::isfinite(yaw) ? yaw : 0.0f;
    const glm::mat3 facing = glm::mat3_cast(glm::angleAxis(-safeYaw, glm::vec3(0.0f, 1.0f, 0.0f)));

    RenderInstance body;
    body.mesh = meshBase_;
    body.position = feet;
    body.basis = facing;
    body.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
    body.jointCount = static_cast<std::uint32_t>(skin.size());
    view.jointPalette.insert(view.jointPalette.end(), skin.begin(), skin.end());
    view.instances.push_back(body);

    for (const JointSocket& socket : kit_.sockets) {
        if (socket.joint >= skin.size()) continue;
        const glm::mat4 held = heldMatrix(kit_, socket, skin);
        RenderInstance prop;
        prop.mesh = meshBase_ + socket.mesh;
        prop.position = feet + glm::dvec3(facing * glm::vec3(held[3]));
        prop.basis = facing * glm::mat3(held);
        view.instances.push_back(prop);
    }
}

glm::dvec3 ExileAvatar::appendPlayer(glm::dvec3 previous, glm::dvec3 current, double alpha, double tickSeconds, double renderSeconds, double groundY, RenderView& view)
{
    const auto finite = [](glm::dvec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
    if (!finite(previous) || !finite(current)) return finite(current) ? current : glm::dvec3(0.0);
    const double blend = std::isfinite(alpha) ? std::clamp(alpha, 0.0, 1.0) : 1.0;
    glm::dvec3 feet = glm::mix(previous, current, blend);
    feet.y = avatarFeetHeight(feet.y, groundY);
    const double dx = current.x - previous.x;
    const double dz = current.z - previous.z;
    double speed = tickSeconds > 0.0 && std::isfinite(tickSeconds) ? std::hypot(dx, dz) / tickSeconds : 0.0;
    if (speed > kAvatarTeleportAbove) speed = 0.0;
    double seconds = 0.0;
    if (hasSeconds_ && std::isfinite(renderSeconds)) seconds = std::clamp(renderSeconds - lastSeconds_, 0.0, 0.1);
    if (std::isfinite(renderSeconds)) {
        lastSeconds_ = renderSeconds;
        hasSeconds_ = true;
    }
    if (std::isfinite(speed) && speed >= kAvatarStandBelow) {
        yaw_ = turnToward(yaw_, yawOfMotion(dx, dz), kAvatarTurnPerSecond * static_cast<float>(seconds));
    }
    append(feet, yaw_, static_cast<float>(speed), seconds, view);
    return feet;
}

}
