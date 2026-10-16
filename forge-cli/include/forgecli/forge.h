#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace forgecli {

// One output image. channels is 1 (greyscale) or 3 (RGB); pixels is width * height * channels bytes.
struct Layer {
    std::string name;
    int width = 0;
    int height = 0;
    int channels = 1;
    std::vector<std::uint8_t> pixels;
};

struct PhaseResult {
    std::vector<Layer> layers;
    std::string error; // non-empty on failure
};

// A phase turns (seed, size) into layers. Adding a phase is one entry in phaseRegistry().
struct Phase {
    std::string_view name;
    std::string_view description;
    PhaseResult (*run)(std::uint64_t seed, int size);
};

const std::vector<Phase>& phaseRegistry();
const Phase* findPhase(std::string_view name);

// FNV-1a 64 over name, dimensions and pixels, as 16 lowercase hex digits.
std::string layerHash(const Layer& layer);

struct LayerRecord {
    std::string name;
    std::string phase;
    std::string file; // relative to the output directory; empty when nothing was written
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string hash;
};

struct SeedRecord {
    std::uint64_t seed = 0;
    std::string error; // non-empty when a phase failed for this seed
    std::vector<LayerRecord> layers;
};

struct RunOptions {
    std::uint64_t seedBegin = 0;
    std::uint64_t seedEnd = 0; // inclusive
    int size = 256;
    std::vector<std::string> phases; // empty means every registered phase
    std::string outDir;              // empty means compute and hash only, write nothing
};

constexpr std::uint64_t kMaxBatchSeeds = 100000;

struct RunResult {
    std::vector<SeedRecord> seeds;
    std::string manifest;
    std::string error; // set when the options are invalid or a file cannot be written
};

RunResult runForge(const RunOptions& options);

std::string manifestJson(const RunOptions& options, const std::vector<SeedRecord>& seeds);

}
