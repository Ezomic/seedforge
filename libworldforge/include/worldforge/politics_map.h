#pragma once

#include <worldforge/erosion_map.h>
#include <worldforge/resource_map.h>
#include <worldforge/subseed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <queue>
#include <span>
#include <vector>

// Phase 9, Politics (doc 08, doc 03): faction home assignment, weighted territory flood-fill,
// a computed relationship matrix and contested cell identification. Later phases (POIs, binding)
// consume PoliticsMap; none of them may mutate it. The inputs are never modified.
//
// Settlement sites (phase 7) are plain input: a list of cells. The generator does not care how
// they were produced, so this phase stands alone from settlements and roads.
//
// Output contract (PoliticsMap, same grid as the inputs, row-major, index = y * width + x):
//  - owner[c]      : faction index (position in the input faction list) that holds land cell c, or
//                    kPoliticsNoOwner for ocean cells, for land the flood never reached (a land
//                    component with no faction home, or beyond every faction's reach) and for land
//                    that only Routes factions could claim. Every land cell in a component that
//                    contains the home of a claiming faction (any strategy but Routes) is owned,
//                    unless a restricted reach (FactionSpec::reach, NodesOnly radius) leaves it out.
//  - rival[c]      : the strongest other faction contesting an owned cell, or kPoliticsNoOwner. A
//                    cell is contested when rival != kPoliticsNoOwner: some other faction could have
//                    taken it for at most PoliticsParams::contestMargin more cost than the owner
//                    paid. Contested cells keep their owner and are always adjacent (4-neighbour) to
//                    a cell owned by their rival, i.e. they lie on borders.
//  - homes[f]      : the cell of faction f's home settlement.
//  - homeSite[f]   : the index into the input site list that became faction f's home.
//  - siteOwner[s]  : owner of the cell of input site s (kPoliticsNoOwner if unowned).
//  - territory[f]  : number of cells owned by faction f.
//  - hostility     : factionCount * factionCount matrix, row-major, symmetric, zero diagonal:
//                    hostility(A, B) = ideological distance + resource overlap (doc 03).
//                    Ideological distance is |dOrder| + |dLaw| in [0, 400]. Resource overlap is the
//                    ore-weighted count of cells contested between A and B divided by
//                    PoliticsParams::overlapDivisor, capped at 255. Values are in [0, 655].
//
// Territory model: one multi-source Dijkstra (4-neighbour) over land cells. Each faction pays its own
// per-cell entry cost: a base plus a slope penalty, modified by its ClaimStrategy (ResourceFollowing
// finds ore cheap, Defensive finds marginal land cheap and arable land dear, Contiguous and
// NodesOnly pay the base), plus decayPerCell added per 8 cells of Manhattan distance from the home
// (the Archonate decays with distance from its centre). Land beside deep ocean has a steep slope, so
// coasts are dear to claim. Cells whose path cost would reach 2^32 - 1 count as out of reach. The flood
// is multi-source: a faction cannot path through cells another faction settled first. A cell goes to
// the faction the flood reaches it from first, i.e. cheapest, ties to the lower faction index, then the lower cell index. Every claimed cell was
// reached through a neighbour of the same faction, so each territory is 4-connected. NodesOnly
// factions cannot enter cells farther than PoliticsParams::nodeRadius (Manhattan) from their home,
// and any faction with reach > 0 is bounded the same way. Routes factions (the Xenoi) hold a home
// but claim no cells. A faction that is the only one on its landmass owns all of it.
//
// Home assignment: a spec with homeSite >= 0 uses that site; the others, in list order, take the
// unused site with the best score oreWeight * ore + arableWeight * arable + isolationWeight *
// isolation (ore is the best grade within 3 cells, isolation the Manhattan distance to the nearest
// other site, capped at 255), ties to the lower site index.
//
// Determinism: integer-only, no libm, no floating point, no unordered container; all ties resolve
// by index. Memory is O(cells), at most about 48 bytes per cell (transient, plus a lazy-deletion heap of up to 4 entries per cell) plus O(sites * factions),
// with cells <= kTectonicsMaxSize^2.
namespace worldforge {

enum class ClaimStrategy : std::uint8_t { Contiguous, ResourceFollowing, NodesOnly, Defensive, Routes };

constexpr std::uint8_t kPoliticsNoOwner = 255;
constexpr int kPoliticsMaxFactions = 16;
constexpr int kPoliticsMaxSites = 4096;
constexpr int kPoliticsMaxMargin = 4096;
constexpr int kPoliticsMaxRadius = 2048;
constexpr int kPoliticsMaxDivisor = 1 << 20;
constexpr int kPoliticsMaxDecay = 64;
constexpr int kPoliticsMaxWeight = 255;
constexpr int kPoliticsMaxIdeology = 100;

struct PoliticsParams {
    int contestMargin = 24; // extra cost a rival may need and still contest a cell, in [0, 4096]
    int nodeRadius = 6;     // Manhattan radius of a NodesOnly faction, in [1, 2048]
    int overlapDivisor = 8; // contested ore-weighted cells per point of resource overlap, in [1, 2^20]
};

struct PoliticsSite {
    std::int16_t x = 0;
    std::int16_t y = 0;
};

struct FactionSpec {
    ClaimStrategy strategy = ClaimStrategy::Contiguous;
    int homeSite = -1;       // index into the site list, or -1 to assign by score
    int reach = 0;           // Manhattan radius bound around the home, 0 for unbounded, in [0, 2048]
    int decayPerCell = 0;    // cost added per 8 cells of Manhattan distance from the home, in [0, 64]
    int oreWeight = 0;       // home scoring weights, each in [0, 255]
    int arableWeight = 0;
    int isolationWeight = 0;
    int oldOrder = 0;        // restore (-100) .. preserve (0) .. replace (100)
    int law = 0;             // control (-100) .. free access (0) .. renounce (100)
};

struct PoliticsInputs {
    const ErodedMap* eroded = nullptr;
    const ResourceMap* resources = nullptr;
    std::span<const PoliticsSite> sites;
    std::span<const FactionSpec> factions;
};

struct PoliticsMap {
    std::uint64_t seed = 0;
    int width = 0;
    int height = 0;
    int factionCount = 0;
    std::vector<std::uint8_t> owner;
    std::vector<std::uint8_t> rival;
    std::vector<PoliticsSite> homes;
    std::vector<std::int32_t> homeSite;
    std::vector<std::uint8_t> siteOwner;
    std::vector<std::uint32_t> territory;
    std::vector<std::uint16_t> hostility;

