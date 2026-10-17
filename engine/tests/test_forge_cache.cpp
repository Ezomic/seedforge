#include "ichor/save/atomic_file.h"
#include "ichor/save/forge_cache.h"
#include "test_temp.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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
        fs::remove_all(path, ec);
    }
};

ForgeCacheKey keyOf(std::uint64_t seed, std::uint32_t version = 1, std::uint64_t digest = 0xabcdef)
{
    return {seed, version, digest};
}

std::vector<std::uint8_t> payloadOf(std::size_t size, std::uint8_t fill)
{
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[i] = static_cast<std::uint8_t>(fill + i * 7);
    }
    return bytes;
}

std::vector<std::uint8_t> slurp(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("forge record round trips and encodes deterministically", "[forge_cache]")
{
    const ForgeCacheRecord record{7, keyOf(42, 3, 99), payloadOf(100, 5)};
    const auto bytes = encodeForgeRecord(record);
    CHECK(bytes == encodeForgeRecord(record));
    const auto decoded = decodeForgeRecord(bytes);
    REQUIRE(decoded.ok());
    CHECK(decoded.record.sequence == 7);
    CHECK(decoded.record.key == record.key);
    CHECK(decoded.record.payload == record.payload);
}

TEST_CASE("forge record rejects every truncation", "[forge_cache]")
{
    const auto bytes = encodeForgeRecord({1, keyOf(1), payloadOf(40, 1)});
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const auto result = decodeForgeRecord(std::span(bytes).first(n));
        CHECK_FALSE(result.ok());
        CHECK(result.record.payload.empty());
    }
}

TEST_CASE("forge record rejects every single flipped byte", "[forge_cache]")
{
    const auto good = encodeForgeRecord({9, keyOf(5, 2, 77), payloadOf(40, 3)});
    for (std::size_t i = 0; i < good.size(); ++i) {
        auto bytes = good;
        bytes[i] ^= 0x01;
        CHECK_FALSE(decodeForgeRecord(bytes).ok());
    }
}

TEST_CASE("forge record rejects trailing bytes, hostile lengths and oversize payloads", "[forge_cache]")
{
    auto bytes = encodeForgeRecord({1, keyOf(1), payloadOf(8, 1)});
    auto trailing = bytes;
    trailing.push_back(0);
    CHECK(decodeForgeRecord(trailing).status == ForgeRecordStatus::TrailingBytes);

    auto hostile = bytes;
    hostile[33] = hostile[34] = hostile[35] = hostile[36] = 0xff;
    CHECK(decodeForgeRecord(hostile).status == ForgeRecordStatus::TooLarge);

    CHECK(decodeForgeRecord(bytes, 4).status == ForgeRecordStatus::TooLarge);
    CHECK(decodeForgeRecord({}).status == ForgeRecordStatus::Truncated);
}

TEST_CASE("forge cache stores and returns a payload, across a restart", "[forge_cache]")
{
    Dir dir("forge_cache_roundtrip");
    const auto payload = payloadOf(500, 9);
    {
        ForgeCache cache(dir.path);
        REQUIRE(cache.put(keyOf(1), payload) == ForgePutStatus::Stored);
        const auto hit = cache.get(keyOf(1));
        REQUIRE(hit.status == ForgeGetStatus::Hit);
        CHECK(hit.payload == payload);
    }
    ForgeCache again(dir.path);
    const auto report = again.scan();
    CHECK(report.kept == 1);
    CHECK(report.discarded == 0);
    const auto hit = again.get(keyOf(1));
    REQUIRE(hit.status == ForgeGetStatus::Hit);
    CHECK(hit.payload == payload);
}

TEST_CASE("forge cache keys differ by seed, version and digest", "[forge_cache]")
{
    Dir dir("forge_cache_keys");
    ForgeCache cache(dir.path);
    REQUIRE(cache.put(keyOf(1, 1, 10), payloadOf(10, 1)) == ForgePutStatus::Stored);
    CHECK(cache.get(keyOf(1, 1, 10)).status == ForgeGetStatus::Hit);
    CHECK(cache.get(keyOf(2, 1, 10)).status == ForgeGetStatus::Miss);
    CHECK(cache.get(keyOf(1, 2, 10)).status == ForgeGetStatus::Miss);
    CHECK(cache.get(keyOf(1, 1, 11)).status == ForgeGetStatus::Miss);
}

