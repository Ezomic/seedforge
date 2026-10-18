#include <worldforge/tectonics_map.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>
#include <vector>

using namespace worldforge;

namespace {

constexpr std::uint64_t kSeed = 0x123456789abcdef0ull;

TectonicMap make(std::uint64_t seed, int w, int h, int plates)
{
    auto map = generateTectonics(seed, { w, h, plates });
    REQUIRE(map.has_value());
    return *map;
}

bool touchesOtherPlate(const TectonicMap& m, int x, int y)
{
    const int id = m.plateId[m.index(x, y)];
    constexpr int dx[4] = { 1, 0, -1, 0 };
    constexpr int dy[4] = { 0, 1, 0, -1 };
    for (int n = 0; n < 4; ++n) {
        const int nx = x + dx[n];
        const int ny = y + dy[n];
        if (nx >= 0 && ny >= 0 && nx < m.width && ny < m.height && m.plateId[m.index(nx, ny)] != id) return true;
    }
    return false;
}

}

TEST_CASE("tectonics is deterministic across repeat runs", "[worldforge][tectonics]")
{
    const auto a = make(kSeed, 128, 96, 12);
    const auto b = make(kSeed, 128, 96, 12);
    CHECK(a.plateId == b.plateId);
    CHECK(a.boundary == b.boundary);
    CHECK(a.heightM == b.heightM);
    CHECK(hashTectonicMap(a) == hashTectonicMap(b));
}

TEST_CASE("tectonics: different seeds give different maps", "[worldforge][tectonics]")
{
    std::set<std::uint64_t> hashes;
    std::set<std::vector<std::uint8_t>> layouts;
    for (std::uint64_t s = 0; s < 16; ++s) {
        const auto m = make(s, 64, 64, 8);
        hashes.insert(hashTectonicMap(m));
        layouts.insert(m.plateId);
    }
    CHECK(hashes.size() == 16);
    CHECK(layouts.size() >= 15);
}

TEST_CASE("tectonics: every cell has one valid plate and boundary cells sit between different plates", "[worldforge][tectonics]")
{
    for (std::uint64_t s = 1; s <= 24; ++s) {
        const int w = 32 + static_cast<int>(s) * 5;
        const int h = 48 + static_cast<int>(s) * 3;
        const int plates = 2 + static_cast<int>(s % 20);
        const auto m = make(s, w, h, plates);
        REQUIRE(m.plates.size() == static_cast<std::size_t>(plates));
        REQUIRE(m.plateId.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        REQUIRE(m.boundary.size() == m.plateId.size());
        REQUIRE(m.heightM.size() == m.plateId.size());
        std::set<std::pair<int, int>> seeds;
        for (const auto& p : m.plates) {
            CHECK(p.seedX >= 0);
            CHECK(p.seedX < w);
            CHECK(p.seedY >= 0);
            CHECK(p.seedY < h);
            CHECK(seeds.insert({ p.seedX, p.seedY }).second);
            CHECK(p.driftX >= -8);
            CHECK(p.driftX <= 8);
            CHECK(p.driftY >= -8);
            CHECK(p.driftY <= 8);
        }
        bool sawBoundary = false;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t c = m.index(x, y);
                REQUIRE(m.plateId[c] < plates);
                REQUIRE(m.boundary[c] <= static_cast<std::uint8_t>(BoundaryKind::Transform));
                const bool isBoundary = m.boundaryAt(x, y) != BoundaryKind::None;
                REQUIRE(isBoundary == touchesOtherPlate(m, x, y));
                sawBoundary = sawBoundary || isBoundary;
            }
        }
        CHECK(sawBoundary);
    }
}

