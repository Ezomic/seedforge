#include "ichor/world/poi_discovery.h"

#include "ichor/world/dungeon_entry.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace ichor {

namespace {

StableId markerId(std::uint64_t index) { return StableId::of("poi_discovered", index); }

DiscoveryKind kindOf(PlacedPoiKind kind)
{
    switch (kind) {
    case PlacedPoiKind::Dungeon:
        return DiscoveryKind::Poi;
    case PlacedPoiKind::Landmark:
    case PlacedPoiKind::Settlement:
        return DiscoveryKind::Landmark;
    }
    return DiscoveryKind::Landmark;
}

double scoreOf(const PlacedPoi& poi)
{
    return poi.kind == PlacedPoiKind::Settlement ? poi_discovery::kSettlementScore : static_cast<double>(poi.sightlinePermille) / 1000.0;
}

std::string poiOfMarker(const Scene& scene, entt::entity marker)
{
    const auto* named = scene.registry().try_get<Name>(marker);
    const std::string_view prefix = poi_discovery::kMarkerPrefix;
    if (named == nullptr || std::string_view(named->value).substr(0, prefix.size()) != prefix) return {};
    return named->value.substr(prefix.size());
}

std::string_view labelOf(const PlacedPoi& poi)
{
    switch (poi.kind) {
    case PlacedPoiKind::Dungeon:
        return "a dungeon";
    case PlacedPoiKind::Landmark:
        return "a landmark";
    case PlacedPoiKind::Settlement:
        return "a settlement";
    }
    return "a place";
}

}

PoiDiscovery::PoiDiscovery(const ForgedGame& game, PoiDiscoveryParams params)
    : game_(game)
    , params_(params)
{
}

void PoiDiscovery::install(Scene& scene)
{
    scene.addSystem("poi_discovery", [this](Scene& s, double, const InputFrame&) { step(s); });
}

double PoiDiscovery::reachOf(PlacedPoiKind kind) const
{
    switch (kind) {
    case PlacedPoiKind::Dungeon:
        return params_.dungeonReachM;
    case PlacedPoiKind::Landmark:
        return params_.landmarkReachM;
    case PlacedPoiKind::Settlement:
        return params_.settlementReachM;
    }
    return 0.0;
}

std::vector<PoiDiscoveryEntry> PoiDiscovery::found(const Scene& scene) const
{
    std::vector<PoiDiscoveryEntry> entries;
    DiscoveryLedger ledger;
    for (std::uint64_t index = 0; index < kMaxDiscoveries; ++index) {
        const entt::entity marker = scene.find(markerId(index));
        if (marker == entt::null) break;
        std::string id = poiOfMarker(scene, marker);
        const PlacedPoi* poi = game_.pois ? game_.pois->find(id) : nullptr;
        if (poi == nullptr) continue;
        const DiscoveryResult result = ledger.record({id, kindOf(poi->kind), scoreOf(*poi)});
        if (result.status == DiscoveryStatus::Awarded) entries.push_back({std::move(id), result.xp});
    }
    return entries;
}

std::int64_t PoiDiscovery::totalXp(const Scene& scene) const
{
    std::int64_t total = 0;
    for (const PoiDiscoveryEntry& entry : found(scene)) total = std::min(kMaxTotalXp, total + entry.xp);
    return total;
}

std::string PoiDiscovery::notice(const Scene& scene) const
{
    return scene.time() - noticeAt_ < params_.noticeSeconds ? notice_ : std::string();
}

void PoiDiscovery::step(Scene& scene)
{
    if (!game_.pois) return;
    glm::dvec3 at{0.0};
    bool hasPlayer = false;
    for (auto [entity, transform, player] : scene.registry().view<const Transform, const Player>().each()) {
        at = transform.position;
        hasPlayer = true;
        break;
    }
    if (!hasPlayer || !std::isfinite(at.x) || !std::isfinite(at.z) || inDungeonPocket(at)) return;

    const PlacedPoi* entered = nullptr;
    for (const PlacedPoi& poi : game_.pois->all()) {
        if (std::hypot(poi.x - at.x, poi.z - at.z) <= reachOf(poi.kind)) {
            entered = &poi;
            break;
        }
    }
    if (entered == nullptr) return;

    std::uint64_t count = 0;
    while (scene.find(markerId(count)) != entt::null) {
        if (poiOfMarker(scene, scene.find(markerId(count))) == entered->id) return;
        ++count;
    }
    if (count >= kMaxDiscoveries) return;

    SpawnCommand marker;
    marker.id = markerId(count);
    marker.name.value = std::string(poi_discovery::kMarkerPrefix) + entered->id;
    marker.transform.position = {entered->x, static_cast<double>(entered->groundY), entered->z};
    scene.enqueue(marker);

    DiscoveryLedger ledger;
    for (std::uint64_t i = 0; i < count; ++i) {
        const PlacedPoi* earlier = game_.pois->find(poiOfMarker(scene, scene.find(markerId(i))));
        if (earlier != nullptr) ledger.record({earlier->id, kindOf(earlier->kind), scoreOf(*earlier)});
    }
    const std::int64_t paid = ledger.record({entered->id, kindOf(entered->kind), scoreOf(*entered)}).xp;
    notice_ = "Found " + std::string(labelOf(*entered)) + ": +" + std::to_string(paid) + " discovery XP";
    noticeAt_ = scene.time();
}

}
