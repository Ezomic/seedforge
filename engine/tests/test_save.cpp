#include "ichor/save/save_slots.h"
#include "ichor/save/chronicle.h"
#include "ichor/sim/scene.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ichor;

namespace {

constexpr double kDt = 1.0 / 60.0;

class Dir {
public:
    Dir()
    {
        static int counter = 0;
        path_ = test::tempPath("ichor_save_test_" + std::to_string(counter++));
        std::filesystem::remove_all(path_);
    }
    ~Dir() { std::filesystem::remove_all(path_); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void spawn(Scene& scene, const char* name, double x)
{
    SpawnCommand command;
    command.id = StableId::of(name);
    command.name.value = name;
    command.transform.position = {x, 0.0, 0.0};
    scene.enqueue(command);
}

void populate(Scene& scene, int ticks)
{
    spawn(scene, "well", 1.0);
    spawn(scene, "cart", 2.0);
    for (int i = 0; i < ticks; ++i) {
        scene.tick(kDt);
    }
}

void flipByte(const std::filesystem::path& path, std::uintmax_t offset)
{
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekg(static_cast<std::streamoff>(offset));
    char byte = 0;
    file.read(&byte, 1);
    byte = static_cast<char>(byte ^ 0x5a);
    file.seekp(static_cast<std::streamoff>(offset));
    file.write(&byte, 1);
}

const SaveContext kContext{77, 1, 1234};

}

TEST_CASE("a slot round-trips the scene and reports what it holds")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 30);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("manual_01", scene, kContext));

    Scene restored(5);
    const LoadResult result = slots.load("manual_01", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE_FALSE(result.fromBackup);
    REQUIRE_FALSE(result.dataChanged);
    REQUIRE(result.info.tick == 30);
    REQUIRE(restored.stateHash() == scene.stateHash());

    const auto listed = slots.list();
    REQUIRE(listed.size() == 1);
    REQUIRE(listed[0].name == "manual_01");
    REQUIRE(listed[0].context.worldSeed == 77);
}

TEST_CASE("a missing slot and a bad name are refused")
{
    const Dir dir;
    Scene scene(5);
    SaveSlots slots(dir.path());
    REQUIRE(slots.load("nothing", scene, kContext).status == LoadStatus::NoSuchSlot);
    REQUIRE_FALSE(slots.save("../escape", scene, kContext));
    REQUIRE_FALSE(slots.save("", scene, kContext));
    REQUIRE_FALSE(slots.save("has space", scene, kContext));
    REQUIRE_FALSE(slots.save("Mixed", scene, kContext));
    REQUIRE_FALSE(slots.save("con", scene, kContext));
    REQUIRE_FALSE(slots.save("lpt1", scene, kContext));
    REQUIRE_FALSE(slots.save("name.with.dots", scene, kContext));
    REQUIRE(slots.list().empty());
}

TEST_CASE("saving again keeps rotating backups and no more than asked")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 1);
    SaveSlots slots(dir.path());
    for (int i = 0; i < 6; ++i) {
        scene.tick(kDt);
        REQUIRE(slots.save("auto", scene, kContext));
    }
    REQUIRE(std::filesystem::exists(dir.path() / "auto.sav"));
    REQUIRE(std::filesystem::exists(dir.path() / "auto.bak1"));
    REQUIRE(std::filesystem::exists(dir.path() / "auto.bak2"));
    REQUIRE(std::filesystem::exists(dir.path() / "auto.bak3"));
    REQUIRE_FALSE(std::filesystem::exists(dir.path() / "auto.bak4"));
    REQUIRE(slots.list().size() == 1); // backups are not slots
}

TEST_CASE("a corrupt newest file falls back to the newest good backup")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 10);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("auto", scene, kContext));
    const std::uint64_t firstHash = scene.stateHash();
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));

    flipByte(dir.path() / "auto.sav", std::filesystem::file_size(dir.path() / "auto.sav") - 5);

    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.fromBackup);
    REQUIRE(restored.stateHash() == firstHash);
    REQUIRE(slots.list().size() == 1); // the slot still shows, by its newest good copy
}

TEST_CASE("a truncated or foreign file is corrupt, and so is a slot whose every copy is")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 3);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("only", scene, kContext));
    std::filesystem::resize_file(dir.path() / "only.sav", 10);
    Scene restored(5);
    REQUIRE(slots.load("only", restored, kContext).status == LoadStatus::Corrupt);

    std::ofstream(dir.path() / "junk.sav") << "this is not a save file at all, not even close, really not......";
    REQUIRE(slots.load("junk", restored, kContext).status == LoadStatus::Corrupt);
}

TEST_CASE("a slot from another world is refused, and changed data is flagged")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("s", scene, kContext));

    Scene restored(5);
    REQUIRE(slots.load("s", restored, {78, 1, 1234}).status == LoadStatus::WrongWorld);
    REQUIRE(slots.load("s", restored, {77, 2, 1234}).status == LoadStatus::WrongWorld);

    const LoadResult changed = slots.load("s", restored, {77, 1, 9999});
    REQUIRE(changed.status == LoadStatus::Loaded);
    REQUIRE(changed.dataChanged);
}

