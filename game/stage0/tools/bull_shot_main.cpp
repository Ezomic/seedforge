// Renders the Bull-Warden in five combat states next to the grey player body, on a headless device (no window),
// and writes one PNG: stage0_bull_shot out.png [behind]
#include "ichor/assets/image.h"
#include "ichor/gfx/vk_offscreen.h"
#include "ichor/stage0/bull_warden.h"
#include "ichor/stage0/grey_box_mesh.h"
#include "ichor/stage0/character_pose.h"

#include <glm/gtc/quaternion.hpp>

#include <cstdio>
#include <string_view>

using namespace ichor;
using namespace ichor::stage0;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::puts("usage: stage0_bull_shot out.png [behind]");
        return 2;
    }
    constexpr std::uint32_t kWidth = 1600;
    constexpr std::uint32_t kHeight = 600;
    OffscreenRenderer renderer;
    if (!renderer.init(kWidth, kHeight, false)) {
        std::puts("no Vulkan 1.3 device available");
        return 1;
    }
    SceneBundle grey;
    const GreyBoxMeshes meshes = appendGreyBoxMeshes(grey);
    if (!renderer.uploadBundle(grey)) return 1;
    BullWardenKit kit;
    if (!cookBullWarden(std::filesystem::path(ICHOR_ART_DIR) / "monsters" / "bull_warden", kit)) return 1;
    kit.mesh = static_cast<std::uint32_t>(grey.meshes.size());
    if (!renderer.uploadBundle(kit.bundle)) return 1;

    const Tuning tuning;
    struct Shot {
        const char* name;
        CombatState state;
        float time;
    };
    const Shot shots[] = {
        {"idle", CombatState::Idle, 0.0f},
        {"wind-up", CombatState::Windup, tuning.lightWindup},
        {"swing", CombatState::Active, tuning.lightActive * 0.5f},
        {"stagger", CombatState::Staggered, tuning.staggerDuration * 0.5f},
        {"dead", CombatState::Dead, 3.9f},
    };

    RenderView view;
    view.sunDirection = glm::normalize(glm::vec3(-0.4f, 0.9f, 0.5f));
    const bool behind = argc > 2 && std::string_view(argv[2]) == "behind"; // the camera on the -Z side, where the Bull-Warden looks
    const glm::dvec3 eye(0.0, 2.2, behind ? -10.5 : 10.5);
    const glm::dvec3 target(0.0, 1.2, 0.0);
    view.camera.position = eye;
    view.camera.rotation = glm::quatLookAt(glm::normalize(glm::vec3(target - eye)), glm::vec3(0.0f, 1.0f, 0.0f));
    view.camera.fovY = glm::radians(40.0f);
    double x = -6.4;
    for (const Shot& shot : shots) {
        Combatant c;
        c.state = static_cast<std::uint8_t>(shot.state);
        c.stateTime = shot.time;
        const BullWardenPose pose = bullWardenPose(c, tuning, kit.clips, kit.swing, false, 0.0);
        BullWardenFrame frame;
        if (!bullWardenFrame(kit, pose, frame)) return 1;
        RenderInstance bull;
        bull.mesh = kit.mesh;
        bull.position = {x, static_cast<double>(frame.lift * kit.scale), 0.0};
        bull.basis = glm::mat3(kit.scale);
        bull.jointOffset = static_cast<std::uint32_t>(view.jointPalette.size());
        bull.jointCount = static_cast<std::uint32_t>(frame.skin.size());
        view.jointPalette.insert(view.jointPalette.end(), frame.skin.begin(), frame.skin.end());
        view.instances.push_back(bull);
        // The player's grey body beside it, for scale.
        const CharacterTransforms player = characterTransforms(characterPose(PoseInput{}), {x + 1.3, 0.0, 0.0}, 0.0f);
        RenderInstance body;
        body.mesh = meshes.body;
        body.position = player.body.position;
        body.basis = player.body.basis;
        view.instances.push_back(body);
        x += 3.2;
    }
    for (int i = -8; i <= 8; ++i) {
        view.lines.push_back({glm::dvec3(i, 0.0, -3.0), glm::dvec3(i, 0.0, 3.0), glm::vec3(0.3f)});
    }
    for (int i = -3; i <= 3; ++i) {
        view.lines.push_back({glm::dvec3(-8.0, 0.0, i), glm::dvec3(8.0, 0.0, i), glm::vec3(0.3f)});
    }
    std::vector<std::uint8_t> pixels;
    if (!renderer.render(view, pixels)) return 1;
    return writePng(argv[1], kWidth, kHeight, pixels.data()) ? 0 : 1;
}
