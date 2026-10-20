#include "ichor/core/hash.h"
#include "ichor/world/forge_job.h"
#include "ichor/world/forged_campaign_hook.h"
#include "ichor/world/forged_phase_maps.h"
#include "test_temp.h"

#include <worldforge/gen_version.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;
namespace wf = worldforge;

namespace {

constexpr int kGrid = 64;

WorldDesc requestedWorld(std::uint64_t seed)
{
    WorldDesc desc;
    desc.seed = seed;
    return desc;
}

std::unique_ptr<ForgedGame> forgeGameFor(std::uint64_t seed)
{
    ForgeJob job(requestedWorld(seed), kGrid);
    REQUIRE(job.start());
    for (int i = 0; i < 12000 && job.state() == ForgeJobState::Running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    INFO(job.error());
    REQUIRE(job.state() == ForgeJobState::Done);
    auto game = buildForgedGame(*job.result(), requestedWorld(seed));
    REQUIRE(game->ok());
    return game;
}

const ForgedGame& sharedGame()
{
    static const std::unique_ptr<ForgedGame> game = forgeGameFor(1);
    return *game;
}

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_forged_phase_maps");
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

std::filesystem::path blobPath(const Scratch& dir, const char* campaign, const char* blob)
{
    return dir.path / campaign / "world" / blob;
}

// Recomputes the trailing hash, so a test can alter the body and still reach the checks behind the checksum.
std::vector<std::uint8_t> resealed(std::vector<std::uint8_t> bytes)
{
    if (bytes.size() < 8) {
        return bytes;
    }
    const std::size_t body = bytes.size() - 8;
    const std::uint64_t sum = hash64(std::string_view(reinterpret_cast<const char*>(bytes.data()), body));
    std::memcpy(bytes.data() + body, &sum, 8);
    return bytes;
}

CachedForgedWorld cachedFrom(const ForgedGame& game, const wf::ForgedWorld& world)
{
    CachedForgedWorld cached;
    cached.worldHash = world.worldHash();
    cached.heightfield = game.heightfield;
    cached.manifest = *world.manifest();
    return cached;
}

wf::detail::ForgedParts partsOf(const wf::ForgedWorld& world)
{
    wf::detail::ForgedParts p;
    p.seed = world.seed();
    p.size = world.size();
    p.stopAfter = wf::ForgePhase::Content;
    p.completed = wf::kForgePhaseCount;
    p.phaseHashes = world.phaseHashes();
    p.hydrology = world.hydrology();
    p.biomes = world.biomes();
    p.settlements = world.settlements();
    p.roads = world.roads();
    p.politics = world.politics();
    p.pois = world.pois();
    p.landmarkCoverage = world.landmarkCoverage();
    p.manifest = world.manifest();
    return p;
}

// A world whose phase hashes are made consistent under `version`: what a forge of that version would have produced,
// or an attacker who recomputed every hash after altering a map.
wf::ForgedWorld sealed(wf::detail::ForgedParts p, std::uint32_t version)
{
    p.manifest->genVersion = version;
    p.phaseHashes[2] = wf::hashHydrologyMap(*p.hydrology);
    p.phaseHashes[4] = wf::hashBiomeMap(*p.biomes);
    p.phaseHashes[6] = wf::hashSettlementMap(*p.settlements);
    p.phaseHashes[7] = wf::hashRoadMap(*p.roads);
    p.phaseHashes[8] = wf::hashPoliticsMap(*p.politics);
    p.phaseHashes[9] = wf::splitmix64(wf::hashPoiMap(*p.pois) ^ wf::splitmix64(wf::hashLandmarkCoverage(*p.landmarkCoverage)));
    p.phaseHashes[10] = wf::hashManifest(*p.manifest);
    p.worldHash = chainForgeHashAt(p.seed, version, p.size, p.phaseHashes, &p.chain);
    return wf::ForgedWorld(std::move(p));
}

ForgedWorldKey keyAt(std::uint32_t version)
{
    ForgedWorldKey key = forgedWorldKey(requestedWorld(1), kGrid);
    key.genVersion = version;
    return key;
}

}

TEST_CASE("the chain at the current genVersion is the forge's own world hash", "[forged_phase_maps]")
{
    const wf::ForgedWorld& world = *sharedGame().world;
    std::array<std::uint64_t, wf::kForgePhaseCount> chain{};
    CHECK(chainForgeHashAt(world.seed(), wf::kGenVersion, world.size(), world.phaseHashes(), &chain) == world.worldHash());
    CHECK(chain == world.chain());
    CHECK(chainForgeHashAt(world.seed(), wf::kGenVersion - 1, world.size(), world.phaseHashes()) != world.worldHash());
}

TEST_CASE("a saved campaign loads from its stored files with no forge and the same world", "[forged_phase_maps]")
{
    Scratch dir;
    const ForgedGame& game = sharedGame();
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, game) == CampaignStatus::Ok);

    const std::uint64_t forgesBefore = ForgeJob::forgesStarted();
    const CampaignWorldPlan plan = planCampaignWorld(dir.path, "alpha");
    REQUIRE(plan.action == CampaignWorldAction::Load);
    CHECK_FALSE(plan.warn);
    REQUIRE(plan.forged);
    const std::unique_ptr<ForgedGame> loaded = buildStoredCampaignGame(plan, requestedWorld(plan.key.seed));
    INFO(loaded->error);
    REQUIRE(loaded->ok());
    CHECK(ForgeJob::forgesStarted() == forgesBefore);

    CHECK(loaded->world->worldHash() == game.world->worldHash());
    CHECK(loaded->stateHash() == game.stateHash());
    CHECK(loaded->heightfield.samples() == game.heightfield.samples());
    CHECK(loaded->roads->segmentCount() == game.roads->segmentCount());
    CHECK(loaded->pois->all().size() == game.pois->all().size());
    CHECK(loaded->buildings->all().size() == game.buildings->all().size());
    CHECK(loaded->tiers.tiers == game.tiers.tiers);
    CHECK(loaded->spawn.x == game.spawn.x);
    CHECK(loaded->spawn.z == game.spawn.z);
}

