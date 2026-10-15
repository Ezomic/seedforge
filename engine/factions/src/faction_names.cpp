#include "ichor/factions/faction_names.h"

#include "ichor/core/hash.h"
#include "ichor/core/rng.h"

#include <array>
#include <set>

namespace ichor {

namespace {

constexpr std::size_t kSuffixCount = 5;
constexpr std::uint32_t kRerolls = 8;

const std::array<std::array<std::string_view, kSuffixCount>, kFactionRoleCount> kSuffixes = { {
    { "Assize", "Throne", "Synod", "Council", "Hegemony" },
    { "League", "Banner", "Brotherhood", "Levy", "Covenant" },
    { "Mnemones", "Choir", "Brood", "Tithe", "Lamplighters" },
    { "Vigil", "Wardens", "Keepers", "Stewards", "Archive" },
    { "Remnant", "Dissenters", "Hermits", "Abstainers", "Few" },
    { "Heralds", "Voices", "Oracles", "Criers", "Harbingers" },
} };

bool isAlnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::vector<std::string> usableNouns(const std::vector<std::string>& nouns)
{
    std::vector<std::string> out;
    const std::size_t looked = nouns.size() < kMaxNameNouns ? nouns.size() : kMaxNameNouns;
    for (std::size_t i = 0; i < looked; ++i) {
        std::string noun = cleanNameNoun(nouns[i]);
        if (!noun.empty()) {
            out.push_back(std::move(noun));
        }
    }
    return out;
}

std::string compose(std::uint64_t seed, std::string_view factionId, FactionRole role, const std::vector<std::string>& pool, std::uint32_t attempt)
{
    const auto& suffixes = kSuffixes[static_cast<std::size_t>(role)];
    Rng rng(hashCombine(hashCombine(seed, hash64(factionId)), attempt), static_cast<std::uint64_t>(role));
    const std::string_view suffix = suffixes[rng.range(0, kSuffixCount - 1)];
    if (pool.empty()) {
        return "The " + std::string(suffix);
    }
    const std::string& noun = pool[rng.range(0, static_cast<std::uint32_t>(pool.size() - 1))];
    if (rng.range(0, 3) == 0) {
        return "The " + std::string(suffix) + " of " + noun;
    }
    return "The " + noun + " " + std::string(suffix);
}

}

std::string cleanNameNoun(std::string_view raw)
{
    std::string out;
    bool wordStart = true;
    for (const char c : raw) {
        if (out.size() >= kMaxNameNounLength) {
            break;
        }
        if (isAlnum(c)) {
            const bool upper = c >= 'A' && c <= 'Z';
            const bool lower = c >= 'a' && c <= 'z';
            if (wordStart && lower) {
                out.push_back(static_cast<char>(c - 'a' + 'A'));
            } else if (!wordStart && upper) {
                out.push_back(static_cast<char>(c - 'A' + 'a'));
            } else {
                out.push_back(c);
            }
            wordStart = false;
        } else if (c == '\'' || c == '-') {
            if (!out.empty()) {
                out.push_back(c);
            }
        } else if (c == '_' || c == ' ') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
                wordStart = true;
            }
        }
    }
    while (!out.empty() && !isAlnum(out.back())) {
        out.pop_back();
    }
    return out;
}

std::string composeFactionName(std::uint64_t seed, std::string_view factionId, FactionRole role, const std::vector<std::string>& nouns)
{
    return compose(seed, factionId, role, usableNouns(nouns), 0);
}

std::vector<std::string> composeFactionNames(std::uint64_t seed, const std::vector<Faction>& factions, const std::vector<std::string>& nouns)
{
    const std::vector<std::string> pool = usableNouns(nouns);
    std::set<std::string> used;
    std::vector<std::string> out;
    out.reserve(factions.size());
    for (const Faction& faction : factions) {
        std::string name;
        for (std::uint32_t attempt = 0; attempt <= kRerolls; ++attempt) {
            name = compose(seed, faction.id, faction.role, pool, attempt);
            if (!used.contains(name)) {
                break;
            }
        }
        if (used.contains(name)) {
            const std::string base = name;
            for (std::size_t k = 2; used.contains(name); ++k) {
                name = base + " " + std::to_string(k);
            }
        }
        used.insert(name);
        out.push_back(std::move(name));
    }
    return out;
}

}
