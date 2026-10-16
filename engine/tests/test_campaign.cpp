#include "ichor/save/campaign.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ichor;
namespace fs = std::filesystem;

namespace {

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = test::tempPath("ichor_campaign_test_" + std::to_string(counter++));
        fs::remove_all(path_);
        fs::create_directories(path_);
    }
    ~Dir() { fs::remove_all(path_); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<std::uint8_t> pattern(std::size_t size, std::uint8_t salt)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>((i * 31 + salt) & 0xff);
    }
    return bytes;
}

std::vector<std::uint8_t> slurp(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

void overwrite(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

struct World {
    std::vector<std::uint8_t> height = pattern(200000, 1);
    std::vector<std::uint8_t> layouts = pattern(5000, 2);
    std::vector<WorldBlob> blobs() const { return { { "heightmap.r16", height }, { "dungeon_layouts.bin", layouts } }; }
};

CampaignStatus forge(const fs::path& root, std::string_view name, const World& world, std::uint64_t seed = 77, std::uint32_t gen = 3)
{
    const auto blobs = world.blobs();
    return Campaign::create(root, name, seed, gen, blobs);
}

std::uintmax_t folderSize(const fs::path& path)
{
    std::uintmax_t total = 0;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
        if (entry.is_regular_file()) {
            total += entry.file_size();
        }
    }
    return total;
}

}

TEST_CASE("a campaign round-trips its world and reports seed and generator version")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);

    CampaignStatus status = CampaignStatus::BadName;
    const auto campaign = Campaign::open(dir.path(), "kraken-77", &status);
    REQUIRE(campaign);
    REQUIRE(status == CampaignStatus::Ok);
    REQUIRE(campaign->manifest().worldSeed == 77);
    REQUIRE(campaign->manifest().genVersion == 3);
    REQUIRE(campaign->manifest().files.size() == 2);
    REQUIRE(campaign->manifest().files[0].name == "dungeon_layouts.bin");
    REQUIRE(campaign->verifyWorld());
    REQUIRE(campaign->readBlob("heightmap.r16") == world.height);
    REQUIRE(campaign->readBlob("dungeon_layouts.bin") == world.layouts);
    REQUIRE_FALSE(campaign->readBlob("nothing.bin"));
    REQUIRE(Campaign::list(dir.path()) == std::vector<std::string>{ "kraken-77" });
}

TEST_CASE("many slots cost small files and never touch the world")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    auto campaign = Campaign::open(dir.path(), "kraken-77");
    REQUIRE(campaign);

    const std::uintmax_t worldBytes = folderSize(campaign->worldDirectory());
    const auto before = slurp(campaign->worldDirectory() / "heightmap.r16");
    const auto manifestBefore = slurp(campaign->worldDirectory() / Campaign::kManifestFile);

    Scene scene(5);
    SaveSlots slots = campaign->slots();
    for (int i = 0; i < 20; ++i) {
        scene.tick(1.0 / 60.0);
        REQUIRE(slots.save("manual_" + std::to_string(i), scene, campaign->slotContext(9)));
    }

    const std::uintmax_t slotBytes = folderSize(campaign->slotsDirectory());
    REQUIRE(slots.list().size() == 20);
    REQUIRE(slotBytes < worldBytes);
    REQUIRE(slurp(campaign->worldDirectory() / "heightmap.r16") == before);
    REQUIRE(slurp(campaign->worldDirectory() / Campaign::kManifestFile) == manifestBefore);
    REQUIRE(campaign->verifyWorld());

    Scene restored(5);
    const LoadResult loaded = slots.load("manual_19", restored, campaign->slotContext(9));
    REQUIRE(loaded.status == LoadStatus::Loaded);
    REQUIRE(restored.stateHash() == scene.stateHash());
}