TEST_CASE("forge cache replaces an entry for the same key and accounts the bytes", "[forge_cache]")
{
    Dir dir("forge_cache_replace");
    ForgeCache cache(dir.path);
    REQUIRE(cache.put(keyOf(1), payloadOf(100, 1)) == ForgePutStatus::Stored);
    REQUIRE(cache.put(keyOf(1), payloadOf(30, 2)) == ForgePutStatus::Stored);
    CHECK(cache.size() == 1);
    CHECK(cache.totalBytes() == 30);
    CHECK(cache.get(keyOf(1)).payload == payloadOf(30, 2));
}

TEST_CASE("forge cache evicts the oldest entry past the entry limit", "[forge_cache]")
{
    Dir dir("forge_cache_evict_count");
    ForgeCache cache(dir.path, {.maxEntries = 3});
    for (std::uint64_t s = 1; s <= 5; ++s) {
        REQUIRE(cache.put(keyOf(s), payloadOf(10, 1)) == ForgePutStatus::Stored);
    }
    CHECK(cache.size() == 3);
    CHECK_FALSE(cache.contains(keyOf(1)));
    CHECK_FALSE(cache.contains(keyOf(2)));
    CHECK(cache.contains(keyOf(5)));
    CHECK_FALSE(fs::exists(cache.pathFor(keyOf(1))));
    CHECK(fs::exists(cache.pathFor(keyOf(3))));
}

TEST_CASE("forge cache evicts to fit the byte limit and never evicts the new entry", "[forge_cache]")
{
    Dir dir("forge_cache_evict_bytes");
    ForgeCache cache(dir.path, {.maxEntries = 10, .maxTotalBytes = 100});
    REQUIRE(cache.put(keyOf(1), payloadOf(40, 1)) == ForgePutStatus::Stored);
    REQUIRE(cache.put(keyOf(2), payloadOf(40, 1)) == ForgePutStatus::Stored);
    REQUIRE(cache.put(keyOf(3), payloadOf(60, 1)) == ForgePutStatus::Stored);
    CHECK(cache.totalBytes() == 100);
    CHECK(cache.contains(keyOf(2)));
    CHECK(cache.contains(keyOf(3)));
    CHECK_FALSE(cache.contains(keyOf(1)));
    CHECK(cache.put(keyOf(4), payloadOf(101, 1)) == ForgePutStatus::PayloadTooLarge);
    CHECK(cache.contains(keyOf(3)));
}

TEST_CASE("forge cache rejects payloads over the per-entry limit and a zero-entry cache", "[forge_cache]")
{
    Dir dir("forge_cache_limits");
    ForgeCache small(dir.path, {.maxPayloadBytes = 16});
    CHECK(small.put(keyOf(1), payloadOf(17, 1)) == ForgePutStatus::PayloadTooLarge);
    CHECK(small.put(keyOf(1), payloadOf(16, 1)) == ForgePutStatus::Stored);
    ForgeCache none(dir.path, {.maxEntries = 0});
    CHECK(none.put(keyOf(2), payloadOf(1, 1)) == ForgePutStatus::PayloadTooLarge);
}

TEST_CASE("forge cache eviction order is the insertion history, deterministically", "[forge_cache]")
{
    Dir a("forge_cache_order_a");
    Dir b("forge_cache_order_b");
    const std::uint64_t order[] = {5, 3, 9, 1, 7, 3, 2};
    ForgeCache one(a.path, {.maxEntries = 4});
    ForgeCache two(b.path, {.maxEntries = 4});
    for (const auto s : order) {
        one.put(keyOf(s), payloadOf(8, 1));
        two.put(keyOf(s), payloadOf(8, 1));
    }
    CHECK(one.keysByAge() == two.keysByAge());
    // 5 and 9 fell out; 3 was re-put so it is newer than 1 and 7.
    const std::vector<ForgeCacheKey> expect{keyOf(1), keyOf(7), keyOf(3), keyOf(2)};
    CHECK(one.keysByAge() == expect);
}

