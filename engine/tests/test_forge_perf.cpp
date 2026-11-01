#include "codec_fuzz.h"

#include <catch2/catch_test_macros.hpp>

#include <worldforge/forge_world.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// Forge pipeline performance budget (SEED-450). forgeWorld is run at kGuardSize with per-phase
// timing and allocation counting, then compared to the numbers recorded in docs/forge-performance.md.
//
// Wall clock on a shared runner is noisy, so no absolute time is asserted. Two kinds of check:
//  - Time: each phase time is divided by the time of a fixed reference workload measured on the
//    same machine in the same run, and that ratio must stay under a generous multiple of the
//    recorded ratio (best of a few runs, because noise only adds time). Sanitizer builds skip
//    this check: instrumentation slows memory-heavy phases and the reference loop unevenly.
//  - Memory: allocation volume per phase and the largest single allocation come from the test
//    binary's allocation hook, so they are exact and identical on every machine. They are held to
//    a tight multiple.
// The hidden "[.perf_report]" case prints the table used to refresh the record.

using namespace worldforge;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kPhases = kForgePhaseCount;

struct Measure {
    std::array<double, kPhases> phaseNs{};
    std::array<std::int64_t, kPhases> phaseAllocBytes{};
    std::int64_t maxSingleAlloc = 0;
    double referenceNs = 0;
};

ForgeConfig perfConfig()
{
    ForgeConfig c;
    c.itemIds = { "sword", "shield", "ring", "lantern" };
    c.npcArchetypes = { "smith", "healer" };
    c.dungeonArchetypes = { "crypt", "mine" };
    FactionSpec a;
    a.strategy = ClaimStrategy::Contiguous;
    FactionSpec b;
    b.strategy = ClaimStrategy::ResourceFollowing;
    c.factions = { a, b };
    return c;
}

double referenceNs()
{
    std::vector<std::uint32_t> table(1 << 18, 1u);
    std::uint64_t x = 0x9E3779B97F4A7C15ull;
    const auto t0 = Clock::now();
    for (int i = 0; i < (1 << 24); ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        table[static_cast<std::size_t>(x) & (table.size() - 1)] += static_cast<std::uint32_t>(x >> 40);
    }
    const auto t1 = Clock::now();
    volatile std::uint32_t sink = table[x & (table.size() - 1)];
    (void)sink;
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

Measure measureOnce(int size)
{
    Measure m;
    m.referenceNs = referenceNs();
    std::array<Clock::time_point, kPhases> start{};
    std::array<std::size_t, kPhases> allocAtStart{};
    const ForgeConfig config = perfConfig();
    auto& alloc = ichor::fuzz::detail::allocState();
    alloc = {};
    alloc.maxSingle = std::numeric_limits<std::size_t>::max();
    alloc.maxTotal = std::numeric_limits<std::size_t>::max();
    alloc.active = true;
    bool complete = false;
    {
        const ForgedWorld w = forgeWorld(11, size, config, ForgePhase::Content, [&](const ForgeProgress& p) {
            const auto i = static_cast<std::size_t>(p.phase) - 1;
            if (!p.finished) {
                allocAtStart[i] = alloc.total;
                start[i] = Clock::now();
            } else {
                m.phaseNs[i] = std::chrono::duration<double, std::nano>(Clock::now() - start[i]).count();
                m.phaseAllocBytes[i] = static_cast<std::int64_t>(alloc.total - allocAtStart[i]);
            }
        });
        complete = w.complete();
    }
    m.maxSingleAlloc = static_cast<std::int64_t>(alloc.worst);
    alloc = {};
    REQUIRE(complete);
    return m;
}

// Best of `runs` per phase: noise only ever adds time, so the minimum is the stable estimate.
Measure measure(int size, int runs)
{
    Measure best = measureOnce(size);
    for (int r = 1; r < runs; ++r) {
        const Measure m = measureOnce(size);
        for (std::size_t i = 0; i < kPhases; ++i) best.phaseNs[i] = std::min(best.phaseNs[i], m.phaseNs[i]);
        best.referenceNs = std::min(best.referenceNs, m.referenceNs);
    }
    return best;
}

struct Record {
    std::array<double, kPhases> timeRatio;
    std::array<std::int64_t, kPhases> allocBytes;
    std::int64_t maxSingleAlloc;
};

// Recorded at kGuardSize, see docs/forge-performance.md. timeRatio is phase time over the
// reference workload time.
constexpr int kGuardSize = 256;
constexpr Record kRecord = {
    { 0.18, 12.2, 0.13, 0.026, 0.040, 0.032, 0.11, 1.5, 0.058, 0.21, 0.001 },
    { 1279143, 113652606, 2360268, 524405, 196725, 1048888, 2165163, 4428318, 857842, 132251, 25405 },
    524288,
};

// A phase may cost this many times its recorded ratio before the guard fails. Wide enough for
// a loaded shared runner and a different CPU, narrow enough that a phase made 10x slower fails.
constexpr double kTimeMultiple = 6.0;
// Phases cheaper than this fraction of the reference are too small to time reliably, so their
// limit is computed from the floor instead.
constexpr double kRatioFloor = 0.02;
constexpr double kAllocMultiple = 1.5;

#if defined(__SANITIZE_ADDRESS__)
constexpr bool kSanitized = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
constexpr bool kSanitized = true;
#else
constexpr bool kSanitized = false;
#endif
#else
constexpr bool kSanitized = false;
#endif

struct Verdict {
    bool ok = true;
    std::string why;
    void fail(const std::string& s)
    {
        ok = false;
        why += s + "; ";
    }
};

Verdict checkAgainstRecord(const Measure& m, const Record& rec, bool checkTime = !kSanitized)
{
    Verdict v;
    for (std::size_t i = 0; i < kPhases; ++i) {
        const std::string name(forgePhaseName(static_cast<ForgePhase>(i + 1)));
        const double ratio = m.phaseNs[i] / m.referenceNs;
        const double limit = std::max(rec.timeRatio[i], kRatioFloor) * kTimeMultiple;
        if (checkTime && ratio > limit) v.fail(name + " time ratio " + std::to_string(ratio) + " over limit " + std::to_string(limit));
        const double allocLimit = static_cast<double>(rec.allocBytes[i]) * kAllocMultiple;
        if (static_cast<double>(m.phaseAllocBytes[i]) > allocLimit)
            v.fail(name + " allocated " + std::to_string(m.phaseAllocBytes[i]) + " bytes over limit " + std::to_string(static_cast<std::int64_t>(allocLimit)));
    }
    if (static_cast<double>(m.maxSingleAlloc) > static_cast<double>(rec.maxSingleAlloc) * kAllocMultiple)
        v.fail("largest allocation " + std::to_string(m.maxSingleAlloc) + " over " + std::to_string(rec.maxSingleAlloc) + " x " + std::to_string(kAllocMultiple));
    return v;
}

Measure atRecord()
{
    Measure m;
    m.referenceNs = 1e8;
    for (std::size_t i = 0; i < kPhases; ++i) {
        m.phaseNs[i] = kRecord.timeRatio[i] * m.referenceNs;
        m.phaseAllocBytes[i] = kRecord.allocBytes[i];
    }
    m.maxSingleAlloc = kRecord.maxSingleAlloc;
    return m;
}

}

