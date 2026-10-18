#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <worldforge/forge_world.h>

namespace forgecli {

constexpr std::uint64_t kMaxGallerySeeds = 256;

struct GalleryOptions {
    std::uint64_t seedBegin = 0;
    std::uint64_t seedEnd = 0; // inclusive
    int size = 128;            // forge grid size and cell width in pixels
    std::optional<worldforge::ForgeConfig> config; // default: defaultForgeConfig(size)
};

struct GalleryResult {
    std::vector<std::uint8_t> png; // empty when error is set
    int width = 0;
    int height = 0;
    std::vector<std::uint64_t> failedSeeds; // drawn as red cells
    std::string error;                      // invalid options
};

// One grid PNG, a cell per seed in seed order: hillshade of the carved terrain tinted by biome, rivers
// in blue, labelled with the seed. Integer maths and stored deflate, so the bytes depend only on the options.
GalleryResult makeGallery(const GalleryOptions& options);

// `gallery --seeds A..B [--size N] [--out FILE]`; argv[1] is the subcommand name. Returns the exit code.
int galleryMain(int argc, char** argv);

}