    std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x); }
    bool contested(std::size_t c) const { return rival[c] != kPoliticsNoOwner; }
    int hostilityOf(int a, int b) const { return hostility[static_cast<std::size_t>(a) * static_cast<std::size_t>(factionCount) + static_cast<std::size_t>(b)]; }
};

namespace detail {

constexpr std::uint64_t kPoliticsDomain = 0x706f6c6974696339ull;

constexpr int kPoliticsBaseCost = 16;
constexpr int kPoliticsSlopeCapM = 192;
constexpr int kPoliticsSlopeDiv = 4;
constexpr int kPoliticsOreDiscount = 4;
constexpr int kPoliticsOreScanRadius = 3;
constexpr int kPoliticsContestOreBase = 16;

inline bool politicsInputValid(const PoliticsInputs& in)
{
    if (in.eroded == nullptr || in.resources == nullptr) return false;
    const ErodedMap& e = *in.eroded;
    const ResourceMap& r = *in.resources;
    if (e.width < kTectonicsMinSize || e.width > kTectonicsMaxSize || e.height < kTectonicsMinSize || e.height > kTectonicsMaxSize) return false;
    const std::size_t cells = static_cast<std::size_t>(e.width) * static_cast<std::size_t>(e.height);
    if (e.heightM.size() != cells) return false;
    if (r.width != e.width || r.height != e.height || r.seed != e.seed || r.oreGrade.size() != cells || r.arable.size() != cells) return false;
    const auto inRange = [](std::int16_t v) { return v >= kTectonicsMinHeightM && v <= kTectonicsMaxHeightM; };
    if (!std::all_of(e.heightM.begin(), e.heightM.end(), inRange)) return false;
    if (in.sites.size() > static_cast<std::size_t>(kPoliticsMaxSites) || in.factions.size() > static_cast<std::size_t>(kPoliticsMaxFactions)) return false;
    std::vector<std::uint8_t> taken(cells, 0);
    for (const PoliticsSite& s : in.sites) {
        if (s.x < 0 || s.y < 0 || s.x >= e.width || s.y >= e.height) return false;
        const std::size_t c = static_cast<std::size_t>(s.y) * static_cast<std::size_t>(e.width) + static_cast<std::size_t>(s.x);
        if (e.heightM[c] < 0 || taken[c] != 0) return false;
        taken[c] = 1;
    }
    std::vector<std::uint8_t> usedSite(in.sites.size(), 0);
    for (const FactionSpec& f : in.factions) {
        if (static_cast<std::uint8_t>(f.strategy) > static_cast<std::uint8_t>(ClaimStrategy::Routes)) return false;
        if (f.homeSite < -1 || f.homeSite >= static_cast<int>(in.sites.size())) return false;
        if (f.homeSite >= 0) {
            if (usedSite[static_cast<std::size_t>(f.homeSite)] != 0) return false;
            usedSite[static_cast<std::size_t>(f.homeSite)] = 1;
        }
        if (f.reach < 0 || f.reach > kPoliticsMaxRadius || f.decayPerCell < 0 || f.decayPerCell > kPoliticsMaxDecay) return false;
        const auto weightOk = [](int w) { return w >= 0 && w <= kPoliticsMaxWeight; };
        if (!weightOk(f.oreWeight) || !weightOk(f.arableWeight) || !weightOk(f.isolationWeight)) return false;
        const auto ideologyOk = [](int v) { return v >= -kPoliticsMaxIdeology && v <= kPoliticsMaxIdeology; };
        if (!ideologyOk(f.oldOrder) || !ideologyOk(f.law)) return false;
    }
    const std::size_t autoHomes = static_cast<std::size_t>(std::count_if(in.factions.begin(), in.factions.end(), [](const FactionSpec& f) { return f.homeSite < 0; }));
    const std::size_t freeSites = in.sites.size() - static_cast<std::size_t>(std::count(usedSite.begin(), usedSite.end(), 1));
    return autoHomes <= freeSites;
}

inline bool politicsParamsValid(const PoliticsParams& p)
{
    return p.contestMargin >= 0 && p.contestMargin <= kPoliticsMaxMargin && p.nodeRadius >= 1 && p.nodeRadius <= kPoliticsMaxRadius && p.overlapDivisor >= 1
        && p.overlapDivisor <= kPoliticsMaxDivisor;
}

inline int manhattan(int x0, int y0, int x1, int y1)
{
    return std::abs(x0 - x1) + std::abs(y0 - y1);
}

inline int bestOreNear(const ResourceMap& r, int x, int y)
{
    int best = 0;
    for (int dy = -kPoliticsOreScanRadius; dy <= kPoliticsOreScanRadius; ++dy) {
        for (int dx = -kPoliticsOreScanRadius; dx <= kPoliticsOreScanRadius; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= r.width || ny >= r.height) continue;
            if (std::abs(dx) + std::abs(dy) > kPoliticsOreScanRadius) continue;
            best = std::max<int>(best, r.oreGrade[r.index(nx, ny)]);
        }
    }
    return best;
}

