#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <worldforge/forge_world.h>
#include <worldforge/world_validation.h>

namespace forgecli {

constexpr std::uint64_t kMaxGallerySeeds = 256;

struct GalleryOptions {
    std::uint64_t seedBegin = 0;
    std::uint64_t seedEnd = 0; // inclusive
    int size = 128;            // forge grid size and cell width in pixels
    std::optional<worldforge::ForgeConfig> config; // default: defaultForgeConfig(size)
    bool sanity = true;                            // forge every phase and caption each cell with its sanity score
    std::optional<worldforge::GateThresholds> thresholds; // default: sanityThresholds()
};

// One world's sanity score: the evaluated gates (skipped ones do not count) that passed, and which failed.
struct SanityRow {
    std::uint64_t seed = 0;
    int passed = 0;
    int evaluated = 0;
    std::vector<std::string> gateIds;     // every gate in report order, numbered from 1 in the caption
    std::vector<std::string> failedGates; // gate ids, in report order
    std::vector<int> failedIndexes;       // 1-based position in the report, the numbers drawn in the caption
    std::string error;                    // invalid thresholds
    std::int64_t landSharePermille = -1;  // permille of the world above sea level; -1 when not measured
};

struct GalleryResult {
    std::vector<std::uint8_t> png; // empty when error is set
    int width = 0;
    int height = 0;
    std::vector<std::uint64_t> failedSeeds; // drawn as red cells
    std::vector<SanityRow> sanity;          // one row per world that forged, in seed order; empty when options.sanity is off
    std::string error;                      // invalid options
};

// One grid PNG, a cell per seed in seed order: hillshade of the carved terrain tinted by biome, rivers
// in blue, labelled with the seed. Integer maths and stored deflate, so the bytes depend only on the options.
GalleryResult makeGallery(const GalleryOptions& options);

SanityRow sanityRow(const worldforge::ValidationReport& report);

// Each world is forged once with its own seed, so a score can read FAIL where --validate would accept a reroll.
// "5/7" then the position of each failed gate ("5/7 2 4"), or "X" when the thresholds are invalid. The caption font has only digits, '/', '-' and 'X'.
std::string sanityCaption(const SanityRow& row);

// One line per row ("seed 3 5/7 FAIL road_connected no_dead_ends land 37.9%"), then the gate numbering the captions use.
std::string sanitySummary(const std::vector<SanityRow>& rows);

// `gallery --seeds A..B [--size N] [--out FILE]`; argv[1] is the subcommand name. Returns the exit code.
int galleryMain(int argc, char** argv);

}
