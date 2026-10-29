#include "exile_demo.h"

#include "ichor/sim/attachment.h"

#include <glm/matrix.hpp>

#include <string>
#include <vector>

namespace {

struct Placement {
    const char* name;
    std::uint32_t clip;
    glm::dvec3 offset;
};

struct HeldProp {
    ichor::JointAttachment attachment;
    const ichor::JointSocket* socket = nullptr;
};

}

bool loadExileDemo(const std::filesystem::path& folder, ichor::Renderer& renderer, ichor::AnimationSet& animations, ExileDemo& demo)
{
    demo.meshBase = renderer.meshCount();
    if (!ichor::cookExile(folder, demo.kit)) return false;
    for (const ichor::SceneBundle& bundle : demo.kit.bundles) {
        if (!renderer.uploadBundle(bundle)) return false;
    }
    // Looked up in a set of the kit alone: findClip on the shared set would take the first clip of that name from any bundle.
    ichor::AnimationSet own;
    if (!own.load(demo.kit.bundles[0])) return false;
    const std::uint32_t base = animations.clipCount();
    demo.idleClip = own.findClip("idle");
    demo.walkClip = own.findClip("walk");
    if (demo.idleClip == ichor::AnimationSet::kNoClip || demo.walkClip == ichor::AnimationSet::kNoClip) return false;
    demo.idleClip += base;
    demo.walkClip += base;
    return animations.load(demo.kit.bundles[0]);
}

void spawnExileDemo(ichor::Scene& scene, const ExileDemo& demo, glm::dvec3 floorPoint)
{
    const Placement placements[] = {{"exile-idle", demo.idleClip, {1.3, 0.0, -0.4}}, {"exile-walk", demo.walkClip, {0.3, 0.0, 1.4}}};
    std::vector<HeldProp> held;
    for (const Placement& placement : placements) {
        ichor::SpawnCommand exile;
        exile.id = ichor::StableId::of(placement.name);
        exile.name.value = placement.name;
        exile.transform.position = floorPoint + placement.offset;
        exile.mesh = ichor::MeshRef{demo.meshBase};
        // Root motion stays off: the travel of Walk is in the pose, so the walker strides
        // along and returns at each loop instead of leaving the arena.
        exile.animator = ichor::Animator{placement.clip, 0.0f, 1.0f, true, false};
        scene.enqueue(exile);
        for (const ichor::JointSocket& socket : demo.kit.sockets) {
            ichor::SpawnCommand prop;
            prop.id = ichor::StableId::of(std::string(placement.name) + "-" + socket.name);
            prop.name.value = std::string(placement.name) + "-" + socket.name;
            prop.transform.position = floorPoint + placement.offset;
            prop.mesh = ichor::MeshRef{demo.meshBase + socket.mesh};
            scene.enqueue(prop);
            held.push_back({{exile.id, prop.id, socket.joint, socket.local}, &socket});
        }
    }
    // `demo` outlives the scene's systems (main keeps it), so the kit is held by pointer rather than copied.
    scene.addSystem("exile_sockets", [held, kit = &demo.kit](ichor::Scene& s, double, const ichor::InputFrame&) {
        for (const HeldProp& prop : held) {
            ichor::JointAttachment attachment = prop.attachment;
            const entt::entity holder = s.find(attachment.holder);
            const auto* pose = holder == entt::null ? nullptr : s.registry().try_get<ichor::SkinPose>(holder);
            if (prop.socket->facesForward && pose != nullptr && attachment.joint < pose->matrices.size()) {
                attachment.local = glm::inverse(pose->matrices[attachment.joint]) * ichor::heldMatrix(*kit, *prop.socket, pose->matrices);
            }
            ichor::applyAttachment(s, attachment);
        }
    });
}
