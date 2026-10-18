#include "exile_demo.h"

#include "ichor/sim/attachment.h"

#include <string>
#include <vector>

namespace {

struct Placement {
    const char* name;
    const char* clip;
    glm::dvec3 offset;
};

}

void spawnExileDemo(ichor::Scene& scene, const ExileDemo& demo, glm::dvec3 floorPoint)
{
    const Placement placements[] = {{"exile-idle", "Idle", {1.3, 0.0, -0.4}}, {"exile-walk", "Walk", {0.3, 0.0, 1.4}}};
    std::vector<ichor::JointAttachment> attachments;
    for (const Placement& placement : placements) {
        std::uint32_t clip = 0;
        for (std::size_t i = 0; i < std::size(ichor::kExileClipNames); ++i) {
            if (std::string(ichor::kExileClipNames[i]) == placement.clip) clip = demo.clipBase + static_cast<std::uint32_t>(i);
        }
        ichor::SpawnCommand exile;
        exile.id = ichor::StableId::of(placement.name);
        exile.name.value = placement.name;
        exile.transform.position = floorPoint + placement.offset;
        exile.mesh = ichor::MeshRef{demo.meshBase + demo.kit.rig.characterMesh};
        // Root motion stays off: the travel of Walk is in the pose, so the walker strides
        // along and returns at each loop instead of leaving the arena.
        exile.animator = ichor::Animator{clip, 0.0f, 1.0f, true, false};
        scene.enqueue(exile);
        for (const ichor::JointSocket& socket : demo.kit.sockets) {
            ichor::SpawnCommand prop;
            prop.id = ichor::StableId::of(std::string(placement.name) + "-" + socket.name);
            prop.name.value = std::string(placement.name) + "-" + socket.name;
            prop.transform.position = floorPoint + placement.offset;
            prop.mesh = ichor::MeshRef{demo.meshBase + socket.mesh};
            scene.enqueue(prop);
            attachments.push_back({exile.id, prop.id, socket.joint, socket.local});
        }
    }
    scene.addSystem("exile_sockets", [attachments](ichor::Scene& s, double, const ichor::InputFrame&) {
        for (const ichor::JointAttachment& attachment : attachments) {
            ichor::applyAttachment(s, attachment);
        }
    });
}