TEST_CASE("the load reads only the stored files, not today's generator", "[forged_phase_maps]")
{
    Scratch dir;
    const ForgedGame& game = sharedGame();
    // A world no forge of seed 1 would make: another seed's phase maps and hashes stored under seed 1's key would not
    // verify, so alter a map and seal it, and check the load gives that map back.
    wf::detail::ForgedParts parts = partsOf(*game.world);
    REQUIRE_FALSE(parts.settlements->sites.empty());
    parts.settlements->sites.back().score ^= 1;
    const wf::ForgedWorld altered = sealed(std::move(parts), wf::kGenVersion);
    REQUIRE(altered.worldHash() != game.world->worldHash());
    REQUIRE(saveForgedWorld(dir.path, "alpha", keyAt(wf::kGenVersion), cachedFrom(game, altered), &altered) == CampaignStatus::Ok);

    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "alpha");
    REQUIRE(loaded.ok());
    REQUIRE(loaded.forged);
    CHECK(loaded.forged->worldHash() == altered.worldHash());
    CHECK(loaded.forged->settlements()->sites.back().score == altered.settlements()->sites.back().score);
}

TEST_CASE("save, load and save again is byte identical, blobs and manifest", "[forged_phase_maps]")
{
    Scratch dir;
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, sharedGame()) == CampaignStatus::Ok);
    const LoadedCampaignWorld loaded = loadForgedWorld(dir.path, "alpha");
    REQUIRE(loaded.ok());
    REQUIRE(loaded.forged);
    REQUIRE(saveForgedWorld(dir.path, "beta", loaded.key, loaded.world, loaded.forged.get()) == CampaignStatus::Ok);

    for (const char* file : {kForgedWorldBlob, kForgedPhasesBlob, "manifest.bin"}) {
        const std::vector<std::uint8_t> first = slurp(blobPath(dir, "alpha", file));
        REQUIRE_FALSE(first.empty());
        CHECK(slurp(blobPath(dir, "beta", file)) == first);
    }
}