TEST_CASE("a slot from another campaign's world is refused")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "alpha", world, 1, 3) == CampaignStatus::Ok);
    REQUIRE(forge(dir.path(), "beta", world, 2, 3) == CampaignStatus::Ok);
    const auto alpha = Campaign::open(dir.path(), "alpha");
    const auto beta = Campaign::open(dir.path(), "beta");
    REQUIRE((alpha && beta));

    Scene scene(5);
    SaveSlots alphaSlots = alpha->slots();
    REQUIRE(alphaSlots.save("quick", scene, alpha->slotContext()));
    Scene restored(5);
    REQUIRE(alphaSlots.load("quick", restored, beta->slotContext()).status == LoadStatus::WrongWorld);
    REQUIRE(beta->slots().list().empty());
}

TEST_CASE("create never overwrites an existing campaign")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    const auto before = slurp(dir.path() / "kraken-77" / "world" / "heightmap.r16");

    World other;
    other.height = pattern(100, 9);
    REQUIRE(forge(dir.path(), "kraken-77", other, 99, 4) == CampaignStatus::AlreadyExists);
    REQUIRE(slurp(dir.path() / "kraken-77" / "world" / "heightmap.r16") == before);
    REQUIRE(Campaign::open(dir.path(), "kraken-77")->manifest().worldSeed == 77);
}

TEST_CASE("bad names, duplicate blobs and oversize input are refused without leaving a folder")
{
    const Dir dir;
    const World world;
    for (const char* bad : { "", "Upper", "a b", "../x", "a/b", "a\\b", "con.", ".hidden" }) {
        CAPTURE(bad);
        REQUIRE(forge(dir.path(), bad, world) == CampaignStatus::BadName);
    }
    REQUIRE(forge(dir.path(), std::string(49, 'a'), world) == CampaignStatus::BadName);

    const auto bytes = pattern(10, 0);
    for (const char* bad : { "", ".x", "manifest.bin", "a/b", "..", "A.bin", "a b", "x.bin.tmp", "a." }) {
        CAPTURE(bad);
        const std::vector<WorldBlob> blobs{ { bad, bytes } };
        REQUIRE(Campaign::create(dir.path(), "ok", 1, 1, blobs) == CampaignStatus::BadName);
    }
    const std::vector<WorldBlob> twice{ { "a.bin", bytes }, { "a.bin", bytes } };
    REQUIRE(Campaign::create(dir.path(), "ok", 1, 1, twice) == CampaignStatus::BadName);

    CampaignLimits tiny;
    tiny.maxFiles = 1;
    tiny.maxFileBytes = 5;
    const std::vector<WorldBlob> two{ { "a.bin", bytes }, { "b.bin", bytes } };
    REQUIRE(Campaign::create(dir.path(), "ok", 1, 1, two, tiny) == CampaignStatus::TooLarge);
    const std::vector<WorldBlob> one{ { "a.bin", bytes } };
    REQUIRE(Campaign::create(dir.path(), "ok", 1, 1, one, tiny) == CampaignStatus::TooLarge);

    REQUIRE(fs::is_empty(dir.path()));
    REQUIRE(Campaign::create(dir.path() / "missing", "ok", 1, 1, one) == CampaignStatus::WriteFailed);
}

TEST_CASE("an interrupted forge is incomplete, not a campaign")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    fs::remove(dir.path() / "kraken-77" / "world" / Campaign::kManifestFile);

    CampaignStatus status = CampaignStatus::Ok;
    REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));
    REQUIRE(status == CampaignStatus::Incomplete);
    REQUIRE(Campaign::list(dir.path()).empty());
}

TEST_CASE("a missing campaign is reported as such")
{
    const Dir dir;
    CampaignStatus status = CampaignStatus::Ok;
    REQUIRE_FALSE(Campaign::open(dir.path(), "nope", &status));
    REQUIRE(status == CampaignStatus::NoSuchCampaign);
    REQUIRE_FALSE(Campaign::open(dir.path(), "../nope", &status));
    REQUIRE(status == CampaignStatus::BadName);
    REQUIRE(Campaign::list(dir.path() / "absent").empty());
}

