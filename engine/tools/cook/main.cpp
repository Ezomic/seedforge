#include "ichor/assets/audio_cook.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/assets/material_limits.h"
#include "ichor/core/log.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// Lists the sockets, LODs and collision shapes found in each file, and what is wrong with their names.
int validate(int argc, char** argv)
{
    int status = 0;
    for (int i = 2; i < argc; ++i) {
        ichor::ConventionScan scan;
        if (!ichor::inspectGltf(argv[i], scan)) {
            std::printf("%s: cannot read\n", argv[i]);
            status = 1;
            continue;
        }
        std::printf("%s: %zu sockets, %zu lod groups, %zu collision shapes, %zu issues\n", argv[i], scan.sockets.size(), scan.lods.size(),
            scan.collision.size(), scan.issues.size());
        for (const std::string& line : ichor::describeConventions(scan)) {
            std::printf("  %s\n", line.c_str());
        }
    }
    return status;
}

}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "--validate") {
        return validate(argc, argv);
    }
    if (argc < 3) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_cook <scene.glb> <out.iscene> [sound.wav ...]\n       ichor_cook --validate <scene.glb> ...");
        return 2;
    }

    ichor::SceneBundle bundle;
    ichor::CookStats stats;
    if (!ichor::cookGltf(argv[1], bundle, stats)) {
        return 1;
    }
    ichor::MaterialLimits limits;
    std::vector<std::string> problems;
    if (ichor::loadMaterialLimits(std::filesystem::path(ICHOR_DATA_DIR) / "materials" / "response.json", limits, problems)) {
        ichor::logMaterialIssues(std::filesystem::path(argv[1]).filename().string(), ichor::findMaterialIssues(bundle.materials, limits));
    }
    for (const std::string& problem : problems) {
        ichor::logf(ichor::LogLevel::Warn, "material limits: {}", problem);
    }
    for (int i = 3; i < argc; ++i) {
        ichor::SoundClip clip;
        if (!ichor::cookSound(argv[i], clip)) {
            return 1;
        }
        bundle.sounds.push_back(std::move(clip));
    }
    if (!ichor::writeBundle(argv[2], bundle)) {
        return 1;
    }
    ichor::logf(ichor::LogLevel::Info, "cooked {}: {} meshes, {} materials, {} textures, {} skeletons, {} clips, {} sounds, {} nodes, {} -> {} vertices, {} triangles{}",
        std::filesystem::path(argv[1]).filename().string(), bundle.meshes.size(), bundle.materials.size(),
        bundle.textures.size(), bundle.skeletons.size(), bundle.clips.size(), bundle.sounds.size(), bundle.nodes.size(), stats.verticesIn, stats.verticesOut, stats.triangles,
        stats.tangentsGenerated != 0 ? " (tangents generated)" : "");
    return 0;
}
