#pragma once

#include "ichor/graph/quest_expansion.h"
#include "ichor/graph/quest_refusal.h"

#include <worldforge/content_manifest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ichor {

// The bake step that turns the content manifest's quest tensions into quest instances (doc 04, doc 08
// phase 12). Each tension (two settlements and an intensity) picks a tension kind, expands into a
// quest graph with the shipped rules (graph/quest_expansion.h), binds every beat to a persistent
// entity (graph/quest_binding.h) and gets its refusal paths (graph/quest_refusal.h).
//
// What the manifest gives to bind against:
//   Site    : every manifest POI; the tension's two settlements are preferred, A for the situation.
//   Npc     : every cast member; those homed on either settlement are preferred.
//   Faction : every cast archetype (the manifest has no factions yet), preferred when a local NPC has it.
//   Road    : one synthetic road per tension, joining its two settlements.
// The preference is the binder's own "appearances" tie-break, so the quest stays in its region.
//
// A tension that cannot become a quest is not dropped: it lands in `refused` with the reason. The
// set is ordered by the manifest's tension order (poiA, poiB), so nothing depends on cell load order.
//
// Deferred: the spine (graph/spine_cast_binding.h) needs factions with tags and hostility, which the
// manifest does not carry, so it is not bound here. Real factions replace the archetype stand-ins
// when the faction flood-fill reaches the manifest.

enum class QuestEntityKind : std::uint8_t { Faction, Npc, Site, Road };

struct QuestBeat {
    std::uint32_t node = 0; // node id in the quest graph
    std::string type;       // beat type, e.g. "fetch", "walk_away"
    QuestEntityKind kind = QuestEntityKind::Site;
    std::string entity; // POI id, NPC id, cast archetype, or "poiA>poiB" for the road; empty for walk_away
};

struct QuestInstance {
    std::string id;          // "quest_NNNN" in tension order
    std::string tensionKind; // one of graph::quest::kTensionKinds
    std::string poiA;
    std::string poiB;
    std::uint8_t intensity = 0;
    std::uint64_t seed = 0;
    std::uint64_t graphDigest = 0;
    std::vector<QuestBeat> beats;        // in node id order
    graph::questrefusal::Quest refusals; // linear steps with decline, walk away and tell options
};

struct RefusedQuest {
    std::size_t tensionIndex = 0;
    std::string poiA;
    std::string poiB;
    std::string reason;
};

class PlacedQuestSet {
public:
    const std::vector<QuestInstance>& all() const { return quests_; }
    const std::vector<RefusedQuest>& refused() const { return refused_; }
    bool empty() const { return quests_.empty(); }

    const QuestInstance* find(const std::string& questId) const;
    // Quests that start or end at `poiId` or whose beats use it, in quest id order.
    std::vector<const QuestInstance*> touching(const std::string& poiId) const;

private:
    friend struct PlacedQuestBuilder;
    std::vector<QuestInstance> quests_;
    std::vector<RefusedQuest> refused_;
};

struct PlacedQuests {
    PlacedQuestSet set;
    std::string error; // empty on success; set only when the manifest or rules are unusable

    bool ok() const { return error.empty(); }
};

// Fails (error set, nothing placed) on a manifest that does not survive its own codec (unsorted or
// duplicate ids, dangling references, over-cap counts) or on rules that failed to load.
PlacedQuests placeManifestQuests(const worldforge::ContentManifest& manifest, const graph::quest::Ruleset& rules);

}
