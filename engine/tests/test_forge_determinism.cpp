#include <catch2/catch_test_macros.hpp>

#include <forgecli/forge.h>

#include <worldforge/forge_world.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// The end-to-end forge digest: forgeWorld runs phases 1 to 11 for a fixed set of (size, seed) pairs and
// the chained world hash plus every phase hash must equal tests/data/forge_world_digests.json on every
// OS. Both CI jobs (Windows, Linux with ASan/UBSan) read that one file.

namespace {

const std::filesystem::path kTablePath = std::filesystem::path(ICHOR_TEST_DATA) / "forge_world_digests.json";

constexpr const char* kRegenerate = "If a phase changed on purpose, regenerate the table with: ICHOR_UPDATE_FORGE_DIGESTS=1 bin/build -t \"forge world digests\" "
                                    "and commit tests/data/forge_world_digests.json. If not, this is a determinism regression: do not regenerate.";

struct Case {
    int size;
    std::uint64_t seed;
};

std::vector<Case> cases()
{
    std::vector<Case> out;
    for (std::uint64_t seed = 0; seed < 48; ++seed) out.push_back({ 64, seed });
    for (const std::uint64_t seed : { 1ull, 42ull, 0xffffffffffffffffull }) out.push_back({ 128, seed });
    out.push_back({ 256, 7 });
    return out;
}

std::string key(const Case& c)
{
    return std::format("{}/{}", c.size, c.seed);
}

std::string hex(std::uint64_t v)
{
    return std::format("{:016x}", v);
}

using Observed = std::map<std::string, std::string>;

// Flat name to hex map: "world", one entry per completed phase, and "failed" when a phase rejected the run.
Observed observe(const Case& c)
{
    const worldforge::ForgedWorld w = worldforge::forgeWorld(c.seed, c.size, forgecli::defaultForgeConfig(c.size));
    Observed seen;
    seen["world"] = hex(w.worldHash());
    for (int i = 0; i < w.completedPhases(); ++i) {
        const auto phase = static_cast<worldforge::ForgePhase>(i + 1);
        seen[std::format("{:02}_{}", i + 1, worldforge::forgePhaseName(phase))] = hex(w.phaseHashes()[static_cast<std::size_t>(i)]);
    }
    if (w.failedPhase()) seen["failed"] = std::string(worldforge::forgePhaseName(*w.failedPhase()));
    return seen;
}

std::map<std::string, Observed> observeAll()
{
    std::map<std::string, Observed> all;
    for (const Case& c : cases()) all[key(c)] = observe(c);
    return all;
}

std::string render(const std::map<std::string, Observed>& all)
{
    nlohmann::ordered_json root;
    for (const auto& [k, seen] : all) {
        for (const auto& [name, value] : seen) root["worlds"][k][name] = value;
    }
    return root.dump(2) + "\n";
}

std::vector<std::string> compare(const std::string& text, const std::map<std::string, Observed>& all)
{
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("worlds") || !root["worlds"].is_object()) {
        return { std::format("forge_world_digests.json is not a valid table. {}", kRegenerate) };
    }
    std::vector<std::string> problems;
    for (const auto& [k, seen] : all) {
        const auto entry = root["worlds"].find(k);
        if (entry == root["worlds"].end() || !entry->is_object()) {
            problems.push_back(std::format("{}: no row in forge_world_digests.json. {}", k, kRegenerate));
            continue;
        }
        for (const auto& [name, actual] : seen) {
            const auto expected = entry->find(name);
            const std::string want = expected != entry->end() && expected->is_string() ? expected->get<std::string>() : "(missing)";
            if (want != actual) {
                const std::string what = name == "world" ? "chained world hash" : "phase " + name;
                problems.push_back(std::format("size/seed {}: {} moved: expected {} but got {}. {}", k, what, want, actual, kRegenerate));
            }
        }
        for (const auto& [name, value] : entry->items()) {
            if (!seen.contains(name)) problems.push_back(std::format("size/seed {}: table has {} but the run did not produce it. {}", k, name, kRegenerate));
        }
    }
    for (const auto& [k, value] : root["worlds"].items()) {
        if (!all.contains(k)) problems.push_back(std::format("{}: table has a row the test no longer runs. {}", k, kRegenerate));
    }
    return problems;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines) out += line + "\n";
    return out;
}

}

TEST_CASE("forge world digests: every phase of the forge matches the checked-in table on every platform", "[forge][determinism]")
{
    const auto all = observeAll();

    for (const auto& [k, seen] : all) {
        INFO(k);
        const bool diedInPhaseOne = seen.contains("failed") && seen.size() == 2;
        REQUIRE_FALSE(diedInPhaseOne); // a table of runs that die in phase 1 would pin nothing
    }
    std::size_t complete = 0;
    for (const auto& [k, seen] : all) complete += seen.size() == 12 ? 1 : 0;
    REQUIRE(complete * 2 > all.size());

    const char* update = std::getenv("ICHOR_UPDATE_FORGE_DIGESTS");
    if (update != nullptr && update[0] == '1') {
        REQUIRE(std::getenv("CI") == nullptr);
        std::ofstream out(kTablePath, std::ios::binary | std::ios::trunc);
        out << render(all);
        REQUIRE(static_cast<bool>(out));
        SUCCEED("forge_world_digests.json written");
        return;
    }

    std::ifstream in(kTablePath, std::ios::binary);
    if (!in) FAIL("no table at " << kTablePath.string() << ". " << kRegenerate);
    std::stringstream text;
    text << in.rdbuf();
    const auto problems = compare(text.str(), all);
    INFO(joined(problems));
    REQUIRE(problems.empty());
}

TEST_CASE("forge world digests: a moved phase hash is reported by phase with the regenerate command", "[forge][determinism]")
{
    const auto all = observeAll();
    const std::string table = render(all);
    REQUIRE(compare(table, all).empty());

    auto moved = all;
    moved.begin()->second["06_resources"] = "0000000000000000";
    const auto problems = compare(table, moved);
    REQUIRE(problems.size() == 1);
    REQUIRE(problems[0].find("phase 06_resources") != std::string::npos);
    REQUIRE(problems[0].find("ICHOR_UPDATE_FORGE_DIGESTS=1") != std::string::npos);

    REQUIRE_FALSE(compare("not json", all).empty());
}
