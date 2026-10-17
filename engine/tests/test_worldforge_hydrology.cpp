#include <worldforge/hydrology_map.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

ErodedMap makeEroded(std::uint64_t seed, int w, int h, int plates)
{
    auto base = generateTectonics(seed, { w, h, plates });
    REQUIRE(base.has_value());
    auto eroded = erodeTectonics(*base);
    REQUIRE(eroded.has_value());
    return *eroded;
}

ErodedMap flatMap(int w, int h, std::int16_t level)
{
    ErodedMap m;
    m.seed = 1;
    m.width = w;
    m.height = h;
    m.heightM.assign(static_cast<std::size_t>(w) * h, level);
    return m;
}

HydrologyMap hydrology(const ErodedMap& m, const HydrologyParams& p = {})
{
    auto out = computeHydrology(m, p);
    REQUIRE(out.has_value());
    return *out;
}

bool isOutlet(const HydrologyMap& m, std::size_t c) { return m.flowDir[c] == kHydrologyNoFlow; }

std::size_t receiver(const HydrologyMap& m, std::size_t c)
{
    const int x = static_cast<int>(c % m.width) + (std::array<int, 8>{ 1, 1, 0, -1, -1, -1, 0, 1 })[m.flowDir[c]];
    const int y = static_cast<int>(c / m.width) + (std::array<int, 8>{ 0, 1, 1, 1, 0, -1, -1, -1 })[m.flowDir[c]];
    return m.index(x, y);
}

int interiorSinks(const ErodedMap& m, int seaLevel)
{
    int sinks = 0;
    for (int y = 1; y + 1 < m.height; ++y) {
        for (int x = 1; x + 1 < m.width; ++x) {
            const std::int16_t v = m.heightM[static_cast<std::size_t>(y) * m.width + x];
            if (v <= seaLevel) continue;
            bool lower = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx != 0 || dy != 0) && m.heightM[static_cast<std::size_t>(y + dy) * m.width + x + dx] < v) lower = true;
                }
            }
            if (!lower) ++sinks;
        }
    }
    return sinks;
}

void requireDrainsEverywhere(const HydrologyMap& m)
{
    const std::size_t cells = m.heightM.size();
    for (std::size_t c = 0; c < cells; ++c) {
        std::size_t at = c;
        std::size_t steps = 0;
        while (!isOutlet(m, at)) {
            const std::size_t next = receiver(m, at);
            REQUIRE(m.filledM[next] <= m.filledM[at]);
            at = next;
            REQUIRE(++steps <= cells);
        }
    }
}

}

TEST_CASE("hydrology is deterministic across repeat runs", "[worldforge][hydrology]")
{
    const auto eroded = makeEroded(kSeed, 128, 96, 12);
    const auto a = hydrology(eroded);
    const auto b = hydrology(eroded);
    CHECK(a.heightM == b.heightM);
    CHECK(a.accumulation == b.accumulation);
    CHECK(hashHydrologyMap(a) == hashHydrologyMap(b));
}

TEST_CASE("hydrology golden hash is stable", "[worldforge][hydrology]")
{
    CHECK(hashHydrologyMap(hydrology(makeEroded(kSeed, 128, 96, 12))) == 0xf831cefa80216496ull);
}

TEST_CASE("hydrology does not mutate the input and keeps the grid", "[worldforge][hydrology]")
{
    const auto eroded = makeEroded(kSeed, 64, 48, 8);
    const auto copy = eroded;
    const auto out = hydrology(eroded);
    CHECK(eroded.heightM == copy.heightM);
    CHECK(out.width == 64);
    CHECK(out.height == 48);
    CHECK(out.seed == kSeed);
    const std::size_t cells = 64u * 48u;
    CHECK(out.filledM.size() == cells);
    CHECK(out.lakeDepthM.size() == cells);
    CHECK(out.heightM.size() == cells);
    CHECK(out.flowDir.size() == cells);
    CHECK(out.accumulation.size() == cells);
    CHECK(out.river.size() == cells);
}

TEST_CASE("depression filling leaves no sink that the eroded field had", "[worldforge][hydrology]")
{
    auto base = generateTectonics(kSeed, { 128, 128, 12 });
    REQUIRE(base.has_value());
    auto raw = erodeTectonics(*base, ErosionParams{ .iterations = 0 });
    REQUIRE(raw.has_value());
    const ErodedMap& eroded = *raw;
    REQUIRE(interiorSinks(eroded, 0) > 0);
    const auto out = hydrology(eroded);
    requireDrainsEverywhere(out);

    std::int64_t lakeCells = 0;
    for (std::size_t c = 0; c < eroded.heightM.size(); ++c) {
        CHECK(out.filledM[c] >= eroded.heightM[c]);
        CHECK(out.lakeDepthM[c] == out.filledM[c] - eroded.heightM[c]);
        if (out.lakeDepthM[c] != 0) ++lakeCells;
    }
    CHECK(lakeCells > 0);
}

