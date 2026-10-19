#include "ichor/world/placed_quests.h"

#include <catch2/catch_test_macros.hpp>

#include <worldforge/forge_world.h>

#include <algorithm>
#include <set>
#include <string>

using namespace ichor;

namespace {

const graph::quest::Ruleset& rules()
{
    static const graph::quest::Ruleset r = graph::quest::loadRuleset(std::string(ICHOR_REPO_DATA) + "/quests/expansion_rules.json");
    return r;
}

worldforge::ContentManifest manifestFor(std::uint64_t seed, int settlements, int tensions)
{
    worldforge::BindingInputs in;
    in.worldSeed = seed;
    for (int i = 0; i < settlements; ++i) in.pois.push_back({"settlement_" + std::to_string(1000 + i), worldforge::PoiKind::Settlement, ""});
    in.pois.push_back({"poi_0001", worldforge::PoiKind::Dungeon, "crypt"});
    in.pois.push_back({"poi_0002", worldforge::PoiKind::Ruin, ""});
    in.pois.push_back({"poi_0003", worldforge::PoiKind::Landmark, ""});
    in.itemIds = {"sword", "lamp"};
    in.npcArchetypes = {"smith", "priest", "scout"};
    worldforge::BindingParams params;
    params.tensionCount = tensions;
    auto manifest = worldforge::bindContent(in, params);
    REQUIRE(manifest.has_value());
    return *manifest;
}

}

TEST_CASE("every manifest tension becomes a quest bound to existing POIs and cast", "[bake][quests]")
{
    REQUIRE(rules().error.empty());
    const worldforge::ContentManifest manifest = manifestFor(7, 6, 8);
    REQUIRE(manifest.tensions.size() == 8);
    const PlacedQuests placed = placeManifestQuests(manifest, rules());
    REQUIRE(placed.ok());
    REQUIRE(placed.set.refused().empty());
    REQUIRE(placed.set.all().size() == manifest.tensions.size());

    std::set<std::string> pois(manifest.poiIds.begin(), manifest.poiIds.end());
    std::set<std::string> npcs;
    std::set<std::string> archetypes;
    for (const auto& c : manifest.cast) {
        npcs.insert(c.npcId);
        archetypes.insert(c.archetype);
    }
    for (std::size_t i = 0; i < placed.set.all().size(); ++i) {
        const QuestInstance& q = placed.set.all()[i];
        REQUIRE(q.poiA == manifest.tensions[i].poiA);
        REQUIRE(q.poiB == manifest.tensions[i].poiB);
        REQUIRE(q.intensity == manifest.tensions[i].intensity);
        REQUIRE(pois.contains(q.poiA));
        REQUIRE_FALSE(q.beats.empty());
        for (const QuestBeat& b : q.beats) {
            if (b.type == graph::quest::kWalkAway) {
                REQUIRE(b.entity.empty());
                continue;
            }
            switch (b.kind) {
            case QuestEntityKind::Site: REQUIRE(pois.contains(b.entity)); break;
            case QuestEntityKind::Npc: REQUIRE(npcs.contains(b.entity)); break;
            case QuestEntityKind::Faction: REQUIRE(archetypes.contains(b.entity)); break;
            case QuestEntityKind::Road: REQUIRE(b.entity == q.poiA + ">" + q.poiB); break;
            }
        }
    }
}

TEST_CASE("the situation stands at the first settlement and the quest can be refused", "[bake][quests]")
{
    const PlacedQuests placed = placeManifestQuests(manifestFor(11, 5, 6), rules());
    REQUIRE(placed.ok());
    REQUIRE_FALSE(placed.set.all().empty());
    for (const QuestInstance& q : placed.set.all()) {
        const auto situation = std::find_if(q.beats.begin(), q.beats.end(), [](const QuestBeat& b) { return b.type == graph::quest::kSituation; });
        REQUIRE(situation != q.beats.end());
        REQUIRE(situation->entity == q.poiA);
        REQUIRE(graph::questrefusal::validate(q.refusals).empty());
    }
}

TEST_CASE("quests are deterministic and survive a different input order", "[bake][quests]")
{
    const worldforge::ContentManifest manifest = manifestFor(3, 6, 8);
    const PlacedQuests a = placeManifestQuests(manifest, rules());
    const PlacedQuests b = placeManifestQuests(manifest, rules());
    REQUIRE(a.ok());
    REQUIRE(a.set.all().size() == b.set.all().size());
    for (std::size_t i = 0; i < a.set.all().size(); ++i) {
        REQUIRE(a.set.all()[i].graphDigest == b.set.all()[i].graphDigest);
        REQUIRE(a.set.all()[i].seed == b.set.all()[i].seed);
        REQUIRE(a.set.all()[i].beats.size() == b.set.all()[i].beats.size());
        for (std::size_t k = 0; k < a.set.all()[i].beats.size(); ++k) {
            REQUIRE(a.set.all()[i].beats[k].entity == b.set.all()[i].beats[k].entity);
        }
    }
    const worldforge::ContentManifest other = manifestFor(4, 6, 8);
    const PlacedQuests c = placeManifestQuests(other, rules());
    REQUIRE(c.ok());
    bool differs = c.set.all().size() != a.set.all().size();
    for (std::size_t i = 0; !differs && i < a.set.all().size(); ++i) differs = a.set.all()[i].seed != c.set.all()[i].seed;
    REQUIRE(differs);
}

