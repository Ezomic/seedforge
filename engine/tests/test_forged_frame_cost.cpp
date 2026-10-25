#include "forged_world_fixture.h"

#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"
#include "ichor/world/streamer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

// SEED-651: a headless frame cost guard for the forged world. Wall-clock time is noise on a shared runner, so
// this counts what a frame costs the GPU and the loader instead: draw calls per stage, instances, resident cells
// and the meshes rebuilt when cells stream in. A walk over three fixed seeds is measured and every count must stay
// within 10 percent of the checked-in budget (engine/tests/data/forged_frame_budget.json).
//
// To regenerate the budget after an intended change (merge origin/main first, then review the diff of the JSON):
//   engine/build/windows-ci/tests/ichor_tests_world.exe "[.frame_cost_regen]"
// Regenerating is allowed only when the cost moved on purpose (a world, scenery, water or streaming change that is
// meant to draw more or less) and the PR says why; never to silence a red run that nobody explained.

namespace {

constexpr int kGrid = 129;
constexpr int kFrames = 120;
constexpr std::array<std::uint64_t, 3> kSeeds = {1, 2, 3};
constexpr double kHour = 12.0;

// The stages a frame is made of, as counts. The order is the order in the budget file.
constexpr std::array<const char*, 9> kStages = {"terrainDraws", "terrainIndices", "vegetationDraws", "plantInstances", "waterDraws", "instances", "cellsResidentPeak", "cellLoads", "meshesRebuilt"};

using Counts = std::map<std::string, std::uint64_t>;

// Test seam: scales the instance count the way a regression that draws every placed object twice would.
struct Seam {
    std::uint32_t instanceScale = 1;
};

struct Forged {
    std::unique_ptr<ForgedGame> game;
    ForgedScenery scenery;
    std::string error;
};

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

Forged forge(std::uint64_t seed)
{
    Forged made;
    const test::ForgedFixture forged = test::forgedFixture(descFor(seed), kGrid);
    if (!forged.ok()) {
        made.error = "the forge did not finish: " + forged.error;
        return made;
    }
    made.game = buildForgedGame(forged.result, descFor(seed));
    if (!made.game->ok()) {
        made.error = made.game->error;
        return made;
    }
    made.scenery = buildForgedScenery(*made.game);
    return made;
}

struct ResidentCost {
    glm::dvec3 origin{0.0};
    std::array<std::uint64_t, 3> lodIndices{};
    std::uint64_t plants = 0;
    bool hasWater = false;
    std::uint64_t instances = 0;
};

// The anchor visits the spawn and then the first settlements, a quarter of a cell per frame, so cells stream in and
// out under the same budget the app uses.
std::vector<glm::dvec2> waypoints(const ForgedGame& game)
{
    std::vector<glm::dvec2> points = {{game.spawn.x, game.spawn.z}};
    for (std::size_t i = 0; i < game.sites.size() && i < 3; ++i) {
        points.push_back({game.sites[i].x, game.sites[i].z});
    }
    return points;
}

glm::dvec3 anchorAt(const ForgedGame& game, const std::vector<glm::dvec2>& points, int frame)
{
    const double step = static_cast<double>(game.desc.cellMetres) * 0.25;
    double travelled = step * frame;
    glm::dvec2 position = points.front();
    for (std::size_t i = 1; i < points.size() && travelled > 0.0; ++i) {
        const double leg = glm::length(points[i] - points[i - 1]);
        if (travelled >= leg) {
            travelled -= leg;
            position = points[i];
        } else {
            position = points[i - 1] + (points[i] - points[i - 1]) * (travelled / leg);
            travelled = 0.0;
        }
    }
    return {position.x, game.heightfield.heightAt(position.x, position.y), position.y};
}

// One walk, measured once per seam: the seams only change how the instance count is scaled, so a single pass serves all.
std::vector<Counts> measure(const Forged& world, const std::vector<Seam>& seams)
{
    const ForgedGame& game = *world.game;
    std::vector<std::uint64_t> instances(seams.size(), 0);

    StreamerConfig config;
    config.synchronous = true;
    config.vegetation = true;
    config.water = true;
    config.rivers = game.water.rivers;
    config.hydrology = game.water.hydrology;
    config.biomes = game.biomes;
    config.scenery = forgedSceneryGuard(game);
    config.pois = game.pois;
    config.buildings = game.buildings;
    config.streets = game.streets;

    std::map<CellId, ResidentCost> resident;
    Counts counts;
    for (const char* stage : kStages) {
        counts[stage] = 0;
    }
    WorldStreamer streamer(game.heightfield, config);
    streamer.onLoaded([&](const CellPayload& payload) {
        ResidentCost cost;
        cost.origin = {game.heightfield.cellOriginX(payload.cell.id), 0.0, game.heightfield.cellOriginZ(payload.cell.id)};
        for (std::size_t lod = 0; lod < cost.lodIndices.size() && lod < payload.lods.size(); ++lod) {
            cost.lodIndices[lod] = payload.lods[lod].indices.size();
        }
        cost.plants = payload.plants.size();
        cost.hasWater = !payload.water.vertices.empty();
        cost.instances = (world.scenery.roadMesh.contains(payload.cell.id) ? 1 : 0) + (world.scenery.streetMesh.contains(payload.cell.id) ? 1 : 0);
        for (const PlacedPoi& poi : payload.pois) {
            cost.instances += world.scenery.poiMesh.contains(poi.id) ? 1 : 0;
        }
        // The app draws the textured kit pieces when the kit cooks, and the box buildings when it does not.
        cost.instances += game.kit && !game.kit->empty() ? game.kit->inCell(payload.cell.id).size() : payload.buildings.size();
        resident[payload.cell.id] = cost;
        counts["cellLoads"] += 1;
        counts["meshesRebuilt"] += payload.lods.size() + (payload.plants.empty() ? 0 : 1) + (cost.hasWater ? 1 : 0);
    });
    streamer.onUnloaded([&](CellId id) { resident.erase(id); });

    const std::vector<glm::dvec2> points = waypoints(game);
    const double cellMetres = game.desc.cellMetres;
    for (int frame = 0; frame < kFrames; ++frame) {
        const glm::dvec3 anchor = anchorAt(game, points, frame);
        streamer.update(anchor);
        counts["cellsResidentPeak"] = std::max<std::uint64_t>(counts["cellsResidentPeak"], resident.size());
        for (const auto& [id, cell] : resident) {
            const glm::dvec3 centre = cell.origin + glm::dvec3(cellMetres * 0.5, 0.0, cellMetres * 0.5);
            const double distance = glm::length(glm::dvec2(centre.x - anchor.x, centre.z - anchor.z)) / cellMetres;
            const std::size_t lod = distance < 1.2 ? 0 : distance < 2.2 ? 1 : 2;
            counts["terrainDraws"] += 1;
            counts["terrainIndices"] += cell.lodIndices[lod];
            if (cell.hasWater) {
                counts["waterDraws"] += 1;
            }
            if (distance >= 2.2) {
                continue;
            }
            if (cell.plants > 0) {
                counts["vegetationDraws"] += 1;
                counts["plantInstances"] += cell.plants;
            }
            for (std::size_t i = 0; i < seams.size(); ++i) {
                instances[i] += cell.instances * seams[i].instanceScale;
            }
        }
    }
    std::vector<Counts> out(seams.size(), counts);
    for (std::size_t i = 0; i < seams.size(); ++i) {
        out[i]["instances"] = instances[i];
    }
    return out;
}

std::filesystem::path budgetPath()
{
    return std::filesystem::path(ICHOR_TEST_DATA) / "forged_frame_budget.json";
}

nlohmann::json toJson(const std::map<std::uint64_t, Counts>& measured)
{
    nlohmann::json seeds = nlohmann::json::object();
    for (const auto& [seed, counts] : measured) {
        nlohmann::json entry = nlohmann::json::object();
        for (const char* stage : kStages) {
            entry[stage] = counts.at(stage);
        }
        seeds[std::to_string(seed)] = entry;
    }
    return {{"grid", kGrid}, {"frames", kFrames}, {"seeds", seeds}};
}

// A stage is over budget when it is more than 10 percent above it; a budget of zero allows none.
bool overBudget(std::uint64_t actual, std::uint64_t budget)
{
    return actual * 10 > budget * 11;
}

// A stage that fell to under half its budget means the feature stopped drawing, which no upper bound would notice.
bool collapsed(std::uint64_t actual, std::uint64_t budget)
{
    return actual * 2 < budget;
}

std::vector<std::string> overruns(const Counts& actual, const nlohmann::json& budget)
{
    std::vector<std::string> problems;
    for (const char* stage : kStages) {
        if (!budget.contains(stage)) {
            problems.push_back(std::string(stage) + ": no budget");
            continue;
        }
        if (!budget.at(stage).is_number_unsigned()) {
            problems.push_back(std::string(stage) + ": the budget is not a count");
            continue;
        }
        const std::uint64_t allowed = budget.at(stage).get<std::uint64_t>();
        if (overBudget(actual.at(stage), allowed) || collapsed(actual.at(stage), allowed)) {
            problems.push_back(std::string(stage) + ": " + std::to_string(actual.at(stage)) + " against a budget of " + std::to_string(allowed));
        }
    }
    return problems;
}

nlohmann::json loadBudget()
{
    std::ifstream in(budgetPath());
    REQUIRE(in.good());
    return nlohmann::json::parse(in);
}

struct SeedRun {
    std::string error;
    Counts normal;
    Counts doubled;
    bool repeats = false;
};

// The three seeds run side by side (forge and walk are independent), which keeps the whole test under its time limit.
std::map<std::uint64_t, SeedRun> runAllSeeds()
{
    std::vector<std::pair<std::uint64_t, std::future<SeedRun>>> tasks;
    for (const std::uint64_t seed : kSeeds) {
        tasks.emplace_back(seed, std::async(std::launch::async, [seed] {
            SeedRun run;
            const Forged world = forge(seed);
            if (!world.error.empty()) {
                run.error = world.error;
                return run;
            }
            const std::vector<Counts> counts = measure(world, {Seam{}, Seam{.instanceScale = 2}});
            run.normal = counts[0];
            run.doubled = counts[1];
            run.repeats = seed != kSeeds.back() || measure(world, {Seam{}}).front() == run.normal;
            return run;
        }));
    }
    std::map<std::uint64_t, SeedRun> runs;
    for (auto& [seed, task] : tasks) {
        runs[seed] = task.get();
    }
    return runs;
}

}

