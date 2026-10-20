#include "ichor/world/forge_job.h"
#include "ichor/world/forged_campaign_hook.h"
#include "test_temp.h"

#include <worldforge/gen_version.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace ichor;

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

struct Scratch {
    std::filesystem::path path = test::tempPath("ichor_forged_campaign_hook");
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

CachedForgedWorld storedFrom(const ForgedGame& game)
{
    CachedForgedWorld world;
    world.worldHash = game.world->worldHash();
    world.heightfield = game.heightfield;
    world.manifest = *game.world->manifest();
    return world;
}

}

TEST_CASE("a saved campaign plans a rebuild of the same seed and grid, and the rebuild matches", "[forged_campaign_hook]")
{
    Scratch dir;
    const auto game = forgeGameFor(1);
    REQUIRE(planCampaignWorld(dir.path, "alpha").action == CampaignWorldAction::ForgeAndSave);
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, *game) == CampaignStatus::Ok);

    const CampaignWorldPlan plan = planCampaignWorld(dir.path, "alpha");
    REQUIRE(plan.action == CampaignWorldAction::Rebuild);
    CHECK_FALSE(plan.warn);
    CHECK(plan.key.seed == 1);
    CHECK(plan.key.gridSize == kGrid);
    CHECK(plan.stored.worldHash == game->world->worldHash());

    const auto again = forgeGameFor(plan.key.seed);
    CHECK(verifyRebuiltWorld(plan, *again).empty());
    CHECK(again->stateHash() == game->stateHash());
}

TEST_CASE("a rebuild that is not the stored world is reported, never accepted", "[forged_campaign_hook]")
{
    Scratch dir;
    const auto game = forgeGameFor(1);
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, *game) == CampaignStatus::Ok);
    const CampaignWorldPlan plan = planCampaignWorld(dir.path, "alpha");
    REQUIRE(plan.action == CampaignWorldAction::Rebuild);

    CampaignWorldPlan otherHash = plan;
    otherHash.stored.worldHash ^= 1;
    CHECK_FALSE(verifyRebuiltWorld(otherHash, *game).empty());

    CampaignWorldPlan otherTerrain = plan;
    std::vector<std::uint16_t> samples = plan.stored.heightfield.samples();
    REQUIRE_FALSE(samples.empty());
    samples[samples.size() / 2] ^= 1;
    otherTerrain.stored.heightfield = Heightfield(plan.stored.heightfield.desc(), samples);
    CHECK_FALSE(verifyRebuiltWorld(otherTerrain, *game).empty());

    CHECK_FALSE(verifyRebuiltWorld(plan, *forgeGameFor(2)).empty());
}

TEST_CASE("an older genVersion plans a rebuild with a warning, a newer one is refused", "[forged_campaign_hook]")
{
    Scratch dir;
    const auto game = forgeGameFor(1);
    for (const std::int32_t delta : {-1, 1}) {
        const std::uint32_t genVersion = static_cast<std::uint32_t>(static_cast<std::int32_t>(worldforge::kGenVersion) + delta);
        ForgedWorldKey key = forgedWorldKey(requestedWorld(1), kGrid);
        key.genVersion = genVersion;
        CachedForgedWorld world = storedFrom(*game);
        world.manifest.genVersion = genVersion;
        const std::string name = delta < 0 ? "old" : "new";
        REQUIRE(saveForgedWorld(dir.path, name, key, world) == CampaignStatus::Ok);
        const CampaignWorldPlan plan = planCampaignWorld(dir.path, name);
        if (delta < 0) {
            CHECK(plan.action == CampaignWorldAction::Rebuild);
            CHECK(plan.warn);
            CHECK(plan.message.find("older") != std::string::npos);
        } else {
            CHECK(plan.action == CampaignWorldAction::Refuse);
            CHECK(plan.message.find("newer") != std::string::npos);
        }
    }
}

TEST_CASE("damaged, interrupted and badly named campaigns are refused with a reason and nothing is touched", "[forged_campaign_hook]")
{
    Scratch dir;
    const auto game = forgeGameFor(1);
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, *game) == CampaignStatus::Ok);
    const std::filesystem::path blob = dir.path / "alpha" / "world" / kForgedWorldBlob;
    const std::vector<std::uint8_t> good = slurp(blob);

    std::vector<std::vector<std::uint8_t>> hostile;
    hostile.push_back({});
    hostile.push_back(std::vector<std::uint8_t>(good.begin(), good.begin() + 40));
    hostile.push_back(std::vector<std::uint8_t>(64, 0xff));
    std::vector<std::uint8_t> flipped = good;
    flipped[good.size() / 2] ^= 0x10;
    hostile.push_back(flipped);
    for (const auto& bytes : hostile) {
        spit(blob, bytes);
        const CampaignWorldPlan plan = planCampaignWorld(dir.path, "alpha");
        CHECK(plan.action == CampaignWorldAction::Refuse);
        CHECK(plan.message.find("damaged") != std::string::npos);
        CHECK(slurp(blob) == bytes);
    }

    std::filesystem::create_directories(dir.path / "half" / "world");
    spit(dir.path / "half" / "world" / kForgedWorldBlob, {1, 2, 3});
    const CampaignWorldPlan half = planCampaignWorld(dir.path, "half");
    CHECK(half.action == CampaignWorldAction::Refuse);
    CHECK(half.message.find("interrupted") != std::string::npos);

    CHECK(planCampaignWorld(dir.path, "../escape").action == CampaignWorldAction::Refuse);
    CHECK(planCampaignWorld(dir.path, "").action == CampaignWorldAction::Refuse);
}

TEST_CASE("saving over an existing campaign is refused and keeps the stored world", "[forged_campaign_hook]")
{
    Scratch dir;
    const auto game = forgeGameFor(1);
    REQUIRE(saveCampaignWorld(dir.path, "alpha", requestedWorld(1), kGrid, *game) == CampaignStatus::Ok);
    const std::vector<std::uint8_t> before = slurp(dir.path / "alpha" / "world" / kForgedWorldBlob);
    CHECK(saveCampaignWorld(dir.path, "alpha", requestedWorld(2), kGrid, *forgeGameFor(2)) == CampaignStatus::AlreadyExists);
    CHECK(slurp(dir.path / "alpha" / "world" / kForgedWorldBlob) == before);
}
