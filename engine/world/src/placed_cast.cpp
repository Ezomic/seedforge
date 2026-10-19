#include "ichor/world/placed_cast.h"

#include <worldforge/forge_world.h>
#include <worldforge/subseed.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

namespace ichor {

struct PlacedCastBuilder {
    static std::vector<NpcSpawn>& spawns(PlacedCastSet& set) { return set.spawns_; }

    static void index(PlacedCastSet& set, const Heightfield& heightfield)
    {
        std::sort(set.spawns_.begin(), set.spawns_.end(), [](const NpcSpawn& a, const NpcSpawn& b) { return a.npcId < b.npcId; });
        const auto last = static_cast<std::int32_t>(heightfield.desc().cellsPerSide()) - 1;
        for (std::size_t i = 0; i < set.spawns_.size(); ++i) {
            NpcSpawn& spawn = set.spawns_[i];
            CellId cell = heightfield.cellAt(spawn.x, spawn.z);
            cell.x = std::clamp(cell.x, 0, last);
            cell.z = std::clamp(cell.z, 0, last);
            spawn.cell = cell;
            set.byCell_[cell].push_back(i);
        }
    }
};

namespace {

constexpr std::uint64_t kCastSpawnDomain = 0x636173747370776eull;
constexpr std::uint64_t kCastScheduleSalt = 1;
constexpr std::uint64_t kCastOffsetSalt = 2;
constexpr std::uint64_t kCastRelatedSalt = 3;
constexpr std::size_t kMaxTemplates = 64;

PlacedCast failure(std::string message)
{
    PlacedCast out;
    out.error = std::move(message);
    return out;
}

std::uint64_t castHash(std::uint64_t seed, std::uint64_t salt, std::string_view id, std::uint64_t extra = 0)
{
    return worldforge::splitmix64(worldforge::splitmix64(worldforge::splitmix64(seed ^ kCastSpawnDomain) ^ salt) ^ worldforge::splitmix64(worldforge::hashText(id) ^ extra));
}

double unit(std::uint64_t h) { return static_cast<double>(h >> 11) * (1.0 / 9007199254740992.0); }

struct Templates {
    std::vector<Schedule> defaults;
    std::map<std::string, Schedule, std::less<>> byArchetype;
};

bool parseTemplates(const PlacedCastParams& p, Templates& out, std::string& error)
{
    if (p.defaultSchedules.empty() || p.defaultSchedules.size() > kMaxTemplates || p.archetypeSchedules.size() > kMaxTemplates) {
        error = "the schedule templates are missing or too many";
        return false;
    }
    for (const auto& entries : p.defaultSchedules) {
        ScheduleParse parsed = parseSchedule(entries);
        if (!parsed.schedule) {
            error = "a default schedule is not legal: " + parsed.error;
            return false;
        }
        if (parsed.schedule->entries.size() > kMaxScheduleEntries) {
            error = "a default schedule has too many entries";
            return false;
        }
        out.defaults.push_back(std::move(*parsed.schedule));
    }
    for (const ArchetypeSchedule& a : p.archetypeSchedules) {
        ScheduleParse parsed = parseSchedule(a.entries);
        if (!parsed.schedule) {
            error = std::format("the schedule for archetype '{}' is not legal: {}", a.archetype, parsed.error);
            return false;
        }
        if (parsed.schedule->entries.size() > kMaxScheduleEntries) {
            error = std::format("the schedule for archetype '{}' has too many entries", a.archetype);
            return false;
        }
        if (!out.byArchetype.emplace(a.archetype, std::move(*parsed.schedule)).second) {
            error = std::format("two schedules for archetype '{}'", a.archetype);
            return false;
        }
    }
    return true;
}

}

std::vector<NpcSpawn> PlacedCastSet::inCell(CellId id) const
{
    std::vector<NpcSpawn> out;
    if (const auto found = byCell_.find(id); found != byCell_.end()) {
        for (const std::size_t i : found->second) out.push_back(spawns_[i]);
    }
    return out;
}

std::vector<NpcSpawn> PlacedCastSet::ofSettlement(const std::string& poiId) const
{
    std::vector<NpcSpawn> out;
    for (const NpcSpawn& s : spawns_) {
        if (s.homePoiId == poiId) out.push_back(s);
    }
    return out;
}

const NpcSpawn* PlacedCastSet::find(const std::string& npcId) const
{
    const auto it = std::lower_bound(spawns_.begin(), spawns_.end(), npcId, [](const NpcSpawn& s, const std::string& id) { return s.npcId < id; });
    return it != spawns_.end() && it->npcId == npcId ? &*it : nullptr;
}

PlacedCast placeCast(const worldforge::ContentManifest& manifest, const PlacedPoiSet& pois, const Heightfield& heightfield, const PlacedCastParams& params)
{
    if (heightfield.empty() || !heightfield.desc().valid()) return failure("the heightfield is empty");
    if (!std::isfinite(params.spawnRadiusM) || params.spawnRadiusM < 0.0f || params.spawnRadiusM > kMaxSpawnRadiusM) return failure("the spawn radius is out of range or not finite");
    Templates templates;
    std::string error;
    if (!parseTemplates(params, templates, error)) return failure(error);
    if (manifest.cast.size() > kMaxCastSpawns) return failure("the cast is too large");

    std::vector<std::string_view> ids;
    ids.reserve(manifest.cast.size());
    for (const worldforge::CastMember& member : manifest.cast) {
        if (member.npcId.empty()) return failure("a cast member has an empty id");
        ids.push_back(member.npcId);
    }
    std::sort(ids.begin(), ids.end());
    if (const auto dup = std::adjacent_find(ids.begin(), ids.end()); dup != ids.end()) return failure(std::format("two cast members share the id {}", *dup));

    const std::uint64_t seed = manifest.worldSeed;
    const double extent = heightfield.desc().sizeMetres;
    const double radius = params.spawnRadiusM;
    PlacedCast out;
    std::vector<NpcSpawn>& spawns = PlacedCastBuilder::spawns(out.set);
    spawns.reserve(manifest.cast.size());
    for (const worldforge::CastMember& member : manifest.cast) {
        const PlacedPoi* home = pois.find(member.homePoiId);
        if (home == nullptr || home->kind != PlacedPoiKind::Settlement) return failure(std::format("{} has no placed settlement as a home ({})", member.npcId, member.homePoiId));
        NpcSpawn spawn;
        spawn.npcId = member.npcId;
        spawn.archetype = member.archetype;
        spawn.homePoiId = member.homePoiId;
        const double dx = (unit(castHash(seed, kCastOffsetSalt, member.npcId, 0)) * 2.0 - 1.0) * radius;
        const double dz = (unit(castHash(seed, kCastOffsetSalt, member.npcId, 1)) * 2.0 - 1.0) * radius;
        spawn.x = std::clamp(home->x + dx, 0.0, extent);
        spawn.z = std::clamp(home->z + dz, 0.0, extent);
        const float ground = heightfield.heightAt(spawn.x, spawn.z);
        spawn.groundY = std::isfinite(ground) ? ground : home->groundY;
        if (const auto own = templates.byArchetype.find(member.archetype); own != templates.byArchetype.end()) {
            spawn.schedule = own->second;
        } else {
            spawn.schedule = templates.defaults[castHash(seed, kCastScheduleSalt, member.npcId) % templates.defaults.size()];
        }
        spawns.push_back(std::move(spawn));
    }
    PlacedCastBuilder::index(out.set, heightfield);

    std::vector<std::pair<std::uint64_t, std::size_t>> ranked;
    ranked.reserve(spawns.size());
    for (std::size_t i = 0; i < spawns.size(); ++i) ranked.emplace_back(castHash(seed, kCastRelatedSalt, spawns[i].npcId), i);
    std::sort(ranked.begin(), ranked.end());
    ranked.resize(std::min(ranked.size(), kMaxRelatedCast));
    std::vector<RelationshipNpc> related;
    for (const auto& [rank, i] : ranked) related.push_back({spawns[i].npcId, NpcTier::Regular, spawns[i].homePoiId});
    for (const RelationshipEdge& edge : generateRelationships(seed, related)) {
        for (const std::string* end : {&edge.a, &edge.b}) {
            const auto it = std::lower_bound(spawns.begin(), spawns.end(), *end, [](const NpcSpawn& s, const std::string& id) { return s.npcId < id; });
            it->relationships.push_back(edge);
        }
    }
    return out;
}

PlacedCast placeForgedCast(const worldforge::ForgedWorld& world, const PlacedPoiSet& pois, const Heightfield& heightfield, const PlacedCastParams& params)
{
    if (!world.manifest().has_value()) return failure(world.ok() ? "the forged world stopped before the content phase" : world.error());
    return placeCast(*world.manifest(), pois, heightfield, params);
}

}