inline std::vector<std::int32_t> assignHomes(const PoliticsInputs& in)
{
    const ResourceMap& r = *in.resources;
    const std::size_t sites = in.sites.size();
    std::vector<std::int32_t> homeSite(in.factions.size(), -1);
    std::vector<std::uint8_t> used(sites, 0);
    for (std::size_t f = 0; f < in.factions.size(); ++f) {
        if (in.factions[f].homeSite < 0) continue;
        homeSite[f] = in.factions[f].homeSite;
        used[static_cast<std::size_t>(in.factions[f].homeSite)] = 1;
    }
    std::vector<int> ore(sites), arable(sites), isolation(sites);
    for (std::size_t s = 0; s < sites; ++s) {
        const PoliticsSite& a = in.sites[s];
        ore[s] = bestOreNear(r, a.x, a.y);
        arable[s] = r.arable[r.index(a.x, a.y)];
        int nearest = 255;
        for (std::size_t o = 0; o < sites; ++o) {
            if (o != s) nearest = std::min(nearest, manhattan(a.x, a.y, in.sites[o].x, in.sites[o].y));
        }
        isolation[s] = nearest;
    }
    for (std::size_t f = 0; f < in.factions.size(); ++f) {
        if (homeSite[f] >= 0) continue;
        const FactionSpec& spec = in.factions[f];
        std::int64_t bestScore = -1;
        std::int32_t best = -1;
        for (std::size_t s = 0; s < sites; ++s) {
            if (used[s] != 0) continue;
            const std::int64_t score = static_cast<std::int64_t>(spec.oreWeight) * ore[s] + static_cast<std::int64_t>(spec.arableWeight) * arable[s]
                + static_cast<std::int64_t>(spec.isolationWeight) * isolation[s];
            if (score > bestScore) {
                bestScore = score;
                best = static_cast<std::int32_t>(s);
            }
        }
        homeSite[f] = best;
        used[static_cast<std::size_t>(best)] = 1;
    }
    return homeSite;
}