TEST_CASE("a flipped byte in the phase blob is rejected, in the campaign and in the decoder", "[forged_phase_maps]")
{
    Scratch dir;
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, sharedGame()) == CampaignStatus::Ok);
    const std::filesystem::path blob = blobPath(dir, "alpha", kForgedPhasesBlob);
    const std::vector<std::uint8_t> good = slurp(blob);
    REQUIRE(good.size() > 1000);

    const LoadedCampaignWorld intact = loadForgedWorld(dir.path, "alpha");
    REQUIRE(intact.ok());
    REQUIRE(decodeForgedPhaseMaps(good, intact.key, intact.world));

    const std::size_t step = good.size() / 97 + 1;
    for (std::size_t at = 0; at < good.size(); at += step) {
        std::vector<std::uint8_t> flipped = good;
        flipped[at] ^= 0x04;
        CHECK_FALSE(decodeForgedPhaseMaps(flipped, intact.key, intact.world));
        spit(blob, flipped);
        const CampaignWorldPlan plan = planCampaignWorld(dir.path, "alpha");
        CHECK(plan.action == CampaignWorldAction::Refuse);
        CHECK(plan.message.find("damaged") != std::string::npos);
    }
    std::vector<std::uint8_t> last = good;
    last.back() ^= 0x80;
    CHECK_FALSE(decodeForgedPhaseMaps(last, intact.key, intact.world));
}

TEST_CASE("a flipped byte behind a recomputed checksum is still rejected by the hashes", "[forged_phase_maps]")
{
    Scratch dir;
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, sharedGame()) == CampaignStatus::Ok);
    const LoadedCampaignWorld intact = loadForgedWorld(dir.path, "alpha");
    REQUIRE(intact.ok());
    const std::vector<std::uint8_t> good = encodeForgedPhaseMaps(intact.key, *intact.forged);
    const std::size_t body = good.size() - 8;
    const std::size_t step = body / 211 + 1;
    for (std::size_t at = 0; at < body; at += step) {
        std::vector<std::uint8_t> flipped = good;
        flipped[at] ^= 0x01;
        CHECK_FALSE(decodeForgedPhaseMaps(resealed(std::move(flipped)), intact.key, intact.world));
    }
}

