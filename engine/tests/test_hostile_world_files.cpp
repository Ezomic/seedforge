#include "codec_fuzz.h"
#include "hostile_text.h"

#include "ichor/sim/scene.h"
#include "ichor/sim/scene_io.h"
#include "ichor/world/forge.h"
#include "ichor/world/hydrology.h"
#include "ichor/world/hydrology_cache.h"
#include "ichor/world/world_io.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

using namespace ichor;
using ichor::fuzz::Bytes;
using ichor::fuzz::Canonical;
using ichor::fuzz::Options;

namespace {

namespace fs = std::filesystem;

struct ScratchDir {
    fs::path path;
    explicit ScratchDir(const std::string& tag) : path(hostile::uniqueDir(tag)) {}
    ~ScratchDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

Bytes slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const fs::path& path, std::span<const std::uint8_t> bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

WorldDesc smallWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

void expectClean(const ichor::fuzz::Report& report)
{
    INFO(ichor::fuzz::summary(report));
    CHECK(report.cases > 100);
    CHECK(report.rejected > 0);
    CHECK(report.ok());
}

}

TEST_CASE("hostile: world manifest through readWorld", "[hostile][world_io]")
{
    ScratchDir dir("world_manifest");
    const Heightfield field = forgeHeightfield(smallWorld(11));
    REQUIRE(writeWorld(dir.path, field));
    const fs::path manifest = dir.path / "manifest.iworld";
    const Bytes valid = slurp(manifest);
    const Bytes heights = slurp(dir.path / "heightmap.r16");

    expectClean(ichor::fuzz::fuzzCodec(valid, [&](std::span<const std::uint8_t> b) -> Canonical {
        spit(manifest, b);
        Heightfield read;
        if (!readWorld(dir.path, read)) return std::nullopt;
        if (read.digest() != field.digest()) return Bytes{'d', 'i', 'f', 'f'};
        return Bytes(b.begin(), b.end());
    }));
    CHECK(slurp(dir.path / "heightmap.r16") == heights);
}

TEST_CASE("hostile: world heightmap through readWorld", "[hostile][world_io]")
{
    ScratchDir dir("world_heights");
    const Heightfield field = forgeHeightfield(smallWorld(12));
    REQUIRE(writeWorld(dir.path, field));
    const fs::path heightmap = dir.path / "heightmap.r16";
    const Bytes valid = slurp(heightmap);

    Options options;
    options.window = 64;
    options.randomMutations = 300;
    expectClean(ichor::fuzz::fuzzCodec(valid, [&](std::span<const std::uint8_t> b) -> Canonical {
        spit(heightmap, b);
        Heightfield read;
        if (!readWorld(dir.path, read)) return std::nullopt;
        return Bytes(b.begin(), b.end());
    }, options));
}

TEST_CASE("hostile: hydrology cache", "[hostile][hydrology_cache]")
{
    const Heightfield field = forgeHeightfield(smallWorld(7));
    const Hydrology computed = computeHydrology(field);
    REQUIRE_FALSE(computed.empty());
    const Bytes valid = encodeHydrologyCache(field, computed);

    Options options;
    options.window = 96;
    options.randomMutations = 800;
    expectClean(ichor::fuzz::fuzzCodec(valid, [&](std::span<const std::uint8_t> b) -> Canonical {
        Hydrology out;
        if (!decodeHydrologyCache(Bytes(b.begin(), b.end()), field, out)) return std::nullopt;
        return encodeHydrologyCache(field, out);
    }, options));
}

TEST_CASE("hostile: scene snapshot through loadScene", "[hostile][scene_io]")
{
    ScratchDir dir("scene");
    const fs::path file = dir.path / "scene.isav";
    Scene scene(9);
    SpawnCommand ground;
    ground.id = StableId::of("ground");
    ground.name.value = "ground";
    ground.transform.position = {0.0, -0.5, 0.0};
    ground.staticBox = StaticBox{glm::vec3(20.0f, 0.5f, 20.0f)};
    scene.enqueue(ground);
    SpawnCommand hero;
    hero.id = StableId::of("hero");
    hero.name.value = "hero";
    hero.transform.position = {1.0, 1.0, 2.0};
    hero.player = Player{4.0f};
    hero.character = CharacterBody{0.3f, 0.6f};
    scene.enqueue(hero);
    scene.tick(1.0 / 60.0);
    REQUIRE(saveScene(file, scene));
    const Bytes valid = slurp(file);

    Options options;
    options.window = 96;
    options.randomMutations = 300;
    options.allowNonCanonical = true;
    expectClean(ichor::fuzz::fuzzCodec(valid, [&](std::span<const std::uint8_t> b) -> Canonical {
        spit(file, b);
        Scene target(9);
        if (!loadScene(file, target)) return std::nullopt;
        const fs::path again = dir.path / "again.isav";
        if (!saveScene(again, target)) return Bytes{};
        return slurp(again);
    }, options));
}
