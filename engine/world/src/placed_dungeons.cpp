#include "ichor/world/placed_dungeons.h"

#include "ichor/graph/dungeon_forge.h"
#include "ichor/graph/dungeon_validate_layout.h"
#include "ichor/graph/mission.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <tuple>

namespace ichor {

namespace {

using graph::NodeId;
namespace embedding = graph::embedding;

embedding::Params footprint()
{
    embedding::Params p;
    p.maxExtent = 24;
    p.maxFloors = 8;
    return p;
}

bool parseCell(std::string_view text, embedding::Cell& cell)
{
    const char* at = text.data();
    const char* end = at + text.size();
    int v[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        const auto result = std::from_chars(at, end, v[i]);
        if (result.ec != std::errc()) return false;
        at = result.ptr;
        if (i < 2 && (at == end || *at++ != ',')) return false;
    }
    if (at != end) return false;
    cell = {v[0], v[1], v[2]};
    return true;
}

// "id:type@x,y,floor"
bool parseRoom(const std::string& text, std::size_t expectedId, std::string& type, embedding::Cell& cell)
{
    const std::size_t colon = text.find(':');
    const std::size_t at = text.find('@');
    if (colon == std::string::npos || at == std::string::npos || at < colon) return false;
    if (text.substr(0, colon) != std::to_string(expectedId)) return false;
    type = text.substr(colon + 1, at - colon - 1);
    return parseCell(std::string_view(text).substr(at + 1), cell);
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
        if (result.ec != std::errc() || result.ptr == end || *result.ptr != terminator) return false;
        at = result.ptr + 1;
        return true;
    };
    if (!number(from, '>') || !number(to, ':')) return false;
    link = {from, to, std::string(at, end)};
    return true;
}

bool stringList(const nlohmann::json& record, const char* key, std::size_t cap, std::vector<std::string>& out)
{
    if (!record.contains(key) || !record[key].is_array() || record[key].size() > cap) return false;
    for (const auto& item : record[key]) {
        if (!item.is_string()) return false;
        out.push_back(item.get<std::string>());
    }
    return true;
}

std::vector<std::string> problems(const graph::Graph& g, const std::map<NodeId, embedding::Cell>& cells)
{
    std::vector<std::string> out;
    for (const graph::dungeon::Finding& f : graph::dungeon::validateLayout(graph::dungeon::fromGraph(g))) out.push_back("layout: " + f.message);
    for (const std::string& e : graph::mission::validateMission(g)) out.push_back("mission: " + e);
    for (const std::string& e : embedding::check(g, cells, footprint())) out.push_back("embedding: " + e);
    return out;
}

bool stringField(const nlohmann::json& record, const char* key, std::string& out)
{
    if (!record.contains(key) || !record[key].is_string()) return false;
    out = record[key].get<std::string>();
    return !out.empty();
}

}

void DungeonSkeletonCatalog::note(std::string message)
{
    if (errors_.size() < kMaxSkeletonErrors) errors_.push_back(std::move(message));
    else if (errors_.size() == kMaxSkeletonErrors) errors_.push_back("more errors were dropped");
}

void DungeonSkeletonCatalog::add(std::string_view fileName, std::string_view document)
{
    const std::string file(fileName);
    const nlohmann::json json = nlohmann::json::parse(document.begin(), document.end(), nullptr, false);
    if (json.is_discarded() || !json.is_array()) {
        note(file + ": not a JSON array");
        return;
    }
    if (json.size() > kMaxSkeletonRecords) note(file + ": more than " + std::to_string(kMaxSkeletonRecords) + " records, the rest are ignored");
    std::size_t seen = 0;
    for (const auto& record : json) {
        if (seen++ >= kMaxSkeletonRecords) break;
        DungeonSkeleton skeleton;
        if (!record.is_object() || !stringField(record, "id", skeleton.id) || !stringField(record, "theme", skeleton.theme)) {
            note(file + ": a record needs a non-empty string id and theme");
            continue;
        }
        skeleton.name = record.contains("name") && record["name"].is_string() ? record["name"].get<std::string>() : skeleton.id;
        std::vector<std::string> rooms;
        std::vector<std::string> links;
        if (!stringList(record, "rooms", kMaxSkeletonRooms, rooms) || !stringList(record, "links", kMaxSkeletonLinks, links)) {
            note(skeleton.id + ": rooms and links must be lists of strings within the size limits");
            continue;
        }
        bool ok = true;
        for (std::size_t i = 0; i < rooms.size() && ok; ++i) {
            std::string type;
            embedding::Cell cell;
            ok = parseRoom(rooms[i], i + 1, type, cell);
            if (!ok) {
                note(skeleton.id + ": bad room '" + rooms[i] + "'");
                break;
            }
            skeleton.cells[skeleton.graph.addNode(type)] = cell;
        }
        for (const std::string& text : links) {
            if (!ok) break;
            std::tuple<NodeId, NodeId, std::string> link;
            ok = parseLink(text, link) && skeleton.graph.addEdge(std::get<0>(link), std::get<1>(link), std::get<2>(link)) != 0;
            if (!ok) note(skeleton.id + ": bad link '" + text + "'");
        }
        if (!ok) continue;
        const std::vector<std::string> found = problems(skeleton.graph, skeleton.cells);
        if (!found.empty()) {
            note(skeleton.id + ": " + found.front());
            continue;
        }
        skeletons_.push_back(std::move(skeleton));
    }
}

