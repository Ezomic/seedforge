#include "ichor/core/hash.h"
#include "ichor/core/input.h"
#include "ichor/core/rng.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/loop/engine.h"
#include "ichor/platform/null_platform.h"
#include "ichor/sim/scene.h"
#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/hydrology_cache.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace ichor;

// The same seed must give the same world and the same simulation on every OS and compiler. The expected
// values live in tests/data/determinism.json and both CI jobs compare against that one file.

namespace {

constexpr int kFrames = 120;
constexpr std::uint64_t kSeeds[] = {0, 1, 42, std::numeric_limits<std::uint64_t>::max()};

const std::filesystem::path kExpectedPath = std::filesystem::path(ICHOR_TEST_DATA) / "determinism.json";

constexpr const char* kRegenerate = "If the change was meant to alter worldgen or the simulation, regenerate the file with ICHOR_UPDATE_DETERMINISM=1 "
                                    "and commit it. If not, this is a determinism regression: do not regenerate.";

// Named values, so a mismatch says which part of the world or the simulation moved and, for the walked
// scene, at which tick it first did.
using Observed = std::map<std::string, std::uint64_t>;

using Observations = std::vector<std::pair<std::uint64_t, Observed>>;

WorldDesc worldFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 1024;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

std::uint64_t hashBytes(const std::vector<std::uint8_t>& bytes)
{
    return hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

// A scene with ground, a wall and a character, walked by seeded random input: physics, movement and
// the scene's own state hash, which an empty scene would leave almost untouched.
void walkScene(std::uint64_t seed, Observed& out)
{
    Scene scene(seed);
    SpawnCommand ground;
    ground.id = StableId::of("ground");
    ground.transform.position = {0.0, -0.5, 0.0};
    ground.staticBox = StaticBox{glm::vec3(20.0f, 0.5f, 20.0f)};
    scene.enqueue(ground);
    SpawnCommand wall;
    wall.id = StableId::of("wall");
    wall.transform.position = {3.0, 1.0, 0.0};
    wall.staticBox = StaticBox{glm::vec3(0.5f, 1.0f, 10.0f)};
    scene.enqueue(wall);
    SpawnCommand hero;
    hero.id = StableId::of("hero");
    hero.transform.position = {0.0, 3.0, 0.0};
    hero.player = Player{4.0f};
    hero.character = CharacterBody{0.3f, 0.6f};
    scene.enqueue(hero);

    Rng rng(seed);
    for (int i = 0; i < kFrames * 2; ++i) {
        InputFrame frame;
        frame.moveX = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.moveY = static_cast<float>(rng.range(0, 2)) - 1.0f;
        frame.buttons = rng.range(0, 1) * InputFrame::Sprint;
        scene.tick(1.0 / 60.0, frame);
        const std::uint64_t ticks = scene.tickCount();
        if (ticks == 1 || ticks == 30 || ticks == 60 || ticks == 120 || ticks == 240) {
            out[std::format("physics_t{:03}", ticks)] = scene.stateHash();
        }
    }
}

void headlessRun(std::uint64_t seed, Observed& out)
{
    NullPlatform platform;
    NullRenderer renderer;
    REQUIRE(platform.init({}));
    REQUIRE(renderer.init());
    EngineConfig config;
    config.seed = seed;
    Engine engine(platform, renderer, config);
    for (int i = 0; i < kFrames; ++i) {
        REQUIRE(engine.frame(config.fixedDt));
    }
    out["engine"] = engine.scene().stateHash();
    walkScene(seed, out);
}

Observed observe(std::uint64_t seed)
{
    const Heightfield field = forgeHeightfield(worldFor(seed));
    REQUIRE_FALSE(field.empty());
    const Hydrology hydrology = computeHydrology(field);
    REQUIRE_FALSE(hydrology.empty());
    REQUIRE(*std::ranges::max_element(hydrology.accumulation) > 100); // a digest of a trivial result would pin nothing
    Observed seen;
    seen["heightfield"] = field.digest();
    seen["hydrology"] = hashBytes(encodeHydrologyCache(field, hydrology));
    headlessRun(seed, seen);
    return seen;
}

Observations observeAll()
{
    Observations all;
    for (const std::uint64_t seed : kSeeds) {
        all.emplace_back(seed, observe(seed));
    }
    return all;
}

std::string hex(std::uint64_t value)
{
    return std::format("{:016x}", value);
}

std::string render(const Observations& all)
{
    nlohmann::ordered_json root;
    root["frames"] = kFrames;
    for (const auto& [seed, seen] : all) {
        for (const auto& [name, value] : seen) {
            root["seeds"][std::to_string(seed)][name] = hex(value);
        }
    }
    return root.dump(2) + "\n";
}

// Every way `text` disagrees with what was observed, each naming the seed, the value and how to fix it.
std::vector<std::string> compare(const std::string& text, const Observations& all)
{
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("seeds") || !root["seeds"].is_object()) {
        return {std::format("determinism.json is not a valid expectations file. {}", kRegenerate)};
    }
    if (!root.contains("frames") || !root["frames"].is_number_integer() || root["frames"].get<int>() != kFrames) {
        return {std::format("determinism.json was not recorded for {} frames. {}", kFrames, kRegenerate)};
    }

    std::vector<std::string> problems;
    for (const auto& [seed, seen] : all) {
        const auto entry = root["seeds"].find(std::to_string(seed));
        if (entry == root["seeds"].end() || !entry->is_object()) {
            problems.push_back(std::format("seed {}: no expected values in determinism.json. {}", seed, kRegenerate));
            continue;
        }
        for (const auto& [name, actual] : seen) {
            const auto expected = entry->find(name);
            const std::string want = expected != entry->end() && expected->is_string() ? expected->get<std::string>() : "(missing)";
            if (want != hex(actual)) {
                problems.push_back(std::format("seed {}: {} moved: expected {} but got {}. {}", seed, name, want, hex(actual), kRegenerate));
            }
        }
    }
    for (const auto& [key, value] : root["seeds"].items()) {
        const bool known = std::ranges::any_of(all, [&](const auto& seen) { return std::to_string(seen.first) == key; });
        if (!known) {
            problems.push_back(std::format("seed {}: determinism.json has values for a seed the test no longer runs. {}", key, kRegenerate));
        }
    }
    return problems;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines) {
        out += line + "\n";
    }
    return out;
}

}

