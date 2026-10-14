#include "ichor/data/data_set.h"
#include "ichor/data/npc_traits.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

namespace {

std::filesystem::path uniqueRoot()
{
    static int counter = 0;
    const auto root = std::filesystem::temp_directory_path()
        / ("ichor_traits_test_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "npcs");
    return root;
}

std::optional<TraitPool> poolFromJson(const std::string& json, std::string& error)
{
    const auto root = uniqueRoot();
    std::ofstream(root / "npcs" / "traits.json", std::ios::binary) << json;
    DataSet data;
    const bool loaded = data.loadDirectory(root);
    std::filesystem::remove_all(root);
    if (!loaded) {
        error = data.lastError();
        return std::nullopt;
    }
    const DataTable* table = data.table("npcs");
    if (table == nullptr) {
        error = "no npcs table";
        return std::nullopt;
    }
    return TraitPool::fromTable(*table, error);
}

TraitPool shippedPool()
{
    std::string error;
    std::ifstream file(std::filesystem::path(ICHOR_GAME_DATA_DIR) / "npcs" / "traits.json", std::ios::binary);
    REQUIRE(file.good());
    const std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto pool = poolFromJson(json, error);
    INFO(error);
    REQUIRE(pool.has_value());
    return *pool;
}

Trait trait(const std::string& id, std::vector<std::string> incompatible = {})
{
    return Trait { id, id, "", std::move(incompatible) };
}

}

TEST_CASE("the shipped pool has about twenty valid traits")
{
    const TraitPool pool = shippedPool();
    CHECK(pool.traits().size() >= 18);
    CHECK(pool.traits().size() <= 22);
    for (const Trait& t : pool.traits()) {
        CHECK_FALSE(t.name.empty());
        CHECK_FALSE(t.description.empty());
        CHECK(t.description.find("\xE2\x80\x94") == std::string::npos);
    }
}

TEST_CASE("assignment is deterministic and independent of call order")
{
    const TraitPool pool = shippedPool();
    const NpcTraits a = assignTraits(42, 7, pool);
    for (std::uint64_t other = 0; other < 50; ++other) {
        (void)assignTraits(42, other, pool);
    }
    const NpcTraits b = assignTraits(42, 7, pool);
    CHECK(a.ids == b.ids);
    CHECK(a.wanted == b.wanted);
}

TEST_CASE("the seed and the npc id both change the result")
{
    const TraitPool pool = shippedPool();
    bool seedMatters = false;
    bool idMatters = false;
    for (std::uint64_t i = 0; i < 40; ++i) {
        seedMatters |= assignTraits(1, i, pool).ids != assignTraits(2, i, pool).ids;
        idMatters |= assignTraits(1, i, pool).ids != assignTraits(1, i + 1, pool).ids;
    }
    CHECK(seedMatters);
    CHECK(idMatters);
}

TEST_CASE("assignment draws two or three distinct compatible traits")
{
    const TraitPool pool = shippedPool();
    std::map<std::size_t, int> counts;
    for (std::uint64_t id = 0; id < 5000; ++id) {
        const NpcTraits result = assignTraits(99, id, pool);
        REQUIRE(result.complete());
        REQUIRE(result.ids.size() >= kMinNpcTraits);
        REQUIRE(result.ids.size() <= kMaxNpcTraits);
        ++counts[result.ids.size()];

        std::vector<std::size_t> indices;
        for (const std::string& traitId : result.ids) {
            const auto it = std::find_if(pool.traits().begin(), pool.traits().end(), [&](const Trait& t) { return t.id == traitId; });
            REQUIRE(it != pool.traits().end());
            indices.push_back(static_cast<std::size_t>(it - pool.traits().begin()));
        }
        for (std::size_t i = 0; i < indices.size(); ++i) {
            for (std::size_t j = i + 1; j < indices.size(); ++j) {
                REQUIRE(indices[i] != indices[j]);
                REQUIRE_FALSE(pool.incompatible(indices[i], indices[j]));
            }
        }
    }
    CHECK(counts[2] > 1500);
    CHECK(counts[3] > 1500);
}

TEST_CASE("over many npcs every trait is used and none dominates")
{
    const TraitPool pool = shippedPool();
    constexpr int npcs = 20000;
    std::map<std::string, int> used;
    int total = 0;
    for (std::uint64_t id = 0; id < npcs; ++id) {
        for (const std::string& traitId : assignTraits(2024, id, pool).ids) {
            ++used[traitId];
            ++total;
        }
    }
    const double mean = static_cast<double>(total) / static_cast<double>(pool.traits().size());
    for (const Trait& t : pool.traits()) {
        INFO(t.id << " used " << used[t.id] << " times, mean " << mean);
        CHECK(used[t.id] > 0);
        // Each trait lands within 0.6x to 1.5x of an even share. Conflicts skew
        // traits with many incompatibilities, so the band is not tight.
        CHECK(used[t.id] > mean * 0.6);
        CHECK(used[t.id] < mean * 1.5);
    }
}

TEST_CASE("npc id extremes are handled")
{
    const TraitPool pool = shippedPool();
    for (const std::uint64_t id : { std::uint64_t { 0 }, std::numeric_limits<std::uint64_t>::max() }) {
        const NpcTraits result = assignTraits(0, id, pool);
        CHECK(result.complete());
        CHECK(result.ids == assignTraits(0, id, pool).ids);
    }
    CHECK(assignTraits(std::numeric_limits<std::uint64_t>::max(), 0, pool).complete());
}

TEST_CASE("a pool where every pair conflicts terminates and says it is incomplete")
{
    std::string error;
    auto pool = TraitPool::fromTraits({ trait("a", { "b", "c" }), trait("b", { "a", "c" }), trait("c", { "a", "b" }) }, error);
    REQUIRE(pool.has_value());
    for (std::uint64_t id = 0; id < 100; ++id) {
        const NpcTraits result = assignTraits(5, id, *pool);
        CHECK(result.ids.size() == 1);
        CHECK_FALSE(result.complete());
    }
}

TEST_CASE("a pool that can place only two of three reports it as incomplete")
{
    std::string error;
    auto pool = TraitPool::fromTraits({ trait("a", { "c" }), trait("b", { "c" }), trait("c", { "a", "b" }) }, error);
    REQUIRE(pool.has_value());
    int wantedThree = 0;
    for (std::uint64_t id = 0; id < 200; ++id) {
        const NpcTraits result = assignTraits(5, id, *pool);
        if (result.wanted == 3) {
            ++wantedThree;
            CHECK(result.ids.size() < 3);
            CHECK_FALSE(result.complete());
        }
    }
    CHECK(wantedThree > 0);
}

TEST_CASE("golden: known inputs give known traits")
{
    const TraitPool pool = shippedPool();
    CHECK(assignTraits(42, 7, pool).ids == std::vector<std::string> { "loyal", "gossipy" });
    CHECK(assignTraits(0, 0, pool).ids == std::vector<std::string> { "greedy", "secretive" });
}

TEST_CASE("an invalid pool is refused with a reason")
{
    std::string error;

    CHECK_FALSE(TraitPool::fromTraits({}, error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait("a"), trait("b") }, error).has_value());
    CHECK(error.find("at least") != std::string::npos);

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait("a"), trait("b"), trait("a") }, error).has_value());
    CHECK(error.find("duplicate") != std::string::npos);

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait("a", { "b" }), trait("b"), trait("c") }, error).has_value());
    CHECK(error.find("symmetric") != std::string::npos);

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait("a", { "ghost" }), trait("b"), trait("c") }, error).has_value());
    CHECK(error.find("ghost") != std::string::npos);

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait("a", { "a" }), trait("b"), trait("c") }, error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(TraitPool::fromTraits({ trait(""), trait("b"), trait("c") }, error).has_value());
    CHECK_FALSE(error.empty());
}

TEST_CASE("bad JSON shapes are refused")
{
    std::string error;
    CHECK_FALSE(poolFromJson(R"([{"id": "a", "name": 5, "description": "x", "incompatible": []}])", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(poolFromJson(R"([{"id": "a", "name": "A", "description": "x", "incompatible": "b"}])", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(poolFromJson(R"([{"id": "a", "name": "A"}])", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(poolFromJson(R"([{"id": "a", "name": "A", "description": "x", "incompatible": []}, {"id": "a", "name": "A", "description": "x", "incompatible": []}])", error).has_value());
    CHECK_FALSE(error.empty());

    error.clear();
    CHECK_FALSE(poolFromJson(R"([])", error).has_value());
    CHECK_FALSE(error.empty());
}
