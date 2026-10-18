#include "ichor/anim/animation.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/exile.h"
#include "ichor/assets/fbx.h"
#include "ichor/sim/attachment.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace ichor;

namespace {

const std::filesystem::path kExile = std::filesystem::path(ICHOR_ART_DIR) / "exile";

struct Cooked {
    SceneBundle bundle;
    ExileKit kit;
};

const Cooked& cookedExile()
{
    static const Cooked cooked = [] {
        Cooked c;
        REQUIRE(cookExile(kExile, FbxImportOptions{}, c.bundle, c.kit));
        return c;
    }();
    return cooked;
}

std::uint32_t jointIndex(const Skeleton& skeleton, std::string_view name)
{
    const auto it = std::find(skeleton.jointNames.begin(), skeleton.jointNames.end(), name);
    REQUIRE(it != skeleton.jointNames.end());
    return static_cast<std::uint32_t>(it - skeleton.jointNames.begin());
}

glm::vec3 bindPosition(const Cooked& c, std::string_view name)
{
    return glm::vec3(c.kit.rig.bindWorld[jointIndex(c.bundle.skeletons[0], name)][3]);
}

// Which way the cooked body faces on the ground: from the left ankle to its toes.
glm::vec2 bodyForward(const Cooked& c)
{
    const glm::vec3 d = bindPosition(c, "mixamorig:LeftToeBase") - bindPosition(c, "mixamorig:LeftFoot");
    return glm::normalize(glm::vec2(d.x, d.z));
}

std::vector<std::uint8_t> readAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Mean ground speed of the left foot while it is planted, over one cycle. A foot that
// stays put while the body travels is the whole point of getting the root travel right.
float plantedFootSpeed(const FbxImportOptions& options, const char* clipName)
{
    SceneBundle bundle;
    ExileKit kit;
    REQUIRE(importFbxCharacter(kExile / "SK_Exile.fbx", options, bundle, kit.rig));
    REQUIRE(importFbxClip(kExile / (std::string("SK_Exile_") + clipName + ".fbx"), clipName, options, kit.rig, bundle));
    AnimationSet set;
    REQUIRE(set.load(bundle));
    const std::uint32_t foot = jointIndex(bundle.skeletons[0], "mixamorig:LeftFoot");
    const float duration = set.clipDuration(0);
    const int steps = static_cast<int>(duration * 60.0f);
    std::vector<glm::vec3> position;
    std::vector<glm::mat4> pose;
    for (int i = 0; i <= steps; ++i) {
        REQUIRE(set.samplePose(0, duration * static_cast<float>(i) / static_cast<float>(steps), pose));
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
    REQUIRE(planted > 5);
    return sum / static_cast<float>(planted);
}

}

TEST_CASE("the Exile skeleton cooks to a root joint and 34 bones with their file names")
{
    const Cooked& c = cookedExile();
    REQUIRE(c.bundle.skeletons.size() == 1);
    const Skeleton& skeleton = c.bundle.skeletons[0];
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

    AnimationSet set;
    REQUIRE(set.load(c.bundle));
    REQUIRE(set.jointCount(0) == 35);
    REQUIRE(set.clipCount() == 9);
    for (const char* clip : kExileClipNames) {
        REQUIRE(set.findClip(clip) != AnimationSet::kNoClip);
    }
}

TEST_CASE("the Exile mesh is metric, upright, on the ground, facing -Z and fully skinned")
{
    const Cooked& c = cookedExile();
    const Mesh& mesh = c.bundle.meshes[c.kit.rig.characterMesh];
    REQUIRE(mesh.skeleton == c.kit.rig.skeleton);
    // Two metres tall: a missing 0.01 would make it 200 or 0.02, a wrong up axis would lie it down.
    REQUIRE(mesh.boundsMin.y == Catch::Approx(0.0f).margin(0.001f));
    REQUIRE(mesh.boundsMax.y == Catch::Approx(2.0f).margin(0.05f));
    REQUIRE(mesh.boundsMax.x - mesh.boundsMin.x < 1.5f);
    REQUIRE(mesh.boundsMax.z - mesh.boundsMin.z < 1.0f);
    // Toes lie ahead of the ankles, and Ichor's forward is -Z.
    const glm::vec2 forward = bodyForward(c);
    REQUIRE(forward.y < -0.5f);
    REQUIRE(c.kit.rig.unweightedVertices == 0);
    for (const Vertex& v : mesh.vertices) {
        std::uint32_t sum = 0;
        for (std::uint16_t w : v.weights) sum += w;
        REQUIRE(sum == 65535);
        for (int k = 0; k < 4; ++k) REQUIRE(v.joints[k] < 35);
        REQUIRE(std::abs(glm::length(v.normal) - 1.0f) < 1e-3f);
    }
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) REQUIRE(mesh.indices[i] < mesh.vertices.size());
}