TEST_CASE("determinism: the forged world and the simulation match the checked-in values on every platform")
{
    const Observations all = observeAll();

    const char* update = std::getenv("ICHOR_UPDATE_DETERMINISM");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(std::getenv("CI") == nullptr); // CI must compare, never rewrite the expectations
        std::ofstream out(kExpectedPath, std::ios::binary | std::ios::trunc);
        out << render(all);
        REQUIRE(static_cast<bool>(out));
        SUCCEED("determinism.json written");
        return;
    }

    std::ifstream in(kExpectedPath, std::ios::binary);
    if (!in) {
        FAIL("no expectations file at " << kExpectedPath.string() << ". " << kRegenerate);
    }
    std::stringstream text;
    text << in.rdbuf();
    const auto problems = compare(text.str(), all);
    INFO(joined(problems));
    REQUIRE(problems.empty());
}

TEST_CASE("determinism: observing a seed twice gives the same values")
{
    for (const std::uint64_t seed : kSeeds) {
        const Observed a = observe(seed);
        const Observed b = observe(seed);
        REQUIRE(a == b);
    }
    const Observed one = observe(1);
    const Observed other = observe(42);
    REQUIRE(one.size() == other.size());
    for (const auto& [name, value] : one) {
        REQUIRE(value != other.at(name));
    }
}

TEST_CASE("determinism: a moved value, a missing seed and a broken file are each reported by name, not crashed on")
{
    const Observations all = observeAll();
    const std::string good = render(all);
    REQUIRE(compare(good, all).empty());

    Observations moved = all;
    moved[2].second["hydrology"] ^= 1;
    const auto movedProblems = compare(good, moved);
    REQUIRE(movedProblems.size() == 1);
    REQUIRE(movedProblems[0].find("seed 42: hydrology moved") != std::string::npos);
    REQUIRE(movedProblems[0].find("ICHOR_UPDATE_DETERMINISM=1") != std::string::npos);

    Observations drifted = all;
    drifted[3].second["physics_t060"] ^= 1;
    const auto driftedProblems = compare(good, drifted);
    REQUIRE(driftedProblems.size() == 1);
    REQUIRE(driftedProblems[0].find("seed 18446744073709551615: physics_t060 moved") != std::string::npos);

    Observations extra = all;
    extra.emplace_back(7, all[0].second);
    REQUIRE(compare(good, extra)[0].find("seed 7: no expected values") != std::string::npos);

    Observations fewer = all;
    fewer.pop_back();
    REQUIRE(compare(good, fewer)[0].find("no longer runs") != std::string::npos);

    REQUIRE(compare("", all).size() == 1);
    REQUIRE(compare("{not json", all).size() == 1);
    REQUIRE(compare("[]", all).size() == 1);
    REQUIRE(compare("{\"seeds\": 3}", all).size() == 1);
    REQUIRE_FALSE(compare("{\"frames\": 120, \"seeds\": {\"0\": {\"heightfield\": 5}}}", all).empty());
    REQUIRE(compare("{\"frames\": \"120\", \"seeds\": {}}", all).size() == 1);
    REQUIRE(compare("{\"frames\": 7, \"seeds\": {}}", all)[0].find("not recorded for 120 frames") != std::string::npos);
}