TEST_CASE("tectonics: boundary kind matches the drift of the plates it separates", "[worldforge][tectonics]")
{
    const auto m = make(kSeed, 128, 128, 16);
    int seen[4] = {};
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x + 1 < m.width; ++x) {
            const int a = m.plateId[m.index(x, y)];
            const int b = m.plateId[m.index(x + 1, y)];
            if (a == b) continue;
            const auto& pa = m.plates[static_cast<std::size_t>(a)];
            const auto& pb = m.plates[static_cast<std::size_t>(b)];
            const auto expected = detail::classifyBoundary(pa, pb);
            CHECK(expected == detail::classifyBoundary(pb, pa));
            CHECK(m.boundaryAt(x, y) != BoundaryKind::None);
            ++seen[static_cast<int>(expected)];
        }
    }
    CHECK(seen[static_cast<int>(BoundaryKind::Convergent)] > 0);
    CHECK(seen[static_cast<int>(BoundaryKind::Divergent)] > 0);
    CHECK(seen[static_cast<int>(BoundaryKind::Transform)] > 0);

    TectonicPlate a;
    TectonicPlate b;
    b.seedX = 10;
    a.driftX = 3;
    b.driftX = -3;
    CHECK(detail::classifyBoundary(a, b) == BoundaryKind::Convergent);
    a.driftX = -3;
    b.driftX = 3;
    CHECK(detail::classifyBoundary(a, b) == BoundaryKind::Divergent);
    a.driftX = 0;
    a.driftY = 4;
    b.driftX = 0;
    b.driftY = -4;
    CHECK(detail::classifyBoundary(a, b) == BoundaryKind::Transform);
    a.driftY = b.driftY = 0;
    CHECK(detail::classifyBoundary(a, b) == BoundaryKind::Transform);
}

TEST_CASE("tectonics: height stays within bounds", "[worldforge][tectonics]")
{
    for (std::uint64_t s = 0; s < 12; ++s) {
        const auto m = make(s * 7919, 100, 70, 2 + static_cast<int>(s) * 5);
        for (const auto v : m.heightM) {
            REQUIRE(v >= kTectonicsMinHeightM);
            REQUIRE(v <= kTectonicsMaxHeightM);
        }
    }
}

