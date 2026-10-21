#include <worldforge/climate_map.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;
constexpr int kW = 128;
constexpr int kH = 64;
constexpr int kRidgeStart = 60;
constexpr int kRidgePeak = 70;
constexpr int kRidgeEnd = 80;

ErodedMap makeErodedBase(std::uint64_t seed, int w, int h, int plates)
{
    auto tectonics = generateTectonics(seed, { w, h, plates });
    REQUIRE(tectonics.has_value());
    auto eroded = erodeTectonics(*tectonics);
    REQUIRE(eroded.has_value());
    return *eroded;
}

// Ocean to the west (x < 16), flat 100 m land, and a triangular ridge peaking at 3000 m.
ErodedMap makeRidge()
{
    ErodedMap m;
    m.seed = 1;
    m.width = kW;
    m.height = kH;
    m.heightM.assign(static_cast<std::size_t>(kW) * kH, 100);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            int v = 100;
            if (x < 16) v = -200;
            else if (x >= kRidgeStart && x <= kRidgePeak) v = 100 + (x - kRidgeStart) * 290;
            else if (x > kRidgePeak && x <= kRidgeEnd) v = 100 + (kRidgeEnd - x) * 290;
            m.heightM[m.index(x, y)] = static_cast<std::int16_t>(v);
        }
    }
    return m;
}

ClimateParams ridgeParams()
{
    ClimateParams p;
    p.landLoss = 1;
    return p;
}

ClimateMap climate(const ErodedMap& base, const ClimateParams& params = {})
{
    auto out = generateClimate(base, params);
    REQUIRE(out.has_value());
    return *out;
}

double meanMoisture(const ClimateMap& m, int x0, int x1)
{
    std::int64_t sum = 0;
    std::int64_t count = 0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = x0; x < x1; ++x) {
            sum += m.moisture[m.index(x, y)];
            ++count;
        }
    }
    return static_cast<double>(sum) / static_cast<double>(count);
}

ClimateParams noLift()
{
    ClimateParams p = ridgeParams();
    p.orographicGain = 0;
    return p;
}

ClimateParams noLapse()
{
    ClimateParams p = ridgeParams();
    p.lapseC10PerKm = 0;
    return p;
}

ClimateParams noOrography()
{
    ClimateParams p = noLift();
    p.lapseC10PerKm = 0;
    return p;
}

ErodedMap makeNoisy(std::uint64_t seed, int w, int h)
{
    ErodedMap m;
    m.seed = seed;
    m.width = w;
    m.height = h;
    m.heightM.resize(static_cast<std::size_t>(w) * h);
    for (std::size_t c = 0; c < m.heightM.size(); ++c) {
        const auto r = static_cast<std::int64_t>(splitmix64(seed ^ c) % 4001);
        const int x = static_cast<int>(c % static_cast<std::size_t>(w));
        m.heightM[c] = static_cast<std::int16_t>(x < w / 4 ? -300 : r - 500 + (x * 20));
    }
    return m;
}

}

TEST_CASE("climate is deterministic across repeat runs", "[worldforge][climate]")
{
    const auto base = makeErodedBase(kSeed, 128, 96, 12);
    const auto a = climate(base);
    const auto b = climate(base);
    CHECK(a.temperatureC10 == b.temperatureC10);
    CHECK(a.moisture == b.moisture);
    CHECK(hashClimateMap(a) == hashClimateMap(b));
}

TEST_CASE("climate golden hash is stable", "[worldforge][climate]")
{
    const auto out = climate(makeNoisy(kSeed, 128, 96));
    CHECK(hashClimateMap(out) == 0xab96735ced7e1b75ull);
}

TEST_CASE("climate does not mutate the input and keeps the grid", "[worldforge][climate]")
{
    const auto base = makeErodedBase(kSeed, 64, 48, 8);
    const auto copy = base;
    const auto out = climate(base);
    CHECK(base.heightM == copy.heightM);
    CHECK(out.width == 64);
    CHECK(out.height == 48);
    CHECK(out.seed == kSeed);
    CHECK(out.temperatureC10.size() == 64u * 48u);
    CHECK(out.moisture.size() == 64u * 48u);
}

