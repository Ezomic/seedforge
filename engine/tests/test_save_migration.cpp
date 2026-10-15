#include "ichor/save/save_slots.h"
#include "ichor/sim/scene.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
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

constexpr double kDt = 1.0 / 60.0;
const SaveContext kContext{77, 1, 1234};

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = std::filesystem::temp_directory_path() / ("ichor_save_migration_" + std::to_string(ICHOR_PID) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
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

void populate(Scene& scene, int ticks)
{
    SpawnCommand command;
    command.id = StableId::of("well");
    command.name.value = "well";
    scene.enqueue(command);
    for (int i = 0; i < ticks; ++i) {
        scene.tick(kDt);
    }
}

// Each synthetic step leaves a mark in the chronicle so the order they ran in shows.
bool markStep(SlotBody& body, const char* text)
{
    Chronicle chronicle;
    if (!body.chronicle.empty() && !Chronicle::parse(body.chronicle, chronicle)) {
        return false;
    }
    chronicle.add(0, "migrated", text);
    body.chronicle = chronicle.serialize();
    return true;
}
bool fromOne(SlotBody& body) { return markStep(body, "from1"); }
bool fromTwo(SlotBody& body) { return markStep(body, "from2"); }
bool refuses(SlotBody&) { return false; }

const MigrationRegistry kV1(1, {});
const MigrationRegistry kV3(3, {{2, &fromTwo}, {1, &fromOne}});
const MigrationRegistry kV3Gap(3, {{2, &fromTwo}});
const MigrationRegistry kV3Refusing(3, {{1, &fromOne}, {2, &refuses}});
const MigrationRegistry kV4(4, {{1, &fromOne}, {2, &fromTwo}, {3, &fromTwo}});

SavePolicy with(const MigrationRegistry& registry)
{
    SavePolicy policy;
    policy.migrations = &registry;
    return policy;
}

}

TEST_CASE("an old-format slot is migrated step by step in order and loads")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 6);
    REQUIRE(SaveSlots(dir.path(), with(kV1)).save("auto", scene, kContext));

    SaveSlots slots(dir.path(), with(kV3));
    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.migratedFrom == 1);
    REQUIRE(restored.stateHash() == scene.stateHash());
    REQUIRE(result.chronicle.entries().size() == 2);
    REQUIRE(result.chronicle.entries()[0].text == "from1");
    REQUIRE(result.chronicle.entries()[1].text == "from2");
}

TEST_CASE("a migration keeps the old file and rewrites the slot once")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 6);
    SaveSlots old(dir.path(), with(kV1));
    REQUIRE(old.save("auto", scene, kContext));
    scene.tick(kDt);
    REQUIRE(old.save("auto", scene, kContext)); // auto.bak1 is the older of the two
    const std::vector<char> before = bytesOf(dir.path() / "auto.sav");
    const std::vector<char> backupBefore = bytesOf(dir.path() / "auto.bak1");

    SaveSlots slots(dir.path(), with(kV3));
    Scene restored(5);
    REQUIRE(slots.load("auto", restored, kContext).status == LoadStatus::Loaded);

    REQUIRE(bytesOf(dir.path() / "auto.premigrate-v1") == before);
    REQUIRE(bytesOf(dir.path() / "auto.sav") != before);
    REQUIRE(bytesOf(dir.path() / "auto.bak1") == backupBefore);

    const LoadResult again = slots.load("auto", restored, kContext);
    REQUIRE(again.status == LoadStatus::Loaded);
    REQUIRE(again.migratedFrom == 0);
    REQUIRE(again.chronicle.entries().size() == 2);
    REQUIRE(slots.list().size() == 1);
}

TEST_CASE("saving over an old-format slot keeps it as a loadable backup")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    REQUIRE(SaveSlots(dir.path(), with(kV1)).save("auto", scene, kContext));
    const std::vector<char> old = bytesOf(dir.path() / "auto.sav");

    SaveSlots slots(dir.path(), with(kV3));
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));
    REQUIRE(bytesOf(dir.path() / "auto.bak1") == old);

    std::filesystem::remove(dir.path() / "auto.sav");
    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.fromBackup);
    REQUIRE(result.migratedFrom == 1);
    REQUIRE(bytesOf(dir.path() / "auto.bak1") == old);
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "auto.premigrate-v1"));
}

TEST_CASE("a slot newer than the build knows is refused and never overwritten")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    REQUIRE(SaveSlots(dir.path(), with(kV4)).save("auto", scene, kContext));
    const std::vector<char> before = bytesOf(dir.path() / "auto.sav");

    SaveSlots slots(dir.path(), with(kV3));
    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::UnsupportedVersion);
    REQUIRE(result.savedFormat == 4);
    REQUIRE(slots.lastError().find("newer") != std::string::npos);
    REQUIRE(restored.stateHash() == Scene(5).stateHash());

    REQUIRE_FALSE(slots.save("auto", scene, kContext));
    REQUIRE(slots.lastError().find("newer") != std::string::npos);
    REQUIRE(bytesOf(dir.path() / "auto.sav") == before);
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "auto.bak1"));
    REQUIRE(slots.list().empty());
}

TEST_CASE("an old format with a gap in its steps is refused and left alone")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    REQUIRE(SaveSlots(dir.path(), with(kV1)).save("auto", scene, kContext));
    const std::vector<char> before = bytesOf(dir.path() / "auto.sav");

    SaveSlots slots(dir.path(), with(kV3Gap));
    Scene restored(5);
    REQUIRE(slots.load("auto", restored, kContext).status == LoadStatus::UnsupportedVersion);
    REQUIRE_FALSE(slots.save("auto", scene, kContext));
    REQUIRE(bytesOf(dir.path() / "auto.sav") == before);
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "auto.premigrate-v1"));
}

TEST_CASE("a step that refuses leaves the file as it was and a good older copy can still load")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    SaveSlots current(dir.path(), with(kV3Refusing));
    REQUIRE(current.save("auto", scene, kContext)); // becomes auto.bak1
    scene.tick(kDt);
    REQUIRE(current.save("auto", scene, kContext));
    const Dir oldDir;
    scene.tick(kDt);
    REQUIRE(SaveSlots(oldDir.path(), with(kV1)).save("auto", scene, kContext));
    std::filesystem::copy_file(oldDir.path() / "auto.sav", dir.path() / "auto.sav", std::filesystem::copy_options::overwrite_existing); // auto.sav is format 1

    const std::vector<char> before = bytesOf(dir.path() / "auto.sav");
    SaveSlots slots(dir.path(), with(kV3Refusing));
    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.fromBackup);
    REQUIRE(bytesOf(dir.path() / "auto.sav") == before);
    REQUIRE(slots.lastError().empty());
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "auto.premigrate-v1"));

    const Dir lone;
    REQUIRE(SaveSlots(lone.path(), with(kV1)).save("auto", scene, kContext));
    Scene none(5);
    const LoadResult refused = SaveSlots(lone.path(), with(kV3Refusing)).load("auto", none, kContext);
    REQUIRE(refused.status == LoadStatus::MigrationFailed);
    REQUIRE(refused.savedFormat == 1);
}

TEST_CASE("the format this build ships has no steps yet and reads only itself")
{
    const MigrationRegistry& standard = MigrationRegistry::standard();
    REQUIRE(standard.canMigrate(standard.current()));
    REQUIRE_FALSE(standard.canMigrate(standard.current() + 1));
    REQUIRE_FALSE(standard.canMigrate(standard.current() - 1));
    REQUIRE_FALSE(standard.canMigrate(0));
    REQUIRE(standard.isNewer(standard.current() + 1));
}