TEST_CASE("tectonics: size and plate count limits are enforced", "[worldforge][tectonics]")
{
    CHECK_FALSE(generateTectonics(kSeed, { kTectonicsMinSize - 1, 64, 4 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { 64, kTectonicsMinSize - 1, 4 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { kTectonicsMaxSize + 1, 64, 4 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { 64, kTectonicsMaxSize + 1, 4 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { 64, 64, kTectonicsMinPlates - 1 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { 64, 64, kTectonicsMaxPlates + 1 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { 0, 0, 0 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { -5, 64, 4 }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max() }).has_value());
    CHECK_FALSE(generateTectonics(kSeed, { std::numeric_limits<int>::min(), 64, 4 }).has_value());

    CHECK(generateTectonics(kSeed, { kTectonicsMinSize, kTectonicsMinSize, kTectonicsMaxPlates }).has_value());
    CHECK(generateTectonics(kSeed, { kTectonicsMinSize, kTectonicsMinSize, kTectonicsMinPlates }).has_value());
    CHECK(generateTectonics(std::numeric_limits<std::uint64_t>::max(), { 64, 64, 4 }).has_value());
    CHECK(generateTectonics(0, { 64, 64, 4 }).has_value());
}

TEST_CASE("tectonics: the largest grid generates", "[worldforge][tectonics]")
{
    const auto m = make(kSeed, kTectonicsMaxSize, kTectonicsMaxSize, 4);
    CHECK(m.plateId.size() == static_cast<std::size_t>(kTectonicsMaxSize) * kTectonicsMaxSize);
}

TEST_CASE("tectonics: convergent boundaries raise ranges and divergent boundaries cut rifts", "[worldforge][tectonics]")
{
    double sumAll = 0;
    double sumConv = 0;
    double sumDiv = 0;
    long nAll = 0;
    long nConv = 0;
    long nDiv = 0;
    for (std::uint64_t s = 1; s <= 6; ++s) {
        const auto m = make(s, 256, 256, 14);
        for (int y = 0; y < m.height; ++y) {
            for (int x = 0; x < m.width; ++x) {
                const std::size_t c = m.index(x, y);
                if (m.plates[m.plateId[c]].type != PlateType::Continental) continue;
                sumAll += m.heightM[c];
                ++nAll;
                if (m.boundaryAt(x, y) == BoundaryKind::Convergent) {
                    sumConv += m.heightM[c];
                    ++nConv;
                } else if (m.boundaryAt(x, y) == BoundaryKind::Divergent) {
                    sumDiv += m.heightM[c];
                    ++nDiv;
                }
            }
        }
    }
    REQUIRE(nConv > 0);
    REQUIRE(nDiv > 0);
    const double mean = sumAll / nAll;
    CHECK(sumConv / nConv > mean + 800.0);
    CHECK(sumDiv / nDiv < mean - 300.0);
}

TEST_CASE("tectonics: high ground is a chain along convergent boundaries, not scattered blobs", "[worldforge][tectonics]")
{
    const auto m = make(kSeed, 256, 256, 14);
    const int reach = 256 / 12;
    long high = 0;
    long highNearConvergent = 0;
    for (int y = 0; y < m.height; ++y) {
        for (int x = 0; x < m.width; ++x) {
            if (m.heightM[m.index(x, y)] < 1500) continue;
            ++high;
            bool near = false;
            for (int yy = std::max(0, y - reach); yy <= std::min(m.height - 1, y + reach) && !near; ++yy) {
                for (int xx = std::max(0, x - reach); xx <= std::min(m.width - 1, x + reach); ++xx) {
                    if (m.boundaryAt(xx, yy) == BoundaryKind::Convergent) {
                        near = true;
                        break;
                    }
                }
            }
            if (near) ++highNearConvergent;
        }
    }
    REQUIRE(high > 100);
    CHECK(highNearConvergent == high);
}

TEST_CASE("tectonics golden hash for a fixed seed", "[worldforge][tectonics]")
{
    CHECK(hashTectonicMap(make(kSeed, 128, 96, 12)) == 0x218b53893b54d4abull);
    CHECK(hashTectonicMap(make(42, 64, 64, 6)) == 0x9604e29b169d23d2ull);
    CHECK(hashTectonicMap(make(0, 16, 16, 2)) == 0xe93a0666f46b2584ull);
}

TEST_CASE("tectonics: plate interiors have relief, not flat plateaus", "[worldforge][tectonics][relief]")
{
    double sumAbs = 0;
    long n = 0;
    for (std::uint64_t s = 1; s <= 8; ++s) {
        const auto m = make(s, 256, 256, 10);
        const int reach = 256 / 12;
        for (int y = 2 * reach; y + 8 < m.height - 2 * reach; y += 2) {
            for (int x = 2 * reach; x + 8 < m.width - 2 * reach; x += 2) {
                const std::size_t c = m.index(x, y);
                const int id = m.plateId[c];
                if (m.plateId[m.index(x + 8, y)] != id || m.plateId[m.index(x, y + 8)] != id) continue;
                bool interior = true;
                for (int d = -2 * reach; d <= 2 * reach && interior; d += reach) {
                    interior = m.plateId[m.index(x + d, y)] == id && m.plateId[m.index(x, y + d)] == id;
                }
                if (!interior) continue;
                sumAbs += std::abs(m.heightM[c] - m.heightM[m.index(x + 8, y)]) + std::abs(m.heightM[c] - m.heightM[m.index(x, y + 8)]);
                n += 2;
            }
        }
    }
    REQUIRE(n > 500);
    CHECK(sumAbs / static_cast<double>(n) > 8.0);
}

TEST_CASE("tectonics: no cliff steps between neighbouring cells", "[worldforge][tectonics][relief]")
{
    int worst = 0;
    for (std::uint64_t s = 1; s <= 8; ++s) {
        const auto m = make(s, 256, 256, 14);
        for (int y = 0; y + 1 < m.height; ++y) {
            for (int x = 0; x + 1 < m.width; ++x) {
                const int h0 = m.heightM[m.index(x, y)];
                worst = std::max({ worst, std::abs(h0 - m.heightM[m.index(x + 1, y)]), std::abs(h0 - m.heightM[m.index(x, y + 1)]) });
            }
        }
    }
    CHECK(worst <= 450);
}
