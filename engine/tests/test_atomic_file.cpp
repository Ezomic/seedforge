#include "ichor/save/atomic_file.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace ichor;
namespace fs = std::filesystem;

namespace {

struct Dir {
    fs::path path;
    explicit Dir(const char* name)
        : path(test::tempPath(name))
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~Dir()
    {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(path, ec)) {
            fs::permissions(entry.path(), fs::perms::owner_write, fs::perm_options::add, ec);
        }
        fs::remove_all(path, ec);
    }
};

std::vector<std::uint8_t> bytesOf(const std::string& text)
{
    return { text.begin(), text.end() };
}

std::string slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

void put(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

bool startsWithGood(std::span<const std::uint8_t> b)
{
    return b.size() >= 4 && b[0] == 'G' && b[1] == 'O' && b[2] == 'O' && b[3] == 'D';
}

AtomicWriteOptions strict(std::uint32_t backups = 3)
{
    AtomicWriteOptions options;
    options.backups = backups;
    options.valid = startsWithGood;
    return options;
}

AtomicWriteResult write(const fs::path& target, const std::string& text, const AtomicWriteOptions& options = {})
{
    const auto bytes = bytesOf(text);
    return writeAtomic(target, bytes, options);
}

} // namespace

TEST_CASE("a write lands the bytes and leaves no temp file behind", "[atomic_file]")
{
    Dir dir("atomic_basic");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(write(target, "GOOD one").ok());
    CHECK(slurp(target) == "GOOD one");
    CHECK_FALSE(fs::exists(tempPath(target)));
    CHECK_FALSE(fs::exists(backupPath(target, 1)));
}

TEST_CASE("overwriting rotates the history newest first and drops the oldest", "[atomic_file]")
{
    Dir dir("atomic_rotate");
    const fs::path target = dir.path / "a.sav";
    for (const char* text : { "GOOD 1", "GOOD 2", "GOOD 3", "GOOD 4", "GOOD 5" }) {
        REQUIRE(write(target, text, strict(3)).ok());
    }
    CHECK(slurp(target) == "GOOD 5");
    CHECK(slurp(backupPath(target, 1)) == "GOOD 4");
    CHECK(slurp(backupPath(target, 2)) == "GOOD 3");
    CHECK(slurp(backupPath(target, 3)) == "GOOD 2");
    CHECK_FALSE(fs::exists(backupPath(target, 4)));
}

TEST_CASE("zero backups keeps no history", "[atomic_file]")
{
    Dir dir("atomic_nohistory");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(write(target, "GOOD 1", strict(0)).ok());
    REQUIRE(write(target, "GOOD 2", strict(0)).ok());
    CHECK(slurp(target) == "GOOD 2");
    CHECK_FALSE(fs::exists(backupPath(target, 1)));
}

TEST_CASE("a corrupt current file never pushes a good backup out", "[atomic_file]")
{
    Dir dir("atomic_corrupt_rotate");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(write(target, "GOOD 1", strict(2)).ok());
    REQUIRE(write(target, "GOOD 2", strict(2)).ok());
    put(target, "garbage"); // damaged on disk between sessions
    REQUIRE(write(target, "GOOD 3", strict(2)).ok());
    CHECK(slurp(target) == "GOOD 3");
    CHECK(slurp(backupPath(target, 1)) == "GOOD 1");
}

TEST_CASE("a crash at any stage leaves the previous file intact", "[atomic_file]")
{
    for (const AtomicStage stage : { AtomicStage::TempPartial, AtomicStage::TempComplete, AtomicStage::BackupsRotated }) {
        Dir dir("atomic_crash");
        const fs::path target = dir.path / "a.sav";
        REQUIRE(write(target, "GOOD old", strict()).ok());

        AtomicWriteOptions crash = strict();
        crash.interruptAt = stage;
        CHECK(write(target, "GOOD new content that is longer", crash).status == AtomicWriteStatus::Interrupted);

        CHECK(slurp(target) == "GOOD old");
        const RecoveredFile recovered = recoverNewestValid(target, 3, startsWithGood);
        REQUIRE(recovered.found);
        CHECK(recovered.source == 0);
        CHECK(std::string(recovered.bytes.begin(), recovered.bytes.end()) == "GOOD old");

        // the next write succeeds over the stale temp file
        REQUIRE(write(target, "GOOD next", strict()).ok());
        CHECK(slurp(target) == "GOOD next");
        CHECK_FALSE(fs::exists(tempPath(target)));
    }
}

TEST_CASE("a partial temp file is really partial", "[atomic_file]")
{
    Dir dir("atomic_partial");
    const fs::path target = dir.path / "a.sav";
    AtomicWriteOptions crash;
    crash.interruptAt = AtomicStage::TempPartial;
    REQUIRE(write(target, "0123456789", crash).status == AtomicWriteStatus::Interrupted);
    CHECK(slurp(tempPath(target)).size() == 5);
    CHECK_FALSE(fs::exists(target));
}

