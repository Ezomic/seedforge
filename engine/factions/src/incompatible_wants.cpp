#include "ichor/factions/incompatible_wants.h"

#include <iterator>
#include <set>
#include <string>
#include <tuple>

namespace ichor {

namespace {

using Key = std::tuple<std::string, std::string, std::string>; // axis, faction, stance

std::set<std::string> knownIds(const std::vector<Faction>& factions)
{
    std::set<std::string> ids;
    for (const Faction& faction : factions) {
        ids.insert(faction.id);
    }
    return ids;
}

std::set<Key> usableWants(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants)
{
    const std::set<std::string> ids = knownIds(factions);
    std::set<Key> unique;
    for (const FactionWant& want : wants) {
        if (!want.axis.empty() && !want.stance.empty() && ids.count(want.faction) != 0) {
            unique.emplace(want.axis, want.faction, want.stance);
        }
    }
    return unique;
}

bool incompatible(const Key& a, const Key& b) { return std::get<1>(a) != std::get<1>(b) && std::get<2>(a) != std::get<2>(b); }

}

std::vector<IncompatiblePair> incompatiblePairs(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants)
{
    const std::set<Key> unique = usableWants(factions, wants);
    std::vector<IncompatiblePair> pairs;
    for (auto first = unique.begin(); first != unique.end(); ++first) {
        for (auto second = std::next(first); second != unique.end() && std::get<0>(*second) == std::get<0>(*first); ++second) {
            if (incompatible(*first, *second)) {
                pairs.push_back({std::get<0>(*first), std::get<1>(*first), std::get<2>(*first), std::get<1>(*second), std::get<2>(*second)});
            }
        }
    }
    return pairs;
}

bool hasIncompatiblePair(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants)
{
    const std::set<Key> unique = usableWants(factions, wants);
    for (auto first = unique.begin(); first != unique.end(); ++first) {
        for (auto second = std::next(first); second != unique.end() && std::get<0>(*second) == std::get<0>(*first); ++second) {
            if (incompatible(*first, *second)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> validateIncompatibleWants(const std::vector<Faction>& factions, const std::vector<FactionWant>& wants)
{
    const std::set<std::string> ids = knownIds(factions);
    std::vector<std::string> problems;
    for (std::size_t i = 0; i < wants.size(); ++i) {
        const std::string at = "wants[" + std::to_string(i) + "]";
        if (ids.count(wants[i].faction) == 0) {
            problems.push_back(at + ": faction \"" + wants[i].faction.substr(0, 40) + "\" is not in the faction set");
        }
        if (wants[i].axis.empty()) {
            problems.push_back(at + ": axis is empty");
        }
        if (wants[i].stance.empty()) {
            problems.push_back(at + ": stance is empty");
        }
    }
    if (!hasIncompatiblePair(factions, wants)) {
        problems.push_back("no two factions want incompatible things: the player never actually chooses (doc 03 rule 4)");
    }
    return problems;
}

}
