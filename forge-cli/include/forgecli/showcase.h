#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <worldforge/forge_world.h>
#include <worldforge/world_validation.h>

namespace forgecli {

// The showcase seed picker (SEED-650): forges a range of seeds, scores each one on what makes a world
// worth showing, ranks them, and keeps the best few. The scoring is documented in docs/showcase-seeds.md.
//
// Everything a score is made of is a number in ShowcaseMetrics, measured live from the forged world; no
// seed number appears in the scoring. Each term turns its metric into permille (0 to 1000) and a weight
// (kShowcaseWeights, the one place the weights live) turns that into points. Integer maths only, so a
// score is identical on every platform.

// What the walk probe says about the spawn and the walk to the nearest settlement. The probe needs the
// engine (heightfield, water, the walking rules), so forge-cli's library takes it as a function and the
// engine-side implementation lives in forge-cli/walk. A default value is "not measured".
struct WalkProbeResult {
    bool spawnFound = false;
    double spawnX = 0.0; // metres
    double spawnZ = 0.0;
    double metresPerCell = 0.0; // forged cell size in metres, to bring the spawn back onto the forged grids
    bool pocket = false;        // the walkable ground around the spawn is closed in
    double reachMetres = 0.0;   // how far from the spawn the walker can get on foot, up to the probe's cap
    bool hasTarget = false;     // there was another settlement to walk to
    double tripMetres = 0.0;    // straight line from the spawn to it
    bool routeFound = false;
    bool arrived = false;
    double walkedMetres = 0.0;
    std::string blocker; // what stopped the walk, empty when it arrived
    std::string error;   // the probe could not run
};

using WalkProbe = std::function<WalkProbeResult(const worldforge::ForgedWorld&)>;

struct ShowcaseMetrics {
    std::uint64_t seed = 0;
    int size = 0;
    std::string error; // the world did not forge, or its artifacts are missing: every term scores 0

    int gatesPassed = 0; // first attempt: no reroll, the seed itself
    int gatesEvaluated = 0;
    std::vector<std::string> failedGates;

    int landSharePermille = -1; // -1 when not measured

    int riverSystems = 0; // connected river networks of at least kMinRiverCells cells
    int lakes = 0;        // connected lakes of at least kMinLakeCells cells

    int settlements = 0;
    int tiersPresent = 0; // distinct settlement tiers (hamlet, village, town, city)

    bool walkMeasured = false; // a spawn and another settlement to walk to
    bool walkArrived = false;
    int tripMetres = 0;
    int walkedMetres = 0;
    std::string walkBlocker;

    bool spawnFound = false;
    bool pocketSpawn = false;
    int biomesNearSpawn = 0; // distinct land biomes within kBiomeRadiusMetres of the spawn
};

constexpr int kMinRiverCells = 10;
constexpr int kMinLakeCells = 4;
constexpr int kBiomeRadiusMetres = 300;

enum class ShowcaseTerm : std::uint8_t {
    Gates,
    LandShare,
    Rivers,
    Lakes,
    Settlements,
    TierSpread,
    WalkArrival,
    BiomeVariety,
    Spawn,
};

constexpr int kShowcaseTermCount = 9;

constexpr std::array<std::string_view, kShowcaseTermCount> kShowcaseTermNames = {
    "gates", "land_share", "rivers", "lakes", "settlements", "tier_spread", "walk_arrival", "biome_variety", "spawn",
};

// The weights and targets of every term, in one place. Points are weight * permille / 1000, so a
// perfect world scores the sum of the weights, which is 100. Change a number here and the doc table in
// docs/showcase-seeds.md with it.
struct ShowcaseWeights {
    std::array<int, kShowcaseTermCount> weight = {20, 15, 10, 5, 10, 5, 15, 10, 10};
    int landIdealMinPermille = 250;   // land share in [min, max] scores full
    int landIdealMaxPermille = 650;
    int landFalloffPermille = 150;    // and falls to 0 this far outside it
    int riversForFull = 12;
    int lakesForFull = 4;
    int settlementsForFull = 20;
    int tiersForFull = 4;
    int biomesForFull = 6;
};

inline constexpr ShowcaseWeights kShowcaseWeights{};

struct ScoredTerm {
    ShowcaseTerm term = ShowcaseTerm::Gates;
    int permille = 0; // how well the world did, 0 to 1000
    int weight = 0;
    int pointsMilli = 0; // weight * permille, so 100000 is a perfect term of weight 100
    std::string note;    // the measured fact, "land 41.2%"
};

struct ShowcaseScore {
    std::uint64_t seed = 0;
    int size = 0;
    int totalMilli = 0; // sum of pointsMilli over 1000, in thousandths of a point: 87400 is 87.4
    std::array<ScoredTerm, kShowcaseTermCount> terms;
};

ShowcaseScore scoreShowcase(const ShowcaseMetrics& metrics, const ShowcaseWeights& weights = kShowcaseWeights);

// "87.4" for 87400.
std::string pointsText(int milli);

// Metrics of a forged world. `report` is validateWorld of the same world (first attempt). `walk` is the probe's answer.
ShowcaseMetrics measureShowcase(const worldforge::ForgedWorld& world, const worldforge::ValidationReport& report, const WalkProbeResult& walk);

// Rivers and lakes as connected components of the hydrology masks.
int countRiverSystems(const worldforge::HydrologyMap& hydrology, int minCells = kMinRiverCells);
int countLakes(const worldforge::HydrologyMap& hydrology, int minCells = kMinLakeCells);

// Highest score first, ties by the lower seed. Pure, so the same scores always rank the same.
std::vector<ShowcaseScore> rankShowcase(std::vector<ShowcaseScore> scores);

// One line per fact worth showing: the terms that scored well first, then the ones that cost points.
std::vector<std::string> showcaseReasons(const ShowcaseScore& score);

std::string showcaseCsv(const std::vector<ShowcaseScore>& ranked, const std::vector<ShowcaseMetrics>& metrics);
std::string showcaseMarkdown(const std::vector<ShowcaseScore>& ranked, const std::vector<ShowcaseMetrics>& metrics, int top);

// The top records as data/showcase_seeds.json: an array of {id, rank, seed, size, score, reasons}.
std::string showcaseSeedsJson(const std::vector<ShowcaseScore>& ranked, int top);

struct ShowcaseOptions {
    std::uint64_t seedBegin = 1;
    std::uint64_t seedEnd = 200; // inclusive
    int size = 256;
    int top = 5;
    std::string outDir; // empty: nothing is written, the tables go to stdout
    std::string seedsJson; // path of the top-N JSON, empty to skip
};

constexpr std::uint64_t kMaxShowcaseSeeds = 2000;

struct ShowcaseResult {
    std::vector<ShowcaseMetrics> metrics; // in seed order
    std::vector<ShowcaseScore> ranked;    // best first
    std::string csv;
    std::string markdown;
    std::string json;
    std::string error; // invalid options or an unwritable file
};

// Forges every seed (defaultForgeConfig, the sanity thresholds), measures it with `probe` (none: the walk and
// spawn terms score 0), ranks them and, with outDir, writes ranking.csv, ranking.md and a thumbnail per top
// seed (the gallery cell of that seed: no window) to outDir/top/.
ShowcaseResult runShowcase(const ShowcaseOptions& options, const WalkProbe& probe);

// `showcase --seeds A..B [--size N] [--top K] [--out DIR] [--json FILE]`; argv[1] is the subcommand name.
int showcaseMain(int argc, char** argv, const WalkProbe& probe);

}