struct Flood {
    const PoliticsInputs& in;
    const PoliticsParams& params;
    const std::vector<PoliticsSite>& homes;
    std::vector<std::uint8_t> slope; // 0..255, steepest 4-neighbour height difference (ocean neighbours included)

    int bound(std::size_t f) const
    {
        const FactionSpec& s = in.factions[f];
        if (s.strategy == ClaimStrategy::NodesOnly) return s.reach > 0 ? std::min(s.reach, params.nodeRadius) : params.nodeRadius;
        return s.reach;
    }

    bool canEnter(std::size_t f, int x, int y) const
    {
        const int limit = bound(f);
        return limit == 0 || manhattan(x, y, homes[f].x, homes[f].y) <= limit;
    }

    // Always >= 1, so the flood is a proper shortest-path problem.
    std::uint32_t stepCost(std::size_t f, int x, int y) const
    {
        const FactionSpec& s = in.factions[f];
        const std::size_t c = in.eroded->index(x, y);
        std::int64_t cost = kPoliticsBaseCost + slope[c] / kPoliticsSlopeDiv;
        if (s.strategy == ClaimStrategy::ResourceFollowing && in.resources->oreGrade[c] > 0) {
            cost = std::max<std::int64_t>(1, cost / kPoliticsOreDiscount);
        } else if (s.strategy == ClaimStrategy::Defensive) {
            const std::int64_t arable = in.resources->arable[c];
            cost = std::max<std::int64_t>(1, cost * (32 + arable) / 96);
        }
        cost += static_cast<std::int64_t>(s.decayPerCell) * manhattan(x, y, homes[f].x, homes[f].y) / 8;
        return static_cast<std::uint32_t>(cost);
    }
};

inline std::vector<std::uint8_t> slopeField(const ErodedMap& e)
{
    std::vector<std::uint8_t> out(e.heightM.size(), 0);
    const std::vector<std::int16_t>& z = e.heightM;
    const int w = e.width;
    for (int y = 0; y < e.height; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t c = e.index(x, y);
            int slope = 0;
            if (x > 0) slope = std::max<int>(slope, std::abs(z[c] - z[c - 1]));
            if (x + 1 < w) slope = std::max<int>(slope, std::abs(z[c] - z[c + 1]));
            if (y > 0) slope = std::max<int>(slope, std::abs(z[c] - z[c - static_cast<std::size_t>(w)]));
            if (y + 1 < e.height) slope = std::max<int>(slope, std::abs(z[c] - z[c + static_cast<std::size_t>(w)]));
            out[c] = static_cast<std::uint8_t>(std::min(slope, kPoliticsSlopeCapM) * kPoliticsSlopeDiv * 255 / (kPoliticsSlopeCapM * kPoliticsSlopeDiv));
        }
    }
    return out;
}

constexpr int kPoliticsDx[4] = { 1, -1, 0, 0 };
constexpr int kPoliticsDy[4] = { 0, 0, 1, -1 };

