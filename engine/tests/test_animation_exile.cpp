#include "ichor/anim/animation.h"
#include "ichor/anim/exile.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/sim/attachment.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>

using namespace ichor;

namespace {

constexpr const char* kExileClipNames[] = {"idle", "walk", "run", "thrust", "hit", "swing", "block", "dodge", "death"};

const std::filesystem::path kExile = std::filesystem::path(ICHOR_ART_DIR) / "exile";

const ExileKit& exile()
{
    static const ExileKit kit = [] {
        ExileKit k;
        REQUIRE(cookExile(kExile, k));
        return k;
    }();
    return kit;
}

const SceneBundle& character()
{
    return exile().bundles[0];
}

std::uint32_t jointIndex(std::string_view name)
{
    const auto& names = character().skeletons[0].jointNames;
    const auto it = std::find(names.begin(), names.end(), name);
    REQUIRE(it != names.end());
    return static_cast<std::uint32_t>(it - names.begin());
}

std::unique_ptr<AnimationSet> loadAnimations();

// Which way the body faces on the ground at the first frame of idle: from the left ankle to its toes.
glm::vec2 bodyForward()
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    std::vector<glm::mat4> pose;
    REQUIRE(set.samplePose(set.findClip("idle"), 0.0f, pose));
    const glm::vec3 d = glm::vec3(pose[jointIndex("mixamorig:LeftToeBase")][3]) - glm::vec3(pose[jointIndex("mixamorig:LeftFoot")][3]);
    return glm::normalize(glm::vec2(d.x, d.z));
}

// Not a static: ozz asserts at exit that nothing it allocated is still alive.
std::unique_ptr<AnimationSet> loadAnimations()
{
    auto set = std::make_unique<AnimationSet>();
    REQUIRE(set->load(character()));
    return set;
}

// Mean ground speed of the left ankle while it is planted, over one cycle. A foot that stays put
// while the body travels is the whole point of the root travel matching the legs.
float plantedFootSpeed(const char* clipName)
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    const std::uint32_t clip = set.findClip(clipName);
    REQUIRE(clip != AnimationSet::kNoClip);
    const std::uint32_t foot = jointIndex("mixamorig:LeftFoot");
    const float duration = set.clipDuration(clip);
    const int steps = static_cast<int>(duration * 60.0f);
    std::vector<glm::vec3> position;
    std::vector<glm::mat4> pose;
    for (int i = 0; i <= steps; ++i) {
        REQUIRE(set.samplePose(clip, duration * static_cast<float>(i) / static_cast<float>(steps), pose));
        position.push_back(glm::vec3(pose[foot][3]));
    }
    float lowest = position[0].y;
    for (const glm::vec3& p : position) lowest = std::min(lowest, p.y);
    float sum = 0.0f;
    int planted = 0;
    const float dt = duration / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        if (position[i].y > lowest + 0.04f) continue;
        sum += glm::length(glm::vec2(position[i + 1].x - position[i].x, position[i + 1].z - position[i].z)) / dt;
        ++planted;
    }
    REQUIRE(planted >= 3);
    return sum / static_cast<float>(planted);
}

}

TEST_CASE("the Exile skeleton cooks to the root joint and 34 bones with their file names")
{
    const SceneBundle& c = character();
    REQUIRE(c.skeletons.size() == 1);
    const Skeleton& skeleton = c.skeletons[0];
    REQUIRE(skeleton.jointNames.size() == 35);
    REQUIRE(skeleton.inverseBind.size() == 35);
    REQUIRE(skeleton.jointNames[0] == "root");
    REQUIRE(skeleton.jointNames[1] == "mixamorig:Hips");
    for (const char* name : {"mixamorig:Spine2", "mixamorig:Head", "mixamorig:LeftHand", "mixamorig:RightHand", "mixamorig:LeftToeBase",
             "mixamorig:RightToeBase", "mixamorig:LeftHandThumb3", "mixamorig:RightHandIndex3"}) {
        REQUIRE(std::find(skeleton.jointNames.begin(), skeleton.jointNames.end(), name) != skeleton.jointNames.end());
    }
    auto sorted = skeleton.jointNames;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());

    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    REQUIRE(set.jointCount(0) == 35);
    REQUIRE(set.clipCount() == 9);
    for (const char* clip : kExileClipNames) {
        REQUIRE(set.findClip(clip) != AnimationSet::kNoClip);
    }
}