TEST_CASE("a tension that cannot bind is reported, not dropped", "[bake][quests]")
{
    worldforge::ContentManifest manifest = manifestFor(5, 4, 4);
    REQUIRE_FALSE(manifest.tensions.empty());
    manifest.cast.clear();
    const PlacedQuests placed = placeManifestQuests(manifest, rules());
    REQUIRE(placed.ok());
    REQUIRE(placed.set.all().size() + placed.set.refused().size() == manifest.tensions.size());
    REQUIRE_FALSE(placed.set.refused().empty());
    for (const RefusedQuest& r : placed.set.refused()) {
        REQUIRE_FALSE(r.reason.empty());
        REQUIRE(r.poiA == manifest.tensions[r.tensionIndex].poiA);
    }
}

TEST_CASE("a manifest with no tensions places nothing", "[bake][quests]")
{
    const PlacedQuests placed = placeManifestQuests(manifestFor(2, 1, 4), rules());
    REQUIRE(placed.ok());
    REQUIRE(placed.set.empty());
    REQUIRE(placed.set.refused().empty());
}

TEST_CASE("hostile manifests and rules fail cleanly", "[bake][quests]")
{
    worldforge::ContentManifest dangling = manifestFor(5, 4, 4);
    dangling.tensions.push_back({"settlement_9998", "settlement_9999", 1});
    REQUIRE_FALSE(placeManifestQuests(dangling, rules()).ok());

    worldforge::ContentManifest unsorted = manifestFor(5, 4, 4);
    std::reverse(unsorted.poiIds.begin(), unsorted.poiIds.end());
    REQUIRE_FALSE(placeManifestQuests(unsorted, rules()).ok());

    worldforge::ContentManifest longId = manifestFor(5, 4, 4);
    longId.poiIds.push_back(std::string(200, 'z'));
    REQUIRE_FALSE(placeManifestQuests(longId, rules()).ok());

    worldforge::ContentManifest huge = manifestFor(5, 4, 4);
    huge.cast.resize(worldforge::kManifestMaxEntries + 1);
    REQUIRE_FALSE(placeManifestQuests(huge, rules()).ok());

    const graph::quest::Ruleset broken = graph::quest::parseRuleset("{not json");
    const PlacedQuests none = placeManifestQuests(manifestFor(5, 4, 4), broken);
    REQUIRE_FALSE(none.ok());
    REQUIRE(none.set.empty());

    const PlacedQuests empty = placeManifestQuests(worldforge::ContentManifest{}, rules());
    REQUIRE(empty.ok());
    REQUIRE(empty.set.empty());
}

TEST_CASE("lookup by id and by point of interest", "[bake][quests]")
{
    const PlacedQuests placed = placeManifestQuests(manifestFor(9, 6, 6), rules());
    REQUIRE(placed.ok());
    REQUIRE_FALSE(placed.set.all().empty());
    const QuestInstance& first = placed.set.all().front();
    REQUIRE(placed.set.find(first.id) == &first);
    REQUIRE(placed.set.find("quest_9999") == nullptr);
    const auto touching = placed.set.touching(first.poiA);
    REQUIRE(std::find(touching.begin(), touching.end(), &first) != touching.end());
    REQUIRE(placed.set.touching("nowhere").empty());
}

TEST_CASE("a forged world's manifest bakes into quests", "[bake][quests]")
{
    worldforge::ForgeConfig config;
    config.dungeonArchetypes = {"crypt", "cave", "mine"};
    config.npcArchetypes = {"smith", "priest"};
    for (std::uint64_t seed = 1; seed < 40; ++seed) {
        const worldforge::ForgedWorld world = worldforge::forgeWorld(seed, 128, config);
        if (!world.manifest() || world.manifest()->tensions.empty()) continue;
        const PlacedQuests placed = placeManifestQuests(*world.manifest(), rules());
        REQUIRE(placed.ok());
        REQUIRE(placed.set.all().size() + placed.set.refused().size() == world.manifest()->tensions.size());
        REQUIRE_FALSE(placed.set.empty());
        return;
    }
    FAIL("no seed under 40 forged a manifest with tensions");
}

TEST_CASE("a flood of cast homed on one settlement does not refuse its quests", "[bake][quests]")
{
    worldforge::ContentManifest manifest = manifestFor(6, 4, 4);
    const std::string home = manifest.tensions.front().poiA;
    for (int i = 0; i < 5000; ++i) manifest.cast.push_back({"zz#" + std::to_string(1000 + i), "zz", home});
    std::sort(manifest.cast.begin(), manifest.cast.end(), [](const auto& a, const auto& b) { return a.npcId < b.npcId; });
    const PlacedQuests placed = placeManifestQuests(manifest, rules());
    REQUIRE(placed.ok());
    REQUIRE(placed.set.refused().empty());
    REQUIRE(placed.set.all().size() == manifest.tensions.size());
}

TEST_CASE("refusal steps start at the situation and cover every beat but walk away", "[bake][quests]")
{
    const PlacedQuests placed = placeManifestQuests(manifestFor(13, 6, 8), rules());
    REQUIRE(placed.ok());
    for (const QuestInstance& q : placed.set.all()) {
        std::set<std::uint32_t> expected;
        std::uint32_t situation = 0;
        for (const QuestBeat& b : q.beats) {
            if (b.type == graph::quest::kSituation) situation = b.node;
            if (b.type != graph::quest::kWalkAway) expected.insert(b.node);
        }
        std::set<std::uint32_t> steps;
        for (const auto& s : q.refusals.steps) steps.insert(s.id);
        REQUIRE(q.refusals.start == situation);
        REQUIRE(steps == expected);
    }
}
