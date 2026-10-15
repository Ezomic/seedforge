#include "ichor/npc/regular_generation.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"
#include "ichor/npc/relationships.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace ichor {

namespace {

constexpr std::size_t kMaxWord = 64;
constexpr std::size_t kMaxNameBytes = 80;
constexpr std::size_t kMaxSentence = 160;
constexpr std::size_t kIdOverhead = 8 + 7; // "reg_" + "_" + three digits of index, and the "ledger_" memory prefix
constexpr std::size_t kMaxBaseName = kMaxNameBytes - 4; // room for a " 99" reuse suffix
constexpr std::size_t kMaxInputEntries = 10000;
constexpr std::uint64_t kRoleBaseWeight = 64;
constexpr std::string_view kEmDash = "\xE2\x80\x94";

bool isWord(const std::string& s, std::size_t maxBytes = kMaxWord)
{
    if (s.empty() || s.size() > maxBytes) {
        return false;
    }
    return std::all_of(s.begin(), s.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool isText(const std::string& s, std::size_t maxBytes)
{
    return !s.empty() && s.size() <= maxBytes && s.find_first_not_of(" \t\r\n") != std::string::npos && s.find(kEmDash) == std::string::npos;
}

// Items are sorted by key. Keeps one per key; a key that appears with
// different content is ambiguous without relying on input order, so every
// copy of it is dropped.
template <typename T, typename Key, typename Same>
void keepUnambiguous(std::vector<T>& items, Key key, Same same)
{
    std::vector<T> kept;
    for (std::size_t begin = 0; begin < items.size();) {
        std::size_t end = begin + 1;
        bool agree = true;
        while (end < items.size() && key(items[end]) == key(items[begin])) {
            agree = agree && same(items[begin], items[end]);
            ++end;
        }
        if (agree) {
            kept.push_back(items[begin]);
        }
        begin = end;
    }
    items = std::move(kept);
}

struct Candidate {
    std::string role;
    std::uint64_t weight = 0;
};

struct Town {
    const SettlementEconomy* source = nullptr;
    std::vector<Candidate> candidates;
    std::uint64_t share = 0;
};

std::vector<Candidate> candidatesFor(const SettlementEconomy& settlement, const std::vector<EconomicRole>& roles)
{
    std::map<std::string, int> abundance;
    for (const EconomyGood& g : settlement.goods) {
        const int clamped = std::clamp(g.abundance, -kMaxAbundance, kMaxAbundance);
        auto [it, fresh] = abundance.emplace(g.good, clamped);
        if (!fresh) {
            it->second = std::max(it->second, clamped);
        }
    }
    std::vector<Candidate> out;
    for (const EconomicRole& role : roles) {
        if (role.good.empty()) {
            out.push_back({role.id, 1});
            continue;
        }
        const auto have = abundance.find(role.good);
        const int need = std::clamp(role.minAbundance, 1, kMaxAbundance);
        if (have != abundance.end() && have->second >= need) {
            out.push_back({role.id, static_cast<std::uint64_t>(have->second - need + 1)});
        }
    }
    return out;
}

// A role already held by others in the town is less likely, so one abundant
// good does not turn the whole settlement into the same job.
const std::string& pickRole(Rng& rng, const std::vector<Candidate>& candidates, const std::map<std::string, std::uint64_t>& taken)
{
    std::vector<std::uint64_t> weights;
    std::uint64_t total = 0;
    for (const Candidate& c : candidates) {
        const auto it = taken.find(c.role);
        const std::uint64_t crowd = 1 + (it == taken.end() ? 0 : it->second);
        weights.push_back(std::max<std::uint64_t>(1, c.weight * kRoleBaseWeight / crowd));
        total += weights.back();
    }
    std::uint64_t roll = rng.nextU64() % total;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (roll < weights[i]) {
            return candidates[i].role;
        }
        roll -= weights[i];
    }
    return candidates.back().role;
}

std::uint64_t weightOf(const Desire& d)
{
    return static_cast<std::uint64_t>(std::clamp(d.intensity, kMinIntensity, kMaxIntensity));
}

const std::string& pickDesire(Rng& rng, const std::vector<Desire>& desires, DesireKind kind, const WorldSnapshot& world)
{
    std::vector<const Desire*> pool;
    for (const Desire& d : desires) {
        if (d.kind == kind && conditionHolds(d.condition, world)) {
            pool.push_back(&d);
        }
    }
    if (pool.empty()) {
        for (const Desire& d : desires) {
            if (d.kind == kind) {
                pool.push_back(&d);
            }
        }
    }
    std::uint64_t total = 0;
    for (const Desire* d : pool) {
        total += weightOf(*d);
    }
    std::uint64_t roll = rng.nextU64() % total;
    for (const Desire* d : pool) {
        if (roll < weightOf(*d)) {
            return d->id;
        }
        roll -= weightOf(*d);
    }
    return pool.back()->id;
}

std::uint64_t populationWeight(const SettlementEconomy& s)
{
    return std::clamp<std::uint32_t>(s.population, 1, kMaxPopulation);
}

// Largest remainder, ties to the earlier settlement, so shares sum to total.
void allocate(std::vector<Town>& towns, std::uint64_t total)
{
    std::uint64_t weightSum = 0;
    for (const Town& t : towns) {
        weightSum += populationWeight(*t.source);
    }
    std::vector<std::pair<std::uint64_t, std::size_t>> remainders;
    std::uint64_t given = 0;
    for (std::size_t i = 0; i < towns.size(); ++i) {
        const std::uint64_t w = populationWeight(*towns[i].source);
        towns[i].share = total * w / weightSum;
        given += towns[i].share;
        remainders.emplace_back(total * w % weightSum, i);
    }
    std::stable_sort(remainders.begin(), remainders.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    for (std::size_t k = 0; given < total; ++k, ++given) {
        ++towns[remainders[k % remainders.size()].second].share;
    }
}

std::vector<EconomicRole> usableRoles(const std::vector<EconomicRole>& input)
{
    std::vector<EconomicRole> roles;
    for (const EconomicRole& role : input) {
        if (isWord(role.id) && (role.good.empty() || isWord(role.good))) {
            roles.push_back(role);
            roles.back().minAbundance = std::clamp(role.minAbundance, 1, kMaxAbundance);
        }
    }
    std::sort(roles.begin(), roles.end(), [](const EconomicRole& l, const EconomicRole& r) {
        return std::tie(l.id, l.good, l.minAbundance) < std::tie(r.id, r.good, r.minAbundance);
    });
    keepUnambiguous(
        roles, [](const EconomicRole& r) { return r.id; },
        [](const EconomicRole& l, const EconomicRole& r) { return l.good == r.good && l.minAbundance == r.minAbundance; });
    return roles;
}

std::vector<Desire> usableDesires(const std::vector<Desire>& input)
{
    std::vector<Desire> desires;
    for (const Desire& d : input) {
        if (isText(d.id, kMaxSentence)) {
            desires.push_back(d);
        }
    }
    std::vector<std::pair<std::pair<std::string, std::uint64_t>, Desire>> keyed;
    for (Desire& d : desires) {
        keyed.push_back({{d.id, desireDigest(d)}, std::move(d)});
    }
    std::sort(keyed.begin(), keyed.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
    keepUnambiguous(
        keyed, [](const auto& k) { return k.first.first; }, [](const auto& l, const auto& r) { return l.first.second == r.first.second; });
    std::vector<Desire> out;
    for (auto& k : keyed) {
        out.push_back(std::move(k.second));
    }
    return out;
}

std::vector<std::string> usableNames(const std::vector<std::string>& input)
{
    std::vector<std::string> names;
    for (const std::string& name : input) {
        if (isText(name, kMaxBaseName)) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

std::uint64_t settlementDigest(const SettlementEconomy& s)
{
    std::map<std::string, int> goods;
    for (const EconomyGood& g : s.goods) {
        const int clamped = std::clamp(g.abundance, -kMaxAbundance, kMaxAbundance);
        auto [it, fresh] = goods.emplace(g.good, clamped);
        if (!fresh) {
            it->second = std::max(it->second, clamped);
        }
    }
    std::uint64_t h = hashCombine(hash64(s.id), populationWeight(s));
    for (const auto& [good, amount] : goods) {
        h = hashCombine(hashCombine(h, hash64(good)), static_cast<std::uint64_t>(static_cast<std::int64_t>(amount)));
    }
    for (const auto& [tag, value] : s.world) {
        h = hashCombine(hashCombine(h, hash64(tag)), static_cast<std::uint64_t>(value));
    }
    return h;
}

}

std::vector<Regular> generateRegulars(std::uint64_t worldSeed, const RegularInputs& inputs)
{
    if (inputs.traits == nullptr) {
        return {};
    }
    if (inputs.settlements.size() > kMaxInputEntries || inputs.roles.size() > kMaxInputEntries || inputs.names.size() > kMaxInputEntries
        || inputs.desires.size() > kMaxInputEntries) {
        return {};
    }
    for (const SettlementEconomy& s : inputs.settlements) {
        if (s.goods.size() > kMaxInputEntries || s.world.size() > kMaxInputEntries) {
            return {};
        }
    }
    const std::vector<EconomicRole> roles = usableRoles(inputs.roles);
    const std::vector<Desire> desires = usableDesires(inputs.desires);
    const std::vector<std::string> names = usableNames(inputs.names);
    const bool haveWant = std::any_of(desires.begin(), desires.end(), [](const Desire& d) { return d.kind == DesireKind::Want; });
    const bool haveFear = std::any_of(desires.begin(), desires.end(), [](const Desire& d) { return d.kind == DesireKind::Fear; });
    if (roles.empty() || names.empty() || !haveWant || !haveFear) {
        return {};
    }

    struct Source {
        const SettlementEconomy* settlement;
        std::uint64_t digest;
    };
    std::vector<Source> keyed;
    for (const SettlementEconomy& s : inputs.settlements) {
        if (isWord(s.id, kMaxWord - kIdOverhead)) {
            keyed.push_back({&s, settlementDigest(s)});
        }
    }
    std::sort(keyed.begin(), keyed.end(), [](const Source& l, const Source& r) {
        return std::tie(l.settlement->id, l.digest) < std::tie(r.settlement->id, r.digest);
    });
    keepUnambiguous(
        keyed, [](const Source& s) { return s.settlement->id; }, [](const Source& l, const Source& r) { return l.digest == r.digest; });
    std::vector<const SettlementEconomy*> sources;
    for (const Source& k : keyed) {
        sources.push_back(k.settlement);
    }

    std::vector<Town> towns;
    for (const SettlementEconomy* s : sources) {
        Town town;
        town.source = s;
        town.candidates = candidatesFor(*s, roles);
        if (!town.candidates.empty()) {
            towns.push_back(std::move(town));
        }
    }
    if (towns.empty()) {
        return {};
    }

    Rng countRng(hashCombine(worldSeed, hash64("regular_count")));
    allocate(towns, kMinRegulars + countRng.range(0, static_cast<std::uint32_t>(kMaxRegulars - kMinRegulars)));

    Rng nameRng(hashCombine(worldSeed, hash64("regular_names")));
    std::vector<std::string> unused = names;
    std::size_t round = 1;
    std::set<std::string> usedNames;

    std::vector<Regular> regulars;
    for (const Town& town : towns) {
        std::map<std::string, std::uint64_t> taken;
        for (std::uint64_t n = 0; n < town.share; ++n) {
            Regular r;
            r.id = "reg_" + town.source->id + "_" + std::to_string(n);
            r.home = town.source->id;

            const NpcTraits traits = assignTraits(worldSeed, hash64(r.id), *inputs.traits);
            if (traits.ids.size() < kMinNpcTraits) {
                continue;
            }
            r.traits = traits.ids;

            Rng rng(hashCombine(worldSeed, hash64(r.id)));
            r.role = pickRole(rng, town.candidates, taken);
            ++taken[r.role];
            r.want = pickDesire(rng, desires, DesireKind::Want, town.source->world);
            r.fear = pickDesire(rng, desires, DesireKind::Fear, town.source->world);
            r.memory = "ledger_" + r.id;

            do {
                if (unused.empty()) {
                    unused = names;
                    ++round;
                }
                const std::size_t at = nameRng.range(0, static_cast<std::uint32_t>(unused.size() - 1));
                r.name = unused[at];
                unused.erase(unused.begin() + static_cast<std::ptrdiff_t>(at));
                if (round > 1) {
                    r.name += " " + std::to_string(round);
                }
            } while (!usedNames.insert(r.name).second);
            regulars.push_back(std::move(r));
        }
    }

    std::vector<RelationshipNpc> people;
    std::map<std::string, std::size_t> index;
    for (std::size_t i = 0; i < regulars.size(); ++i) {
        people.push_back({regulars[i].id, NpcTier::Regular, regulars[i].home});
        index[regulars[i].id] = i;
    }
    for (const RelationshipEdge& e : generateRelationships(worldSeed, people)) {
        regulars[index.at(e.a)].relationships.push_back({e.kind, e.b});
        regulars[index.at(e.b)].relationships.push_back({e.kind, e.a});
    }

    std::sort(regulars.begin(), regulars.end(), [](const Regular& l, const Regular& r) { return l.id < r.id; });
    return regulars;
}

}
