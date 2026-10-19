#pragma once

#include "ichor/npc/relationships.h"
#include "ichor/npc/schedule.h"
#include "ichor/world/heightfield.h"
#include "ichor/world/placed_pois.h"

#include <worldforge/content_manifest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace worldforge {
class ForgedWorld;
}

namespace ichor {

// The bake step that turns the content manifest's cast (doc 08 phase 11) into NPC spawn records
// (doc 10). The manifest says who lives where: an id, an archetype and a home settlement. This gives
// each one a spot near that settlement, a daily schedule and their relationships, ready for the
// streamer to hand out per cell.
//
// Spot: the home settlement's position plus a seeded offset of up to spawnRadiusM on each axis,
// clamped inside the world, standing on the terrain. The offset comes from the world seed and the
// NPC id alone, so it does not depend on cast order or on which cells are loaded. Roads, water and
// buildings are not consulted; the streamer decides what to do with a spot it cannot use.
//
// Schedule: the archetype's entry in PlacedCastParams::archetypeSchedules, else one of the default
// templates picked by the NPC's id. Every template is parsed with parseSchedule, so a schedule that
// leaves here is legal (ascending, no duplicate start, known states).
//
// Relationships: every cast member is taken as a Regular and generateRelationships links them
// (family, rival, creditor, lover, grudge, three to five each). Each spawn carries the edges that
// touch it. That generator scans every pair, so only the first kMaxRelatedCast members by id take
// part; the rest have none.
//
// Deferred: generateRegulars (names, traits, wants, fears, economic roles) needs name, desire and
// trait pools the manifest does not carry, and succession needs a live ledger. Both hang off npcId.
//
// Output is sorted by npc id; the cast must have unique ids and homes that are placed settlements.

constexpr std::size_t kMaxCastSpawns = 65536;
constexpr std::size_t kMaxRelatedCast = 512;
constexpr float kMaxSpawnRadiusM = 200.0f;

struct NpcSpawn {
    std::string npcId;
    std::string archetype;
    std::string homePoiId;
    double x = 0.0; // world metres
    double z = 0.0;
    float groundY = 0.0f;
    CellId cell;
    Schedule schedule;
    std::vector<RelationshipEdge> relationships; // the edges touching this NPC, sorted by (a, b)
};

struct ArchetypeSchedule {
    std::string archetype;
    std::vector<std::string> entries; // "HH:MM state", as parseSchedule reads them
};

struct PlacedCastParams {
    float spawnRadiusM = 12.0f; // in [0, kMaxSpawnRadiusM]
    std::vector<ArchetypeSchedule> archetypeSchedules;
    std::vector<std::vector<std::string>> defaultSchedules = {
        {"06:00 work", "12:00 social", "13:00 work", "18:00 home", "20:00 social", "22:00 sleep"},
        {"05:00 home", "06:00 work", "14:00 social", "16:00 home", "21:30 sleep"},
        {"07:00 home", "08:00 work", "17:00 social", "19:00 home", "23:00 sleep"},
    };
};

class PlacedCastSet {
public:
    const std::vector<NpcSpawn>& all() const { return spawns_; }
    bool empty() const { return spawns_.empty(); }

    // The NPCs standing in `id`, sorted by npc id; empty for a cell with none or outside the world.
    std::vector<NpcSpawn> inCell(CellId id) const;
    std::vector<NpcSpawn> ofSettlement(const std::string& poiId) const;
    const NpcSpawn* find(const std::string& npcId) const;

private:
    friend struct PlacedCastBuilder;
    std::vector<NpcSpawn> spawns_;
    std::map<CellId, std::vector<std::size_t>> byCell_;
};

struct PlacedCast {
    PlacedCastSet set;
    std::string error; // empty on success

    bool ok() const { return error.empty(); }
};

// Fails, with a message, on an empty heightfield, bad params or schedules, a cast over kMaxCastSpawns,
// two members with one id, an empty id, or a home that is not a placed settlement.
PlacedCast placeCast(const worldforge::ContentManifest& manifest, const PlacedPoiSet& pois, const Heightfield& heightfield, const PlacedCastParams& params = {});

// The forged world's own manifest. Fails when the world did not reach the content phase.
PlacedCast placeForgedCast(const worldforge::ForgedWorld& world, const PlacedPoiSet& pois, const Heightfield& heightfield, const PlacedCastParams& params = {});

}