TEST_CASE("damage to a blob is caught by size on open and by hash on verify")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    const fs::path blob = dir.path() / "kraken-77" / "world" / "heightmap.r16";

    auto flipped = world.height;
    flipped[1234] ^= 0x40;
    overwrite(blob, flipped);
    const auto campaign = Campaign::open(dir.path(), "kraken-77");
    REQUIRE(campaign);
    REQUIRE_FALSE(campaign->verifyWorld());
    REQUIRE_FALSE(campaign->readBlob("heightmap.r16"));
    REQUIRE(campaign->readBlob("dungeon_layouts.bin") == world.layouts);

    auto shorter = world.height;
    shorter.pop_back();
    overwrite(blob, shorter);
    CampaignStatus status = CampaignStatus::Ok;
    REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));
    REQUIRE(status == CampaignStatus::CorruptWorld);

    fs::remove(blob);
    REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));
    REQUIRE(status == CampaignStatus::CorruptWorld);
}

TEST_CASE("a hostile or damaged manifest never opens and never crashes")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    const fs::path manifest = dir.path() / "kraken-77" / "world" / Campaign::kManifestFile;
    const auto good = slurp(manifest);
    REQUIRE(good.size() > 30);

    CampaignStatus status = CampaignStatus::Ok;
    for (std::size_t i = 0; i < good.size(); ++i) {
        auto bad = good;
        bad[i] ^= 0x01;
        overwrite(manifest, bad);
        CAPTURE(i);
        REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));
        REQUIRE(status == CampaignStatus::CorruptWorld);
    }
    for (std::size_t length = 0; length < good.size(); ++length) {
        overwrite(manifest, { good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length) });
        CAPTURE(length);
        REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));
    }
    overwrite(manifest, std::vector<std::uint8_t>(4096, 0xff));
    REQUIRE_FALSE(Campaign::open(dir.path(), "kraken-77", &status));

    overwrite(manifest, good);
    REQUIRE(Campaign::open(dir.path(), "kraken-77"));
}

TEST_CASE("an empty world and an empty blob are valid")
{
    const Dir dir;
    REQUIRE(Campaign::create(dir.path(), "bare", 5, 1, {}) == CampaignStatus::Ok);
    REQUIRE(Campaign::open(dir.path(), "bare")->verifyWorld());

    const std::vector<std::uint8_t> nothing;
    const std::vector<WorldBlob> blobs{ { "empty.bin", nothing } };
    REQUIRE(Campaign::create(dir.path(), "hollow", 5, 1, blobs) == CampaignStatus::Ok);
    const auto hollow = Campaign::open(dir.path(), "hollow");
    REQUIRE(hollow);
    REQUIRE(hollow->verifyWorld());
    REQUIRE(hollow->readBlob("empty.bin")->empty());
}

TEST_CASE("a crashed forge can be retried but an unrelated folder is never removed")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    fs::remove(dir.path() / "kraken-77" / "world" / Campaign::kManifestFile);
    REQUIRE(forge(dir.path(), "kraken-77", world, 5, 1) == CampaignStatus::Ok);
    REQUIRE(Campaign::open(dir.path(), "kraken-77")->manifest().worldSeed == 5);

    fs::create_directories(dir.path() / "photos");
    overwrite(dir.path() / "photos" / "keep.txt", pattern(8, 0));
    REQUIRE(forge(dir.path(), "photos", world) == CampaignStatus::AlreadyExists);
    REQUIRE(fs::exists(dir.path() / "photos" / "keep.txt"));
}

TEST_CASE("a blob that grew after the manifest was written fails verification")
{
    const Dir dir;
    const World world;
    REQUIRE(forge(dir.path(), "kraken-77", world) == CampaignStatus::Ok);
    const auto campaign = Campaign::open(dir.path(), "kraken-77");
    REQUIRE(campaign);
    auto grown = world.layouts;
    grown.push_back(0);
    overwrite(campaign->worldDirectory() / "dungeon_layouts.bin", grown);
    REQUIRE_FALSE(campaign->verifyWorld());
    REQUIRE_FALSE(campaign->readBlob("dungeon_layouts.bin"));
}