TEST_CASE("truncated, empty and garbage phase blobs never crash and are never accepted", "[forged_phase_maps]")
{
    Scratch dir;
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, sharedGame()) == CampaignStatus::Ok);
    const LoadedCampaignWorld intact = loadForgedWorld(dir.path, "alpha");
    REQUIRE(intact.ok());
    const std::vector<std::uint8_t> good = encodeForgedPhaseMaps(intact.key, *intact.forged);

    CHECK_FALSE(decodeForgedPhaseMaps({}, intact.key, intact.world));
    const std::size_t step = good.size() / 151 + 1;
    for (std::size_t length = 0; length < good.size(); length += step) {
        const std::vector<std::uint8_t> cut(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
        CHECK_FALSE(decodeForgedPhaseMaps(cut, intact.key, intact.world));
        CHECK_FALSE(decodeForgedPhaseMaps(resealed(cut), intact.key, intact.world));
    }
    std::vector<std::uint8_t> longer = good;
    longer.push_back(0);
    CHECK_FALSE(decodeForgedPhaseMaps(longer, intact.key, intact.world));
    CHECK_FALSE(decodeForgedPhaseMaps(resealed(longer), intact.key, intact.world));

    for (const std::uint8_t fill : std::initializer_list<std::uint8_t>{0x00, 0xff, 0x7f, 0x41}) {
        std::vector<std::uint8_t> garbage(good.size(), fill);
        CHECK_FALSE(decodeForgedPhaseMaps(garbage, intact.key, intact.world));
        CHECK_FALSE(decodeForgedPhaseMaps(resealed(garbage), intact.key, intact.world));
    }
    std::uint64_t state = 12345;
    std::vector<std::uint8_t> noise(good.size());
    for (std::uint8_t& byte : noise) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        byte = static_cast<std::uint8_t>(state >> 56);
    }
    CHECK_FALSE(decodeForgedPhaseMaps(noise, intact.key, intact.world));
    CHECK_FALSE(decodeForgedPhaseMaps(resealed(noise), intact.key, intact.world));

    // A header that is right and a body that claims huge arrays must fail on the bytes left, not allocate.
    std::vector<std::uint8_t> huge(good.begin(), good.begin() + 4 + 4 + 8 + 4 + 4 + 8 + 8 * wf::kForgePhaseCount + 8);
    huge.insert(huge.end(), {0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0});
    huge.resize(huge.size() + 8);
    CHECK_FALSE(decodeForgedPhaseMaps(resealed(huge), intact.key, intact.world));

    // On disk: a damaged blob in the campaign is refused with a reason, never loaded.
    for (const std::vector<std::uint8_t>& bytes : {std::vector<std::uint8_t>{}, noise, std::vector<std::uint8_t>(64, 0xff)}) {
        spit(blobPath(dir, "alpha", kForgedPhasesBlob), bytes);
        CHECK(planCampaignWorld(dir.path, "alpha").action == CampaignWorldAction::Refuse);
    }
}

TEST_CASE("maps that hash consistently but are not sound are rejected", "[forged_phase_maps]")
{
    const ForgedGame& game = sharedGame();
    const std::uint32_t version = wf::kGenVersion;
    const auto rejected = [&](const std::function<void(wf::detail::ForgedParts&)>& tamper) {
        wf::detail::ForgedParts parts = partsOf(*game.world);
        tamper(parts);
        const wf::ForgedWorld world = sealed(std::move(parts), version);
        const std::vector<std::uint8_t> bytes = encodeForgedPhaseMaps(keyAt(version), world);
        REQUIRE_FALSE(bytes.empty());
        return !decodeForgedPhaseMaps(bytes, keyAt(version), cachedFrom(game, world));
    };

    SECTION("the untouched world passes the same path")
    {
        CHECK_FALSE(rejected([](wf::detail::ForgedParts&) {}));
    }
    SECTION("a flow direction that is not a direction")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.hydrology->flowDir[5] = 9; }));
    }
    SECTION("a biome past the table")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.biomes->biome[3] = 200; }));
    }
    SECTION("a settlement outside the grid")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.settlements->sites.front().x = 4000; }));
    }
    SECTION("a settlement count that disagrees with the manifest")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.settlements->sites.pop_back(); }));
    }
    SECTION("a road edge to a site that is not there")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.roads->edges.front().b = 1u << 30; }));
    }
    SECTION("a road path that runs past its cells")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.roads->edges.front().pathCount = 1u << 30; }));
    }
    SECTION("a path cell outside the grid")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.roads->pathCells.front() = 1u << 30; }));
    }
    SECTION("an owner that is not a faction")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.politics->owner[0] = 254; }));
    }
    SECTION("a point of interest outside the grid")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.pois->pois.front().y = -1; }));
    }
    SECTION("a short per-cell array")
    {
        CHECK(rejected([](wf::detail::ForgedParts& p) { p.biomes->blend.pop_back(); }));
    }
}

