#pragma once

#include "ichor/data/data_set.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace ichor {

// Doc 06: the catalogue is authored, the seed only decides where items are. An item can appear in a
// world when at least one of its home axioms is active there; its weight is how many of its homes
// are active, so an item that fits the world twice over turns up more often than one that fits once.

constexpr std::uint32_t kMaxDistributionHomes = 64;

struct CatalogueHome {
    std::string item;
    std::vector<std::string> homes;
};

struct DistributedItem {
    std::string item;
    std::uint32_t weight = 0; // distinct active home axioms, always at least 1
};

// Items that can appear under the active axioms, ordered by item id. Duplicate ids in either input
// count once; an item with no home, or no active home, is left out; unknown axiom ids match nothing.
std::vector<DistributedItem> availableItems(const std::vector<CatalogueHome>& catalogue, const std::set<std::string>& activeAxioms);

// Reads item id and the "home" list from a loaded catalogue table (null table gives an empty list).
std::vector<CatalogueHome> catalogueHomes(const DataTable* table);

// A world's coherent subset: up to count distinct items drawn without replacement, each draw
// proportional to weight. Deterministic for (candidates, seed, count); count above the candidate
// total returns every candidate. Result is ordered by item id.
std::vector<DistributedItem> chooseWorldSubset(const std::vector<DistributedItem>& candidates, std::uint64_t seed, std::uint32_t count);

}