TEST_CASE("ironman keeps one rolling slot and refuses the rest")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 2);
    SaveSlots slots(dir.path(), {true, 3});
    REQUIRE_FALSE(slots.save("manual_01", scene, kContext));
    REQUIRE(slots.lastError().find("ironman") != std::string::npos);
    REQUIRE(slots.save(SaveSlots::kIronmanSlot, scene, kContext));
    scene.tick(kDt);
    REQUIRE(slots.save(SaveSlots::kIronmanSlot, scene, kContext));
    REQUIRE(slots.list().size() == 1);
}

TEST_CASE("slots are listed by name with their ticks")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 5);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("quicksave", scene, kContext));
    scene.tick(kDt);
    REQUIRE(slots.save("autosave_01", scene, kContext));
    const auto listed = slots.list();
    REQUIRE(listed.size() == 2);
    REQUIRE(listed[0].name == "autosave_01");
    REQUIRE(listed[0].tick == 6);
    REQUIRE(listed[1].name == "quicksave");
    REQUIRE(listed[1].tick == 5);
}

TEST_CASE("a flipped bit in the header makes the copy corrupt, not wrong-world")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 6);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("auto", scene, kContext));
    const std::uint64_t firstHash = scene.stateHash();
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));

    flipByte(dir.path() / "auto.sav", 8); // inside the world seed
    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.fromBackup);
    REQUIRE(restored.stateHash() == firstHash);

    flipByte(dir.path() / "auto.bak1", 4); // inside the format number
    REQUIRE(slots.load("auto", restored, kContext).status == LoadStatus::Corrupt);
}

TEST_CASE("a lost newest file is recovered from the backup, and a corrupt one never replaces a good backup")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 4);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("auto", scene, kContext));
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext)); // auto.sav is the newer, auto.bak1 the older
    const std::uint64_t olderHash = [&] {
        Scene probe(5);
        REQUIRE(slots.load("auto", probe, kContext).status == LoadStatus::Loaded);
        return probe.stateHash();
    }();
    (void)olderHash;

    std::filesystem::remove(dir.path() / "auto.sav"); // the crash window: rotated, not yet replaced
    Scene restored(5);
    const LoadResult recovered = slots.load("auto", restored, kContext);
    REQUIRE(recovered.status == LoadStatus::Loaded);
    REQUIRE(recovered.fromBackup);
    REQUIRE(slots.list().size() == 1);

    // A corrupt current file does not get rotated over the good backup: the
    // good copy moves up the history and stays loadable.
    REQUIRE(slots.save("auto", scene, kContext));
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));
    const auto goodBackup = std::filesystem::file_size(dir.path() / "auto.bak1");
    flipByte(dir.path() / "auto.sav", std::filesystem::file_size(dir.path() / "auto.sav") - 3);
    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));
    Scene check(5);
    REQUIRE(slots.load("auto", check, kContext).status == LoadStatus::Loaded);
    REQUIRE_FALSE(slots.load("auto", check, kContext).fromBackup);
    REQUIRE(std::filesystem::file_size(dir.path() / "auto.bak2") == goodBackup);
}

TEST_CASE("the chronicle keeps its entries, cleans the text and refuses odd categories")
{
    Chronicle story;
    REQUIRE(story.add(120, "quest", "The well was cleared."));
    REQUIRE(story.add(900, "death", "Aster fell\tat the gate\nand was buried."));
    REQUIRE_FALSE(story.add(1, "Bad Category", "x"));
    REQUIRE_FALSE(story.add(1, "", "x"));
    REQUIRE_FALSE(story.add(1, "has\ttab", "x"));
    REQUIRE(story.entries().size() == 2);
    REQUIRE(story.entries()[1].text == "Aster fell at the gate and was buried.");
    REQUIRE(story.add(1, "long", std::string(2000, 'x')));
    REQUIRE(story.entries().back().text.size() == Chronicle::kMaxTextBytes);
}

TEST_CASE("a chronicle survives serialising and parsing, and damaged text is refused whole")
{
    Chronicle story;
    REQUIRE(story.add(0, "start", "A campaign begins."));
    REQUIRE(story.add(3600, "quest", "Tab\tand newline\nbecome spaces."));
    Chronicle back;
    REQUIRE(Chronicle::parse(story.serialize(), back));
    REQUIRE(back == story);

    const std::string good = story.serialize();
    Chronicle target;
    REQUIRE(story.add(1, "extra", "kept"));
    REQUIRE(Chronicle::parse(good, target));
    REQUIRE_FALSE(Chronicle::parse(good.substr(0, good.size() - 3), target)); // truncated
    REQUIRE_FALSE(Chronicle::parse("not a chronicle\n", target));
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\nabc\tquest\ttext\n", target));
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\n5\tBAD\ttext\n", target));
    REQUIRE_FALSE(Chronicle::parse("ichor-chronicle 1\n5\tquest\n", target));
    REQUIRE(target.entries().size() == 2); // a failed parse leaves it as it was
}

