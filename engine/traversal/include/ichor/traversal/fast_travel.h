#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// Doc 11, "Fast travel": discovered settlements only, along secured roads only. Cost in coin and clock time is
// a separate rule and is not decided here.

struct TravelSettlement {
    std::string id;
    bool discovered = false;
};

// A road between two settlements. It works in both directions. A cut road is one that is not secured.
struct TravelLink {
    std::string a;
    std::string b;
    std::uint32_t length = 0;
    bool secured = false;
};

struct TravelNetwork {
    std::vector<TravelSettlement> settlements;
    std::vector<TravelLink> links;
};

enum class TravelRefusal {
    None,
    SameSettlement,
    UnknownSettlement,
    NotDiscovered, // the start, the destination, or a settlement the only roads pass through
    NoRoad,        // no road joins them at all, even counting cut ones
    RouteCut,      // a road joins them, but every route needs a link that is not secured
};

struct TravelPlan {
    bool allowed = false;
    TravelRefusal refusal = TravelRefusal::None;
    std::vector<std::string> path; // start to destination, inclusive; empty when refused
    std::uint64_t totalLength = 0;
};

// The shortest route from one settlement to another that passes only through discovered settlements over secured
// links. When two routes are equally long, each settlement on the route keeps the smaller-id neighbour before it,
// so the answer does not depend on the order of the input. Links naming an unknown settlement and links from a settlement to itself are ignored, and for a
// duplicate settlement id the first entry wins. A pair of settlements joined by several links uses its shortest
// secured one.
TravelPlan planFastTravel(const TravelNetwork& network, const std::string& from, const std::string& to);

// Every settlement fast travel can reach from this one, sorted by id, not counting the start. Empty when the
// start is unknown or undiscovered.
std::vector<std::string> fastTravelDestinations(const TravelNetwork& network, const std::string& from);

}
