#include "ichor/anim/animation.h"
#include "ichor/assets/bundle.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/piece_data.h"
#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Cooks one GLB, writes and reads the bundle back, loads it into the animation set and plays it for a few frames.
// Prints one tab-separated RESULT line per file (tools/import_smoke.py collects them into a table).

namespace {

namespace fs = std::filesystem;

constexpr const char* kStandardClips[] = {"idle", "walk", "run", "hit", "death", "block", "dodge", "swing", "thrust", "attack", "stagger", "throw"};
constexpr float kHumanoidMinHeight = 1.7f;
constexpr float kHumanoidMaxHeight = 1.9f;
constexpr float kForwardMinZ = 0.7f;      // body facing, the z of the unit forward vector, must be below -this
constexpr float kInPlaceMetres = 0.05f;   // a walk that travels less than this is authored in place
constexpr float kTravelMinMetres = 0.1f;
constexpr int kFrames = 8;
constexpr float kStep = 1.0f / 60.0f;

struct Result {
    std::string path;
    std::string kind = "static";
    bool cooked = false;
    float yawUsed = 0.0f;
    float yawNeeded = 0.0f;
    bool hasYaw = false;
    float height = -1.0f;
    bool hasWalk = false;
    glm::vec3 walkTravel{0.0f};
    std::size_t clips = 0;
    std::vector<std::string> problems;
};

bool finite(const glm::mat4& m)
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) return false;
        }
    }
    return true;
}

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// What the game would pass as CookOptions::rootYawDegrees. The Exile v6 rig was exported facing -Z and needs no turn,
// every older Exile export (the v6 rig folder and file names carry "v6" or "v7") faces +Z and needs 180, and any other rig is cooked as it is.
float conventionYaw(const fs::path& file)
{
    const std::string name = lower(file.generic_string());
    if (name.find("exile") == std::string::npos) return 0.0f;
    return (name.find("v6") != std::string::npos || name.find("v7") != std::string::npos) ? 0.0f : 180.0f;
}

std::size_t jointEndingWith(const std::vector<std::string>& names, std::string_view suffix)
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].size() >= suffix.size() && std::string_view(names[i]).substr(names[i].size() - suffix.size()) == suffix) return i;
    }
    return names.size();
}

struct Body {
    std::size_t hips = 0;
    std::size_t spine = 0;
    std::size_t left = 0;
    std::size_t right = 0;
    bool found = false;
};

Body findBody(const std::vector<std::string>& names)
{
    Body b;
    b.hips = jointEndingWith(names, "Hips");
    b.spine = jointEndingWith(names, "Spine");
    b.left = jointEndingWith(names, "LeftUpLeg");
    b.right = jointEndingWith(names, "RightUpLeg");
    b.found = std::max({b.hips, b.spine, b.left, b.right}) < names.size();
    return b;
}

// Which way the body faces in a pose: square to the line between the up-legs and to the spine's up.
glm::vec3 facing(const Body& b, const std::vector<glm::mat4>& pose)
{
    const glm::vec3 up = glm::vec3(pose[b.spine][3]) - glm::vec3(pose[b.hips][3]);
    const glm::vec3 side = glm::vec3(pose[b.left][3]) - glm::vec3(pose[b.right][3]);
    return glm::normalize(glm::cross(side, up));
}

void checkMeshes(const ichor::SceneBundle& bundle, const ichor::PieceData& pieces, Result& r)
{
    if (bundle.meshes.empty()) {
        // A UBX_ or UCX_ file holds collision shapes only; the cooker keeps them beside the bundle, not in it.
        if (pieces.collision.empty()) {
            r.problems.push_back("no meshes and no collision shapes");
        } else {
            r.kind = "collision";
        }
    }
    float lo = 1e30f;
    float hi = -1e30f;
    for (const ichor::Mesh& mesh : bundle.meshes) {
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            r.problems.push_back("mesh " + mesh.name + " is empty");
            continue;
        }
        if (!finite(mesh.boundsMin) || !finite(mesh.boundsMax)) r.problems.push_back("mesh " + mesh.name + " has non-finite bounds");
        if (mesh.skeleton != ichor::kNoSkeleton) {
            lo = std::min(lo, mesh.boundsMin.y);
            hi = std::max(hi, mesh.boundsMax.y);
        }
    }
    if (hi > lo) r.height = hi - lo;
}

