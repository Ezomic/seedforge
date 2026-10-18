#include "ichor/graph/dungeon_validate_layout.h"
#include "ichor/graph/embedding.h"
#include "ichor/graph/graph.h"
#include "ichor/graph/mission.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using namespace ichor::graph;
namespace m = ichor::graph::mission;

// Loads regional_tranche_1 to 3 together and checks the whole pool: every skeleton passes the
// layout, mission and embedding validation, nothing repeats across tranches, and the third
// tranche adds variety the first two lack.
namespace {

constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxLinks = 256;

struct Skeleton {
    std::string file;
    std::string id;
    std::string name;
    std::string theme;
    std::vector<std::pair<std::string, embedding::Cell>> rooms; // ids 1..n in order
    std::vector<std::tuple<NodeId, NodeId, std::string>> links;
};

bool splitOn(const std::string& text, char sep, std::vector<std::string>& parts)
{
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == sep) {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    return true;
}

bool toInt(const std::string& text, int& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

// "id:type@x,y,floor"
bool parseRoom(const std::string& text, std::size_t expectedId, std::pair<std::string, embedding::Cell>& room)
{
    const std::size_t colon = text.find(':');
    const std::size_t at = text.find('@');
    if (colon == std::string::npos || at == std::string::npos || at < colon || text.substr(0, colon) != std::to_string(expectedId)) {
        return false;
    }
    room.first = text.substr(colon + 1, at - colon - 1);
    std::vector<std::string> parts;
    splitOn(text.substr(at + 1), ',', parts);
    int v[3] = {0, 0, 0};
    if (parts.size() != 3 || !toInt(parts[0], v[0]) || !toInt(parts[1], v[1]) || !toInt(parts[2], v[2])) {
        return false;
    }
    room.second = {v[0], v[1], v[2]};
    return true;
}

// "from>to:type"
bool parseLink(const std::string& text, std::tuple<NodeId, NodeId, std::string>& link)
{
    const std::size_t arrow = text.find('>');
    const std::size_t colon = text.find(':');
    if (arrow == std::string::npos || colon == std::string::npos || colon < arrow) {
        return false;
    }
    int from = 0;
    int to = 0;
    if (!toInt(text.substr(0, arrow), from) || !toInt(text.substr(arrow + 1, colon - arrow - 1), to) || from < 0 || to < 0) {
        return false;
    }
    link = {static_cast<NodeId>(from), static_cast<NodeId>(to), text.substr(colon + 1)};
    return true;
}

bool stringList(const nlohmann::json& record, const char* key, std::vector<std::string>& out)
{
    if (!record.contains(key) || !record[key].is_array()) {
        return false;
    }
    for (const auto& item : record[key]) {
        if (!item.is_string()) {
            return false;
        }
        out.push_back(item.get<std::string>());
    }
    return true;
}

std::vector<Skeleton> loadFile(const std::string& file, std::vector<std::string>& errors)
{
    std::vector<Skeleton> out;
    std::ifstream stream(std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons" / file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        errors.push_back(file + ": missing or not a JSON array");
        return out;
    }
    for (const auto& record : json) {
        Skeleton s;
        s.file = file;
        std::vector<std::string> rooms;
        std::vector<std::string> links;
        const bool named = record.is_object() && record.contains("id") && record["id"].is_string() && record.contains("name") && record["name"].is_string()
            && record.contains("theme") && record["theme"].is_string();
        if (!named || !stringList(record, "rooms", rooms) || !stringList(record, "links", links) || rooms.size() > kMaxRooms || links.size() > kMaxLinks) {
            errors.push_back(file + ": a record is malformed");
            continue;
        }
        s.id = record["id"].get<std::string>();
        s.name = record["name"].get<std::string>();
        s.theme = record["theme"].get<std::string>();
        bool ok = true;
        for (std::size_t i = 0; ok && i < rooms.size(); ++i) {
            std::pair<std::string, embedding::Cell> room;
            ok = parseRoom(rooms[i], i + 1, room);
            s.rooms.push_back(room);
        }
        for (const std::string& link : links) {
            std::tuple<NodeId, NodeId, std::string> parsed;
            ok = ok && parseLink(link, parsed);
            s.links.push_back(parsed);
        }
        if (ok) {
            out.push_back(std::move(s));
        } else {
            errors.push_back(file + ": " + s.id + " has an unparseable room or link");
        }
    }
    return out;
}

struct Pool {
    std::vector<Skeleton> earlier; // tranches 1 and 2
    std::vector<Skeleton> third;
    std::vector<std::string> errors;

    std::vector<Skeleton> all() const
    {
        std::vector<Skeleton> both = earlier;
        both.insert(both.end(), third.begin(), third.end());
        return both;
    }
};

const Pool& pool()
{
    static const Pool loaded = [] {
        Pool p;
        for (const char* file : {"regional_tranche_1.json", "regional_tranche_2.json"}) {
            const std::vector<Skeleton> part = loadFile(file, p.errors);
            p.earlier.insert(p.earlier.end(), part.begin(), part.end());
        }
        p.third = loadFile("regional_tranche_3.json", p.errors);
        return p;
    }();
    return loaded;
}

Graph buildGraph(const Skeleton& s)
{
    Graph graph;
    for (const auto& room : s.rooms) {
        graph.addNode(room.first);
    }
    for (const auto& [from, to, type] : s.links) {
        graph.addEdge(from, to, type);
    }
    return graph;
}

std::vector<std::string> problems(const Skeleton& s)
{
    std::vector<std::string> out;
    const Graph graph = buildGraph(s);
    if (graph.edges().size() != s.links.size()) {
        out.push_back("a link names a missing room");
        return out;
    }
    for (const dungeon::Finding& f : dungeon::validateLayout(dungeon::fromGraph(graph))) {
        out.push_back("layout: " + f.message);
    }
    for (const std::string& e : m::validateMission(graph)) {
        out.push_back("mission: " + e);
    }
    std::map<NodeId, embedding::Cell> cells;
    for (std::size_t i = 0; i < s.rooms.size(); ++i) {
        cells[static_cast<NodeId>(i + 1)] = s.rooms[i].second;
    }
    embedding::Params footprint;
    footprint.maxExtent = 24;
    footprint.maxFloors = 8;
    for (const std::string& e : embedding::check(graph, cells, footprint)) {
        out.push_back("embedding: " + e);
    }
    return out;
}

std::string signature(const Skeleton& s, bool withCells)
{
    std::string text;
    for (const auto& [type, cell] : s.rooms) {
        text += type;
        if (withCells) {
            text += "@" + std::to_string(cell.x) + "," + std::to_string(cell.y) + "," + std::to_string(cell.floor);
        }
        text += ";";
    }
    std::vector<std::string> links;
    for (const auto& [from, to, type] : s.links) {
        links.push_back(std::to_string(from) + ">" + std::to_string(to) + ":" + type);
    }
    std::sort(links.begin(), links.end());
    for (const std::string& link : links) {
        text += link + ";";
    }
    return text;
}

std::vector<std::string> duplicates(const std::vector<Skeleton>& skeletons)
{
    std::vector<std::string> out;
    std::set<std::string> ids;
    std::set<std::string> names;
    std::set<std::string> themes;
    std::set<std::string> layouts;
    std::set<std::string> topologies;
    for (const Skeleton& s : skeletons) {
        if (!ids.insert(s.id).second) {
            out.push_back("id " + s.id + " is used twice");
        }
        if (!names.insert(s.name).second) {
            out.push_back("name " + s.name + " is used twice");
        }
        if (!themes.insert(s.theme).second) {
            out.push_back("theme " + s.theme + " is used twice");
        }
        if (!layouts.insert(signature(s, true)).second) {
            out.push_back(s.id + " repeats another skeleton layout");
        }
        if (!topologies.insert(signature(s, false)).second) {
            out.push_back(s.id + " repeats another skeleton topology");
        }
    }
    return out;
}

bool mentions(const std::vector<std::string>& found, const std::string& text)
{
    return std::any_of(found.begin(), found.end(), [&](const std::string& f) { return f.find(text) != std::string::npos; });
}

std::size_t countRooms(const Skeleton& s, const char* type)
{
    return static_cast<std::size_t>(std::count_if(s.rooms.begin(), s.rooms.end(), [&](const auto& r) { return r.first == type; }));
}

std::size_t floorCount(const Skeleton& s)
{
    std::set<int> floors;
    for (const auto& room : s.rooms) {
        floors.insert(room.second.floor);
    }
    return floors.size();
}

}

TEST_CASE("all three tranches load", "[dungeon_skeleton_tranche3]")
{
    for (const std::string& e : pool().errors) {
        INFO(e);
        CHECK(e.empty());
    }
    CHECK(pool().errors.empty());
    CHECK(pool().earlier.size() >= 20);
    CHECK(pool().third.size() >= 10);
    CHECK(pool().third.size() <= 15);
}

TEST_CASE("every skeleton of all tranches passes layout, mission and embedding validation", "[dungeon_skeleton_tranche3]")
{
    for (const Skeleton& s : pool().all()) {
        INFO(s.file << " " << s.id);
        const std::vector<std::string> found = problems(s);
        for (const std::string& p : found) {
            INFO(p);
            CHECK(p.empty());
        }
        CHECK(found.empty());
    }
}

TEST_CASE("no skeleton repeats another across the three tranches", "[dungeon_skeleton_tranche3]")
{
    const std::vector<std::string> found = duplicates(pool().all());
    for (const std::string& d : found) {
        INFO(d);
        CHECK(d.empty());
    }
    CHECK(found.empty());
}

TEST_CASE("the third tranche follows the authored shape and widens the pool", "[dungeon_skeleton_tranche3]")
{
    std::size_t smallest = pool().third.front().rooms.size();
    std::size_t largest = smallest;
    std::size_t tallest = 0;
    std::size_t mostGates = 0;
    std::size_t mostShortcuts = 0;
    bool hasLoop = false;
    bool hasRestless = false;
    for (const Skeleton& s : pool().third) {
        INFO(s.id);
        const auto shortcuts = static_cast<std::size_t>(std::count_if(s.links.begin(), s.links.end(), [](const auto& l) { return std::get<2>(l) == m::kShortcut; }));
        const auto paths = static_cast<std::size_t>(std::count_if(s.links.begin(), s.links.end(), [](const auto& l) { return std::get<2>(l) == m::kPath; }));
        CHECK(floorCount(s) >= 2);
        CHECK(countRooms(s, m::kGate) >= 1);
        CHECK(shortcuts >= 1);
        CHECK(s.rooms.size() >= 12);
        CHECK(s.rooms.size() <= 40);
        smallest = std::min(smallest, s.rooms.size());
        largest = std::max(largest, s.rooms.size());
        tallest = std::max(tallest, floorCount(s));
        mostGates = std::max(mostGates, countRooms(s, m::kGate));
        mostShortcuts = std::max(mostShortcuts, shortcuts);
        hasLoop = hasLoop || paths >= s.rooms.size();
        hasRestless = hasRestless || countRooms(s, m::kRest) == 0;
    }
    CHECK(largest - smallest >= 10);
    CHECK(largest > 27); // bigger than anything in tranche two
    CHECK(tallest >= 7);
    CHECK(mostGates >= 3);
    CHECK(mostShortcuts >= 3);
    CHECK(hasLoop);
    CHECK(hasRestless);
}

TEST_CASE("the tranche 3 checks go red on a broken or repeated skeleton", "[dungeon_skeleton_tranche3]")
{
    REQUIRE_FALSE(pool().third.empty());
    const Skeleton& base = pool().third.front();
    REQUIRE(problems(base).empty());
    SECTION("a gate whose key link is removed")
    {
        Skeleton s = base;
        std::erase_if(s.links, [](const auto& l) { return std::get<2>(l) == m::kUnlocks; });
        CHECK(mentions(problems(s), "gate"));
    }
    SECTION("a path between rooms that do not touch")
    {
        Skeleton s = base;
        s.rooms.back().second.x += 7;
        CHECK_FALSE(problems(s).empty());
    }
    SECTION("a link to a room that does not exist")
    {
        Skeleton s = base;
        s.links.emplace_back(1, 99, m::kPath);
        CHECK(mentions(problems(s), "missing room"));
    }
    SECTION("a copy of an earlier skeleton is a duplicate")
    {
        Skeleton copy = pool().earlier.front();
        copy.id = "copy";
        copy.name = "Copy";
        copy.theme = "copy";
        std::vector<Skeleton> both = pool().all();
        both.push_back(copy);
        const auto found = duplicates(both);
        CHECK(mentions(found, "layout"));
        CHECK(mentions(found, "topology"));
    }
    SECTION("an id, name or theme reused from an earlier tranche")
    {
        Skeleton clash = base;
        clash.rooms.pop_back();
        clash.links.pop_back();
        clash.id = pool().earlier.front().id;
        clash.name = pool().earlier.front().name;
        clash.theme = pool().earlier.front().theme;
        std::vector<Skeleton> both = pool().earlier;
        both.push_back(clash);
        const auto found = duplicates(both);
        CHECK(mentions(found, "id "));
        CHECK(mentions(found, "name "));
        CHECK(mentions(found, "theme "));
    }
    SECTION("two skeletons inside tranche 3 sharing an id")
    {
        std::vector<Skeleton> twice = pool().third;
        twice.push_back(twice.front());
        CHECK(mentions(duplicates(twice), "id "));
    }
}