// Returns the settled cost per cell (UINT32_MAX when unreached) and fills out.owner.
inline std::vector<std::uint32_t> floodTerritory(const Flood& fl, PoliticsMap& out)
{
    const ErodedMap& e = *fl.in.eroded;
    const std::size_t cells = e.heightM.size();
    constexpr std::uint32_t kInf = 0xFFFFFFFFu;
    std::vector<std::uint32_t> best(cells, kInf);
    std::vector<std::uint8_t> bestOwner(cells, kPoliticsNoOwner);
    std::vector<std::uint32_t> settled(cells, kInf);
    // key = cost << 32 | faction << 24 | cell, so the heap order is (cost, faction, cell).
    std::priority_queue<std::uint64_t, std::vector<std::uint64_t>, std::greater<std::uint64_t>> heap;
    const auto push = [&](std::uint32_t cost, std::size_t f, std::size_t c) {
        heap.push((static_cast<std::uint64_t>(cost) << 32) | (static_cast<std::uint64_t>(f) << 24) | static_cast<std::uint64_t>(c));
    };
    for (std::size_t f = 0; f < fl.in.factions.size(); ++f) {
        if (fl.in.factions[f].strategy == ClaimStrategy::Routes) continue;
        const std::size_t c = e.index(fl.homes[f].x, fl.homes[f].y);
        best[c] = 0;
        bestOwner[c] = static_cast<std::uint8_t>(f);
        push(0, f, c);
    }
    while (!heap.empty()) {
        const std::uint64_t key = heap.top();
        heap.pop();
        const std::uint32_t cost = static_cast<std::uint32_t>(key >> 32);
        const std::size_t f = static_cast<std::size_t>((key >> 24) & 0xFFu);
        const std::size_t c = static_cast<std::size_t>(key & 0xFFFFFFu);
        if (settled[c] != kInf) continue;
        settled[c] = cost;
        out.owner[c] = static_cast<std::uint8_t>(f);
        const int x = static_cast<int>(c % static_cast<std::size_t>(e.width));
        const int y = static_cast<int>(c / static_cast<std::size_t>(e.width));
        for (int n = 0; n < 4; ++n) {
            const int nx = x + kPoliticsDx[n];
            const int ny = y + kPoliticsDy[n];
            if (nx < 0 || ny < 0 || nx >= e.width || ny >= e.height) continue;
            const std::size_t nc = e.index(nx, ny);
            if (settled[nc] != kInf || e.heightM[nc] < 0 || !fl.canEnter(f, nx, ny)) continue;
            const std::uint64_t wide = static_cast<std::uint64_t>(cost) + fl.stepCost(f, nx, ny);
            if (wide >= kInf) continue;
            const std::uint32_t ncost = static_cast<std::uint32_t>(wide);
            if (ncost < best[nc] || (ncost == best[nc] && f < bestOwner[nc])) {
                best[nc] = ncost;
                bestOwner[nc] = static_cast<std::uint8_t>(f);
                push(ncost, f, nc);
            }
        }
    }
    return settled;
}

inline void markContested(const Flood& fl, const std::vector<std::uint32_t>& cost, PoliticsMap& out)
{
    const ErodedMap& e = *fl.in.eroded;
    const std::uint64_t margin = static_cast<std::uint64_t>(fl.params.contestMargin);
    for (int y = 0; y < e.height; ++y) {
        for (int x = 0; x < e.width; ++x) {
            const std::size_t c = e.index(x, y);
            const std::uint8_t owner = out.owner[c];
            if (owner == kPoliticsNoOwner) continue;
            std::uint64_t bestRival = ~std::uint64_t{ 0 };
            std::uint8_t rival = kPoliticsNoOwner;
            for (int n = 0; n < 4; ++n) {
                const int nx = x + kPoliticsDx[n];
                const int ny = y + kPoliticsDy[n];
                if (nx < 0 || ny < 0 || nx >= e.width || ny >= e.height) continue;
                const std::size_t nc = e.index(nx, ny);
                const std::uint8_t other = out.owner[nc];
                if (other == kPoliticsNoOwner || other == owner || !fl.canEnter(other, x, y)) continue;
                const std::uint64_t alt = static_cast<std::uint64_t>(cost[nc]) + fl.stepCost(other, x, y);
                if (alt < bestRival || (alt == bestRival && other < rival)) {
                    bestRival = alt;
                    rival = other;
                }
            }
            if (rival != kPoliticsNoOwner && bestRival - cost[c] <= margin) out.rival[c] = rival;
        }
    }
}