TEST_CASE("the Exile mesh is metric, upright, on the ground, facing -Z and fully skinned")
{
    const Mesh& mesh = character().meshes[0];
    REQUIRE(mesh.skeleton == 0);
    // About two metres tall: a wrong scale would make it 200 or 0.02, a wrong up axis would lie it down.
    REQUIRE(mesh.boundsMin.y == Catch::Approx(0.0f).margin(0.05f));
    REQUIRE(mesh.boundsMax.y == Catch::Approx(2.0f).margin(0.2f));
    REQUIRE(mesh.boundsMax.x - mesh.boundsMin.x < 1.5f);
    // Posed, the toes lie ahead of the ankles, and Ichor's forward is -Z.
    REQUIRE(bodyForward().y < -0.5f);
    for (const Vertex& v : mesh.vertices) {
        std::uint32_t sum = 0;
        for (std::uint16_t w : v.weights) sum += w;
        REQUIRE(sum >= 65534);
        REQUIRE(sum <= 65536);
        for (int k = 0; k < 4; ++k) REQUIRE(v.joints[k] < 35);
    }
    REQUIRE(character().textures.size() == 1);
}

TEST_CASE("skinning the first frame of idle leaves the Exile standing on the ground")
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    std::vector<glm::mat4> skin;
    REQUIRE(set.skinningMatrices(set.findClip("idle"), 0.0f, skin));
    float lowest = 1e9f;
    float highest = -1e9f;
    for (const Vertex& v : character().meshes[0].vertices) {
        glm::vec4 p(0.0f);
        for (int k = 0; k < 4; ++k) p += (static_cast<float>(v.weights[k]) / 65535.0f) * (skin[v.joints[k]] * glm::vec4(v.position, 1.0f));
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    REQUIRE(lowest == Catch::Approx(0.0f).margin(0.15f));
    REQUIRE(highest == Catch::Approx(2.0f).margin(0.25f));
}

TEST_CASE("walk travels about 1.05 m/s and run about 5.2 m/s along the way the body faces, from the root joint")
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    const glm::vec2 forward = bodyForward();
    const auto travel = [&](const char* name, float& duration) {
        const std::uint32_t clip = set.findClip(name);
        REQUIRE(clip != AnimationSet::kNoClip);
        duration = set.clipDuration(clip);
        glm::vec3 start;
        glm::vec3 end;
        REQUIRE(set.rootTranslation(clip, 0.0f, start));
        REQUIRE(set.rootTranslation(clip, duration, end));
        return end - start;
    };
    float walkSeconds = 0.0f;
    const glm::vec3 walk = travel("walk", walkSeconds);
    REQUIRE(walkSeconds == Catch::Approx(2.7f).margin(0.15f));
    REQUIRE(glm::dot(glm::vec2(walk.x, walk.z), forward) / walkSeconds == Catch::Approx(1.05f).margin(0.1f));
    REQUIRE(std::abs(walk.y) < 0.05f);

    float runSeconds = 0.0f;
    const glm::vec3 run = travel("run", runSeconds);
    REQUIRE(glm::dot(glm::vec2(run.x, run.z), forward) / runSeconds == Catch::Approx(5.2f).margin(0.5f));
    REQUIRE(std::abs(run.y) < 0.05f);
}

TEST_CASE("the feet plant while the body travels: the root travel matches the legs")
{
    // If the root travelled against the legs the planted ankle would slide at about twice walking speed (1.8 m/s
    // against 0.07 here). Run has too few planted frames at 60 Hz to say anything.
    const float walk = plantedFootSpeed("walk");
    INFO("planted ankle speed in walk: " << walk << " m/s");
    REQUIRE(walk < 1.0f);
}

TEST_CASE("no clip drags the hips off the ground or through it")
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    std::vector<glm::mat4> pose;
    for (const char* name : kExileClipNames) {
        const std::uint32_t clip = set.findClip(name);
        REQUIRE(set.clipDuration(clip) > 1.0f);
        for (int i = 0; i <= 20; ++i) {
            REQUIRE(set.samplePose(clip, set.clipDuration(clip) * static_cast<float>(i) / 20.0f, pose));
            INFO(name);
            REQUIRE(pose[1][3].y > 0.2f);
            REQUIRE(pose[1][3].y < 1.8f);
        }
    }
}

