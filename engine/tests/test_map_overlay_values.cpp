#include "ichor/ui/map_overlay_values.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <utility>

using namespace ichor;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

using Key = std::pair<std::int32_t, std::int32_t>;

std::vector<MapOverlayCell> block(int x0, int y0, int w, int h)
{
    std::vector<MapOverlayCell> cells;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            cells.push_back({x, y});
        }
    }
    return cells;
}

std::set<Key> keys(const MapOverlay& o)
{
    std::set<Key> out;
    for (const MapOverlayEntry& e : o.entries) {
        out.insert({e.cell.x, e.cell.y});
    }
    return out;
}

// Every cell of the whole world has data, so only the reveal mask can keep entries out.
MapOverlaySources everywhere()
{
    static const TemenosSanctuary sanctuary{50.0f, 50.0f, 1000.0f, 1.0f};
    MapOverlaySources s;
    s.regionAt = [](std::int32_t x, std::int32_t y) { return static_cast<std::uint32_t>((x / 4) + 100 * (y / 4)); };
    s.ownerAt = [](std::int32_t x, std::int32_t) { return static_cast<std::uint32_t>(x < 0 ? 0 : 1); };
    s.factionCount = 2;
    s.sanctuaries = std::span<const TemenosSanctuary>(&sanctuary, 1);
    s.cellSize = 1.0f;
    return s;
}

constexpr MapOverlayKind kAllKinds[] = {MapOverlayKind::Region, MapOverlayKind::Territory, MapOverlayKind::Temenos};

}

TEST_CASE("unrevealed cells never appear in any overlay", "[map_overlay]")
{
    const std::vector<MapOverlayCell> revealed = block(10, 10, 3, 3);
    std::set<Key> allowed;
    for (const MapOverlayCell& c : revealed) {
        allowed.insert({c.x, c.y});
    }
    std::set<Key> probed;
    MapOverlaySources s = everywhere();
    const auto region = s.regionAt;
    const auto owner = s.ownerAt;
    s.regionAt = [&](std::int32_t x, std::int32_t y) { probed.insert({x, y}); return region(x, y); };
    s.ownerAt = [&](std::int32_t x, std::int32_t y) { probed.insert({x, y}); return owner(x, y); };

    for (MapOverlayKind kind : kAllKinds) {
        const MapOverlay o = computeMapOverlay(kind, revealed, s);
        CHECK(o.status == MapOverlayStatus::Ok);
        CHECK(o.entries.size() == revealed.size());
        CHECK(keys(o) == allowed);
    }
    for (const Key& k : probed) {
        CHECK(allowed.count(k) == 1);
    }
}

TEST_CASE("an empty reveal gives an empty overlay", "[map_overlay]")
{
    for (MapOverlayKind kind : kAllKinds) {
        const MapOverlay o = computeMapOverlay(kind, {}, everywhere());
        CHECK(o.status == MapOverlayStatus::Ok);
        CHECK(o.entries.empty());
    }
}

TEST_CASE("the region overlay reports region ids and omits cells without one", "[map_overlay]")
{
    MapOverlaySources s;
    s.regionAt = [](std::int32_t x, std::int32_t) { return x < 2 ? 7u : (x < 4 ? kMapOverlayNone : 9u); };
    const MapOverlay o = computeMapOverlay(MapOverlayKind::Region, block(0, 0, 6, 1), s);
    REQUIRE(o.entries.size() == 4);
    CHECK(o.entries[0].value == 7);
    CHECK(o.entries[1].value == 7);
    CHECK(o.entries[2].cell.x == 4);
    CHECK(o.entries[2].value == 9);
}

TEST_CASE("the territory overlay reports owners and drops unknown faction indices", "[map_overlay]")
{
    MapOverlaySources s;
    s.factionCount = 3;
    s.ownerAt = [](std::int32_t x, std::int32_t) { return static_cast<std::uint32_t>(x); };
    const MapOverlay o = computeMapOverlay(MapOverlayKind::Territory, block(0, 0, 6, 1), s);
    REQUIRE(o.entries.size() == 3);
    CHECK(o.entries[2].value == 2);

    s.ownerAt = [](std::int32_t, std::int32_t) { return std::numeric_limits<std::uint32_t>::max() - 1; };
    CHECK(computeMapOverlay(MapOverlayKind::Territory, block(0, 0, 4, 4), s).entries.empty());
    s.factionCount = 0;
    s.ownerAt = [](std::int32_t, std::int32_t) { return 0u; };
    CHECK(computeMapOverlay(MapOverlayKind::Territory, block(0, 0, 4, 4), s).entries.empty());
}

TEST_CASE("the temenos overlay scales the field and omits unconsecrated ground", "[map_overlay]")
{
    const TemenosSanctuary sanctuary{5.5f, 0.5f, 3.0f, 1.0f};
    MapOverlaySources s;
    s.sanctuaries = std::span<const TemenosSanctuary>(&sanctuary, 1);
    const MapOverlay o = computeMapOverlay(MapOverlayKind::Temenos, block(0, 0, 12, 1), s);
    REQUIRE_FALSE(o.entries.empty());
    CHECK(o.entries.size() < 12);

    std::uint32_t best = 0;
    std::int32_t bestX = -1;
    for (const MapOverlayEntry& e : o.entries) {
        CHECK(e.value > 0);
        CHECK(e.value <= kMapOverlayTemenosMax);
        if (e.value > best) {
            best = e.value;
            bestX = e.cell.x;
        }
    }
    CHECK(bestX == 5);
    CHECK(best == kMapOverlayTemenosMax);
    // Every cell is a typical value against the centre: the rim cells are far weaker.
    CHECK(o.entries.front().value < best / 2);
}

