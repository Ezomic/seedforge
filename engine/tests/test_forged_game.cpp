#include "ichor/world/forge_job.h"
#include "ichor/world/forged_game.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace ichor;

namespace {

constexpr int kGrid = 64;

WorldDesc descFor(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

std::unique_ptr<ForgedGame> gameFor(std::uint64_t seed)
{
    ForgeJob job(descFor(seed), kGrid);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(job.error());
    REQUIRE(job.state() == ForgeJobState::Done);
    auto game = buildForgedGame(*job.result(), descFor(seed));
    REQUIRE(game->ok());
    return game;
}

Heightfield flatWorld(float heightMetres)
{
    WorldDesc desc;
    desc.sizeMetres = 1024;
    desc.cellMetres = 256;
    desc.minHeight = 0.0f;
    desc.heightRange = 400.0f;
    desc.waterHeight = 10.0f;
    const auto sample = static_cast<std::uint16_t>(std::lround(heightMetres / desc.heightRange * 65535.0f));
    return Heightfield(desc, std::vector<std::uint16_t>(static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide(), sample));
}

}

TEST_CASE("the same seed builds the same forged game and another seed a different one", "[forged_game]")
{
    const auto a = gameFor(1);
    const auto b = gameFor(1);
    const auto c = gameFor(2);
    CHECK(a->stateHash() == b->stateHash());
    CHECK(a->stateHash() != c->stateHash());
    CHECK(a->heightfield.digest() == b->heightfield.digest());
    CHECK(a->heightfield.digest() != c->heightfield.digest());
}

TEST_CASE("the spawn is on dry ground that is not steep", "[forged_game]")
{
    for (const std::uint64_t seed : {1u, 2u, 5u, 9u}) {
        const auto game = gameFor(seed);
        const ForgedSpawn& spawn = game->spawn;
        INFO("seed " << seed);
        REQUIRE(spawn.found);
        CHECK(game->heightfield.heightAt(spawn.x, spawn.z) > game->desc.waterLevel());
        CHECK(game->heightfield.normalAt(spawn.x, spawn.z).y >= ForgedSpawnParams{}.minUpY);
        CHECK(spawn.groundY == game->heightfield.heightAt(spawn.x, spawn.z));
    }
}

TEST_CASE("the spawn search starts at the settlement nearest the centre", "[forged_game]")
{
    const Heightfield flat = flatWorld(100.0f);
    const ForgedSpawn centre = findForgedSpawn(flat, nullptr, {});
    REQUIRE(centre.found);
    CHECK(centre.x == 512.0);
    CHECK(centre.z == 512.0);

    const ForgedSpawn near = findForgedSpawn(flat, nullptr, {{100.0, 100.0}, {600.0, 560.0}, {900.0, 900.0}});
    REQUIRE(near.found);
    CHECK(near.x == 600.0);
    CHECK(near.z == 560.0);
}

TEST_CASE("the spawn skips ground that is wet, steep or blocked", "[forged_game]")
{
    // A world that is all below the water level has no dry ground: the search ends at the centre, not found.
    const ForgedSpawn drowned = findForgedSpawn(flatWorld(5.0f), nullptr, {});
    CHECK_FALSE(drowned.found);
    CHECK(drowned.x == 512.0);

    const Heightfield flat = flatWorld(100.0f);
    const ForgedSpawn moved = findForgedSpawn(flat, nullptr, {}, [](double x, double z) { return std::abs(x - 512.0) < 20.0 && std::abs(z - 512.0) < 20.0; });
    REQUIRE(moved.found);
    CHECK(std::hypot(moved.x - 512.0, moved.z - 512.0) >= 20.0);
    CHECK(std::hypot(moved.x - 512.0, moved.z - 512.0) < 40.0);

    WorldDesc desc = flat.desc();
    std::vector<std::uint16_t> ramp(static_cast<std::size_t>(desc.samplesPerSide()) * desc.samplesPerSide());
    for (std::uint32_t z = 0; z < desc.samplesPerSide(); ++z) {
        for (std::uint32_t x = 0; x < desc.samplesPerSide(); ++x) {
            // 1:1 slope in the middle band, flat plateaus either side.
            const double ground = x < 100 ? 100.0 : x > 156 ? 100.0 + 56.0 * 4.0 : 100.0 + (x - 100) * 4.0;
            ramp[static_cast<std::size_t>(z) * desc.samplesPerSide() + x] = static_cast<std::uint16_t>(std::lround(ground / desc.heightRange * 65535.0));
        }
    }
    const Heightfield hill(desc, std::move(ramp));
    const ForgedSpawn onHill = findForgedSpawn(hill, nullptr, {{512.0, 512.0}});
    REQUIRE(onHill.found);
    CHECK(hill.normalAt(onHill.x, onHill.z).y >= ForgedSpawnParams{}.minUpY);
}

TEST_CASE("a spawn search over empty or bad input finds nothing and does not crash", "[forged_game]")
{
    CHECK_FALSE(findForgedSpawn(Heightfield{}, nullptr, {}).found);
    ForgedSpawnParams bad;
    bad.searchStepMetres = 0.0f;
    CHECK_FALSE(findForgedSpawn(flatWorld(100.0f), nullptr, {}, {}, bad).found);
    const double nan = std::nan("");
    CHECK(findForgedSpawn(flatWorld(100.0f), nullptr, {{nan, nan}}).found);
}

TEST_CASE("an empty forge result is refused with a message", "[forged_game]")
{
    const ForgeJobResult empty;
    const auto game = buildForgedGame(empty, descFor(1));
    CHECK_FALSE(game->ok());
}