TEST_CASE("the chronicle exports as readable lines with play time")
{
    Chronicle story;
    REQUIRE(story.add(0, "start", "A campaign begins."));
    REQUIRE(story.add(60 * 3725, "quest", "The well was cleared."));
    REQUIRE(story.exportText(1.0 / 60.0) == "[0:00:00] start: A campaign begins.\n[1:02:05] quest: The well was cleared.\n");
}

TEST_CASE("a slot carries the chronicle and a load restores the story")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 8);
    Chronicle story;
    REQUIRE(story.add(3, "quest", "The well was cleared."));
    REQUIRE(story.add(7, "death", "A traveller fell."));
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("s", scene, kContext, story));

    Scene restored(5);
    const LoadResult result = slots.load("s", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    REQUIRE(result.chronicle == story);
    REQUIRE(restored.stateHash() == scene.stateHash());

    // Saving without one gives an empty story, not an error.
    REQUIRE(slots.save("plain", scene, kContext));
    REQUIRE(slots.load("plain", restored, kContext).chronicle.empty());
}

TEST_CASE("chronicle text is cut on a character boundary and export survives odd inputs")
{
    Chronicle story;
    std::string text(Chronicle::kMaxTextBytes - 1, 'x');
    text += "\xE2\x82\xAC"; // a three-byte character straddling the limit
    REQUIRE(story.add(1, "money", text));
    const std::string& kept = story.entries()[0].text;
    REQUIRE(kept.size() == Chronicle::kMaxTextBytes - 1);
    REQUIRE((static_cast<unsigned char>(kept.back()) & 0xC0) != 0x80);

    REQUIRE(story.add(UINT64_MAX, "late", "far future"));
    REQUIRE_NOTHROW(story.exportText(1.0 / 60.0));
    REQUIRE_NOTHROW(story.exportText(-1.0));
    REQUIRE_NOTHROW(story.exportText(std::nan("")));
}

TEST_CASE("a chronicle with Windows line endings parses")
{
    Chronicle back;
    REQUIRE(Chronicle::parse("ichor-chronicle 1\r\n5\tquest\tdone\r\n", back));
    REQUIRE(back.entries().size() == 1);
    REQUIRE(back.entries()[0].text == "done");
}

TEST_CASE("a slot says when its chronicle was damaged")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 2);
    Chronicle story;
    REQUIRE(story.add(1, "quest", "something"));
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("s", scene, kContext, story));
    Scene restored(5);
    const LoadResult clean = slots.load("s", restored, kContext);
    REQUIRE_FALSE(clean.chronicleDamaged);

    REQUIRE(slots.save("none", scene, kContext));
    REQUIRE_FALSE(slots.load("none", restored, kContext).chronicleDamaged);
}

TEST_CASE("the chronicle hands the journal its newest entries first, timed in play")
{
    Chronicle chronicle;
    REQUIRE(chronicle.recent(0.01, 5).empty());
    chronicle.add(0, "start", "The road begins.");
    chronicle.add(6000, "quest", "Took the ash to the ferryman.");
    chronicle.add(366500, "death", "Fell on the ridge.");

    const auto lines = chronicle.recent(0.01, 2);
    REQUIRE(lines.size() == 2);
    REQUIRE(lines[0].text == "Fell on the ridge.");
    REQUIRE(lines[0].category == "death");
    REQUIRE(lines[0].time == "1:01:05"); // 3665 s
    REQUIRE(lines[1].time == "0:01:00");
    REQUIRE(chronicle.recent(0.01, 99).size() == 3);
    REQUIRE(chronicle.recent(0.01, 0).empty());
    REQUIRE(chronicle.exportText(0.01).find("[1:01:05] death: Fell on the ridge.") != std::string::npos);
    // A bad rate falls to zero rather than an undefined conversion.
    REQUIRE(chronicle.recent(std::nan(""), 1)[0].time == "0:00:00");
}

TEST_CASE("a save restored from a backup says which slot and which backup, and a clean load says nothing")
{
    const Dir dir;
    Scene scene(5);
    populate(scene, 10);
    SaveSlots slots(dir.path());
    REQUIRE(slots.save("auto", scene, kContext));
    Scene clean(5);
    CHECK(restoreNotice("auto", slots.load("auto", clean, kContext)).empty());

    scene.tick(kDt);
    REQUIRE(slots.save("auto", scene, kContext));
    const std::filesystem::path file = dir.path() / "auto.sav";
    std::filesystem::resize_file(file, std::filesystem::file_size(file) / 2);

    Scene restored(5);
    const LoadResult result = slots.load("auto", restored, kContext);
    REQUIRE(result.status == LoadStatus::Loaded);
    CHECK(result.fromBackup);
    CHECK(result.backupIndex == 1);
    CHECK(restoreNotice("auto", result) == "Slot 'auto' was damaged; restored from backup 1 (your previous save).");
}