TEST_CASE("a pit in a plateau fills to its rim and drains", "[worldforge][hydrology]")
{
    auto m = flatMap(32, 32, 100);
    const std::size_t pit = 16u * 32u + 16u;
    m.heightM[pit] = 40;
    m.heightM[pit + 1] = 60;
    const auto out = hydrology(m);
    CHECK(out.filledM[pit] == 100);
    CHECK(out.lakeDepthM[pit] == 60);
    CHECK(out.lakeDepthM[pit + 1] == 40);
    CHECK(out.lakeDepthM[0] == 0);
    requireDrainsEverywhere(out);
}

TEST_CASE("accumulation conserves the rain that falls", "[worldforge][hydrology]")
{
    const auto eroded = makeEroded(kSeed, 128, 96, 12);
    const auto out = hydrology(eroded);
    std::uint64_t atOutlets = 0;
    std::int64_t outlets = 0;
    for (std::size_t c = 0; c < out.accumulation.size(); ++c) {
        CHECK(out.accumulation[c] >= 1);
        if (isOutlet(out, c)) {
            atOutlets += out.accumulation[c];
            ++outlets;
        } else {
            CHECK(out.accumulation[receiver(out, c)] > out.accumulation[c]);
        }
    }
    CHECK(outlets > 0);
    CHECK(atOutlets == out.accumulation.size());
}

TEST_CASE("a tilted plane collects flow into the downhill rows", "[worldforge][hydrology]")
{
    auto m = flatMap(32, 32, 0);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) m.heightM[static_cast<std::size_t>(y) * 32 + x] = static_cast<std::int16_t>(10 + 10 * (31 - y));
    }
    const auto out = hydrology(m);
    const auto rowSum = [&](int y) {
        std::uint64_t sum = 0;
        for (int x = 0; x < 32; ++x) sum += out.accumulation[static_cast<std::size_t>(y) * 32 + x];
        return sum;
    };
    CHECK(rowSum(30) > 4 * rowSum(2));
}

TEST_CASE("rivers exist and carving lowers the mean channel height", "[worldforge][hydrology]")
{
    const auto eroded = makeEroded(kSeed, 128, 128, 12);
    const auto out = hydrology(eroded);

    std::int64_t riverCells = 0;
    std::int64_t carved = 0;
    std::int64_t before = 0;
    std::int64_t after = 0;
    for (std::size_t c = 0; c < eroded.heightM.size(); ++c) {
        if (!out.river[c]) {
            CHECK(out.heightM[c] == eroded.heightM[c]);
            continue;
        }
        CHECK(eroded.heightM[c] > 0);
        CHECK(out.accumulation[c] >= 48);
        CHECK(out.heightM[c] <= eroded.heightM[c]);
        CHECK(out.heightM[c] >= 1);
        ++riverCells;
        before += eroded.heightM[c];
        after += out.heightM[c];
        if (out.heightM[c] < eroded.heightM[c]) ++carved;
    }
    REQUIRE(riverCells > 50);
    CHECK(carved > riverCells / 2);
    CHECK(static_cast<double>(after) / riverCells < static_cast<double>(before) / riverCells - 1.0);
}

TEST_CASE("river threshold and carving parameters take effect", "[worldforge][hydrology]")
{
    const auto eroded = makeEroded(kSeed, 128, 128, 12);
    const auto count = [](const HydrologyMap& m) {
        std::int64_t n = 0;
        for (const auto v : m.river) n += v;
        return n;
    };
    HydrologyParams low;
    low.riverThreshold = 16;
    HydrologyParams high;
    high.riverThreshold = 400;
    CHECK(count(hydrology(eroded, low)) > count(hydrology(eroded, high)));
    CHECK(count(hydrology(eroded, high)) > 0);

    HydrologyParams none;
    none.maxCarveM = 0;
    CHECK(hydrology(eroded, none).heightM == eroded.heightM);
    none = {};
    none.carveRate = 0;
    CHECK(hydrology(eroded, none).heightM == eroded.heightM);

    HydrologyParams impossible;
    impossible.riverThreshold = kHydrologyMaxRiverThreshold;
    CHECK(count(hydrology(eroded, impossible)) == 0);
}