TEST_CASE("the spear is held in the right hand and the shield in the left, on the hand at the start of idle")
{
    const ExileKit& kit = exile();
    REQUIRE(kit.sockets.size() == 2);
    REQUIRE(kit.sockets[0].name == "spear");
    REQUIRE(kit.sockets[1].name == "shield");
    REQUIRE(character().skeletons[0].jointNames[kit.sockets[0].joint] == "mixamorig:RightHand");
    REQUIRE(character().skeletons[0].jointNames[kit.sockets[1].joint] == "mixamorig:LeftHand");

    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    const std::uint32_t idle = set.findClip("idle");
    std::vector<glm::mat4> skin;
    std::vector<glm::mat4> pose;
    REQUIRE(set.skinningMatrices(idle, 0.0f, skin));
    REQUIRE(set.samplePose(idle, 0.0f, pose));
    for (std::size_t s = 0; s < kit.sockets.size(); ++s) {
        const JointSocket& socket = kit.sockets[s];
        const Mesh& prop = kit.bundles[s + 1].meshes[0];
        REQUIRE(socket.mesh == character().meshes.size() + s);
        const glm::mat4 carried = skin[socket.joint] * socket.local;
        const glm::vec3 hand(pose[socket.joint][3]);
        // The prop's origin is its grip or its centre, so it lands on the hand.
        INFO(socket.name);
        REQUIRE(glm::length(glm::vec3(carried[3]) - hand) < 0.02f);
        // And the box around the carried prop holds the hand.
        glm::vec3 low(1e9f);
        glm::vec3 high(-1e9f);
        for (const Vertex& v : prop.vertices) {
            const glm::vec3 p(carried * glm::vec4(v.position, 1.0f));
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        REQUIRE(glm::length(glm::max(glm::max(low - hand, hand - high), glm::vec3(0.0f))) < 0.02f);
    }
    // The spear stands upright, and is the long one: about 2.5 m.
    const JointSocket& spear = kit.sockets[0];
    const Mesh& spearMesh = kit.bundles[1].meshes[0];
    const glm::mat4 carried = skin[spear.joint] * spear.local;
    glm::vec3 low(1e9f);
    glm::vec3 high(-1e9f);
    for (const Vertex& v : spearMesh.vertices) {
        const glm::vec3 p(carried * glm::vec4(v.position, 1.0f));
        low = glm::min(low, p);
        high = glm::max(high, p);
    }
    REQUIRE(high.y - low.y > 2.0f);
    REQUIRE(high.x - low.x < 0.5f);
    REQUIRE(high.z - low.z < 0.5f);
}

TEST_CASE("the cooked Exile survives the bundle file")
{
    const auto path = test::tempPath("ichor_exile.iscene");
    REQUIRE(writeBundle(path, character()));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.skeletons[0].jointNames == character().skeletons[0].jointNames);
    REQUIRE(loaded.clips.size() == 9);
    REQUIRE(loaded.clips[1].ozzBytes == character().clips[1].ozzBytes);
}

TEST_CASE("idle, walk and run loop: the last pose meets the first")
{
    const auto owned = loadAnimations();
    const AnimationSet& set = *owned;
    // The largest distance any joint is from where it started, with the root's travel taken out.
    const auto seam = [&](const char* name) {
        const std::uint32_t clip = set.findClip(name);
        std::vector<glm::mat4> first;
        std::vector<glm::mat4> last;
        REQUIRE(set.samplePose(clip, 0.0f, first));
        REQUIRE(set.samplePose(clip, set.clipDuration(clip), last));
        float widest = 0.0f;
        for (std::size_t j = 2; j < first.size(); ++j) {
            const glm::vec3 a = glm::vec3(first[j][3]) - glm::vec3(first[0][3]);
            const glm::vec3 b = glm::vec3(last[j][3]) - glm::vec3(last[0][3]);
            widest = std::max(widest, glm::length(a - b));
        }
        return widest;
    };
    REQUIRE(seam("idle") < 0.05f);
    REQUIRE(seam("walk") < 0.05f);
    REQUIRE(seam("run") < 0.05f);
}

TEST_CASE("a joint attachment carries a prop with the holder's joint")
{
    Scene scene(1);
    SpawnCommand holder;
    holder.id = StableId::of("holder");
    holder.name.value = "holder";
    holder.transform.position = {10.0, 0.0, 0.0};
    scene.enqueue(holder);
    SpawnCommand prop;
    prop.id = StableId::of("prop");
    prop.name.value = "prop";
    scene.enqueue(prop);
    scene.tick(0.0);
    SkinPose pose;
    pose.matrices.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -2.0f)) * glm::mat4_cast(glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0))));
    scene.registry().emplace<SkinPose>(scene.find(holder.id), pose);

    applyAttachment(scene, {holder.id, prop.id, 0, glm::mat4(1.0f)});
    const Transform placed = scene.registry().get<Transform>(scene.find(prop.id));
    REQUIRE(glm::length(glm::vec3(placed.position - glm::dvec3(10.0, 1.0, -2.0))) < 1e-5f);
    REQUIRE(glm::length(placed.rotation * glm::vec3(0, 0, 1) - glm::vec3(1, 0, 0)) < 1e-5f);

    // A joint the pose does not have, and a holder that is gone, leave the prop alone.
    applyAttachment(scene, {holder.id, prop.id, 5, glm::mat4(1.0f)});
    applyAttachment(scene, {StableId::of("nobody"), prop.id, 0, glm::mat4(1.0f)});
    REQUIRE(scene.registry().get<Transform>(scene.find(prop.id)).position == placed.position);
}
