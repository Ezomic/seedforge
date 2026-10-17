#include "ichor/graph/dungeon_forge.h"

#include "ichor/core/hash.h"

#include <cstring>

namespace ichor::graph::forge {

namespace {

constexpr std::uint8_t kMagic[4] = {'D', 'G', 'L', 'F'};
constexpr std::uint8_t kVersion = 1;

void put(std::vector<std::uint8_t>& out, std::uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

std::uint64_t get(std::span<const std::uint8_t> in, std::size_t& at, int bytes)
{
    std::uint64_t v = 0;
    for (int i = 0; i < bytes; ++i)
        v |= static_cast<std::uint64_t>(in[at + i]) << (8 * i);
    at += bytes;
    return v;
}

std::uint64_t checksum(std::span<const std::uint8_t> bytes)
{
    std::uint64_t h = 14695981039346656037ull;
    for (std::uint8_t b : bytes)
        h = mix64(h ^ b);
    return h;
}

std::uint64_t attemptSeed(std::uint64_t seed, std::uint32_t attempt)
{
    return mix64(hashCombine(seed, attempt));
}

mission::Params missionParams(const Record& r)
{
    mission::Params p;
    p.seed = r.seed;
    p.locks = r.locks;
    p.growSteps = r.growSteps;
    return p;
}

embedding::Params embedParams(const Record& r)
{
    embedding::Params p;
    p.seed = r.seed;
    p.maxExtent = r.maxExtent;
    p.maxFloors = r.maxFloors;
    return p;
}

struct Built {
    Instance instance;
    dungeon::Layout layout;
    std::string error;
};

Built build(const mission::Ruleset& rules, const Record& r)
{
    Built b;
    mission::Result m = mission::generate(rules, missionParams(r));
    if (!m.error.empty()) {
        b.error = m.error;
        return b;
    }
    if (!mission::validateMission(m.graph).empty()) {
        b.error = "mission invalid";
        return b;
    }
    embedding::Result e = embedding::embed(m.graph, embedParams(r));
    if (!e.error.empty()) {
        b.error = e.error;
        return b;
    }
    b.instance.graph = std::move(m.graph);
    b.instance.cells = std::move(e.cells);
    b.layout = std::move(e.layout);
    for (const auto& [id, node] : b.instance.graph.nodes())
        b.instance.chambers.push_back(id);
    return b;
}

}

std::uint64_t layoutHash(const Graph& graph, const std::map<NodeId, embedding::Cell>& cells)
{
    std::uint64_t h = hash64("dungeon-layout");
    for (const auto& [id, n] : graph.nodes()) {
        h = hashCombine(h, id);
        h = hash64(n.type, h);
        const auto it = cells.find(id);
        if (it != cells.end()) {
            h = hashCombine(h, static_cast<std::uint32_t>(it->second.x));
            h = hashCombine(h, static_cast<std::uint32_t>(it->second.y));
            h = hashCombine(h, static_cast<std::uint32_t>(it->second.floor));
        }
    }
    for (const auto& [id, e] : graph.edges()) {
        h = hashCombine(h, id);
        h = hashCombine(h, e.from);
        h = hashCombine(h, e.to);
        h = hash64(e.type, h);
    }
    return h;
}

std::string validateRecord(const Record& r)
{
    if (r.locks > mission::kMaxLocks) return "locks out of range";
    if (r.growSteps > mission::kMaxGrowSteps) return "growSteps out of range";
    if (r.maxExtent < 1 || r.maxExtent > embedding::kMaxExtentLimit) return "maxExtent out of range";
    if (r.maxFloors < 1 || r.maxFloors > embedding::kMaxFloorsLimit) return "maxFloors out of range";
    if (r.rooms < 1 || r.rooms > dungeon::kMaxRooms) return "room count out of range";
    return {};
}

ForgeResult forgeLayout(const mission::Ruleset& rules, const Params& p)
{
    ForgeResult out;
    if (p.locks > mission::kMaxLocks) { out.error = "locks out of range"; return out; }
    if (p.growSteps > mission::kMaxGrowSteps) { out.error = "growSteps out of range"; return out; }
    if (p.maxExtent < 1 || p.maxExtent > embedding::kMaxExtentLimit) { out.error = "maxExtent out of range"; return out; }
    if (p.maxFloors < 1 || p.maxFloors > embedding::kMaxFloorsLimit) { out.error = "maxFloors out of range"; return out; }

    Record r;
    r.locks = static_cast<std::uint8_t>(p.locks);
    r.growSteps = static_cast<std::uint8_t>(p.growSteps);
    r.maxExtent = static_cast<std::uint8_t>(p.maxExtent);
    r.maxFloors = static_cast<std::uint8_t>(p.maxFloors);

    std::string last = "no attempt";
    for (std::uint32_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        r.seed = attemptSeed(p.seed, attempt);
        Built b = build(rules, r);
        if (!b.error.empty()) { last = b.error; continue; }
        const std::size_t rooms = b.instance.graph.nodes().size();
        if (rooms > dungeon::kMaxRooms) { last = "too many rooms"; continue; }
        if (!dungeon::validateLayout(b.layout).empty()) { last = "layout invalid"; continue; }
        r.rooms = static_cast<std::uint16_t>(rooms);
        r.layoutHash = layoutHash(b.instance.graph, b.instance.cells);
        out.record = r;
        return out;
    }
    out.error = "no valid layout: " + last;
    return out;
}

InstanceResult instantiate(const mission::Ruleset& rules, const Record& record)
{
    InstanceResult out;
    if (std::string bad = validateRecord(record); !bad.empty()) {
        out.error = std::move(bad);
        return out;
    }
    Built b = build(rules, record);
    if (!b.error.empty()) {
        out.error = std::move(b.error);
        return out;
    }
    if (b.instance.graph.nodes().size() != record.rooms ||
        layoutHash(b.instance.graph, b.instance.cells) != record.layoutHash) {
        out.error = "layout differs from the forged record";
        return out;
    }
    out.instance = std::move(b.instance);
    return out;
}

std::vector<std::uint8_t> encode(const Record& r)
{
    std::vector<std::uint8_t> out;
    out.reserve(kEncodedSize);
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    out.push_back(kVersion);
    put(out, r.seed, 8);
    put(out, r.locks, 1);
    put(out, r.growSteps, 1);
    put(out, r.maxExtent, 1);
    put(out, r.maxFloors, 1);
    put(out, r.rooms, 2);
    put(out, r.layoutHash, 8);
    put(out, checksum(out), 8);
    return out;
}

std::optional<Record> decode(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kEncodedSize) return std::nullopt;
    if (std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0 || bytes[4] != kVersion) return std::nullopt;
    std::size_t at = kEncodedSize - 8;
    if (get(bytes, at, 8) != checksum(bytes.first(kEncodedSize - 8))) return std::nullopt;
    at = 5;
    Record r;
    r.seed = get(bytes, at, 8);
    r.locks = static_cast<std::uint8_t>(get(bytes, at, 1));
    r.growSteps = static_cast<std::uint8_t>(get(bytes, at, 1));
    r.maxExtent = static_cast<std::uint8_t>(get(bytes, at, 1));
    r.maxFloors = static_cast<std::uint8_t>(get(bytes, at, 1));
    r.rooms = static_cast<std::uint16_t>(get(bytes, at, 2));
    r.layoutHash = get(bytes, at, 8);
    if (!validateRecord(r).empty()) return std::nullopt;
    return r;
}

}
