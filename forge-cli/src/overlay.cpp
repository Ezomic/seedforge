#include <forgecli/overlay.h>

#include <map>
#include <set>
#include <vector>

#include <nlohmann/json.hpp>

namespace forgecli {

namespace {

using nlohmann::json;
using worldforge::PoiKind;

const char* kindName(PoiKind k)
{
    switch (k) {
    case PoiKind::Settlement: return "settlement";
    case PoiKind::Dungeon: return "dungeon";
    case PoiKind::Ruin: return "ruin";
    case PoiKind::Landmark: return "landmark";
    }
    return "landmark";
}

}

std::string overlayJson(const worldforge::ForgedWorld& world)
{
    if (!world.manifest() || !world.settlements() || !world.pois()) return {};
    const worldforge::ContentManifest& manifest = *world.manifest();

    // Dungeon slots carry the real kind; the rest follow the binding inputs, which do not depend on the dungeon pool.
    const worldforge::BindingInputs inputs = worldforge::detail::forgeBindingInputs(world.seed(), *world.settlements(), *world.pois(), worldforge::ForgeConfig{});
    std::map<std::string, PoiKind> kinds;
    for (const worldforge::PoiInput& p : inputs.pois) kinds[p.id] = p.kind;
    std::map<std::string, const worldforge::DungeonSlot*> dungeons;
    for (const worldforge::DungeonSlot& d : manifest.dungeons) dungeons[d.poiId] = &d;
    std::map<std::string, std::vector<std::string>> items;
    for (const worldforge::ItemPlacement& i : manifest.items) items[i.poiId].push_back(i.itemId);
    std::map<std::string, std::vector<const worldforge::CastMember*>> cast;
    for (const worldforge::CastMember& c : manifest.cast) cast[c.homePoiId].push_back(&c);

    json sites = json::array();
    const auto add = [&](const std::string& id, int x, int y) {
        json site = {{"id", id}, {"x", x}, {"y", y}};
        const auto d = dungeons.find(id);
        const auto k = kinds.find(id);
        site["kind"] = d != dungeons.end() ? "dungeon" : k != kinds.end() ? kindName(k->second) : "landmark";
        if (d != dungeons.end()) site["dungeon"] = {{"archetype", d->second->archetype}, {"seed", std::to_string(d->second->seed)}};
        site["items"] = items.count(id) ? json(items[id]) : json::array();
        json members = json::array();
        if (const auto c = cast.find(id); c != cast.end()) {
            for (const worldforge::CastMember* m : c->second) members.push_back({{"id", m->npcId}, {"archetype", m->archetype}});
        }
        site["cast"] = std::move(members);
        sites.push_back(std::move(site));
    };
    const std::set<std::string> known(manifest.poiIds.begin(), manifest.poiIds.end());
    for (std::size_t i = 0; i < world.settlements()->sites.size(); ++i) {
        const std::string id = worldforge::detail::forgeSettlementId(i);
        if (known.count(id)) add(id, world.settlements()->sites[i].x, world.settlements()->sites[i].y);
    }
    for (std::size_t i = 0; i < world.pois()->pois.size(); ++i) {
        const std::string id = worldforge::detail::forgePoiId(i);
        if (known.count(id)) add(id, world.pois()->pois[i].x, world.pois()->pois[i].y);
    }
    return json{{"seed", std::to_string(world.seed())}, {"size", world.size()}, {"sites", std::move(sites)}}.dump(1) + "\n";
}

}
