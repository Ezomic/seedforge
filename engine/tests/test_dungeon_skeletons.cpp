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

namespace {

constexpr std::size_t kTrancheOneSize = 10;
constexpr std::size_t kMaxRooms = 64;
constexpr std::size_t kMaxLinks = 256;

struct Skeleton {
    std::string id;
    std::string name;
    std::vector<std::pair<std::string, embedding::Cell>> rooms; // in id order, ids 1..n
    std::vector<std::tuple<NodeId, NodeId, std::string>> links;
};

struct Loaded {
    std::vector<Skeleton> skeletons;
    std::vector<std::string> errors;
};

bool parseInt(const char*& at, const char* end, int& value)
{
    const auto result = std::from_chars(at, end, value);
    if (result.ec != std::errc()) {
        return false;
    }
    at = result.ptr;
    return true;
}

bool parseCell(const std::string& text, embedding::Cell& cell)
{
    const char* at = text.data();
    const char* end = at + text.size();
    int v[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        if (!parseInt(at, end, v[i]) || (i < 2 && (at == end || *at++ != ','))) {
            return false;
        }
    }
    if (at != end) {
        return false;
    }
    cell = {v[0], v[1], v[2]};
    return true;
}

// "id:type@x,y,floor"
bool parseRoom(const std::string& text, std::size_t expectedId, std::pair<std::string, embedding::Cell>& room)
{
    const std::size_t colon = text.find(':');
    const std::size_t at = text.find('@');
    if (colon == std::string::npos || at == std::string::npos || at < colon) {
        return false;
    }
    if (text.substr(0, colon) != std::to_string(expectedId)) {
        return false;
    }
    room.first = text.substr(colon + 1, at - colon - 1);
    return parseCell(text.substr(at + 1), room.second);
}

// "from>to:type"
bool parseLink(const std::string& text, std::tuple<NodeId, NodeId, std::string>& link)
{
    const char* at = text.data();
    const char* end = at + text.size();
    std::uint32_t from = 0;
    std::uint32_t to = 0;
    const auto number = [&](std::uint32_t& value, char terminator) {
        const auto result = std::from_chars(at, end, value);
        if (result.ec != std::errc() || result.ptr == end || *result.ptr != terminator) {
            return false;
        }
        at = result.ptr + 1;
        return true;
    };
    if (!number(from, '>') || !number(to, ':')) {
        return false;
    }
    link = {from, to, std::string(at, end)};
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

Loaded loadText(const std::string& file, const std::string& document)
{
    Loaded loaded;
    const nlohmann::json json = nlohmann::json::parse(document, nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        loaded.errors.push_back(file + ": not a JSON array");
        return loaded;
    }
    for (const auto& record : json) {
        Skeleton skeleton;
        if (!record.is_object() || !record.contains("id") || !record["id"].is_string() || !record.contains("name") || !record["name"].is_string()) {
            loaded.errors.push_back(file + ": a record needs a string id and name");
            continue;
        }
        skeleton.id = record["id"].get<std::string>();
        skeleton.name = record["name"].get<std::string>();
        std::vector<std::string> rooms;
        std::vector<std::string> links;
        if (!record.contains("theme") || !record["theme"].is_string() || record["theme"].get<std::string>().empty()) {
            loaded.errors.push_back(skeleton.id + ": needs a non-empty string theme");
            continue;
        }
        if (!stringList(record, "rooms", rooms) || !stringList(record, "links", links) || rooms.size() > kMaxRooms || links.size() > kMaxLinks) {
            loaded.errors.push_back(skeleton.id + ": rooms and links must be lists of strings within the size limits");
            continue;
        }
        bool ok = true;
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            std::pair<std::string, embedding::Cell> room;
            if (!parseRoom(rooms[i], i + 1, room)) {
                loaded.errors.push_back(skeleton.id + ": bad room '" + rooms[i] + "'");
                ok = false;
                break;
            }
            skeleton.rooms.push_back(room);
        }
        for (const std::string& text : links) {
            std::tuple<NodeId, NodeId, std::string> link;
            if (!ok || !parseLink(text, link)) {
                if (ok) {
                    loaded.errors.push_back(skeleton.id + ": bad link '" + text + "'");
                }
                ok = false;
                break;
            }
            skeleton.links.push_back(link);
        }
        if (ok) {
            loaded.skeletons.push_back(std::move(skeleton));
        }
    }
    return loaded;
}

Loaded loadAll()
{
    Loaded all;
    const std::filesystem::path dir = std::filesystem::path(ICHOR_REPO_DATA) / "dungeon_skeletons";
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (std::filesystem::directory_iterator it(dir, code), end; !code && it != end; it.increment(code)) {
        if (it->path().extension() == ".json") {
            files.push_back(it->path());
        }
    }
    if (code) {
        all.errors.push_back("cannot read " + dir.string() + ": " + code.message());
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::ifstream stream(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        Loaded one = loadText(file.filename().string(), text);
        all.errors.insert(all.errors.end(), one.errors.begin(), one.errors.end());
        for (Skeleton& s : one.skeletons) {
            all.skeletons.push_back(std::move(s));
        }
    }
    return all;
}

const Loaded& shipped()
{
    static const Loaded loaded = loadAll();
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

std::map<NodeId, embedding::Cell> cellsOf(const Skeleton& s)
{
    std::map<NodeId, embedding::Cell> cells;
    for (std::size_t i = 0; i < s.rooms.size(); ++i) {
        cells[static_cast<NodeId>(i + 1)] = s.rooms[i].second;
    }
    return cells;
}

embedding::Params footprint()
{
    embedding::Params p;
    p.maxExtent = 24;
    p.maxFloors = 8;
    return p;
}

// Equal for two skeletons with the same room types and links (and, with cells, the same cells).
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
    for (const std::string& e : embedding::check(graph, cellsOf(s), footprint())) {
        out.push_back("embedding: " + e);
    }
    return out;
}

std::vector<std::string> duplicates(const std::vector<Skeleton>& skeletons)
{
    std::vector<std::string> out;
    std::set<std::string> ids;
    std::set<std::string> names;
    std::set<std::string> layouts;
    std::set<std::string> topologies;
    for (const Skeleton& s : skeletons) {
        if (!ids.insert(s.id).second) {
            out.push_back("id " + s.id + " is used twice");
        }
        if (!names.insert(s.name).second) {
            out.push_back("name " + s.name + " is used twice");
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

const Skeleton& first()
{
    REQUIRE_FALSE(shipped().skeletons.empty());
    return shipped().skeletons.front();
}

}

TEST_CASE("every skeleton file parses", "[dungeon_skeletons]")
{
    for (const std::string& e : shipped().errors) {
        INFO(e);
        CHECK(e.empty());
    }
    CHECK(shipped().errors.empty());
    CHECK(shipped().skeletons.size() >= kTrancheOneSize);
}

TEST_CASE("every skeleton passes layout, mission and embedding validation", "[dungeon_skeletons]")
{
    for (const Skeleton& s : shipped().skeletons) {
        INFO(s.id);
        const std::vector<std::string> found = problems(s);
        for (const std::string& p : found) {
            INFO(p);
            CHECK(p.empty());
        }
        CHECK(found.empty());
    }
}

TEST_CASE("skeletons are unique", "[dungeon_skeletons]")
{
    CHECK(duplicates(shipped().skeletons).empty());
}

TEST_CASE("every skeleton has the authored shape the design asks for", "[dungeon_skeletons]")
{
    for (const Skeleton& s : shipped().skeletons) {
        INFO(s.id);
        std::set<int> floors;
        std::size_t gates = 0;
        std::size_t shortcuts = 0;
        for (const auto& [type, cell] : s.rooms) {
            floors.insert(cell.floor);
            gates += type == m::kGate ? 1 : 0;
        }
        for (const auto& link : s.links) {
            shortcuts += std::get<2>(link) == m::kShortcut ? 1 : 0;
        }
        CHECK(floors.size() >= 2);
        CHECK(gates >= 1);
        CHECK(shortcuts >= 1);
        CHECK(s.rooms.size() >= 12);
        CHECK(s.rooms.size() <= 40);
    }
}

TEST_CASE("the checks go red on a broken skeleton", "[dungeon_skeletons]")
{
    REQUIRE(problems(first()).empty());
    SECTION("a gate whose key link is removed")
    {
        Skeleton s = first();
        std::erase_if(s.links, [](const auto& l) { return std::get<2>(l) == m::kUnlocks; });
        const auto found = problems(s);
        CHECK(mentions(found, "layout: gate"));
        CHECK(mentions(found, "mission: gate"));
    }
    SECTION("a path link between rooms that do not touch")
    {
        Skeleton s = first();
        s.rooms.back().second.x += 5;
        CHECK(mentions(problems(s), "does not join touching cells"));
    }
    SECTION("two rooms on one cell")
    {
        Skeleton s = first();
        s.rooms[1].second = s.rooms[0].second;
        CHECK(mentions(problems(s), "shares a cell"));
    }
    SECTION("a link to a room that does not exist")
    {
        Skeleton s = first();
        s.links.emplace_back(1, 9999, m::kPath);
        CHECK(mentions(problems(s), "names a missing room"));
    }
    SECTION("a skeleton moved one cell is still a duplicate topology")
    {
        Skeleton copy = first();
        copy.id = "copy";
        copy.name = "Copy";
        for (auto& room : copy.rooms) {
            room.second.x += 1;
        }
        CHECK(mentions(duplicates({first(), copy}), "topology"));
    }
    SECTION("a verbatim copy is a duplicate layout")
    {
        Skeleton copy = first();
        copy.id = "copy";
        copy.name = "Copy";
        CHECK(mentions(duplicates({first(), copy}), "layout"));
    }
    SECTION("a reused id and name")
    {
        const auto found = duplicates({first(), first()});
        CHECK(mentions(found, "id "));
        CHECK(mentions(found, "name "));
    }
}

TEST_CASE("hostile skeleton text is rejected without crashing", "[dungeon_skeletons]")
{
    const auto record = [](const std::string& rooms, const std::string& links) {
        return "[{\"id\":\"a\",\"name\":\"A\",\"theme\":\"t\",\"rooms\":[" + rooms + "],\"links\":[" + links + "]}]";
    };
    const std::vector<std::string> cases = {
        "", "[", "{}", "[1]", "[{\"id\":1}]", "[{\"id\":\"a\",\"name\":\"A\"}]",
        "[{\"id\":\"a\",\"name\":\"A\",\"rooms\":[\"1:entry@0,0,0\"],\"links\":[]}]",
        record("\"2:entry@0,0,0\"", ""),
        record("\"1:entry@0,0\"", ""),
        record("\"1:entry@99999999999,0,0\"", ""),
        record("\"1:entry@0,0,0@5\"", ""),
        record("\"1:entry@0,0,0\"", "\"1>x:path\""),
        record("\"1:entry@0,0,0\"", "\"1>2x:path\""),
        record("\"1:entry@0,0,0\"", "\"-1>2:path\""),
        record("\"1:entry@0,0,0\"", "\"4294967297>2:path\""),
    };
    for (const std::string& text : cases) {
        INFO(text);
        const Loaded loaded = loadText("hostile.json", text);
        CHECK_FALSE(loaded.errors.empty());
        CHECK(loaded.skeletons.empty());
    }
    std::string tooMany;
    for (std::size_t i = 1; i <= kMaxRooms + 1; ++i) {
        tooMany += (i > 1 ? ",\"" : "\"") + std::to_string(i) + ":rest@" + std::to_string(i) + ",0,0\"";
    }
    CHECK_FALSE(loadText("big.json", record(tooMany, "")).errors.empty());
}
