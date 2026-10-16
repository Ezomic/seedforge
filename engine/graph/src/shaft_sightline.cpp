#include "ichor/graph/shaft_sightline.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace ichor::graph::shaft {

namespace {

constexpr std::size_t kMaxLinks = 8192;
constexpr std::int64_t kProbeCells = 32;

std::uint64_t mix(std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

bool usable(const Room& r)
{
    const std::int64_t w = static_cast<std::int64_t>(r.x1) - r.x0;
    const std::int64_t h = static_cast<std::int64_t>(r.y1) - r.y0;
    return w > 0 && h > 0 && w <= kMaxRoomSide && h <= kMaxRoomSide && r.floor >= -kMaxFloor && r.floor <= kMaxFloor;
}

bool contains(const Room& r, std::int32_t x, std::int32_t y)
{
    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

struct Pair {
    std::size_t upper = 0; // indices into the usable room list
    std::size_t lower = 0;
    std::uint32_t walk = 0;
    std::uint64_t tie = 0;
};

}

std::vector<Shaft> chooseShafts(const std::vector<Room>& input, const std::vector<Link>& inputLinks,
                                const std::vector<Cell>& inputReserved, const Params& params)
{
    std::vector<Shaft> out;
    const std::uint32_t cap = std::min(params.maxShafts, kMaxShafts);
    if (cap == 0) return out;
    const std::uint32_t maxSpan = std::clamp<std::uint32_t>(params.maxSpan, 1, kMaxSpanLimit);

    std::map<std::uint32_t, int> idCount;
    for (const Room& r : input) ++idCount[r.id];
    std::vector<Room> rooms;
    for (const Room& r : input) {
        if (usable(r) && idCount[r.id] == 1) rooms.push_back(r);
    }
    std::sort(rooms.begin(), rooms.end(), [](const Room& a, const Room& b) { return a.id < b.id; });
    if (rooms.size() > kMaxRooms) rooms.resize(kMaxRooms);
    if (rooms.size() < 2) return out;

    std::map<std::uint32_t, std::size_t> indexOf;
    for (std::size_t i = 0; i < rooms.size(); ++i) indexOf[rooms[i].id] = i;

    std::set<std::pair<std::size_t, std::size_t>> linkSet;
    for (const Link& l : inputLinks) {
        auto a = indexOf.find(l.a);
        auto b = indexOf.find(l.b);
        if (a == indexOf.end() || b == indexOf.end() || a->second == b->second) continue;
        linkSet.insert({std::min(a->second, b->second), std::max(a->second, b->second)});
    }
    std::vector<std::vector<std::size_t>> adjacent(rooms.size());
    std::size_t linkCount = 0;
    for (const auto& [a, b] : linkSet) {
        if (++linkCount > kMaxLinks) break;
        adjacent[a].push_back(b);
        adjacent[b].push_back(a);
    }

    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> reserved;
    for (const Cell& c : inputReserved) reserved.insert({c.floor, c.x, c.y});

    std::map<std::int32_t, std::vector<std::size_t>> byFloor;
    for (std::size_t i = 0; i < rooms.size(); ++i) byFloor[rooms[i].floor].push_back(i);
    if (byFloor.size() < 2) return out;

    std::vector<Pair> pairs;
    for (std::size_t src = 0; src < rooms.size(); ++src) {
        std::vector<std::uint32_t> dist(rooms.size(), kUnreachable);
        std::vector<std::size_t> queue{src};
        dist[src] = 0;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            for (std::size_t next : adjacent[queue[head]]) {
                if (dist[next] != kUnreachable) continue;
                dist[next] = dist[queue[head]] + 1;
                queue.push_back(next);
            }
        }
        for (std::size_t other = 0; other < rooms.size(); ++other) {
            const Room& lo = rooms[src];
            const Room& up = rooms[other];
            const std::int64_t span = static_cast<std::int64_t>(up.floor) - lo.floor;
            if (span < 1 || span > maxSpan) continue;
            if (dist[other] != kUnreachable && dist[other] < params.minWalkDistance) continue;
            if (std::max(lo.x0, up.x0) >= std::min(lo.x1, up.x1) || std::max(lo.y0, up.y0) >= std::min(lo.y1, up.y1)) continue;
            Pair p;
            p.upper = other;
            p.lower = src;
            p.walk = dist[other];
            p.tie = mix(params.seed ^ mix((static_cast<std::uint64_t>(up.id) << 32) | lo.id));
            pairs.push_back(p);
        }
    }

    // Reachable pairs rank by walking distance, farthest first; unreachable ones come last.
    auto score = [](const Pair& p) { return p.walk == kUnreachable ? 0u : p.walk; };
    std::sort(pairs.begin(), pairs.end(), [&](const Pair& a, const Pair& b) {
        if (score(a) != score(b)) return score(a) > score(b);
        if (a.tie != b.tie) return a.tie < b.tie;
        return std::tie(a.upper, a.lower) < std::tie(b.upper, b.lower);
    });

    std::set<std::size_t> usedRooms;
    std::set<std::pair<std::int32_t, std::int32_t>> usedColumns;
    for (const Pair& p : pairs) {
        if (out.size() >= cap) break;
        if (usedRooms.count(p.upper) || usedRooms.count(p.lower)) continue;
        const Room& up = rooms[p.upper];
        const Room& lo = rooms[p.lower];

        const std::int32_t ox0 = std::max(lo.x0, up.x0);
        const std::int32_t oy0 = std::max(lo.y0, up.y0);
        const std::int64_t w = static_cast<std::int64_t>(std::min(lo.x1, up.x1)) - ox0;
        const std::int64_t h = static_cast<std::int64_t>(std::min(lo.y1, up.y1)) - oy0;
        const std::int64_t area = w * h;
        const std::int64_t start = static_cast<std::int64_t>(mix(p.tie) % static_cast<std::uint64_t>(area));

        for (std::int64_t probe = 0; probe < std::min(area, kProbeCells); ++probe) {
            const std::int64_t cell = (start + probe) % area;
            const std::int32_t x = ox0 + static_cast<std::int32_t>(cell % w);
            const std::int32_t y = oy0 + static_cast<std::int32_t>(cell / w);
            if (usedColumns.count({x, y}) || reserved.count({up.floor, x, y})) continue;
            bool clear = true;
            for (std::int32_t f = lo.floor + 1; f < up.floor && clear; ++f) {
                clear = !reserved.count({f, x, y});
                auto floorRooms = byFloor.find(f);
                if (floorRooms == byFloor.end()) continue;
                for (std::size_t r : floorRooms->second) {
                    if (contains(rooms[r], x, y)) {
                        clear = false;
                        break;
                    }
                }
            }
            if (!clear) continue;
            out.push_back({up.id, lo.id, x, y, up.floor, lo.floor, p.walk});
            usedRooms.insert(p.upper);
            usedRooms.insert(p.lower);
            usedColumns.insert({x, y});
            break;
        }
    }
    return out;
}

}