TEST_CASE("forge perf report", "[.perf_report]")
{
    for (const int size : { 256, 512, 1024 }) {
        const Measure m = measure(size, 3);
        std::printf("size %d reference %.1f ms largest single allocation %.2f MiB\n", size, m.referenceNs / 1e6, static_cast<double>(m.maxSingleAlloc) / (1024.0 * 1024.0));
        for (std::size_t i = 0; i < kPhases; ++i)
            std::printf("  %-12s %9.2f ms ratio %.4f alloc %lld\n", std::string(forgePhaseName(static_cast<ForgePhase>(i + 1))).c_str(), m.phaseNs[i] / 1e6, m.phaseNs[i] / m.referenceNs, static_cast<long long>(m.phaseAllocBytes[i]));
        std::fflush(stdout);
    }
}

TEST_CASE("every forge phase stays within a generous multiple of its recorded cost", "[forge_perf]")
{
    const Measure m = measure(kGuardSize, 3);
    const Verdict v = checkAgainstRecord(m, kRecord);
    INFO(v.why);
    CHECK(v.ok);
}

TEST_CASE("the recorded numbers describe a run that has real work in every phase", "[forge_perf]")
{
    for (std::size_t i = 0; i < kPhases; ++i) {
        INFO("phase " << forgePhaseName(static_cast<ForgePhase>(i + 1)));
        CHECK(kRecord.allocBytes[i] > 0);
    }
    CHECK(kRecord.maxSingleAlloc > 0);
}

TEST_CASE("allocation counts are identical between runs", "[forge_perf]")
{
    const Measure a = measureOnce(64);
    const Measure b = measureOnce(64);
    CHECK(a.phaseAllocBytes == b.phaseAllocBytes);
    CHECK(a.maxSingleAlloc == b.maxSingleAlloc);
}

TEST_CASE("the guard accepts the record and goes red when a phase is made much slower", "[forge_perf]")
{
    const Measure base = atRecord();
    REQUIRE(checkAgainstRecord(base, kRecord, true).ok);

    for (std::size_t i = 0; i < kPhases; ++i) {
        Measure slow = base;
        const double recorded = std::max(kRecord.timeRatio[i], kRatioFloor);
        slow.phaseNs[i] = recorded * 10.0 * slow.referenceNs;
        INFO("phase " << forgePhaseName(static_cast<ForgePhase>(i + 1)) << " made 10x slower");
        CHECK_FALSE(checkAgainstRecord(slow, kRecord, true).ok);
    }
}

TEST_CASE("the guard goes red when a phase allocates much more", "[forge_perf]")
{
    const Measure base = atRecord();
    for (std::size_t i = 0; i < kPhases; ++i) {
        Measure fat = base;
        fat.phaseAllocBytes[i] = kRecord.allocBytes[i] * 2;
        INFO("phase " << forgePhaseName(static_cast<ForgePhase>(i + 1)) << " allocating 2x");
        CHECK_FALSE(checkAgainstRecord(fat, kRecord).ok);
    }
    Measure big = base;
    big.maxSingleAlloc = kRecord.maxSingleAlloc * 2;
    CHECK_FALSE(checkAgainstRecord(big, kRecord).ok);
}

TEST_CASE("the allocation hook sees a known allocation", "[forge_perf]")
{
    auto& alloc = ichor::fuzz::detail::allocState();
    alloc = {};
    alloc.maxSingle = std::numeric_limits<std::size_t>::max();
    alloc.maxTotal = std::numeric_limits<std::size_t>::max();
    alloc.active = true;
    {
        std::vector<std::uint8_t> v(1 << 20);
        v[0] = 1;
        // A release build drops an allocation nothing observes, so the pointer has to escape.
        static const volatile void* sink;
        sink = v.data();
    }
    CHECK(alloc.total >= (1u << 20));
    CHECK(alloc.worst >= (1u << 20));
    alloc = {};
}