TEST_CASE("an older genVersion loads as stored with a warning, a newer one is refused", "[forged_phase_maps]")
{
    Scratch dir;
    const ForgedGame& game = sharedGame();
    const std::uint64_t forgesBefore = ForgeJob::forgesStarted();

    const std::uint32_t older = wf::kGenVersion - 1;
    const wf::ForgedWorld oldWorld = sealed(partsOf(*game.world), older);
    REQUIRE(oldWorld.worldHash() != game.world->worldHash());
    REQUIRE(saveForgedWorld(dir.path, "old", keyAt(older), cachedFrom(game, oldWorld), &oldWorld) == CampaignStatus::Ok);
    const CampaignWorldPlan plan = planCampaignWorld(dir.path, "old");
    REQUIRE(plan.action == CampaignWorldAction::Load);
    CHECK(plan.warn);
    CHECK(plan.message.find("older") != std::string::npos);
    CHECK(plan.forged->worldHash() == oldWorld.worldHash());
    const std::unique_ptr<ForgedGame> loaded = buildStoredCampaignGame(plan, requestedWorld(1));
    INFO(loaded->error);
    CHECK(loaded->ok());
    CHECK(loaded->world->worldHash() == oldWorld.worldHash());

    const std::uint32_t newer = wf::kGenVersion + 1;
    const wf::ForgedWorld newWorld = sealed(partsOf(*game.world), newer);
    REQUIRE(saveForgedWorld(dir.path, "new", keyAt(newer), cachedFrom(game, newWorld), &newWorld) == CampaignStatus::Ok);
    const CampaignWorldPlan refused = planCampaignWorld(dir.path, "new");
    CHECK(refused.action == CampaignWorldAction::Refuse);
    CHECK(refused.message.find("newer") != std::string::npos);
    CHECK(ForgeJob::forgesStarted() == forgesBefore);
}

TEST_CASE("a phase blob made for another world or key is not accepted", "[forged_phase_maps]")
{
    const ForgedGame& game = sharedGame();
    const ForgedWorldKey key = keyAt(wf::kGenVersion);
    const std::vector<std::uint8_t> bytes = encodeForgedPhaseMaps(key, *game.world);
    const CachedForgedWorld cached = cachedFrom(game, *game.world);
    REQUIRE(decodeForgedPhaseMaps(bytes, key, cached));

    ForgedWorldKey otherSeed = key;
    otherSeed.seed += 1;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, otherSeed, cached));
    ForgedWorldKey otherVersion = key;
    otherVersion.genVersion += 1;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, otherVersion, cached));
    ForgedWorldKey otherGrid = key;
    otherGrid.gridSize = kGrid * 2;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, otherGrid, cached));
    ForgedWorldKey absurdGrid = key;
    absurdGrid.gridSize = -5;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, absurdGrid, cached));
    CachedForgedWorld otherHash = cached;
    otherHash.worldHash ^= 1;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, key, otherHash));
    CachedForgedWorld otherManifest = cached;
    otherManifest.manifest.poiIds.push_back("zzz");
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, key, otherManifest));
}

TEST_CASE("a campaign missing its phase blob is damaged, one saved without it still rebuilds", "[forged_phase_maps]")
{
    Scratch dir;
    const ForgedGame& game = sharedGame();
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, game) == CampaignStatus::Ok);
    std::filesystem::remove(blobPath(dir, "alpha", kForgedPhasesBlob));
    const CampaignWorldPlan damaged = planCampaignWorld(dir.path, "alpha");
    CHECK(damaged.action == CampaignWorldAction::Refuse);
    CHECK(damaged.message.find("damaged") != std::string::npos);

    REQUIRE(saveForgedWorld(dir.path, "legacy", keyAt(wf::kGenVersion), cachedFrom(game, *game.world)) == CampaignStatus::Ok);
    const CampaignWorldPlan legacy = planCampaignWorld(dir.path, "legacy");
    CHECK(legacy.action == CampaignWorldAction::Rebuild);
    CHECK_FALSE(legacy.forged);
}