void DungeonSkeletonCatalog::finish()
{
    std::set<std::string> seen;
    std::vector<DungeonSkeleton> unique;
    for (DungeonSkeleton& s : skeletons_) {
        if (!seen.insert(s.id).second) {
            note(s.id + ": id is used twice, the later one is ignored");
            continue;
        }
        unique.push_back(std::move(s));
    }
    std::sort(unique.begin(), unique.end(), [](const DungeonSkeleton& a, const DungeonSkeleton& b) { return std::tie(a.theme, a.id) < std::tie(b.theme, b.id); });
    skeletons_ = std::move(unique);
}

DungeonSkeletonCatalog DungeonSkeletonCatalog::loadText(std::string_view fileName, std::string_view json)
{
    DungeonSkeletonCatalog catalog;
    catalog.add(fileName, json);
    catalog.finish();
    return catalog;
}

DungeonSkeletonCatalog DungeonSkeletonCatalog::loadDirectory(const std::filesystem::path& dir)
{
    DungeonSkeletonCatalog catalog;
    std::vector<std::filesystem::path> files;
    std::error_code code;
    for (std::filesystem::directory_iterator it(dir, code), end; !code && it != end; it.increment(code)) {
        if (it->path().extension() == ".json") files.push_back(it->path());
    }
    if (code) catalog.note("cannot read " + dir.string() + ": " + code.message());
    std::sort(files.begin(), files.end());
    if (files.size() > kMaxSkeletonFiles) {
        catalog.note(dir.string() + ": more than " + std::to_string(kMaxSkeletonFiles) + " files, the rest are ignored");
        files.resize(kMaxSkeletonFiles);
    }
    for (const std::filesystem::path& file : files) {
        const std::uintmax_t size = std::filesystem::file_size(file, code);
        if (code || size > kMaxSkeletonFileBytes) {
            catalog.note(file.filename().string() + ": unreadable or larger than the limit");
            continue;
        }
        std::ifstream stream(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        catalog.add(file.filename().string(), text);
    }
    catalog.finish();
    return catalog;
}

std::vector<const DungeonSkeleton*> DungeonSkeletonCatalog::forTheme(std::string_view theme) const
{
    std::vector<const DungeonSkeleton*> out;
    for (const DungeonSkeleton& s : skeletons_) {
        if (s.theme == theme) out.push_back(&s);
    }
    return out;
}

DungeonLayouts::DungeonLayouts(const worldforge::ContentManifest& manifest, const DungeonSkeletonCatalog& catalog) : catalog_(catalog)
{
    for (const worldforge::DungeonSlot& slot : manifest.dungeons) {
        if (slot.seed != worldforge::dungeonSeed(manifest.worldSeed, slot.poiId)) {
            slotErrors_.push_back(slot.poiId + ": seed is not dungeonSeed(worldSeed, poi)");
        } else if (!slots_.emplace(slot.poiId, slot).second) {
            slotErrors_.push_back(slot.poiId + ": slot listed twice");
        }
    }
    std::sort(slotErrors_.begin(), slotErrors_.end());
}

std::vector<std::string> DungeonLayouts::poiIds() const
{
    std::vector<std::string> out;
    for (const auto& [id, slot] : slots_) out.push_back(id);
    return out;
}

std::vector<std::string> DungeonLayouts::unknownArchetypes() const
{
    std::vector<std::string> out;
    for (const auto& [id, slot] : slots_) {
        if (catalog_.forTheme(slot.archetype).empty()) out.push_back(id + ": no dungeon skeleton for archetype '" + slot.archetype + "'");
    }
    return out;
}

DungeonLayouts::Entry DungeonLayouts::bake(const worldforge::DungeonSlot& slot) const
{
    Entry entry;
    const std::vector<const DungeonSkeleton*> candidates = catalog_.forTheme(slot.archetype);
    if (candidates.empty()) {
        entry.error = slot.poiId + ": no dungeon skeleton for archetype '" + slot.archetype + "'";
        return entry;
    }
    const DungeonSkeleton& skeleton = *candidates[static_cast<std::size_t>(slot.seed % candidates.size())];
    const std::vector<std::string> found = problems(skeleton.graph, skeleton.cells);
    if (!found.empty()) {
        entry.error = slot.poiId + ": skeleton " + skeleton.id + " is invalid: " + found.front();
        return entry;
    }
    auto dungeon = std::make_unique<BakedDungeon>();
    dungeon->poiId = slot.poiId;
    dungeon->archetype = slot.archetype;
    dungeon->skeletonId = skeleton.id;
    dungeon->seed = slot.seed;
    dungeon->graph = skeleton.graph;
    dungeon->cells = skeleton.cells;
    dungeon->layoutHash = graph::forge::layoutHash(dungeon->graph, dungeon->cells);
    entry.dungeon = std::move(dungeon);
    return entry;
}

DungeonLayoutResult DungeonLayouts::layout(const std::string& poiId)
{
    const auto slot = slots_.find(poiId);
    if (slot == slots_.end()) return {nullptr, poiId + ": no dungeon slot"};
    auto cached = baked_.find(poiId);
    if (cached == baked_.end()) cached = baked_.emplace(poiId, bake(slot->second)).first;
    if (!cached->second.error.empty()) return {nullptr, cached->second.error};
    return {cached->second.dungeon.get(), {}};
}

}
