#include "ichor/sim/attachment.h"

#include "ichor/sim/scene.h"

#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace ichor {

void applyAttachment(Scene& scene, const JointAttachment& attachment)
{
    entt::registry& registry = scene.registry();
    const entt::entity holder = scene.find(attachment.holder);
    const entt::entity prop = scene.find(attachment.prop);
    if (holder == entt::null || prop == entt::null) {
        return;
    }
    const auto* holderTransform = registry.try_get<Transform>(holder);
    const auto* pose = registry.try_get<SkinPose>(holder);
    auto* propTransform = registry.try_get<Transform>(prop);
    if (holderTransform == nullptr || pose == nullptr || propTransform == nullptr || attachment.joint >= pose->matrices.size()) {
        return;
    }
    const glm::mat4 carried = pose->matrices[attachment.joint] * attachment.local;
    glm::mat3 axes(carried);
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(axes[i]);
        if (!(length > 1e-6f) || !std::isfinite(length)) {
            return;
        }
        axes[i] /= length;
    }
    const glm::vec3 offset = holderTransform->rotation * (glm::vec3(carried[3]) * holderTransform->scale);
    propTransform->position = holderTransform->position + glm::dvec3(offset);
    propTransform->rotation = glm::normalize(holderTransform->rotation * glm::quat_cast(axes));
    propTransform->scale = holderTransform->scale;
}

}