TEST_CASE("a world without all its maps cannot be saved with phase maps", "[forged_phase_maps]")
{
    Scratch dir;
    const ForgedGame& game = sharedGame();
    wf::detail::ForgedParts parts = partsOf(*game.world);
    parts.pois.reset();
    const wf::ForgedWorld partial(std::move(parts));
    CHECK(encodeForgedPhaseMaps(keyAt(wf::kGenVersion), partial).empty());
    CHECK(saveForgedWorld(dir.path, "alpha", keyAt(wf::kGenVersion), cachedFrom(game, *game.world), &partial) == CampaignStatus::CorruptWorld);
    CHECK_FALSE(std::filesystem::exists(dir.path / "alpha"));
}

TEST_CASE("an edited point of interest or landmark report is caught by its phase hash", "[forged_phase_maps]")
{
    const ForgedGame& game = sharedGame();
    const ForgedWorldKey key = keyAt(wf::kGenVersion);
    const CachedForgedWorld cached = cachedFrom(game, *game.world);
    const auto decodesAfter = [&](const std::function<void(wf::detail::ForgedParts&)>& edit) {
        wf::detail::ForgedParts parts = partsOf(*game.world);
        parts.worldHash = game.world->worldHash();
        parts.chain = game.world->chain();
        edit(parts);
        const wf::ForgedWorld edited(std::move(parts));
        const std::vector<std::uint8_t> bytes = encodeForgedPhaseMaps(key, edited);
        REQUIRE_FALSE(bytes.empty());
        return decodeForgedPhaseMaps(bytes, key, cached) != nullptr;
    };
    REQUIRE(decodesAfter([](wf::detail::ForgedParts&) {}));
    REQUIRE_FALSE(game.world->pois()->pois.empty());
    CHECK_FALSE(decodesAfter([](wf::detail::ForgedParts& p) { p.pois->pois.front().x = static_cast<std::int16_t>((p.pois->pois.front().x + 1) % kGrid); }));
    CHECK_FALSE(decodesAfter([](wf::detail::ForgedParts& p) { p.pois->pois.front().score ^= 1; }));
    CHECK_FALSE(decodesAfter([](wf::detail::ForgedParts& p) { p.pois->viewpointCount += 1; }));
    CHECK_FALSE(decodesAfter([](wf::detail::ForgedParts& p) { p.landmarkCoverage->coveragePermille += 1; }));
    CHECK_FALSE(decodesAfter([](wf::detail::ForgedParts& p) { p.landmarkCoverage->passed = !p.landmarkCoverage->passed; }));
}

TEST_CASE("a key that does not describe the world cannot be encoded", "[forged_phase_maps]")
{
    const ForgedGame& game = sharedGame();
    ForgedWorldKey key = keyAt(wf::kGenVersion);
    REQUIRE_FALSE(encodeForgedPhaseMaps(key, *game.world).empty());
    key.seed += 1;
    CHECK(encodeForgedPhaseMaps(key, *game.world).empty());
    key = keyAt(wf::kGenVersion);
    key.gridSize += 1;
    CHECK(encodeForgedPhaseMaps(key, *game.world).empty());
}

TEST_CASE("a world stored by an older generator is not held to this build's hash functions", "[forged_phase_maps]")
{
    const ForgedGame& game = sharedGame();
    const std::uint32_t older = wf::kGenVersion - 1;
    wf::detail::ForgedParts parts = partsOf(*game.world);
    parts.manifest->genVersion = older;
    for (std::uint64_t& hash : parts.phaseHashes) {
        hash ^= 0x5555;
    }
    parts.worldHash = 0x1234;
    const wf::ForgedWorld hashedElsewhere(std::move(parts));
    const ForgedWorldKey key = keyAt(older);
    const std::vector<std::uint8_t> bytes = encodeForgedPhaseMaps(key, hashedElsewhere);
    REQUIRE_FALSE(bytes.empty());
    CachedForgedWorld cached = cachedFrom(game, hashedElsewhere);
    CHECK(decodeForgedPhaseMaps(bytes, key, cached));
    cached.worldHash ^= 1;
    CHECK_FALSE(decodeForgedPhaseMaps(bytes, key, cached));
}
