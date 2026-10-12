#include "ichor/assets/audio_cook.h"
#include "ichor/assets/bundle_io.h"
#include "ichor/assets/cooker.h"
#include "ichor/core/log.h"

#include <filesystem>

int main(int argc, char** argv)
{
    if (argc < 3) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor_cook <scene.glb> <out.iscene> [sound.wav ...]");
        return 2;
    }

    ichor::SceneBundle bundle;
    ichor::CookStats stats;
    if (!ichor::cookGltf(argv[1], bundle, stats)) {
        return 1;
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
