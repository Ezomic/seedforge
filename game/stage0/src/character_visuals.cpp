#include "ichor/stage0/character_visuals.h"

#include "ichor/stage0/character_pose.h"

namespace ichor::stage0 {

void appendCharacterInstances(const Scene& scene, double alpha, const Tuning& tuning, const GreyBoxMeshes& meshes, std::vector<RenderInstance>& out, bool skipPlayer, entt::entity skip)
{
    for (auto [entity, transform, previous, c] : scene.registry().view<const Transform, const PreviousTransform, const Combatant>().each()) {
        if (entity == skip || (skipPlayer && c.side == 0)) continue;
        const glm::dvec3 feet = glm::mix(previous.position, transform.position, alpha);
        const CharacterTransforms parts = characterTransforms(characterPose(poseInputFor(c, tuning)), feet, c.yaw);
        const auto add = [&](std::uint32_t mesh, const PartTransform& part) {
            RenderInstance instance;
            instance.mesh = mesh;
            instance.position = part.position;
            instance.basis = part.basis;
            out.push_back(instance);
        };
        add(meshes.body, parts.body);
        add(meshes.spear, parts.spear);
        add(meshes.shield, parts.shield);
    }
}

}