TEST_CASE("temenos overlay clamps stacked sanctuaries and skips bad cell sizes", "[map_overlay]")
{
    const TemenosSanctuary stack[] = {{0.5f, 0.5f, 10.0f, 1.0f}, {0.5f, 0.5f, 10.0f, 1.0f}};
    MapOverlaySources s;
    s.sanctuaries = stack;
    const MapOverlay o = computeMapOverlay(MapOverlayKind::Temenos, block(0, 0, 1, 1), s);
    REQUIRE(o.entries.size() == 1);
    CHECK(o.entries[0].value == kMapOverlayTemenosMax);

    for (float bad : {0.0f, -1.0f, kNaN, kInf, kMapOverlayMaxCellSize * 2.0f}) {
        s.cellSize = bad;
        const MapOverlay r = computeMapOverlay(MapOverlayKind::Temenos, block(0, 0, 2, 2), s);
        CHECK(r.status == MapOverlayStatus::InvalidInput);
        CHECK(r.entries.empty());
    }
}

TEST_CASE("a missing callback for the requested kind is invalid input", "[map_overlay]")
{
    const MapOverlaySources none;
    CHECK(computeMapOverlay(MapOverlayKind::Region, block(0, 0, 2, 2), none).status == MapOverlayStatus::InvalidInput);
    CHECK(computeMapOverlay(MapOverlayKind::Territory, block(0, 0, 2, 2), none).status == MapOverlayStatus::InvalidInput);
    const MapOverlay unknown = computeMapOverlay(static_cast<MapOverlayKind>(200), block(0, 0, 2, 2), everywhere());
    CHECK(unknown.status == MapOverlayStatus::InvalidInput);
    CHECK(unknown.entries.empty());
}

TEST_CASE("output is sorted, unique and independent of input order and duplicates", "[map_overlay]")
{
    std::vector<MapOverlayCell> shuffled = block(-3, -3, 6, 6);
    std::reverse(shuffled.begin(), shuffled.end());
    std::swap(shuffled[3], shuffled[20]);
    std::vector<MapOverlayCell> doubled = shuffled;
    doubled.insert(doubled.end(), shuffled.begin(), shuffled.end());

    for (MapOverlayKind kind : kAllKinds) {
        const MapOverlay a = computeMapOverlay(kind, block(-3, -3, 6, 6), everywhere());
        const MapOverlay b = computeMapOverlay(kind, doubled, everywhere());
        REQUIRE(a.entries.size() == b.entries.size());
        for (std::size_t i = 0; i < a.entries.size(); ++i) {
            CHECK(a.entries[i].cell == b.entries[i].cell);
            CHECK(a.entries[i].value == b.entries[i].value);
            if (i > 0) {
                const MapOverlayCell& p = a.entries[i - 1].cell;
                const MapOverlayCell& c = a.entries[i].cell;
                CHECK((p.y != c.y ? p.y < c.y : p.x < c.x));
            }
        }
    }
}

TEST_CASE("a huge reveal is truncated to the cap and calls the sources a bounded number of times", "[map_overlay]")
{
    const int side = 700; // 490000 cells, well past the cap
    std::size_t calls = 0;
    MapOverlaySources s;
    s.regionAt = [&](std::int32_t, std::int32_t) { ++calls; return 1u; };
    const MapOverlay o = computeMapOverlay(MapOverlayKind::Region, block(0, 0, side, side), s);
    CHECK(o.status == MapOverlayStatus::Truncated);
    CHECK(o.entries.size() == kMaxMapOverlayCells);
    CHECK(calls == kMaxMapOverlayCells);
    // The first rows in canonical order are kept.
    CHECK(o.entries.back().cell.y == static_cast<std::int32_t>(kMaxMapOverlayCells / side));
}

TEST_CASE("extreme coordinates are accepted without overflow", "[map_overlay]")
{
    const std::int32_t lo = std::numeric_limits<std::int32_t>::min();
    const std::int32_t hi = std::numeric_limits<std::int32_t>::max();
    const std::vector<MapOverlayCell> revealed = {{hi, hi}, {lo, lo}, {lo, hi}, {hi, lo}, {0, 0}};
    for (MapOverlayKind kind : kAllKinds) {
        const MapOverlay o = computeMapOverlay(kind, revealed, everywhere());
        CHECK(o.status == MapOverlayStatus::Ok);
        CHECK(o.entries.size() <= revealed.size());
        for (const MapOverlayEntry& e : o.entries) {
            CHECK(std::find(revealed.begin(), revealed.end(), e.cell) != revealed.end());
        }
    }
}

TEST_CASE("hostile ids and sanctuaries cannot break the overlay", "[map_overlay]")
{
    const TemenosSanctuary bad[] = {{kNaN, 0.0f, 5.0f, 1.0f}, {0.0f, 0.0f, kInf, 1.0f}, {0.0f, 0.0f, 5.0f, kNaN}, {0.0f, 0.0f, 5.0f, 9.0f}};
    MapOverlaySources s;
    s.sanctuaries = bad;
    CHECK(computeMapOverlay(MapOverlayKind::Temenos, block(0, 0, 4, 4), s).entries.empty());

    s.regionAt = [](std::int32_t, std::int32_t) { return kMapOverlayNone; };
    CHECK(computeMapOverlay(MapOverlayKind::Region, block(0, 0, 4, 4), s).entries.empty());
}
