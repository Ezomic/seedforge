#include "ichor/factions/contested.h"

#include "ichor/core/hash.h"

#include <algorithm>
#include <array>
#include <bit>

namespace ichor {

namespace {

using Matrix = std::array<std::array<std::int32_t, kMaxTerritoryFactions>, kMaxTerritoryFactions>;
using Mask = std::uint16_t;

bool better(const ContestedCell& a, const ContestedCell& b)
{
    if (a.hostility != b.hostility)
        return a.hostility > b.hostility;
    if (a.value != b.value)
        return a.value > b.value;
    if (a.claimantCount != b.claimantCount)
        return a.claimantCount > b.claimantCount;
    if (a.strength != b.strength)
        return a.strength > b.strength;
    if (a.kind != b.kind)
        return a.kind < b.kind;
    return a.cell < b.cell;
}

Mask dropLowest(Mask m)
{
    return static_cast<Mask>(m & (m - 1));
}

bool validate(std::uint32_t width, std::uint32_t height, const TerritoryResult& t, const std::vector<std::uint8_t>& value, std::vector<std::string>& problems)
{
    const std::size_t before = problems.size();
    if (t.ids.size() > kMaxTerritoryFactions)
        problems.push_back("contested: too many factions");
    if (t.strength.size() != t.ids.size())
        problems.push_back("contested: strength rows do not match faction ids");
    for (std::size_t i = 0; i < t.ids.size(); ++i)
        for (std::size_t j = i + 1; j < t.ids.size(); ++j)
            if (t.ids[i] == t.ids[j])
                problems.push_back("contested: duplicate faction id '" + t.ids[i] + "'");
    const std::uint64_t cells = static_cast<std::uint64_t>(width) * height;
    if (cells > kMaxTerritoryCells) {
        problems.push_back("contested: map has too many cells");
        return false;
    }
    for (const auto& row : t.strength)
        if (row.size() != cells) {
            problems.push_back("contested: strength layer is not width * height long");
            break;
        }
    if (!value.empty() && value.size() != cells)
        problems.push_back("contested: value layer is not width * height long");
    return problems.size() == before;
}

}

bool findContestedCells(std::uint32_t width, std::uint32_t height, const TerritoryResult& territory, const std::vector<HostilityEntry>& matrix,
    const std::vector<std::uint8_t>& value, std::size_t maxResults, ContestedResult& out, std::vector<std::string>& problems)
{
    out = {};
    if (!validate(width, height, territory, value, problems))
        return false;

    const std::size_t cellCount = static_cast<std::size_t>(width) * height;
    const std::size_t factions = territory.ids.size();

    Matrix h{};
    std::array<Mask, kMaxTerritoryFactions> hostile{};
    for (const auto& e : matrix) {
        const auto ia = std::find(territory.ids.begin(), territory.ids.end(), e.a);
        const auto ib = std::find(territory.ids.begin(), territory.ids.end(), e.b);
        if (ia == territory.ids.end() || ib == territory.ids.end() || ia == ib)
            continue;
        const auto a = static_cast<std::size_t>(ia - territory.ids.begin());
        const auto b = static_cast<std::size_t>(ib - territory.ids.begin());
        h[a][b] = h[b][a] = e.hostility;
        if (e.relation == Relation::Hostile) {
            hostile[a] = static_cast<Mask>(hostile[a] | (1u << b));
            hostile[b] = static_cast<Mask>(hostile[b] | (1u << a));
        }
    }

    std::vector<Mask> mask(cellCount, 0);
    for (std::size_t f = 0; f < factions; ++f)
        for (std::size_t c = 0; c < cellCount; ++c)
            if (territory.strength[f][c] != 0)
                mask[c] = static_cast<Mask>(mask[c] | (1u << f));

    std::vector<ContestedCell> found;
    for (std::size_t c = 0; c < cellCount; ++c) {
        const Mask m = mask[c];
        if (m == 0)
            continue;
        ContestedCell cell;
        cell.cell = static_cast<std::uint32_t>(c);
        cell.claimants = m;
        cell.claimantCount = static_cast<std::uint32_t>(std::popcount(m));
        if (cell.claimantCount > 1) {
            cell.kind = ContestedKind::Overlap;
            for (Mask a = m; a != 0; a = dropLowest(a))
                for (Mask b = dropLowest(a); b != 0; b = dropLowest(b))
                    cell.hostility = std::max(cell.hostility, h[std::countr_zero(a)][std::countr_zero(b)]);
        } else {
            const int f = std::countr_zero(m);
            if (hostile[f] == 0)
                continue;
            const std::size_t x = c % width;
            const std::size_t y = c / width;
            Mask around = 0;
            if (x > 0)
                around = static_cast<Mask>(around | mask[c - 1]);
            if (x + 1 < width)
                around = static_cast<Mask>(around | mask[c + 1]);
            if (y > 0)
                around = static_cast<Mask>(around | mask[c - width]);
            if (y + 1 < height)
                around = static_cast<Mask>(around | mask[c + width]);
            around = static_cast<Mask>(around & hostile[f]);
            if (around == 0)
                continue;
            cell.kind = ContestedKind::Border;
            for (; around != 0; around = dropLowest(around))
                cell.hostility = std::max(cell.hostility, h[f][std::countr_zero(around)]);
        }
        cell.value = value.empty() ? 0 : value[c];
        for (Mask s = m; s != 0; s = dropLowest(s))
            cell.strength += territory.strength[std::countr_zero(s)][c];
        (cell.kind == ContestedKind::Overlap ? out.overlapTotal : out.borderTotal)++;
        found.push_back(cell);
    }

    out.total = found.size();
    if (found.size() > maxResults) {
        std::nth_element(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(maxResults), found.end(), better);
        found.resize(maxResults);
    }
    std::sort(found.begin(), found.end(), better);
    out.cells = std::move(found);
    return true;
}

std::uint64_t contestedDigest(const ContestedResult& result)
{
    std::uint64_t d = hash64("contested");
    d = hashCombine(d, result.total);
    for (const auto& c : result.cells) {
        d = hashCombine(d, c.cell);
        d = hashCombine(d, static_cast<std::uint64_t>(c.kind));
        d = hashCombine(d, c.claimants);
        d = hashCombine(d, static_cast<std::uint64_t>(c.hostility));
        d = hashCombine(d, c.value);
        d = hashCombine(d, c.strength);
    }
    return d;
}

}
