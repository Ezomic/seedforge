#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define ICHOR_PID _getpid()
#else
#include <unistd.h>
#define ICHOR_PID getpid()
#endif

using namespace ichor;

// One real slot file per save format that has ever shipped lives in tests/data/saves, beside
// fixtures.json with what loading each one must give. Every run loads all of them through the
// registry this build ships, so a migration step that rots, or a format bump without a fixture
// for the format it leaves behind, fails here rather than on a player's save.
//
// When the slot format is raised, run with ICHOR_UPDATE_SAVE_FIXTURES=1 to add the fixture for
// the new current format; the older fixtures are never rewritten, since their whole point is
// that no current build can produce them.
namespace {

constexpr double kDt = 1.0 / 60.0;
constexpr std::uint64_t kSceneSeed = 5;
constexpr int kTicks = 30;
const SaveContext kContext{77, 1, 1234};
const std::filesystem::path kFixtureDir = std::filesystem::path(ICHOR_TEST_DATA) / "saves";
const std::filesystem::path kIndexPath = kFixtureDir / "fixtures.json";
constexpr const char* kRegenerate = "Run with ICHOR_UPDATE_SAVE_FIXTURES=1 to write the fixture for the current format and its expected values";

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("ichor_save_fixtures_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }
    ~Dir() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::vector<char> bytesOf(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string textOf(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

std::string hex(std::uint64_t value)
{
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

// The same small scene every fixture holds, so the hashes of different formats are comparable by eye.
void populate(Scene& scene)
{
    for (const auto& [name, x] : {std::pair{"well", 1.0}, std::pair{"cart", 2.0}}) {
        SpawnCommand command;
        command.id = StableId::of(name);
        command.name.value = name;
        command.transform.position = {x, 0.0, 0.0};
        scene.enqueue(command);
    }
    for (int i = 0; i < kTicks; ++i) {
        scene.tick(kDt);
    }
}

Chronicle story()
{
    Chronicle chronicle;
    chronicle.add(0, "founded", "The well was dug");
    chronicle.add(12, "arrival", "A cart came up the road");
    return chronicle;
}

std::vector<std::string> lines(const Chronicle& chronicle)
{
    std::vector<std::string> out;
    for (const ChronicleEntry& entry : chronicle.entries()) {
        out.push_back(std::to_string(entry.tick) + " " + entry.category + " " + entry.text);
    }
    return out;
}

nlohmann::ordered_json readIndex()
{
    if (!std::filesystem::exists(kIndexPath)) {
        return nlohmann::ordered_json::object();
    }
    nlohmann::ordered_json index = nlohmann::ordered_json::parse(textOf(kIndexPath), nullptr, false);
    if (index.is_discarded()) {
        FAIL("fixtures.json is not valid JSON: " << kIndexPath.string());
    }
    return index;
}

// Writes the fixture for the format this build ships and records what it must load to.
void regenerate()
{
    REQUIRE(std::getenv("CI") == nullptr); // CI compares, it never rewrites the expectations
    const std::uint32_t current = MigrationRegistry::standard().current();
    const std::string file = "format" + std::to_string(current) + ".sav";

    const Dir dir;
    Scene scene(kSceneSeed);
    populate(scene);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("slot", scene, kContext, story()));
    std::filesystem::create_directories(kFixtureDir);
    std::filesystem::copy_file(dir.path() / "slot.sav", kFixtureDir / file, std::filesystem::copy_options::overwrite_existing);

    nlohmann::ordered_json index = readIndex();
    nlohmann::ordered_json entry;
    entry["format"] = current;
    entry["made"] = "SaveSlots::save of this build, through ICHOR_UPDATE_SAVE_FIXTURES=1 in test_save_fixtures.cpp";
    entry["seed"] = kSceneSeed;
    entry["tick"] = scene.tickCount();
    entry["stateHash"] = hex(scene.stateHash());
    entry["chronicle"] = lines(story());
    index[file] = entry;

    std::ofstream out(kIndexPath, std::ios::binary | std::ios::trunc);
    out << index.dump(2) << '\n';
    REQUIRE(static_cast<bool>(out));
    SUCCEED(file + " written");
}

}

TEST_CASE("save fixtures: every slot format that ever shipped has a checked-in fixture and loads to its recorded state")
{
    const char* update = std::getenv("ICHOR_UPDATE_SAVE_FIXTURES");
    if (update != nullptr && update[0] == '1') {
        regenerate();
        return;
    }

    const MigrationRegistry& registry = MigrationRegistry::standard();
    const nlohmann::ordered_json index = readIndex();
    INFO("fixture index: " << kIndexPath.string() << ". " << kRegenerate);
    REQUIRE(index.is_object());
    REQUIRE_FALSE(index.empty());

    std::vector<bool> covered(registry.current() + 1, false);
    for (const auto& [file, entry] : index.items()) {
        INFO("fixture " << file);
        const std::uint32_t format = entry.at("format").get<std::uint32_t>();
        REQUIRE(format >= 1);
        REQUIRE(format <= registry.current());
        covered[format] = true;

        const std::filesystem::path source = kFixtureDir / file;
        const std::vector<char> original = bytesOf(source);
        REQUIRE_FALSE(original.empty());

        // The slot is loaded from a scratch copy, since a migration rewrites the slot file in place.
        const Dir dir;
        std::filesystem::copy_file(source, dir.path() / "slot.sav");
        SaveSlots slots(dir.path());
        Scene restored(entry.at("seed").get<std::uint64_t>());
        const LoadResult result = slots.load("slot", restored, kContext);
        INFO("load error: " << slots.lastError());

        if (!registry.canMigrate(format)) {
            // Below the oldest format with a migration path: refused and left as it was. "refused" in the
            // index says how: "unsupported" names the format back to the player; "corrupt" is what the reader
            // says of a layout it cannot parse at all, which is the honest answer for format 1 today, since its
            // 56-byte header never passes the current header check that comes before the format is trusted.
            const std::string refusal = entry.value("refused", "unsupported");
            if (refusal == "corrupt") {
                REQUIRE(result.status == LoadStatus::Corrupt);
            } else {
                REQUIRE(refusal == "unsupported");
                REQUIRE(result.status == LoadStatus::UnsupportedVersion);
                REQUIRE(result.savedFormat == format);
                REQUIRE(slots.lastError().find("no longer migrate") != std::string::npos);
            }
            REQUIRE(bytesOf(dir.path() / "slot.sav") == original);
            REQUIRE(slots.list().empty());
            continue;
        }

        REQUIRE(result.status == LoadStatus::Loaded);
        REQUIRE_FALSE(result.fromBackup);
        REQUIRE(result.migratedFrom == (format == registry.current() ? 0 : format));
        REQUIRE(result.info.tick == entry.at("tick").get<std::uint64_t>());
        REQUIRE(result.info.context.worldSeed == kContext.worldSeed);
        REQUIRE(result.info.context.genVersion == kContext.genVersion);
        REQUIRE(hex(restored.stateHash()) == entry.at("stateHash").get<std::string>());
        REQUIRE_FALSE(result.chronicleDamaged);
        REQUIRE(lines(result.chronicle) == entry.at("chronicle").get<std::vector<std::string>>());
        if (format != registry.current()) {
            REQUIRE(std::filesystem::exists(dir.path() / ("slot.premigrate-v" + std::to_string(format))));
            Scene again(entry.at("seed").get<std::uint64_t>());
            const LoadResult second = slots.load("slot", again, kContext);
            REQUIRE(second.status == LoadStatus::Loaded);
            REQUIRE(second.migratedFrom == 0);
            REQUIRE(again.stateHash() == restored.stateHash());
        }
    }

    for (std::uint32_t format = 1; format <= registry.current(); ++format) {
        INFO("no fixture for slot format " << format << ". " << kRegenerate << "; an older format needs a file written by the build that shipped it");
        REQUIRE(covered[format]);
    }
}

TEST_CASE("save fixtures: a fixture written by this build loads back to the scene it was made from")
{
    const Dir dir;
    Scene scene(kSceneSeed);
    populate(scene);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("slot", scene, kContext, story()));

    Scene restored(kSceneSeed);
    const LoadResult result = slots.load("slot", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(restored.stateHash() == scene.stateHash());
    REQUIRE(lines(result.chronicle) == lines(story()));
}
