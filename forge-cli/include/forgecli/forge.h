#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <worldforge/forge_world.h>

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

// A phase turns a ForgedWorld into layers. `needs` is the last forgeWorld phase it reads, so a batch
// forges every seed once, up to the highest `needs` among the selected phases, and each phase only
// draws. `run` is the stand-alone form (forges its own prerequisites with defaultForgeConfig).
// Adding a phase is one entry in phaseRegistry().
struct Phase {
    std::string_view name;
    std::string_view description;
    PhaseResult (*run)(std::uint64_t seed, int size);
    worldforge::ForgePhase needs;
    PhaseResult (*layers)(const worldforge::ForgedWorld& world);
};

// The config every forge-cli run uses. Phases 1 to 5 keep the library defaults; settlement and
// POI spacing shrink with the grid so a small map still gets sites, and a fixed faction set stands
// in for the axiom-resolved one. Item ids, NPC archetypes and dungeon archetypes come from the data scan.
worldforge::ForgeConfig defaultForgeConfig(int size);

struct ContentPools {
    std::vector<std::string> itemIds;
    std::vector<std::string> npcArchetypes;
    std::vector<std::string> dungeonArchetypes;
};

// Reads ids from dataDir: items from catalogue/ and notable_items/, NPC archetypes from npcs/,
// dungeon archetypes from dungeon_skeletons/. Every file is a JSON array of records with a string
// "id"; ids are sorted and unique. A missing subdirectory is an empty pool. `error` is set on a
// missing dataDir, unreadable or malformed file, or duplicate id.
ContentPools scanContentPools(const std::string& dataDir, std::string& error);

// Layers of `phase` from a world forged at least up to phase.needs. When the world stopped or failed
// earlier, the result has an error naming the cause and no layers.
PhaseResult phaseLayers(const Phase& phase, const worldforge::ForgedWorld& world);

// The highest `needs` among `phases` (Tectonics when empty).
worldforge::ForgePhase highestNeed(const std::vector<const Phase*>& phases);

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
    std::string overlay; // relative path of the manifest overlay JSON; empty when none was written
    std::vector<LayerRecord> layers;
    bool validated = false;
    bool rejected = false;                // set when validated and no attempt passed every gate
    std::vector<std::string> causes;      // failed gates of the last attempt, "gate: cause"
};

struct RunOptions {
    std::uint64_t seedBegin = 0;
    std::uint64_t seedEnd = 0; // inclusive
    int size = 256;
    std::vector<std::string> phases; // empty means every registered phase
    std::string outDir;              // empty means compute and hash only, write nothing
    bool validate = false;           // score each seed with forgeValidated (reroll included) and write validation.json
    std::string dataDir;             // empty means phase 11 binds an empty item and NPC pool
};

constexpr std::uint64_t kMaxBatchSeeds = 100000;

struct RunResult {
    std::vector<SeedRecord> seeds;
    std::string manifest;
    std::string validation; // the validation.json text, empty unless options.validate
    std::string validationTable; // one text table per seed, empty unless options.validate
    std::string error; // set when the options are invalid or a file cannot be written
};

RunResult runForge(const RunOptions& options);

std::string manifestJson(const RunOptions& options, const std::vector<SeedRecord>& seeds);

}