TEST_CASE("different seeds give different hydrology", "[worldforge][hydrology]")
{
    std::set<std::uint64_t> hashes;
    for (std::uint64_t s = 0; s < 6; ++s) hashes.insert(hashHydrologyMap(hydrology(makeEroded(s, 64, 64, 8))));
    CHECK(hashes.size() == 6);
}

TEST_CASE("hydrology rejects malformed input and parameters", "[worldforge][hydrology]")
{
    const auto base = makeEroded(kSeed, 64, 64, 8);

    auto wrongLength = base;
    wrongLength.heightM.pop_back();
    CHECK_FALSE(computeHydrology(wrongLength).has_value());

    auto empty = base;
    empty.heightM.clear();
    CHECK_FALSE(computeHydrology(empty).has_value());
    CHECK_FALSE(computeHydrology(ErodedMap{}).has_value());

    auto hostile = base;
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::max();
    CHECK_FALSE(computeHydrology(hostile).has_value());
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::min();
    CHECK_FALSE(computeHydrology(hostile).has_value());

    auto tiny = base;
    tiny.width = 2;
    CHECK_FALSE(computeHydrology(tiny).has_value());

    auto huge = base;
    huge.width = 1 << 30;
    huge.height = 1 << 30;
    CHECK_FALSE(computeHydrology(huge).has_value());

    HydrologyParams p;
    p.seaLevelM = kTectonicsMaxHeightM + 1;
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p = {};
    p.seaLevelM = kTectonicsMinHeightM - 1;
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p = {};
    p.riverThreshold = 0;
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p.riverThreshold = std::numeric_limits<std::int64_t>::max();
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p = {};
    p.carveRate = -1;
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p.carveRate = std::numeric_limits<int>::max();
    CHECK_FALSE(computeHydrology(base, p).has_value());
    p = {};
    p.maxCarveM = kHydrologyMaxCarveM + 1;
    CHECK_FALSE(computeHydrology(base, p).has_value());
}

TEST_CASE("hydrology survives degenerate and extreme terrain", "[worldforge][hydrology]")
{
    SECTION("all flat land")
    {
        const auto out = hydrology(flatMap(64, 64, 500));
        requireDrainsEverywhere(out);
        for (const auto v : out.lakeDepthM) CHECK(v == 0);
    }
    SECTION("all ocean")
    {
        const auto out = hydrology(flatMap(64, 64, -100));
        for (std::size_t c = 0; c < out.flowDir.size(); ++c) {
            CHECK(isOutlet(out, c));
            CHECK(out.river[c] == 0);
            CHECK(out.accumulation[c] == 1);
        }
    }
    SECTION("alternating extremes")
    {
        auto m = flatMap(64, 64, 0);
        for (std::size_t c = 0; c < m.heightM.size(); ++c) m.heightM[c] = (c % 2 == 0) ? kTectonicsMaxHeightM : kTectonicsMinHeightM;
        HydrologyParams p;
        p.riverThreshold = 1;
        p.carveRate = kHydrologyMaxCarveRate;
        p.maxCarveM = kHydrologyMaxCarveM;
        const auto out = hydrology(m, p);
        requireDrainsEverywhere(out);
        for (const auto v : out.heightM) {
            CHECK(v >= kTectonicsMinHeightM);
            CHECK(v <= kTectonicsMaxHeightM);
        }
    }
    SECTION("deep basin under a high rim")
    {
        auto m = flatMap(64, 64, kTectonicsMaxHeightM);
        for (int y = 8; y < 56; ++y) {
            for (int x = 8; x < 56; ++x) m.heightM[static_cast<std::size_t>(y) * 64 + x] = 1;
        }
        const auto out = hydrology(m);
        requireDrainsEverywhere(out);
        CHECK(out.lakeDepthM[32u * 64u + 32u] == kTectonicsMaxHeightM - 1);
    }
    SECTION("largest grid")
    {
        auto m = makeEroded(kSeed, kTectonicsMaxSize, kTectonicsMaxSize, 16);
        const auto out = hydrology(m);
        std::uint64_t atOutlets = 0;
        for (std::size_t c = 0; c < out.accumulation.size(); ++c) {
            if (isOutlet(out, c)) atOutlets += out.accumulation[c];
        }
        CHECK(atOutlets == out.accumulation.size());
    }
}
