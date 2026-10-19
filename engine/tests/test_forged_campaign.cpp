#include "ichor/save/campaign.h"
#include "ichor/world/forged_campaign.h"
#include "test_temp.h"

#include <worldforge/forge_world.h>
#include <worldforge/gen_version.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;

namespace {

constexpr int kGrid = 64;

WorldDesc smallWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    desc.sizeMetres = 512;
    desc.metresPerSample = 8;
    desc.cellMetres = 256;
    return desc;
}

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_forged_campaign");
    Scratch()
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~Scratch()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> slurp(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void spit(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

CachedForgedWorld forgeFresh(const WorldDesc& desc)
{
    const worldforge::ForgedWorld forged = worldforge::forgeWorld(desc.seed, kGrid);
    REQUIRE(forged.complete());
    ForgedHeightfield adapted = heightfieldFromForgedWorld(forged, forgedWorldDesc(desc));
    REQUIRE(adapted.ok());
    CachedForgedWorld world;
    world.worldHash = forged.worldHash();
    world.heightfield = std::move(adapted.field);
    world.manifest = *forged.manifest();
    return world;
}

CachedForgedWorld withGenVersion(CachedForgedWorld world, std::uint32_t genVersion)
{
    world.manifest.genVersion = genVersion;
    return world;
}

bool sameWorld(const CachedForgedWorld& a, const CachedForgedWorld& b)
{
    return a.worldHash == b.worldHash && a.heightfield.desc().seed == b.heightfield.desc().seed && a.heightfield.samples() == b.heightfield.samples()
        && worldforge::encodeManifest(a.manifest) == worldforge::encodeManifest(b.manifest);
}

ForgedWorldKey keyFor(const WorldDesc& desc)
{
    ForgedWorldKey key = forgedWorldKey(desc, kGrid);
    key.configHash = 0x1234abcdULL;
    return key;
}

std::filesystem::path blobPath(const Scratch& dir, const char* campaign)
{
    return dir.path / campaign / "world" / kForgedWorldBlob;
}

}

TEST_CASE("a saved forged world loads back equal with the key it was made under", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    const CachedForgedWorld world = forgeFresh(desc);
    const ForgedWorldKey key = keyFor(desc);
    REQUIRE(saveForgedWorld(dir.path, "alpha", key, world) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "alpha");
    REQUIRE(loaded.ok());
    CHECK_FALSE(loaded.olderGenVersion);
    CHECK(loaded.key == key);
    CHECK(sameWorld(loaded.world, world));
    CHECK(loaded.world.heightfield.digest() == world.heightfield.digest());

    const auto campaign = Campaign::open(dir.path, "alpha");
    REQUIRE(campaign);
    CHECK(campaign->manifest().worldSeed == desc.seed);
    CHECK(campaign->manifest().genVersion == key.genVersion);
    CHECK(campaign->verifyWorld());
}

TEST_CASE("save, load and save again is byte identical, files and manifest", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(42);
    REQUIRE(saveForgedWorld(dir.path, "first", keyFor(desc), forgeFresh(desc)) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "first");
    REQUIRE(loaded.ok());
    REQUIRE(saveForgedWorld(dir.path, "second", loaded.key, loaded.world) == CampaignStatus::Ok);

    const std::vector<std::uint8_t> a = slurp(blobPath(dir, "first"));
    REQUIRE(a.size() > 1000);
    CHECK(a == slurp(blobPath(dir, "second")));
    CHECK(slurp(dir.path / "first" / "world" / Campaign::kManifestFile) == slurp(dir.path / "second" / "world" / Campaign::kManifestFile));
}

TEST_CASE("the stored world is what loads, not today's generator output", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    CachedForgedWorld world = forgeFresh(desc);
    std::vector<std::uint16_t> samples = world.heightfield.samples();
    for (std::uint16_t& s : samples) {
        s = static_cast<std::uint16_t>(s ^ 0x5u);
    }
    world.heightfield = Heightfield(world.heightfield.desc(), std::move(samples));
    REQUIRE(saveForgedWorld(dir.path, "edited", keyFor(desc), world) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "edited");
    REQUIRE(loaded.ok());
    CHECK(loaded.world.heightfield.samples() == world.heightfield.samples());
    CHECK(loaded.world.heightfield.samples() != forgeFresh(desc).heightfield.samples());
}

TEST_CASE("a flipped byte anywhere in the world blob or the manifest is rejected", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    REQUIRE(saveForgedWorld(dir.path, "flip", keyFor(desc), forgeFresh(desc)) == CampaignStatus::Ok);
    const std::filesystem::path blob = blobPath(dir, "flip");
    const std::filesystem::path manifest = dir.path / "flip" / "world" / Campaign::kManifestFile;
    const std::vector<std::uint8_t> blobBytes = slurp(blob);
    const std::vector<std::uint8_t> manifestBytes = slurp(manifest);

    const std::size_t step = blobBytes.size() / 97 + 1;
    for (std::size_t at = 0; at < blobBytes.size(); at += step) {
        std::vector<std::uint8_t> bad = blobBytes;
        bad[at] ^= 0x01;
        spit(blob, bad);
        REQUIRE(loadForgedWorld(dir.path, "flip").status == ForgedLoadStatus::Corrupt);
    }
    spit(blob, blobBytes);
    REQUIRE(loadForgedWorld(dir.path, "flip").ok());

    for (std::size_t at = 0; at < manifestBytes.size(); ++at) {
        std::vector<std::uint8_t> bad = manifestBytes;
        bad[at] ^= 0x01;
        spit(manifest, bad);
        REQUIRE_FALSE(loadForgedWorld(dir.path, "flip").ok());
    }
}

TEST_CASE("an older genVersion still loads its stored world and says so", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    const std::uint32_t older = worldforge::kGenVersion - 1;
    ForgedWorldKey key = keyFor(desc);
    key.genVersion = older;
    const CachedForgedWorld world = withGenVersion(forgeFresh(desc), older);
    REQUIRE(saveForgedWorld(dir.path, "old", key, world) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "old");
    REQUIRE(loaded.ok());
    CHECK(loaded.olderGenVersion);
    CHECK(loaded.key.genVersion == older);
    CHECK(sameWorld(loaded.world, world));
}

TEST_CASE("a newer genVersion is refused clearly, not loaded", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    const std::uint32_t newer = worldforge::kGenVersion + 1;
    ForgedWorldKey key = keyFor(desc);
    key.genVersion = newer;
    REQUIRE(saveForgedWorld(dir.path, "new", key, withGenVersion(forgeFresh(desc), newer)) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "new");
    CHECK(loaded.status == ForgedLoadStatus::NewerGenVersion);
    CHECK(loaded.world.heightfield.empty());
}

TEST_CASE("a blob stamped for another seed or genVersion than the campaign manifest is corrupt", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    REQUIRE(saveForgedWorld(dir.path, "src", keyFor(desc), forgeFresh(desc)) == CampaignStatus::Ok);
    const std::vector<std::uint8_t> blob = slurp(blobPath(dir, "src"));

    const WorldBlob blobs[] = {{kForgedWorldBlob, blob}};
    REQUIRE(Campaign::create(dir.path, "wrongseed", desc.seed + 1, worldforge::kGenVersion, blobs) == CampaignStatus::Ok);
    CHECK(loadForgedWorld(dir.path, "wrongseed").status == ForgedLoadStatus::Corrupt);
    REQUIRE(Campaign::create(dir.path, "wronggen", desc.seed, worldforge::kGenVersion - 1, blobs) == CampaignStatus::Ok);
    CHECK(loadForgedWorld(dir.path, "wronggen").status == ForgedLoadStatus::Corrupt);
}

TEST_CASE("hostile bytes in a well-formed campaign never crash the load", "[forged_campaign]")
{
    Scratch dir;
    const WorldDesc desc = smallWorld(7);
    REQUIRE(saveForgedWorld(dir.path, "src", keyFor(desc), forgeFresh(desc)) == CampaignStatus::Ok);
    const std::vector<std::uint8_t> good = slurp(blobPath(dir, "src"));

    std::vector<std::vector<std::uint8_t>> hostile;
    hostile.push_back({});
    hostile.push_back({0});
    hostile.push_back(std::vector<std::uint8_t>(64, 0xff));
    hostile.push_back(std::vector<std::uint8_t>(good.begin(), good.begin() + 40));
    hostile.push_back(std::vector<std::uint8_t>(good.begin(), good.end() - 1));
    std::vector<std::uint8_t> extended = good;
    extended.push_back(0);
    hostile.push_back(extended);
    std::uint64_t state = 99;
    std::vector<std::uint8_t> noise(4096);
    for (std::uint8_t& b : noise) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        b = static_cast<std::uint8_t>(state >> 56);
    }
    hostile.push_back(noise);
    std::vector<std::uint8_t> headerNoise = good;
    for (std::size_t i = 4; i < 64; ++i) {
        headerNoise[i] = 0xff;
    }
    hostile.push_back(headerNoise);

    int n = 0;
    for (const auto& bytes : hostile) {
        const WorldBlob blobs[] = {{kForgedWorldBlob, bytes}};
        const std::string name = "hostile" + std::to_string(n++);
        REQUIRE(Campaign::create(dir.path, name, desc.seed, worldforge::kGenVersion, blobs) == CampaignStatus::Ok);
        CHECK(loadForgedWorld(dir.path, name).status == ForgedLoadStatus::Corrupt);
        ForgedWorldKey key;
        CachedForgedWorld out;
        CHECK_FALSE(decodeStoredForgedWorld(bytes, key, out));
        CHECK(out.heightfield.empty());
    }
}