inline void computeHostility(const PoliticsInputs& in, const PoliticsParams& p, PoliticsMap& out)
{
    const std::size_t n = in.factions.size();
    std::vector<std::uint64_t> overlap(n * n, 0);
    for (std::size_t c = 0; c < out.owner.size(); ++c) {
        if (out.rival[c] == kPoliticsNoOwner) continue;
        const std::size_t a = out.owner[c];
        const std::size_t b = out.rival[c];
        const std::uint64_t weight = kPoliticsContestOreBase + in.resources->oreGrade[c];
        overlap[a * n + b] += weight;
        overlap[b * n + a] += weight;
    }
    out.hostility.assign(n * n, 0);
    for (std::size_t a = 0; a < n; ++a) {
        for (std::size_t b = 0; b < n; ++b) {
            if (a == b) continue;
            const int ideology = std::abs(in.factions[a].oldOrder - in.factions[b].oldOrder) + std::abs(in.factions[a].law - in.factions[b].law);
            const std::uint64_t scaled = std::min<std::uint64_t>(255, overlap[a * n + b] / static_cast<std::uint64_t>(p.overlapDivisor));
            out.hostility[a * n + b] = static_cast<std::uint16_t>(ideology + static_cast<int>(scaled));
        }
    }
}

}

// Returns nullopt when an input is missing or malformed (null maps, size outside the kTectonics*
// limits, vectors of the wrong length, heights out of range, resources from another seed or grid,
// a site out of bounds, on ocean or sharing a cell, a faction field out of range, two factions on
// one site, more automatic homes than free sites, more than kPoliticsMaxSites sites or
// kPoliticsMaxFactions factions), or a parameter is out of range. Otherwise a pure function of
// (eroded, resources, sites, factions, params). Zero factions give a map with no owners.
inline std::optional<PoliticsMap> generatePolitics(const PoliticsInputs& inputs, const PoliticsParams& params = {})
{
    if (!detail::politicsInputValid(inputs) || !detail::politicsParamsValid(params)) return std::nullopt;
    const ErodedMap& e = *inputs.eroded;
    const std::size_t cells = e.heightM.size();
    const std::size_t factions = inputs.factions.size();
    PoliticsMap out;
    out.seed = e.seed;
    out.width = e.width;
    out.height = e.height;
    out.factionCount = static_cast<int>(factions);
    out.owner.assign(cells, kPoliticsNoOwner);
    out.rival.assign(cells, kPoliticsNoOwner);
    out.territory.assign(factions, 0);
    out.homeSite = detail::assignHomes(inputs);
    for (const std::int32_t s : out.homeSite) out.homes.push_back(inputs.sites[static_cast<std::size_t>(s)]);

    const detail::Flood flood{ inputs, params, out.homes, detail::slopeField(e) };
    const std::vector<std::uint32_t> cost = detail::floodTerritory(flood, out);
    detail::markContested(flood, cost, out);
    detail::computeHostility(inputs, params, out);

    for (const std::uint8_t o : out.owner) {
        if (o != kPoliticsNoOwner) ++out.territory[o];
    }
    for (const PoliticsSite& s : inputs.sites) out.siteOwner.push_back(out.owner[e.index(s.x, s.y)]);
    return out;
}

// Order-sensitive digest of every field of the map; the golden value that guards determinism.
inline std::uint64_t hashPoliticsMap(const PoliticsMap& map)
{
    std::uint64_t h = splitmix64(map.seed ^ detail::kPoliticsDomain);
    const auto mix = [&](std::uint64_t v) { h = splitmix64(h ^ v); };
    mix(static_cast<std::uint64_t>(map.width));
    mix(static_cast<std::uint64_t>(map.height));
    mix(static_cast<std::uint64_t>(map.factionCount));
    for (std::size_t c = 0; c < map.owner.size(); ++c) mix((static_cast<std::uint64_t>(map.owner[c]) << 8) | map.rival[c]);
    for (std::size_t f = 0; f < map.homes.size(); ++f) {
        mix((static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.homes[f].x)) << 32) | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(map.homes[f].y)) << 16)
            | static_cast<std::uint64_t>(static_cast<std::uint32_t>(map.homeSite[f]) & 0xFFFFu));
        mix(map.territory[f]);
    }
    for (const std::uint8_t o : map.siteOwner) mix(o);
    for (const std::uint16_t v : map.hostility) mix(v);
    return h;
}

}