TEST_CASE("skinning at the first frame of Idle leaves the Exile standing where the mesh was baked")
{
    const Cooked& c = cookedExile();
    AnimationSet set;
    REQUIRE(set.load(c.bundle));
    std::vector<glm::mat4> skin;
    REQUIRE(set.skinningMatrices(set.findClip("Idle"), 0.0f, skin));
    const Mesh& mesh = c.bundle.meshes[c.kit.rig.characterMesh];
    float lowest = 1e9f;
    float highest = -1e9f;
    for (const Vertex& v : mesh.vertices) {
        glm::vec4 p(0.0f);
        for (int k = 0; k < 4; ++k) p += (static_cast<float>(v.weights[k]) / 65535.0f) * (skin[v.joints[k]] * glm::vec4(v.position, 1.0f));
        lowest = std::min(lowest, p.y);
        highest = std::max(highest, p.y);
    }
    // The clip's first frame is not the bind pose, so allow a hand's width.
    REQUIRE(lowest == Catch::Approx(0.0f).margin(0.15f));
    REQUIRE(highest == Catch::Approx(2.0f).margin(0.15f));
}

TEST_CASE("Walk travels about 4.2 m forward and Run about 5 m/s, along the way the body faces")
{
    const Cooked& c = cookedExile();
    AnimationSet set;
    REQUIRE(set.load(c.bundle));
    const glm::vec2 forward = bodyForward(c);

    const auto travel = [&](const char* name, float& duration) {
        const std::uint32_t clip = set.findClip(name);
        REQUIRE(clip != AnimationSet::kNoClip);
        duration = set.clipDuration(clip);
        glm::vec3 start;
        glm::vec3 end;
        REQUIRE(set.rootTranslation(clip, 0.0f, start));
        REQUIRE(set.rootTranslation(clip, duration, end));
        REQUIRE(start == glm::vec3(0.0f));
        return end - start;
    };
    float walkSeconds = 0.0f;
    const glm::vec3 walk = travel("Walk", walkSeconds);
    REQUIRE(walkSeconds == Catch::Approx(4.0f).margin(0.1f));
    REQUIRE(walk.y == 0.0f);
    REQUIRE(glm::length(glm::vec2(walk.x, walk.z)) == Catch::Approx(4.2f).margin(0.15f));
    REQUIRE(glm::dot(glm::vec2(walk.x, walk.z), forward) > 3.8f);

    float runSeconds = 0.0f;
    const glm::vec3 run = travel("Run", runSeconds);
    const float runSpeed = glm::dot(glm::vec2(run.x, run.z), forward) / runSeconds;
    REQUIRE(runSpeed == Catch::Approx(5.0f).margin(0.5f));

    // The root joint is the root: nothing above it, no turning, so the engine's root motion is exact.
    std::vector<glm::mat4> pose;
    REQUIRE(set.samplePose(set.findClip("Walk"), 2.0f, pose));
    REQUIRE(glm::length(glm::vec3(pose[0][0]) - glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-3f);
    REQUIRE(glm::length(glm::vec3(pose[0][1]) - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-3f);
}

TEST_CASE("without the root travel fix the feet slide, and without the yaw the body faces +Z")
{
    FbxImportOptions fixed;
    FbxImportOptions slid;
    slid.reverseRootTravel = false;
    const float with = plantedFootSpeed(fixed, "Walk");
    const float without = plantedFootSpeed(slid, "Walk");
    INFO("planted foot speed " << with << " m/s with the fix, " << without << " m/s without");
    REQUIRE(with < 1.0f);
    REQUIRE(without > 2.0f * with);

    FbxImportOptions unturned;
    unturned.yawDegrees = 0.0f;
    SceneBundle bundle;
    FbxRig rig;
    REQUIRE(importFbxCharacter(kExile / "SK_Exile.fbx", unturned, bundle, rig));
    const auto at = [&](std::string_view name) { return glm::vec3(rig.bindWorld[jointIndex(bundle.skeletons[0], name)][3]); };
    REQUIRE((at("mixamorig:LeftToeBase") - at("mixamorig:LeftFoot")).z > 0.0f);
}

TEST_CASE("the Exile clips are in meshes' scale: no clip moves the hips far from the ground")
{
    const Cooked& c = cookedExile();
    AnimationSet set;
    REQUIRE(set.load(c.bundle));
    const std::uint32_t hips = 1;
    std::vector<glm::mat4> pose;
    for (const char* name : kExileClipNames) {
        const std::uint32_t clip = set.findClip(name);
        REQUIRE(set.clipDuration(clip) > 1.0f);
        for (int i = 0; i <= 20; ++i) {
            REQUIRE(set.samplePose(clip, set.clipDuration(clip) * static_cast<float>(i) / 20.0f, pose));
            INFO(name);
            REQUIRE(pose[hips][3].y > 0.2f);
            REQUIRE(pose[hips][3].y < 1.6f);
        }
    }
}

TEST_CASE("the spear is held in the right hand and the shield in the left")
{
    const Cooked& c = cookedExile();
    REQUIRE(c.kit.sockets.size() == 2);
    REQUIRE(c.kit.sockets[0].name == "spear");
    REQUIRE(c.kit.sockets[1].name == "shield");
    REQUIRE(c.bundle.skeletons[0].jointNames[c.kit.sockets[0].joint] == "mixamorig:RightHand");
    REQUIRE(c.bundle.skeletons[0].jointNames[c.kit.sockets[1].joint] == "mixamorig:LeftHand");
    for (const JointSocket& socket : c.kit.sockets) {
        const Mesh& prop = c.bundle.meshes[socket.mesh];
        REQUIRE(prop.skeleton == kNoSkeleton);
        REQUIRE(!prop.vertices.empty());
    }
    // The spear is the long one: about 2.5 m, upright.
    const Mesh& spear = c.bundle.meshes[c.kit.sockets[0].mesh];
    REQUIRE(spear.boundsMax.y - spear.boundsMin.y == Catch::Approx(2.54f).margin(0.1f));
}

TEST_CASE("at the start of Idle each hand is on the prop it carries, and the spear stands upright")
{
    const Cooked& c = cookedExile();
    AnimationSet set;
    REQUIRE(set.load(c.bundle));
    const std::uint32_t idle = set.findClip("Idle");
    std::vector<glm::mat4> skin;
    std::vector<glm::mat4> pose;
    REQUIRE(set.skinningMatrices(idle, 0.0f, skin));
    REQUIRE(set.samplePose(idle, 0.0f, pose));
    for (const JointSocket& socket : c.kit.sockets) {
        const glm::mat4 carried = skin[socket.joint] * socket.local;
        const glm::vec3 hand(pose[socket.joint][3]);
        // Distance from the hand to the box around the carried prop: a thin shaft has no vertex mid-way.
        glm::vec3 low(1e9f);
        glm::vec3 high(-1e9f);
        for (const Vertex& v : c.bundle.meshes[socket.mesh].vertices) {
            const glm::vec3 p(carried * glm::vec4(v.position, 1.0f));
            low = glm::min(low, p);
            high = glm::max(high, p);
        }
        const float nearest = glm::length(glm::max(glm::max(low - hand, hand - high), glm::vec3(0.0f)));
        INFO(socket.name);
        REQUIRE(nearest < 0.12f);
    }
    const JointSocket& spear = c.kit.sockets[0];
    const glm::mat4 carried = skin[spear.joint] * spear.local;
    REQUIRE(glm::normalize(glm::vec3(carried[1])).y > 0.99f);
}

TEST_CASE("the cooked Exile survives the bundle file")
{
    const Cooked& c = cookedExile();
    const auto path = test::tempPath("ichor_exile.iscene");
    REQUIRE(writeBundle(path, c.bundle));
    SceneBundle loaded;
    REQUIRE(readBundle(path, loaded));
    std::filesystem::remove(path);
    REQUIRE(loaded.skeletons[0].jointNames == c.bundle.skeletons[0].jointNames);
    REQUIRE(loaded.clips.size() == 9);
    REQUIRE(loaded.clips[1].ozzBytes == c.bundle.clips[1].ozzBytes);
    REQUIRE(loaded.meshes.size() == 3);
}

TEST_CASE("hostile: a damaged Exile file is refused or read, never a crash", "[hostile][fbx]")
{
    const std::vector<std::uint8_t> valid = readAll(kExile / "SK_Exile_Block.fbx");
    REQUIRE(valid.size() > 1000);
    fbx::Document doc;
    std::string error;
    REQUIRE(fbx::parse(valid, doc, error));

    // Every prefix that cuts the file inside its first kilobytes, and a spread of longer ones. A cut
    // between two top-level nodes is a shorter but well-formed file, so only crashes are failures.
    for (std::size_t length = 0; length < valid.size(); length += (length < 4096 ? 1 : 997)) {
        fbx::Document partial;
        std::string why;
        std::vector<std::uint8_t> cut(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(length));
        (void)fbx::parse(cut, partial, why);
    }

    // Flipped bytes: a deterministic scatter, whatever the parser makes of them.
    std::uint32_t state = 12345;
    for (int round = 0; round < 400; ++round) {
        std::vector<std::uint8_t> damaged = valid;
        for (int k = 0; k < 8; ++k) {
            state = state * 1664525u + 1013904223u;
            const std::size_t at = (state >> 8) % (round < 200 ? 4096 : damaged.size());
            state = state * 1664525u + 1013904223u;
            damaged[at] = static_cast<std::uint8_t>(state >> 16);
        }
        fbx::Document out;
        std::string why;
        (void)fbx::parse(damaged, out, why);
    }

    std::vector<std::uint8_t> notFbx(64, 0x41);
    REQUIRE_FALSE(fbx::parse(notFbx, doc, error));
    REQUIRE_FALSE(fbx::parse({}, doc, error));
}

namespace {

// A binary FBX written by hand, so the importer can be fed object graphs no real exporter writes.
struct Doc {
    struct Prop {
        char type = 'L';
        std::int64_t integer = 0;
        std::string text;
        std::vector<double> reals;
        std::vector<std::int32_t> ints;
    };
    struct Node {
        std::string name;
        std::vector<Prop> props;
        std::vector<Node> children;
    };

    static Prop id(std::int64_t v)
    {
        Prop p;
        p.type = 'L';
        p.integer = v;
        return p;
    }
    static Prop str(std::string v)
    {
        Prop p;
        p.type = 'S';
        p.text = std::move(v);
        return p;
    }
    static Prop doubles(std::vector<double> v)
    {
        Prop p;
        p.type = 'd';
        p.reals = std::move(v);
        return p;
    }
    static Prop ints(std::vector<std::int32_t> v)
    {
        Prop p;
        p.type = 'i';
        p.ints = std::move(v);
        return p;
    }

    static void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    static void put64(std::vector<std::uint8_t>& out, std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    static void write(std::vector<std::uint8_t>& out, const Node& n)
    {
        const std::size_t header = out.size();
        put32(out, 0);
        put32(out, static_cast<std::uint32_t>(n.props.size()));
        put32(out, 0);
        out.push_back(static_cast<std::uint8_t>(n.name.size()));
        out.insert(out.end(), n.name.begin(), n.name.end());
        const std::size_t propsStart = out.size();
        for (const Prop& p : n.props) {
            out.push_back(static_cast<std::uint8_t>(p.type));
            if (p.type == 'L') {
                put64(out, static_cast<std::uint64_t>(p.integer));
            } else if (p.type == 'S') {
                put32(out, static_cast<std::uint32_t>(p.text.size()));
                out.insert(out.end(), p.text.begin(), p.text.end());
            } else if (p.type == 'd') {
                put32(out, static_cast<std::uint32_t>(p.reals.size()));
                put32(out, 0);
                put32(out, static_cast<std::uint32_t>(p.reals.size() * 8));
                for (double d : p.reals) {
                    std::uint64_t bits;
                    std::memcpy(&bits, &d, 8);
                    put64(out, bits);
                }
            } else {
                put32(out, static_cast<std::uint32_t>(p.ints.size()));
                put32(out, 0);
                put32(out, static_cast<std::uint32_t>(p.ints.size() * 4));
                for (std::int32_t v : p.ints) put32(out, static_cast<std::uint32_t>(v));
            }
        }
        const auto propBytes = static_cast<std::uint32_t>(out.size() - propsStart);
        for (const Node& c : n.children) write(out, c);
        if (!n.children.empty()) out.insert(out.end(), 13, 0);
        const auto end = static_cast<std::uint32_t>(out.size());
        std::memcpy(out.data() + header, &end, 4);
        std::memcpy(out.data() + header + 8, &propBytes, 4);
    }

    static std::vector<std::uint8_t> file(const std::vector<Node>& roots)
    {
        std::vector<std::uint8_t> out(27, 0);
        std::memcpy(out.data(), "Kaydara FBX Binary  ", 20);
        out[21] = 0x1a;
        const std::uint32_t version = 7400;
        std::memcpy(out.data() + 23, &version, 4);
        for (const Node& r : roots) write(out, r);
        out.insert(out.end(), 13, 0);
        return out;
    }
};

Doc::Node node(std::string name, std::vector<Doc::Prop> props = {}, std::vector<Doc::Node> children = {})
{
    return {std::move(name), std::move(props), std::move(children)};
}

Doc::Node model(std::int64_t id, const std::string& name, const char* kind)
{
    return node("Model", {Doc::id(id), Doc::str(name + std::string("\0\1Model", 7)), Doc::str(kind)});
}

Doc::Node link(std::int64_t from, std::int64_t to)
{
    return node("C", {Doc::str("OO"), Doc::id(from), Doc::id(to)});
}

// One triangle with the given normals, as a mesh model; optionally with a material that has no name property.
std::vector<Doc::Node> triangleFile(Doc::Prop normals, Doc::Prop normalIndex, bool material)
{
    std::vector<Doc::Node> objects;
    objects.push_back(model(1, "tri", "Mesh"));
    objects.push_back(node("Geometry", {Doc::id(2)},
        {node("Vertices", {Doc::doubles({0, 0, 0, 1, 0, 0, 0, 1, 0})}), node("PolygonVertexIndex", {Doc::ints({0, 1, -3})}),
            node("LayerElementNormal", {Doc::id(0)},
                {node("MappingInformationType", {Doc::str("ByPolygonVertex")}), node("ReferenceInformationType", {Doc::str("IndexToDirect")}),
                    node("Normals", {std::move(normals)}), node("NormalsIndex", {std::move(normalIndex)})})}));
    std::vector<Doc::Node> connections{link(2, 1), link(1, 0)};
    if (material) {
        objects.push_back(node("Material", {Doc::id(3)}));
        connections.push_back(link(3, 1));
    }
    return {node("Objects", {}, std::move(objects)), node("Connections", {}, std::move(connections))};
}

bool importProp(const std::vector<std::uint8_t>& bytes)
{
    const Cooked& c = cookedExile();
    const auto path = test::tempPath("ichor_hostile_prop.fbx");
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    SceneBundle bundle = c.bundle;
    std::uint32_t mesh = 0;
    const bool ok = importFbxProp(path, c.kit.rig, bundle, mesh);
    std::filesystem::remove(path);
    return ok;
}

}

TEST_CASE("hostile: hand-built documents the importer must refuse or survive", "[hostile][fbx]")
{
    const std::vector<double> goodNormals{0, 0, 1};
    const std::vector<std::int32_t> goodIndex{0, 0, 0};

    // The baseline is a valid triangle, so each refusal below is down to its one defect.
    REQUIRE(importProp(Doc::file(triangleFile(Doc::doubles(goodNormals), Doc::ints(goodIndex), false))));
    // A material with no name property used to be read past its end.
    REQUIRE(importProp(Doc::file(triangleFile(Doc::doubles(goodNormals), Doc::ints(goodIndex), true))));
    // An index that wraps when multiplied, and one just past the end.
    REQUIRE_FALSE(importProp(Doc::file(triangleFile(Doc::doubles(goodNormals), Doc::ints({2147483647, 0, 0}), false))));
    REQUIRE_FALSE(importProp(Doc::file(triangleFile(Doc::doubles(goodNormals), Doc::ints({1, 0, 0}), false))));
    // Normals that would overflow a float, and one that is not a number.
    REQUIRE_FALSE(importProp(Doc::file(triangleFile(Doc::doubles({1e300, 0, 0}), Doc::ints(goodIndex), false))));
    REQUIRE_FALSE(importProp(Doc::file(triangleFile(Doc::doubles({std::nan(""), 0, 0}), Doc::ints(goodIndex), false))));

    // Models that are each other's ancestors.
    {
        auto doc = triangleFile(Doc::doubles(goodNormals), Doc::ints(goodIndex), false);
        doc[0].children.push_back(model(10, "a", "LimbNode"));
        doc[0].children.push_back(model(11, "b", "LimbNode"));
        doc[1].children.push_back(link(10, 11));
        doc[1].children.push_back(link(11, 10));
        REQUIRE_FALSE(importProp(Doc::file(doc)));
    }
    // More models than any character has.
    {
        auto doc = triangleFile(Doc::doubles(goodNormals), Doc::ints(goodIndex), false);
        for (int i = 0; i < 5000; ++i) {
            doc[0].children.push_back(model(100 + i, "bone" + std::to_string(i), "LimbNode"));
            if (i > 0) doc[1].children.push_back(link(100 + i, 99 + i));
        }
        REQUIRE_FALSE(importProp(Doc::file(doc)));
    }
}

TEST_CASE("hostile: arrays that claim more than the file can hold are refused", "[hostile][fbx]")
{
    // A compressed array of 30 million doubles in four bytes: deflate cannot do that.
    std::vector<std::uint8_t> bytes(27, 0);
    std::memcpy(bytes.data(), "Kaydara FBX Binary  ", 20);
    bytes[21] = 0x1a;
    const std::uint32_t version = 7400;
    std::memcpy(bytes.data() + 23, &version, 4);
    const std::uint32_t propBytes = 1 + 12 + 4;
    Doc::put32(bytes, static_cast<std::uint32_t>(bytes.size()) + 13 + 1 + propBytes);
    Doc::put32(bytes, 1);
    Doc::put32(bytes, propBytes);
    bytes.push_back(1);
    bytes.push_back('A');
    bytes.push_back('d');
    const std::size_t lengthAt = bytes.size();
    Doc::put32(bytes, 30'000'000u);
    Doc::put32(bytes, 1);
    Doc::put32(bytes, 4);
    Doc::put32(bytes, 0);
    fbx::Document doc;
    std::string error;
    REQUIRE_FALSE(fbx::parse(bytes, doc, error));
    REQUIRE(error.find("impossible") != std::string::npos);

    // More elements than the cap allows at all.
    const std::uint32_t huge = 0x7fffffffu;
    std::memcpy(bytes.data() + lengthAt, &huge, 4);
    REQUIRE_FALSE(fbx::parse(bytes, doc, error));
    REQUIRE(error.find("too large") != std::string::npos);
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

TEST_CASE("hostile: damaged clip files go through the importer without a crash", "[hostile][fbx]")
{
    const Cooked& c = cookedExile();
    const std::vector<std::uint8_t> valid = readAll(kExile / "SK_Exile_Hit.fbx");
    const auto path = test::tempPath("ichor_damaged_clip.fbx");
    {
        SceneBundle bundle = c.bundle;
        bundle.clips.clear();
        REQUIRE(importFbxClip(kExile / "SK_Exile_Hit.fbx", "Hit", FbxImportOptions{}, c.kit.rig, bundle));
    }
    std::uint32_t state = 777;
    int imported = 0;
    for (int round = 0; round < 60; ++round) {
        std::vector<std::uint8_t> damaged = valid;
        for (int k = 0; k < 6; ++k) {
            state = state * 1664525u + 1013904223u;
            const std::size_t at = (state >> 8) % damaged.size();
            state = state * 1664525u + 1013904223u;
            damaged[at] = static_cast<std::uint8_t>(state >> 16);
        }
        {
            std::ofstream out(path, std::ios::binary);
            out.write(reinterpret_cast<const char*>(damaged.data()), static_cast<std::streamsize>(damaged.size()));
        }
        SceneBundle bundle = c.bundle;
        bundle.clips.clear();
        if (importFbxClip(path, "Hit", FbxImportOptions{}, c.kit.rig, bundle)) {
            ++imported;
            REQUIRE(std::isfinite(bundle.clips.back().duration));
            REQUIRE(bundle.clips.back().duration > 0.0f);
        }
    }
    std::filesystem::remove(path);
    INFO(imported << " of 60 damaged files still imported");
    // The arrays are zlib streams with a checksum, so nearly every flip is refused at inflate.
    REQUIRE(imported < 60);
}

TEST_CASE("Idle loops: its last pose meets its first, which Walk and Run, as authored, do not")
{
    const Cooked& c = cookedExile();
    AnimationSet set;
    REQUIRE(set.load(c.bundle));
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
    REQUIRE(seam("Idle") < 0.05f);
    // Placeholder clips are not loop-blended: Walk and Run end in another stride. This pins that
    // down, so a re-exported pair that does loop turns it red and the note in docs/fbx-import.md goes.
    REQUIRE(seam("Walk") > 0.2f);
    REQUIRE(seam("Run") > 0.5f);
}