TEST_CASE("missing, interrupted and unnamed campaigns report why", "[forged_campaign]")
{
    Scratch dir;
    CHECK(loadForgedWorld(dir.path, "nope").status == ForgedLoadStatus::NoSuchCampaign);
    CHECK(loadForgedWorld(dir.path, "../escape").status == ForgedLoadStatus::NoSuchCampaign);

    std::filesystem::create_directories(dir.path / "half" / "world");
    spit(dir.path / "half" / "world" / kForgedWorldBlob, {1, 2, 3});
    CHECK(loadForgedWorld(dir.path, "half").status == ForgedLoadStatus::Incomplete);

    const WorldDesc desc = smallWorld(7);
    const CachedForgedWorld world = forgeFresh(desc);
    REQUIRE(saveForgedWorld(dir.path, "dup", keyFor(desc), world) == CampaignStatus::Ok);
    CHECK(saveForgedWorld(dir.path, "dup", keyFor(desc), world) == CampaignStatus::AlreadyExists);
    CHECK(saveForgedWorld(dir.path, "Bad Name", keyFor(desc), world) == CampaignStatus::BadName);
}

TEST_CASE("peeking a key from short or foreign bytes yields nothing", "[forged_campaign]")
{
    CHECK_FALSE(peekForgedWorldKey({}));
    CHECK_FALSE(peekForgedWorldKey(std::vector<std::uint8_t>(10, 0)));
    CHECK_FALSE(peekForgedWorldKey(std::vector<std::uint8_t>(200, 0x41)));
}