TEST_CASE("different seeds give different climates", "[worldforge][climate]")
{
    std::set<std::uint64_t> hashes;
    for (std::uint64_t s = 0; s < 8; ++s) hashes.insert(hashClimateMap(climate(makeErodedBase(s, 64, 64, 8))));
    CHECK(hashes.size() == 8);
}

TEST_CASE("rain shadow: the lee of a ridge is drier than the windward side", "[worldforge][climate]")
{
    const auto base = makeRidge();
    const auto withRidge = climate(base, ridgeParams());
    const double windward = meanMoisture(withRidge, 30, kRidgeStart);
    const double lee = meanMoisture(withRidge, kRidgeEnd + 10, kRidgeEnd + 40);
    CHECK(windward > 2.0 * lee);

    const auto liftOnly = climate(base, noLapse());
    CHECK(meanMoisture(liftOnly, 30, kRidgeStart) > 2.0 * meanMoisture(liftOnly, kRidgeEnd + 10, kRidgeEnd + 40));
    const auto coolingOnly = climate(base, noLift());
    CHECK(meanMoisture(coolingOnly, 30, kRidgeStart) > 1.5 * meanMoisture(coolingOnly, kRidgeEnd + 10, kRidgeEnd + 40));

    const auto flat = climate(base, noOrography());
    const double flatWindward = meanMoisture(flat, 30, kRidgeStart);
    const double flatLee = meanMoisture(flat, kRidgeEnd + 10, kRidgeEnd + 40);
    CHECK(flatWindward < 1.5 * flatLee);
}

TEST_CASE("rain shadow follows the wind direction", "[worldforge][climate]")
{
    const auto base = makeRidge();
    auto mirrored = base;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) mirrored.heightM[mirrored.index(x, y)] = base.heightM[base.index(kW - 1 - x, y)];
    }
    ClimateParams west = ridgeParams();
    west.windDirection = 4;
    const auto out = climate(mirrored, west);
    const double windward = meanMoisture(out, kW - kRidgeStart, kW - 30);
    const double lee = meanMoisture(out, kW - kRidgeEnd - 40, kW - kRidgeEnd - 10);
    CHECK(windward > 2.0 * lee);
}

TEST_CASE("every wind direction produces a rain shadow behind a ridge", "[worldforge][climate]")
{
    ErodedMap m;
    m.seed = 2;
    m.width = 96;
    m.height = 96;
    m.heightM.assign(96u * 96u, 100);
    for (int y = 0; y < 96; ++y) {
        for (int x = 0; x < 96; ++x) {
            const int d = std::abs(x - 48) + std::abs(y - 48);
            if (d < 6) m.heightM[m.index(x, y)] = static_cast<std::int16_t>(100 + (6 - d) * 450);
        }
    }
    for (int dir = 0; dir < kClimateWindDirections; ++dir) {
        ClimateParams p;
        p.windDirection = dir;
        p.landLoss = 4;
        const auto withRidge = climate(m, p);
        const auto flat = climate(m, [&] {
            auto q = noOrography();
            q.windDirection = dir;
            q.landLoss = 4;
            return q;
        }());
        const int ldx = detail::kErosionDx[dir];
        const int ldy = detail::kErosionDy[dir];
        const std::size_t lee = m.index(48 + ldx * 9, 48 + ldy * 9);
        const std::size_t windward = m.index(48 - ldx * 9, 48 - ldy * 9);
        CHECK(withRidge.moisture[lee] < withRidge.moisture[windward]);
        CHECK(withRidge.moisture[lee] < flat.moisture[lee]);
    }
}

