#pragma once

#include <worldforge/subseed.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Phase 11, Binding (doc 08): the manifest that closes the generator, plus the deterministic step
// that fills it from plain inputs. Nothing here includes engine headers.
//
// Inputs are plain lists (BindingInputs): the POIs of phase 10, the item catalogue, the NPC
// archetypes to cast and the quest tension count. The POI list is taken as data because phase 10
// is built in parallel; once it lands its output is mapped onto PoiInput and nothing else changes.
//
// Manifest contents:
//  - items    : one placement per catalogue item that found a lootable POI (kind Dungeon or Ruin),
//               in catalogue-id order.
//  - cast     : params.castPerArchetype named NPCs for every archetype, homed on Settlement POIs,
//               in (archetype id, ordinal) order. Each NPC id is "<archetype>#<ordinal>".
//  - tensions : up to params.tensionCount quest tensions, each between two distinct Settlement
//               POIs (a < b by id), no pair repeated, ordered by (a, b).
//  - dungeons : one slot per Dungeon POI with its dungeonSeed and archetype, in POI-id order.
//               The layout itself is forged from that seed by dungeon_forge.h, which lives in the
//               engine and is deliberately not called from here (libworldforge stays engine-free).
//
// Deferred: NPC naming (needs the name tables), item rarity weighting (needs the loot tables) and
// the dungeon layouts themselves. Each hangs off a field already in the manifest.
//
// Determinism: integer-only, no libm, no unordered container. Every id is derived from sorted
// input, so the result does not depend on input order. Randomness comes only from subseed.h.
//
// Codec: little-endian binary, magic "WFMF", kManifestVersion. decodeManifest never trusts the
// bytes: counts and string lengths are capped before any allocation, every reference must resolve,
// the POI, item, tension and dungeon lists must be strictly sorted and NPC ids unique, and trailing
// bytes fail.
namespace worldforge {

constexpr std::uint32_t kManifestVersion = 1;
// Version 2 adds genVersion (the generator version, doc 08 determinism note 5) right after the world
// seed. Version 1 manifests stay readable and decode with genVersion 0.
constexpr std::uint32_t kManifestVersionGen = 2;
constexpr std::size_t kManifestMaxPois = 4096;
constexpr std::size_t kManifestMaxEntries = 65536;
constexpr std::size_t kManifestMaxIdLength = 64;
constexpr int kManifestMaxPerArchetype = 64;

enum class PoiKind : std::uint8_t { Settlement, Dungeon, Ruin, Landmark };

struct PoiInput {
    std::string id;
    PoiKind kind = PoiKind::Landmark;
    std::string archetype; // dungeon archetype for Dungeon POIs, otherwise ignored
};

struct BindingInputs {
    std::uint64_t worldSeed = 0;
    std::vector<PoiInput> pois;
    std::vector<std::string> itemIds;
    std::vector<std::string> npcArchetypes;
};

struct BindingParams {
    int castPerArchetype = 2; // in [0, kManifestMaxPerArchetype]
    int tensionCount = 4;     // in [0, 4096]
};

struct ItemPlacement {
    std::string itemId;
    std::string poiId;
    friend bool operator==(const ItemPlacement&, const ItemPlacement&) = default;
};

struct CastMember {
    std::string npcId;
    std::string archetype;
    std::string homePoiId;
    friend bool operator==(const CastMember&, const CastMember&) = default;
};

struct QuestTension {
    std::string poiA;
    std::string poiB;
    std::uint8_t intensity = 0;
    friend bool operator==(const QuestTension&, const QuestTension&) = default;
};

struct DungeonSlot {
    std::string poiId;
    std::string archetype;
    std::uint64_t seed = 0;
    friend bool operator==(const DungeonSlot&, const DungeonSlot&) = default;
};

struct ContentManifest {
    std::uint32_t version = kManifestVersion;
    std::uint64_t worldSeed = 0;
    std::uint32_t genVersion = 0; // encoded only when version is kManifestVersionGen
    std::vector<std::string> poiIds; // every POI the manifest may reference, sorted, unique
    std::vector<ItemPlacement> items;
    std::vector<CastMember> cast;
    std::vector<QuestTension> tensions;
    std::vector<DungeonSlot> dungeons;
    friend bool operator==(const ContentManifest&, const ContentManifest&) = default;
};

namespace detail {

constexpr std::uint64_t kBindingDomain = 0x62696e645f6d616eull;
constexpr char kManifestMagic[4] = { 'W', 'F', 'M', 'F' };

enum BindingSalt : std::uint64_t { kSaltItem = 1, kSaltCast = 2, kSaltTension = 3, kSaltIntensity = 4 };

inline std::uint64_t bindHash(std::uint64_t seed, std::uint64_t salt, std::string_view text, std::uint64_t extra = 0)
{
    return splitmix64(splitmix64(splitmix64(seed ^ kBindingDomain) ^ salt) ^ splitmix64(hashText(text) ^ extra));
}

inline bool idValid(std::string_view id) { return !id.empty() && id.size() <= kManifestMaxIdLength; }

inline bool strictlySorted(const std::vector<std::string>& v)
{
    return std::adjacent_find(v.begin(), v.end(), [](const std::string& a, const std::string& b) { return !(a < b); }) == v.end();
}

inline bool validUniqueIds(std::vector<std::string> v)
{
    std::sort(v.begin(), v.end());
    return std::adjacent_find(v.begin(), v.end()) == v.end() && std::all_of(v.begin(), v.end(), [](const std::string& s) { return idValid(s); });
}

inline bool bindingInputsValid(const BindingInputs& in, const BindingParams& p)
{
    if (p.castPerArchetype < 0 || p.castPerArchetype > kManifestMaxPerArchetype || p.tensionCount < 0 || p.tensionCount > static_cast<int>(kManifestMaxPois)) return false;
    if (in.pois.size() > kManifestMaxPois || in.itemIds.size() > kManifestMaxEntries || in.npcArchetypes.size() > kManifestMaxEntries) return false;
    std::vector<std::string> poiIds;
    poiIds.reserve(in.pois.size());
    for (const PoiInput& poi : in.pois) {
        if (static_cast<std::uint8_t>(poi.kind) > static_cast<std::uint8_t>(PoiKind::Landmark)) return false;
        if (poi.archetype.size() > kManifestMaxIdLength) return false;
        if (poi.kind == PoiKind::Dungeon && !idValid(poi.archetype)) return false;
        poiIds.push_back(poi.id);
    }
    if (!validUniqueIds(std::move(poiIds)) || !validUniqueIds(in.itemIds) || !validUniqueIds(in.npcArchetypes)) return false;
    constexpr std::size_t kNpcSuffixMax = 3; // "#" and up to two digits (ordinal < kManifestMaxPerArchetype)
    const bool archetypesFit = std::all_of(in.npcArchetypes.begin(), in.npcArchetypes.end(), [](const std::string& a) { return a.size() + kNpcSuffixMax <= kManifestMaxIdLength; });
    if (p.castPerArchetype > 0 && !archetypesFit) return false;
    return in.npcArchetypes.size() * static_cast<std::size_t>(p.castPerArchetype) <= kManifestMaxEntries;
}

inline std::uint64_t pickIndex(std::uint64_t hash, std::size_t count) { return hash % static_cast<std::uint64_t>(count); }

}

// Returns nullopt when an input or parameter is out of range, ids are empty, over-long or duplicated,
// or a Dungeon POI has no archetype. Otherwise a pure function of (inputs, params), independent of
// the order of the input lists.
inline std::optional<ContentManifest> bindContent(const BindingInputs& inputs, const BindingParams& params = {})
{
    if (!detail::bindingInputsValid(inputs, params)) return std::nullopt;

    std::vector<PoiInput> pois = inputs.pois;
    std::sort(pois.begin(), pois.end(), [](const PoiInput& a, const PoiInput& b) { return a.id < b.id; });
    std::vector<std::string> items = inputs.itemIds;
    std::sort(items.begin(), items.end());
    std::vector<std::string> archetypes = inputs.npcArchetypes;
    std::sort(archetypes.begin(), archetypes.end());

    std::vector<const PoiInput*> lootable;
    std::vector<const PoiInput*> settlements;
    ContentManifest out;
    out.worldSeed = inputs.worldSeed;
    for (const PoiInput& poi : pois) {
        out.poiIds.push_back(poi.id);
        if (poi.kind == PoiKind::Dungeon || poi.kind == PoiKind::Ruin) lootable.push_back(&poi);
        if (poi.kind == PoiKind::Settlement) settlements.push_back(&poi);
        if (poi.kind == PoiKind::Dungeon) out.dungeons.push_back({ poi.id, poi.archetype, dungeonSeed(inputs.worldSeed, poi.id) });
    }

    if (!lootable.empty()) {
        for (const std::string& item : items) {
            const std::size_t at = detail::pickIndex(detail::bindHash(inputs.worldSeed, detail::kSaltItem, item), lootable.size());
            out.items.push_back({ item, lootable[at]->id });
        }
    }

    if (!settlements.empty()) {
        for (const std::string& archetype : archetypes) {
            for (int n = 0; n < params.castPerArchetype; ++n) {
                const std::size_t at = detail::pickIndex(detail::bindHash(inputs.worldSeed, detail::kSaltCast, archetype, static_cast<std::uint64_t>(n)), settlements.size());
                out.cast.push_back({ archetype + "#" + std::to_string(n), archetype, settlements[at]->id });
            }
        }
    }

    const std::size_t s = settlements.size();
    const std::size_t possiblePairs = s < 2 ? 0 : s * (s - 1) / 2;
    const std::size_t wanted = std::min(static_cast<std::size_t>(params.tensionCount), possiblePairs);
    std::vector<QuestTension> picked;
    std::set<std::uint64_t> seen;
    for (std::uint64_t attempt = 0; picked.size() < wanted && attempt < wanted * 64 + 64; ++attempt) {
        const std::uint64_t h = detail::bindHash(inputs.worldSeed, detail::kSaltTension, "pair", attempt);
        const std::size_t a = detail::pickIndex(h, s);
        const std::size_t b = detail::pickIndex(splitmix64(h), s);
        if (a == b) continue;
        if (!seen.insert(static_cast<std::uint64_t>(std::min(a, b)) * s + std::max(a, b)).second) continue;
        QuestTension t{ settlements[std::min(a, b)]->id, settlements[std::max(a, b)]->id, 0 };
        t.intensity = static_cast<std::uint8_t>(detail::bindHash(inputs.worldSeed, detail::kSaltIntensity, t.poiA + "|" + t.poiB) & 255u);
        picked.push_back(std::move(t));
    }
    std::sort(picked.begin(), picked.end(), [](const QuestTension& x, const QuestTension& y) { return x.poiA != y.poiA ? x.poiA < y.poiA : x.poiB < y.poiB; });
    out.tensions = std::move(picked);
    return out;
}

namespace detail {

inline void putU32(std::vector<std::uint8_t>& o, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void putU64(std::vector<std::uint8_t>& o, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void putStr(std::vector<std::uint8_t>& o, const std::string& s)
{
    o.push_back(static_cast<std::uint8_t>(s.size()));
    o.insert(o.end(), s.begin(), s.end());
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool atEnd() const { return pos_ == bytes_.size(); }
    bool u8(std::uint8_t& v)
    {
        if (bytes_.size() - pos_ < 1) return false;
        v = bytes_[pos_++];
        return true;
    }
    bool u32(std::uint32_t& v)
    {
        if (bytes_.size() - pos_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(bytes_[pos_++]) << (8 * i);
        return true;
    }
    bool u64(std::uint64_t& v)
    {
        if (bytes_.size() - pos_ < 8) return false;
        v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(bytes_[pos_++]) << (8 * i);
        return true;
    }
    bool str(std::string& s)
    {
        std::uint8_t n = 0;
        if (!u8(n) || n == 0 || n > kManifestMaxIdLength || bytes_.size() - pos_ < n) return false;
        s.assign(reinterpret_cast<const char*>(bytes_.data() + pos_), n);
        pos_ += n;
        return true;
    }
    // A count is plausible only if the remaining bytes could hold that many entries of `minBytes`.
    bool count(std::size_t cap, std::size_t minBytes, std::size_t& n)
    {
        std::uint32_t v = 0;
        if (!u32(v) || v > cap || static_cast<std::uint64_t>(v) * minBytes > bytes_.size() - pos_) return false;
        n = v;
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

inline bool contains(const std::vector<std::string>& sortedIds, const std::string& id) { return std::binary_search(sortedIds.begin(), sortedIds.end(), id); }

}

inline std::vector<std::uint8_t> encodeManifest(const ContentManifest& m)
{
    std::vector<std::uint8_t> o(std::begin(detail::kManifestMagic), std::end(detail::kManifestMagic));
    detail::putU32(o, m.version);
    detail::putU64(o, m.worldSeed);
    if (m.version == kManifestVersionGen) detail::putU32(o, m.genVersion);
    detail::putU32(o, static_cast<std::uint32_t>(m.poiIds.size()));
    for (const std::string& id : m.poiIds) detail::putStr(o, id);
    detail::putU32(o, static_cast<std::uint32_t>(m.items.size()));
    for (const ItemPlacement& i : m.items) {
        detail::putStr(o, i.itemId);
        detail::putStr(o, i.poiId);
    }
    detail::putU32(o, static_cast<std::uint32_t>(m.cast.size()));
    for (const CastMember& c : m.cast) {
        detail::putStr(o, c.npcId);
        detail::putStr(o, c.archetype);
        detail::putStr(o, c.homePoiId);
    }
    detail::putU32(o, static_cast<std::uint32_t>(m.tensions.size()));
    for (const QuestTension& t : m.tensions) {
        detail::putStr(o, t.poiA);
        detail::putStr(o, t.poiB);
        o.push_back(t.intensity);
    }
    detail::putU32(o, static_cast<std::uint32_t>(m.dungeons.size()));
    for (const DungeonSlot& d : m.dungeons) {
        detail::putStr(o, d.poiId);
        detail::putStr(o, d.archetype);
        detail::putU64(o, d.seed);
    }
    return o;
}

// Returns nullopt for anything that is not a well-formed version-1 or version-2 manifest: bad magic or version, truncation, over-cap counts, empty or over-long ids, unsorted or
// duplicate ids, references to POIs not in poiIds, a dungeon seed that is not dungeonSeed(worldSeed, poi),
// or trailing bytes. Cast order is not checked beyond unique NPC ids.
inline std::optional<ContentManifest> decodeManifest(std::span<const std::uint8_t> bytes)
{
    detail::Reader r(bytes);
    for (const char c : detail::kManifestMagic) {
        std::uint8_t b = 0;
        if (!r.u8(b) || b != static_cast<std::uint8_t>(c)) return std::nullopt;
    }
    ContentManifest m;
    std::size_t n = 0;
    if (!r.u32(m.version) || (m.version != kManifestVersion && m.version != kManifestVersionGen) || !r.u64(m.worldSeed)) return std::nullopt;
    if (m.version == kManifestVersionGen && !r.u32(m.genVersion)) return std::nullopt;

    if (!r.count(kManifestMaxPois, 2, n)) return std::nullopt;
    m.poiIds.resize(n);
    for (std::string& id : m.poiIds) {
        if (!r.str(id)) return std::nullopt;
    }
    if (!detail::strictlySorted(m.poiIds)) return std::nullopt;

    if (!r.count(kManifestMaxEntries, 4, n)) return std::nullopt;
    m.items.resize(n);
    for (ItemPlacement& i : m.items) {
        if (!r.str(i.itemId) || !r.str(i.poiId) || !detail::contains(m.poiIds, i.poiId)) return std::nullopt;
    }
    if (!std::is_sorted(m.items.begin(), m.items.end(), [](const ItemPlacement& a, const ItemPlacement& b) { return a.itemId < b.itemId; })
        || std::adjacent_find(m.items.begin(), m.items.end(), [](const ItemPlacement& a, const ItemPlacement& b) { return a.itemId == b.itemId; }) != m.items.end())
        return std::nullopt;

    if (!r.count(kManifestMaxEntries, 6, n)) return std::nullopt;
    m.cast.resize(n);
    for (CastMember& c : m.cast) {
        if (!r.str(c.npcId) || !r.str(c.archetype) || !r.str(c.homePoiId) || !detail::contains(m.poiIds, c.homePoiId)) return std::nullopt;
    }
    std::vector<std::string> npcIds;
    npcIds.reserve(m.cast.size());
    for (const CastMember& c : m.cast) npcIds.push_back(c.npcId);
    std::sort(npcIds.begin(), npcIds.end());
    if (std::adjacent_find(npcIds.begin(), npcIds.end()) != npcIds.end()) return std::nullopt;

    if (!r.count(kManifestMaxEntries, 5, n)) return std::nullopt;
    m.tensions.resize(n);
    for (QuestTension& t : m.tensions) {
        if (!r.str(t.poiA) || !r.str(t.poiB) || !r.u8(t.intensity) || !(t.poiA < t.poiB) || !detail::contains(m.poiIds, t.poiA) || !detail::contains(m.poiIds, t.poiB))
            return std::nullopt;
    }
    if (std::adjacent_find(m.tensions.begin(), m.tensions.end(), [](const QuestTension& a, const QuestTension& b) { return !(a.poiA != b.poiA ? a.poiA < b.poiA : a.poiB < b.poiB); }) != m.tensions.end())
        return std::nullopt;

    if (!r.count(kManifestMaxPois, 12, n)) return std::nullopt;
    m.dungeons.resize(n);
    for (DungeonSlot& d : m.dungeons) {
        if (!r.str(d.poiId) || !r.str(d.archetype) || !r.u64(d.seed) || !detail::contains(m.poiIds, d.poiId) || d.seed != dungeonSeed(m.worldSeed, d.poiId)) return std::nullopt;
    }
    if (std::adjacent_find(m.dungeons.begin(), m.dungeons.end(), [](const DungeonSlot& a, const DungeonSlot& b) { return !(a.poiId < b.poiId); }) != m.dungeons.end())
        return std::nullopt;

    if (!r.atEnd()) return std::nullopt;
    return m;
}

// Order-sensitive digest of the canonical encoding; the golden value that guards determinism.
inline std::uint64_t hashManifest(const ContentManifest& m)
{
    const std::vector<std::uint8_t> bytes = encodeManifest(m);
    std::uint64_t h = splitmix64(m.worldSeed ^ detail::kBindingDomain);
    for (const std::uint8_t b : bytes) h = splitmix64(h ^ b);
    return h;
}

}