TEST_CASE("recovery falls back to the newest valid backup", "[atomic_file]")
{
    Dir dir("atomic_recover");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(write(target, "GOOD 1", strict()).ok());
    REQUIRE(write(target, "GOOD 2", strict()).ok());
    REQUIRE(write(target, "GOOD 3", strict()).ok());

    SECTION("truncated main file")
    {
        put(target, "GO");
        const RecoveredFile r = recoverNewestValid(target, 3, startsWithGood);
        REQUIRE(r.found);
        CHECK(r.source == 1);
        CHECK(std::string(r.bytes.begin(), r.bytes.end()) == "GOOD 2");
    }
    SECTION("zero-byte main file")
    {
        put(target, "");
        const RecoveredFile r = recoverNewestValid(target, 3);
        REQUIRE(r.found);
        CHECK(r.source == 1);
    }
    SECTION("main file missing")
    {
        fs::remove(target);
        const RecoveredFile r = recoverNewestValid(target, 3, startsWithGood);
        REQUIRE(r.found);
        CHECK(r.source == 1);
    }
    SECTION("main and first backup both bad")
    {
        put(target, "");
        put(backupPath(target, 1), "xxxx");
        const RecoveredFile r = recoverNewestValid(target, 3, startsWithGood);
        REQUIRE(r.found);
        CHECK(r.source == 2);
        CHECK(std::string(r.bytes.begin(), r.bytes.end()) == "GOOD 1");
    }
    SECTION("everything bad")
    {
        put(target, "");
        put(backupPath(target, 1), "xxxx");
        put(backupPath(target, 2), "");
        const RecoveredFile r = recoverNewestValid(target, 3, startsWithGood);
        CHECK_FALSE(r.found);
        CHECK(r.bytes.empty());
    }
    SECTION("backups beyond the asked count are ignored")
    {
        put(target, "");
        put(backupPath(target, 1), "");
        const RecoveredFile r = recoverNewestValid(target, 1, startsWithGood);
        CHECK_FALSE(r.found);
    }
    SECTION("a stale temp file is never recovered")
    {
        fs::remove(target);
        fs::remove(backupPath(target, 1));
        fs::remove(backupPath(target, 2));
        put(tempPath(target), "GOOD half-written");
        CHECK_FALSE(recoverNewestValid(target, 3, startsWithGood).found);
    }
    SECTION("an oversized file counts as corrupt")
    {
        const RecoveredFile r = recoverNewestValid(target, 3, startsWithGood, 4);
        CHECK_FALSE(r.found);
    }
}

TEST_CASE("recovery with nothing on disk finds nothing", "[atomic_file]")
{
    Dir dir("atomic_empty");
    CHECK_FALSE(recoverNewestValid(dir.path / "a.sav", 3).found);
    CHECK_FALSE(recoverNewestValid(dir.path / "missing" / "a.sav", 3).found);
}

TEST_CASE("a missing directory is reported, not created", "[atomic_file]")
{
    Dir dir("atomic_nodir");
    const fs::path target = dir.path / "nope" / "a.sav";
    CHECK(write(target, "GOOD", strict()).status == AtomicWriteStatus::NoDirectory);
    CHECK_FALSE(fs::exists(dir.path / "nope"));
}

TEST_CASE("a directory standing where the file should be is a failure", "[atomic_file]")
{
    Dir dir("atomic_dirtarget");
    const fs::path target = dir.path / "a.sav";
    fs::create_directories(target);
    const AtomicWriteResult r = write(target, "GOOD", strict());
    CHECK_FALSE(r.ok());
    CHECK(fs::is_directory(target));
}

TEST_CASE("a read-only target is refused and left untouched", "[atomic_file]")
{
    Dir dir("atomic_readonly");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(write(target, "GOOD kept", strict()).ok());
    std::error_code ec;
    fs::permissions(target, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove, ec);
    REQUIRE_FALSE(ec);

    CHECK(write(target, "GOOD changed", strict()).status == AtomicWriteStatus::ReadOnly);
    CHECK(slurp(target) == "GOOD kept");
    CHECK_FALSE(fs::exists(tempPath(target)));
    CHECK_FALSE(fs::exists(backupPath(target, 1)));
}

TEST_CASE("empty and large payloads round-trip", "[atomic_file]")
{
    Dir dir("atomic_sizes");
    const fs::path target = dir.path / "a.sav";
    REQUIRE(writeAtomic(target, {}).ok());
    CHECK(fs::file_size(target) == 0);

    std::vector<std::uint8_t> big(3u << 20);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<std::uint8_t>(i * 31u);
    }
    REQUIRE(writeAtomic(target, big).ok());
    const RecoveredFile r = recoverNewestValid(target, 0);
    REQUIRE(r.found);
    CHECK(r.bytes == big);
}

TEST_CASE("backup and temp paths sit beside the target", "[atomic_file]")
{
    const fs::path target = fs::path("saves") / "slot1.sav";
    CHECK(backupPath(target, 2) == fs::path("saves") / "slot1.bak2");
    CHECK(tempPath(target).parent_path() == target.parent_path());
    CHECK(tempPath(target) != target);
}