TEST_CASE("temperature falls with elevation and with latitude", "[worldforge][climate]")
{
    const auto base = makeRidge();
    const auto out = climate(base);
    const int row = kH / 2;
    const auto flatLand = out.temperatureC10[out.index(40, row)];
    const auto peak = out.temperatureC10[out.index(kRidgePeak, row)];
    CHECK(peak < flatLand - 150);

    const auto noLapse = climate(base, noOrography());
    CHECK(noLapse.temperatureC10[noLapse.index(kRidgePeak, row)] == noLapse.temperatureC10[noLapse.index(40, row)]);

    CHECK(out.temperatureC10[out.index(40, kH / 2)] > out.temperatureC10[out.index(40, 1)]);
    CHECK(out.temperatureC10[out.index(40, 1)] == out.temperatureC10[out.index(40, kH - 2)]);

    ClimateParams exact;
    exact.equatorTempC10 = 300;
    exact.poleTempC10 = -300;
    exact.lapseC10PerKm = 65;
    const auto e = climate(base, exact);
    CHECK(e.temperatureC10[e.index(40, kH / 2)] == 300 - 600 * (1024 * std::abs(2 * (kH / 2) + 1 - kH) / kH) / 1024 - 100 * 65 / 1000);
    CHECK(e.temperatureC10[e.index(kRidgePeak, kH / 2)] == e.temperatureC10[e.index(40, kH / 2)] + 100 * 65 / 1000 - 3000 * 65 / 1000);
}

TEST_CASE("real terrain: high land is colder than low land in the same latitude band", "[worldforge][climate]")
{
    // Not every seed raises mountains inside this band, so take the first one that does.
    for (std::uint64_t seed = kSeed; seed < kSeed + 32; ++seed) {
        const auto base = makeErodedBase(seed, 256, 128, 16);
        const auto out = climate(base);
        std::int64_t highSum = 0, lowSum = 0, highN = 0, lowN = 0;
        for (int y = 48; y < 80; ++y) {
            for (int x = 0; x < 256; ++x) {
                const auto c = base.index(x, y);
                if (base.heightM[c] > 1000) {
                    highSum += out.temperatureC10[c];
                    ++highN;
                } else if (base.heightM[c] > 0 && base.heightM[c] < 300) {
                    lowSum += out.temperatureC10[c];
                    ++lowN;
                }
            }
        }
        if (highN == 0 || lowN == 0) continue;
        CHECK(static_cast<double>(highSum) / highN + 50.0 < static_cast<double>(lowSum) / lowN);
        return;
    }
    FAIL("no seed has both high and low land in the band");
}

TEST_CASE("moisture and temperature stay in range", "[worldforge][climate]")
{
    const auto out = climate(makeErodedBase(kSeed, 128, 128, 12));
    for (std::size_t c = 0; c < out.moisture.size(); ++c) {
        CHECK(out.moisture[c] <= kClimateMaxMoisture);
        CHECK(out.temperatureC10[c] >= -2200);
        CHECK(out.temperatureC10[c] <= kClimateMaxTempC10);
    }
}

TEST_CASE("climate rejects malformed input and parameters", "[worldforge][climate]")
{
    const auto base = makeErodedBase(kSeed, 64, 64, 8);

    auto wrongLength = base;
    wrongLength.heightM.pop_back();
    CHECK_FALSE(generateClimate(wrongLength).has_value());

    auto empty = base;
    empty.heightM.clear();
    CHECK_FALSE(generateClimate(empty).has_value());

    auto hostile = base;
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::max();
    CHECK_FALSE(generateClimate(hostile).has_value());
    hostile.heightM[5] = std::numeric_limits<std::int16_t>::min();
    CHECK_FALSE(generateClimate(hostile).has_value());

    auto tiny = base;
    tiny.width = 2;
    CHECK_FALSE(generateClimate(tiny).has_value());

    auto huge = base;
    huge.width = 1 << 30;
    huge.height = 1 << 30;
    CHECK_FALSE(generateClimate(huge).has_value());

    const auto rejects = [&](auto mutate) {
        ClimateParams p;
        mutate(p);
        return !generateClimate(base, p).has_value();
    };
    CHECK(rejects([](ClimateParams& p) { p.windDirection = -1; }));
    CHECK(rejects([](ClimateParams& p) { p.windDirection = 8; }));
    CHECK(rejects([](ClimateParams& p) { p.equatorTempC10 = 601; }));
    CHECK(rejects([](ClimateParams& p) { p.poleTempC10 = std::numeric_limits<int>::min(); }));
    CHECK(rejects([](ClimateParams& p) { p.lapseC10PerKm = -1; }));
    CHECK(rejects([](ClimateParams& p) { p.lapseC10PerKm = std::numeric_limits<int>::max(); }));
    CHECK(rejects([](ClimateParams& p) { p.evaporation = 1025; }));
    CHECK(rejects([](ClimateParams& p) { p.inflow = -5; }));
    CHECK(rejects([](ClimateParams& p) { p.orographicGain = 257; }));
    CHECK(rejects([](ClimateParams& p) { p.landLoss = std::numeric_limits<int>::max(); }));
}