TEST_CASE("the forged frame counts stay inside the budget and a doubled instance count turns the guard red", "[forged_frame_cost]")
{
    const nlohmann::json budget = loadBudget();
    REQUIRE(budget.at("grid") == kGrid);
    REQUIRE(budget.at("frames") == kFrames);
    const std::map<std::uint64_t, SeedRun> runs = runAllSeeds();
    Counts total;
    for (const auto& [seed, run] : runs) {
        INFO("seed " << seed << " " << run.error);
        REQUIRE(run.error.empty());
        REQUIRE(budget.at("seeds").contains(std::to_string(seed)));
        const nlohmann::json& seedBudget = budget.at("seeds").at(std::to_string(seed));

        const std::vector<std::string> problems = overruns(run.normal, seedBudget);
        for (const std::string& problem : problems) {
            UNSCOPED_INFO(problem);
        }
        CHECK(problems.empty());

        // The seam must trip the instance stage and no other.
        const std::vector<std::string> doubledProblems = overruns(run.doubled, seedBudget);
        REQUIRE(doubledProblems.size() == 1);
        CHECK(doubledProblems.front().starts_with("instances:"));
        CHECK(run.doubled.at("terrainDraws") == run.normal.at("terrainDraws"));

        CHECK(run.repeats);
        for (const auto& [stage, value] : run.normal) {
            total[stage] += value;
        }
    }
    for (const char* stage : kStages) {
        INFO(stage);
        CHECK(total[stage] > 0);
    }
}

TEST_CASE("the tolerance is 10 percent and a zero budget allows nothing", "[forged_frame_cost]")
{
    CHECK_FALSE(overBudget(110, 100));
    CHECK(overBudget(111, 100));
    CHECK_FALSE(overBudget(0, 0));
    CHECK(overBudget(1, 0));
    CHECK_FALSE(overBudget(0, 5));
    CHECK(collapsed(49, 100));
    CHECK_FALSE(collapsed(50, 100));
}

TEST_CASE("regenerate the forged frame budget", "[.frame_cost_regen]")
{
    std::map<std::uint64_t, Counts> measured;
    for (const auto& [seed, run] : runAllSeeds()) {
        REQUIRE(run.error.empty());
        measured[seed] = run.normal;
    }
    std::ofstream out(budgetPath(), std::ios::binary | std::ios::trunc);
    REQUIRE(out.good());
    out << toJson(measured).dump(2) << "\n";
}