TEST_CASE("forge cache keeps its eviction order across a restart", "[forge_cache]")
{
    Dir dir("forge_cache_order_restart");
    {
        ForgeCache cache(dir.path);
        for (const std::uint64_t s : {4, 2, 8}) {
            cache.put(keyOf(s), payloadOf(8, 1));
        }
    }
    ForgeCache again(dir.path, {.maxEntries = 2});
    const auto report = again.scan();
    CHECK(report.evicted == 1);
    CHECK_FALSE(again.contains(keyOf(4)));
    again.put(keyOf(6), payloadOf(8, 1));
    CHECK_FALSE(again.contains(keyOf(2)));
    CHECK(again.contains(keyOf(8)));
    CHECK(again.contains(keyOf(6)));
}

TEST_CASE("forge cache drops a truncated file on scan", "[forge_cache]")
{
    Dir dir("forge_cache_truncate_scan");
    {
        ForgeCache cache(dir.path);
        cache.put(keyOf(1), payloadOf(200, 1));
        cache.put(keyOf(2), payloadOf(200, 2));
    }
    const auto path = ForgeCache(dir.path).pathFor(keyOf(1));
    auto bytes = slurp(path);
    bytes.resize(bytes.size() / 2);
    spit(path, bytes);

    ForgeCache again(dir.path);
    const auto report = again.scan();
    CHECK(report.kept == 1);
    CHECK(report.discarded == 1);
    CHECK_FALSE(fs::exists(path));
    CHECK(again.get(keyOf(2)).status == ForgeGetStatus::Hit);
    CHECK(again.get(keyOf(1)).status == ForgeGetStatus::Miss);
}

TEST_CASE("forge cache survives a flipped byte at any position", "[forge_cache]")
{
    Dir dir("forge_cache_flip");
    ForgeCache cache(dir.path);
    const auto payload = payloadOf(64, 4);
    REQUIRE(cache.put(keyOf(1), payload) == ForgePutStatus::Stored);
    const auto path = cache.pathFor(keyOf(1));
    const auto good = slurp(path);
    for (std::size_t i = 0; i < good.size(); ++i) {
        auto bad = good;
        bad[i] ^= 0x80;
        spit(path, bad);
        const auto got = cache.get(keyOf(1));
        REQUIRE(got.status == ForgeGetStatus::Corrupt);
        CHECK(got.payload.empty());
        CHECK_FALSE(fs::exists(path));
        CHECK_FALSE(cache.contains(keyOf(1)));
        spit(path, good);
        ForgeCache fresh(dir.path);
        REQUIRE(fresh.scan().kept == 1);
        cache = std::move(fresh);
    }
    CHECK(cache.get(keyOf(1)).payload == payload);
}

TEST_CASE("forge cache get after corruption lets the caller forge and store again", "[forge_cache]")
{
    Dir dir("forge_cache_reforge");
    ForgeCache cache(dir.path);
    cache.put(keyOf(1), payloadOf(64, 4));
    auto bytes = slurp(cache.pathFor(keyOf(1)));
    bytes[bytes.size() - 1] ^= 0xff;
    spit(cache.pathFor(keyOf(1)), bytes);
    REQUIRE(cache.get(keyOf(1)).status == ForgeGetStatus::Corrupt);
    REQUIRE(cache.put(keyOf(1), payloadOf(64, 4)) == ForgePutStatus::Stored);
    CHECK(cache.get(keyOf(1)).status == ForgeGetStatus::Hit);
}

TEST_CASE("forge cache never serves a record copied under another key's name", "[forge_cache]")
{
    Dir dir("forge_cache_swapped");
    {
        ForgeCache cache(dir.path);
        cache.put(keyOf(1), payloadOf(32, 1));
    }
    ForgeCache probe(dir.path);
    fs::copy_file(probe.pathFor(keyOf(1)), probe.pathFor(keyOf(2)));
    const auto report = probe.scan();
    CHECK(report.kept == 1);
    CHECK(report.discarded == 1);
    CHECK(probe.get(keyOf(2)).status == ForgeGetStatus::Miss);
    CHECK(probe.get(keyOf(1)).status == ForgeGetStatus::Hit);
}