TEST_CASE("climate survives extreme parameters and extreme heights", "[worldforge][climate]")
{
    auto base = makeErodedBase(kSeed, 64, 64, 8);
    for (std::size_t c = 0; c < base.heightM.size(); ++c) base.heightM[c] = (c % 2 == 0) ? kTectonicsMaxHeightM : kTectonicsMinHeightM;

    ClimateParams p;
    p.equatorTempC10 = kClimateMaxTempC10;
    p.poleTempC10 = kClimateMinTempC10;
    p.lapseC10PerKm = kClimateMaxLapseC10PerKm;
    p.evaporation = kClimateMaxMoisture;
    p.inflow = kClimateMaxMoisture;
    p.orographicGain = kClimateMaxRate;
    p.landLoss = kClimateMaxRate;
    for (int dir = 0; dir < kClimateWindDirections; ++dir) {
        p.windDirection = dir;
        const auto out = climate(base, p);
        for (const auto v : out.moisture) CHECK(v <= kClimateMaxMoisture);
        for (const auto t : out.temperatureC10) CHECK(t >= -2200);
    }

    ClimateParams zero{ 0, 0, 0, 0, 0, 0, 0, 0 };
    const auto dry = climate(base, zero);
    for (const auto v : dry.moisture) CHECK(v == 0);

    ErodedMap all;
    all.seed = 3;
    all.width = kTectonicsMinSize;
    all.height = kTectonicsMinSize;
    all.heightM.assign(static_cast<std::size_t>(all.width) * all.height, kTectonicsMinHeightM);
    CHECK(generateClimate(all).has_value());
    all.heightM.assign(all.heightM.size(), kTectonicsMaxHeightM);
    CHECK(generateClimate(all).has_value());
}

TEST_CASE("largest grid runs within bounded memory and time", "[worldforge][climate]")
{
    ErodedMap big;
    big.seed = 4;
    big.width = kTectonicsMaxSize;
    big.height = kTectonicsMaxSize;
    big.heightM.assign(static_cast<std::size_t>(big.width) * big.height, 50);
    const auto out = generateClimate(big);
    REQUIRE(out.has_value());
    CHECK(out->moisture.size() == big.heightM.size());
}

TEST_CASE("a coast is not a mountain and ocean humidity is capped by temperature", "[worldforge][climate]")
{
    auto base = makeRidge();
    auto p = ridgeParams();
    p.landLoss = 0;
    p.evaporation = 0;
    p.inflow = 1024;
    p.lapseC10PerKm = 0;
    const auto out = climate(base, p);
    const int row = kH / 2;
    CHECK(out.moisture[out.index(16, row)] == 0);
    CHECK(out.moisture[out.index(17, row)] == 0);
    CHECK(out.moisture[out.index(0, row)] == detail::climateCapacity(out.temperatureC10[out.index(0, row)]));
    CHECK(out.moisture[out.index(15, row)] <= detail::climateCapacity(out.temperatureC10[out.index(15, row)]));
}

TEST_CASE("inflow reaches the first land cell when the map edge is land", "[worldforge][climate]")
{
    ErodedMap m;
    m.seed = 5;
    m.width = 32;
    m.height = 32;
    m.heightM.assign(32u * 32u, 100);
    ClimateParams p;
    p.landLoss = 128;
    p.inflow = 512;
    const auto out = climate(m, p);
    CHECK(out.moisture[out.index(0, 10)] > 0);
    CHECK(out.moisture[out.index(0, 10)] > out.moisture[out.index(20, 10)]);
}