void checkAnimation(const ichor::SceneBundle& bundle, const ichor::CookOptions& options, Result& r)
{
    ichor::AnimationSet set;
    if (!set.load(bundle)) {
        r.problems.push_back("animation set did not load");
        return;
    }
    r.clips = set.clipCount();
    if (r.clips == 0) {
        r.problems.push_back("skinned but no clips");
        return;
    }
    for (const ichor::AnimationClip& clip : bundle.clips) {
        const std::string name = lower(clip.name);
        if (std::find(std::begin(kStandardClips), std::end(kStandardClips), name) == std::end(kStandardClips)) {
            r.problems.push_back("clip '" + clip.name + "' is not in the standard set");
        }
    }

    for (std::uint32_t c = 0; c < set.clipCount(); ++c) {
        std::vector<glm::mat4> skin;
        for (int f = 0; f < kFrames; ++f) {
            if (!set.skinningMatrices(c, f * kStep, skin)) {
                r.problems.push_back("clip " + bundle.clips[c].name + " would not sample");
                break;
            }
            if (!std::all_of(skin.begin(), skin.end(), [](const glm::mat4& m) { return finite(m); })) {
                r.problems.push_back("NaN in skin matrices of " + bundle.clips[c].name);
                break;
            }
        }
    }

    const std::vector<std::string>& names = bundle.skeletons[0].jointNames;
    const Body body = findBody(names);
    const std::uint32_t idle = set.findClip("idle");
    if (body.found && idle != ichor::AnimationSet::kNoClip) {
        std::vector<glm::mat4> pose;
        if (set.samplePose(idle, 0.0f, pose)) {
            const glm::vec3 forward = facing(body, pose);
            r.hasYaw = true;
            r.yawUsed = options.rootYawDegrees;
            r.yawNeeded = forward.z < -kForwardMinZ ? r.yawUsed : std::fmod(r.yawUsed + 180.0f, 360.0f);
            if (!(forward.z < -kForwardMinZ)) {
                r.problems.push_back("faces +Z after rootYawDegrees " + std::to_string(static_cast<int>(r.yawUsed)) + "; needs "
                    + std::to_string(static_cast<int>(r.yawNeeded)));
            }
        }
    }

    const std::uint32_t walk = set.findClip("walk");
    if (walk != ichor::AnimationSet::kNoClip) {
        glm::vec3 from;
        glm::vec3 to;
        if (set.rootTranslation(walk, 0.0f, from) && set.rootTranslation(walk, set.clipDuration(walk), to)) {
            r.hasWalk = true;
            r.walkTravel = to - from;
            if (!finite(r.walkTravel)) {
                r.problems.push_back("walk root translation is not finite");
            } else if (glm::length(r.walkTravel) >= kInPlaceMetres && !(r.walkTravel.z < -kTravelMinMetres && std::abs(r.walkTravel.x) < -r.walkTravel.z)) {
                r.problems.push_back("walk root travels backwards or sideways (dz " + std::to_string(r.walkTravel.z) + ")");
            }
        }
    }
}

Result check(const fs::path& file, std::uint32_t textureCap, const fs::path& scratch, std::optional<float> forcedYaw)
{
    Result r;
    r.path = file.generic_string();
    ichor::CookOptions options;
    ichor::TextureBudget budget;
    budget.defaultMax = textureCap;
    options.textureBudget = budget;
    options.rootYawDegrees = forcedYaw.value_or(conventionYaw(file));

    ichor::SceneBundle cooked;
    ichor::PieceData pieces;
    ichor::CookStats stats;
    if (!ichor::cookGltf(file, cooked, pieces, stats, options)) {
        r.problems.push_back("cooker rejected the file");
        return r;
    }
    const fs::path bundlePath = scratch / "smoke.iscene";
    ichor::SceneBundle bundle;
    ichor::PieceData readPieces;
    if (!ichor::writeBundle(bundlePath, cooked, pieces) || !ichor::readBundle(bundlePath, bundle, readPieces)) {
        r.problems.push_back("bundle did not round-trip");
        return r;
    }
    std::error_code ignored;
    fs::remove(bundlePath, ignored);
    r.cooked = true;
    r.clips = bundle.clips.size();
    if (!bundle.skeletons.empty()) r.kind = findBody(bundle.skeletons[0].jointNames).found ? "humanoid" : "skinned";

    checkMeshes(bundle, readPieces, r);
    if (!bundle.skeletons.empty()) checkAnimation(bundle, options, r);

    if (r.kind == "humanoid" && r.height >= 0.0f && (r.height < kHumanoidMinHeight || r.height > kHumanoidMaxHeight)) {
        r.problems.push_back("humanoid height " + std::to_string(r.height) + " m outside 1.7 to 1.9");
    }
    return r;
}

std::string clean(std::string s)
{
    std::replace(s.begin(), s.end(), '\t', ' ');
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

void print(const Result& r)
{
    std::string problems;
    for (const std::string& p : r.problems) problems += (problems.empty() ? "" : "; ") + p;
    char yaw[48] = "-";
    if (r.hasYaw) std::snprintf(yaw, sizeof yaw, "%d/%d", static_cast<int>(r.yawUsed), static_cast<int>(r.yawNeeded));
    char height[24] = "-";
    if (r.height >= 0.0f) std::snprintf(height, sizeof height, "%.2f", r.height);
    char walk[24] = "-";
    if (r.hasWalk) std::snprintf(walk, sizeof walk, "%.2f", r.walkTravel.z);
    std::printf("RESULT\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%zu\t%s\n", clean(r.path).c_str(), r.problems.empty() ? "PASS" : "FAIL", r.kind.c_str(), yaw, height, walk,
        r.cooked ? "yes" : "no", r.clips, clean(problems).c_str());
    std::fflush(stdout);
}

}

int main(int argc, char** argv)
{
    std::uint32_t textureCap = 128;
    std::optional<float> forcedYaw;
    std::vector<fs::path> files;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--texture-cap" && i + 1 < argc) {
            textureCap = static_cast<std::uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--yaw" && i + 1 < argc) {
            forcedYaw = static_cast<float>(std::atof(argv[++i]));
        } else {
            files.emplace_back(arg);
        }
    }
    if (files.empty()) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_import_smoke [--texture-cap N] [--yaw DEGREES] <scene.glb> ...");
        return 2;
    }
    const fs::path scratch = fs::temp_directory_path() / ("ichor_import_smoke_" + std::to_string(std::random_device{}()));
    std::error_code ignored;
    fs::create_directories(scratch, ignored);
    int failed = 0;
    for (const fs::path& file : files) {
        const Result r = check(file, textureCap, scratch, forcedYaw);
        print(r);
        if (!r.problems.empty()) ++failed;
    }
    fs::remove_all(scratch, ignored);
    return failed == 0 ? 0 : 1;
}