TEST_CASE("forge cache scan removes stale temp files and leaves foreign files alone", "[forge_cache]")
{
    Dir dir("forge_cache_scan_files");
    ForgeCache cache(dir.path);
    cache.put(keyOf(1), payloadOf(32, 1));
    const auto temp = tempPath(cache.pathFor(keyOf(2)));
    spit(temp, payloadOf(10, 1));
    spit(dir.path / "notes.txt", payloadOf(10, 1));
    spit(dir.path / "0000000000000001-00000001-0000000000abcdef.icfc.bak1", payloadOf(10, 1));

    ForgeCache again(dir.path);
    again.scan();
    CHECK_FALSE(fs::exists(temp));
    CHECK(fs::exists(dir.path / "notes.txt"));
    CHECK(fs::exists(dir.path / "0000000000000001-00000001-0000000000abcdef.icfc.bak1"));
    CHECK(again.size() == 1);
}

TEST_CASE("forge cache scan drops a file larger than the payload limit", "[forge_cache]")
{
    Dir dir("forge_cache_scan_big");
    {
        ForgeCache cache(dir.path);
        cache.put(keyOf(1), payloadOf(200, 1));
    }
    ForgeCache small(dir.path, {.maxPayloadBytes = 100});
    const auto report = small.scan();
    CHECK(report.kept == 0);
    CHECK(report.discarded == 1);
}

TEST_CASE("forge cache scan of a missing folder is empty and put fails cleanly", "[forge_cache]")
{
    Dir dir("forge_cache_missing");
    ForgeCache cache(dir.path / "nope");
    const auto report = cache.scan();
    CHECK(report.kept == 0);
    CHECK(cache.put(keyOf(1), payloadOf(8, 1)) == ForgePutStatus::IoFailed);
    CHECK(cache.size() == 0);
    CHECK(cache.get(keyOf(1)).status == ForgeGetStatus::Miss);
}

TEST_CASE("forge cache survives a crash during a replace", "[forge_cache]")
{
    Dir dir("forge_cache_crash");
    ForgeCache cache(dir.path);
    const auto first = payloadOf(64, 1);
    REQUIRE(cache.put(keyOf(1), first) == ForgePutStatus::Stored);

    AtomicWriteOptions crash;
    crash.backups = 0;
    crash.interruptAt = AtomicStage::TempPartial;
    const auto next = encodeForgeRecord({2, keyOf(1), payloadOf(64, 2)});
    REQUIRE(writeAtomic(cache.pathFor(keyOf(1)), next, crash).status == AtomicWriteStatus::Interrupted);

    ForgeCache again(dir.path);
    again.scan();
    const auto hit = again.get(keyOf(1));
    REQUIRE(hit.status == ForgeGetStatus::Hit);
    CHECK(hit.payload == first);
}

TEST_CASE("forge cache ignores a forged huge sequence so later puts still work", "[forge_cache]")
{
    Dir dir("forge_cache_poison");
    ForgeCache cache(dir.path);
    const auto key = keyOf(1);
    spit(cache.pathFor(key), encodeForgeRecord({UINT64_MAX - 1, key, payloadOf(8, 1)}));
    const auto report = cache.scan();
    CHECK(report.discarded == 1);
    CHECK(cache.put(keyOf(2), payloadOf(8, 1)) == ForgePutStatus::Stored);
}

TEST_CASE("forge cache keeps the old entry when a replace cannot be written", "[forge_cache]")
{
    Dir dir("forge_cache_failed_put");
    ForgeCache cache(dir.path);
    REQUIRE(cache.put(keyOf(1), payloadOf(16, 1)) == ForgePutStatus::Stored);
    fs::remove(cache.pathFor(keyOf(1)));
    fs::create_directory(cache.pathFor(keyOf(1)));
    CHECK(cache.put(keyOf(1), payloadOf(32, 2)) == ForgePutStatus::IoFailed);
    CHECK(cache.totalBytes() == 16);
    CHECK(cache.contains(keyOf(1)));
}
